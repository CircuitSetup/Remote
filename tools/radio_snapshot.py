"""Capture, compare, and carefully apply ExpressLRS radio parameters offline."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import re
import sys
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode, urlsplit, urlunsplit
from urllib.request import HTTPRedirectHandler, ProxyHandler, Request, build_opener
from pathlib import Path


SNAPSHOT_FORMAT = "remote-radio-snapshot"
PREVIEW_FORMAT = "remote-radio-preview"
SCHEMA_VERSION = 1
MAX_FILE_BYTES = 262_144
MAX_RESPONSE_BYTES = 524_288
MAX_DEVICES = 4
MAX_FIELDS_PER_DEVICE = 64
MAX_OUTPUTS = 16
PERSISTENT_TYPES = {0, 1, 2, 3, 9}
ROLE_ADDRESS = {"tx": 0xEE, "rx": 0xEC}
CRITICAL_ROLES = {"rfBand", "rfRate", "antenna", "switchMode", "modelMatch"}
SELECTOR_GROUP = {"inputch", "outputmode", "invert", "protocol", "pairedpin"}
REQUIRED_CONTEXT_FIELDS = {"inputch", "invert", "outputmode"}
SELECTOR_NAMES = {"outputch", "outputchannel"}


def _utc_now() -> datetime:
    return datetime.now(timezone.utc)


def _iso(value: datetime) -> str:
    return value.isoformat(timespec="seconds").replace("+00:00", "Z")


def _canonical(value) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"),
                      allow_nan=False).encode("utf-8")


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _json_bytes(data: bytes):
    depth, quoted, escaped = 0, False, False
    for byte in data:
        char = chr(byte)
        if quoted:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
        elif char == '"':
            quoted = True
        elif char in "[{":
            depth += 1
            if depth > 32:
                raise ValueError("JSON nesting exceeds the 32-level limit")
        elif char in "]}":
            depth -= 1
    try:
        return json.loads(data.decode("utf-8"), object_pairs_hook=_no_duplicates,
                          parse_constant=lambda item: (_ for _ in ()).throw(ValueError(item)))
    except (UnicodeDecodeError, json.JSONDecodeError, RecursionError) as exc:
        raise ValueError("invalid UTF-8 JSON document") from exc


class _NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class RadioClient:
    """Narrow client for live radio operations from the Remote AP network."""

    def __init__(self, host: str, timeout: float = 4.0, poll_interval: float = 0.2,
                 operation_timeout: float = 30.0):
        parsed = urlsplit(host)
        if (parsed.scheme != "http" or not parsed.hostname or parsed.username or parsed.password or
                parsed.path not in ("", "/") or parsed.query or parsed.fragment):
            raise ValueError("host must be an explicit plain HTTP origin such as http://192.168.4.1")
        try:
            parsed.port
        except ValueError:
            raise ValueError("host has an invalid port") from None
        self.origin = urlunsplit(("http", parsed.netloc, "", "", ""))
        self.timeout = timeout
        self.poll_interval = poll_interval
        self.operation_timeout = operation_timeout
        self.nonce = ""
        self._opener = build_opener(ProxyHandler({}), _NoRedirect)

    def _request(self, method: str, path: str, values=None):
        headers = {"Origin": self.origin, "Cache-Control": "no-store", "Accept": "application/json"}
        if self.nonce:
            headers["X-Remote-Nonce"] = self.nonce
        data = None
        if values is not None:
            data = urlencode(values).encode("ascii")
            headers["Content-Type"] = "application/x-www-form-urlencoded"
        request = Request(self.origin + path, data=data, headers=headers, method=method)
        try:
            with self._opener.open(request, timeout=self.timeout) as response:
                if response.status >= 300:
                    raise ValueError(f"Remote returned HTTP {response.status}")
                payload = response.read(MAX_RESPONSE_BYTES + 1)
        except HTTPError as exc:
            try:
                payload = exc.read(MAX_RESPONSE_BYTES + 1)
                doc = _json_bytes(payload)
                if not isinstance(doc, dict):
                    raise ValueError("unexpected response")
            except Exception:
                raise ValueError(f"Remote rejected the request with HTTP {exc.code}") from None
            code, field = doc.get("code", "REQUEST_FAILED"), doc.get("path", "")
            message = str(doc.get("message", "Rejected"))
            if re.search(r"password|credential|secret|token|ssid", str(field) + " " + message, re.I):
                message = "Response details masked"
                field = ""
            raise ValueError(f"{code} {field}: {message}".strip()) from None
        except (URLError, TimeoutError, OSError) as exc:
            raise ValueError("Remote API request failed; check the Remote AP connection") from None
        if len(payload) > MAX_RESPONSE_BYTES:
            raise ValueError("Remote API response exceeds its bound")
        doc = _json_bytes(payload)
        if not isinstance(doc, dict):
            raise ValueError("Remote API returned an unexpected JSON object")
        return doc

    def open_session(self):
        response = self._request("POST", "/remote/session", {})
        nonce = response.get("nonce")
        if not isinstance(nonce, str) or not re.fullmatch(r"[0-9a-fA-F]{32}", nonce):
            raise ValueError("Remote did not return a valid session from the Remote AP")
        self.nonce = nonce

    def devices(self):
        if not self.nonce:
            raise ValueError("Open a Remote AP session first")
        response = self._request("GET", "/remote/devices")
        if not response.get("localAP"):
            raise ValueError("Connect through this Remote AP's WiFi network")
        devices = response.get("devices")
        if not isinstance(devices, list) or len(devices) > MAX_DEVICES:
            raise ValueError("Remote device list is missing or exceeds the four-device bound")
        return response

    def action(self, **values):
        if not self.nonce:
            raise ValueError("Open a protected Remote session first")
        if values.get("action") not in {"discover", "status", "read", "write"}:
            raise ValueError("radio snapshots never issue commands, assignments, or profile selections")
        response = self._request("POST", "/remote/action", values)
        if response.get("state") != "queued" or not isinstance(response.get("id"), int):
            raise ValueError("Remote did not queue the requested operation")
        operation_id = response["id"]
        deadline = time.monotonic() + self.operation_timeout
        while time.monotonic() < deadline:
            result = self._request("GET", "/remote/operation")
            if result.get("id") != operation_id:
                time.sleep(self.poll_interval)
                continue
            state = result.get("state")
            if state == "complete":
                return result
            if state in {"failed", "cancelled", "unknown"}:
                if state == "unknown":
                    raise OperationOutcomeUnknown("operation outcome is unknown; reread before retrying")
                raise ValueError(f"Remote operation {state}: {result.get('error', 'rejected')}")
            if state == "confirmation":
                raise ValueError("unexpected command confirmation; commands are excluded from automation")
            time.sleep(self.poll_interval)
        raise OperationOutcomeUnknown("operation acknowledgment timed out; outcome is unknown")

    def discover(self):
        self.action(action="discover")
        return self.devices()

    def status(self):
        self.action(action="status")
        return self.devices()

    def read_parameter(self, device: int, field: int, generation: int):
        result = self.action(action="read", device=device, field=field, generation=generation)
        param = result.get("parameter")
        if not isinstance(param, dict) or param.get("device") != device or param.get("field") != field:
            raise ValueError(f"field {field} did not return a matching parameter descriptor")
        if param.get("generation") != generation:
            raise ValueError(f"field {field} descriptor generation changed; reread the device")
        return param

    def write_parameter(self, param: dict, generation: int, desired: int):
        result = self.action(action="write", confirmed=1, device=param["device"], field=param["field"],
                             generation=generation, type=param["type"], hash=param["hash"],
                             expected=param["value"], value=desired)
        observed = result.get("parameter")
        if (not isinstance(observed, dict) or observed.get("device") != param["device"] or
                observed.get("field") != param["field"]):
            raise OperationOutcomeUnknown("write completed without a matching readback")
        return observed


class OperationOutcomeUnknown(ValueError):
    pass


def _name_key(value):
    return re.sub(r"[^a-z0-9]", "", str(value).casefold())


def _field_value(param):
    value = param.get("value")
    if type(value) is not int:
        raise ValueError(f"field {param.get('field')} has a noninteger value")
    label = None
    if param.get("type") == 9:
        options = str(param.get("options", "")).split(";")
        if value < 0 or value >= len(options) or not options[value]:
            raise ValueError(f"selection field {param.get('name')} has no exact current option label")
        label = options[value]
    return value, label


def _captured_field(param):
    value, label = _field_value(param)
    return {"field": param["field"], "type": param["type"], "name": str(param.get("name", "")),
            "unit": str(param.get("unit", "")), "value": value, "valueLabel": label,
            "minimum": int(param.get("minimum", 0)), "maximum": int(param.get("maximum", 0)),
            "options": str(param.get("options", "")), "descriptorHash": int(param.get("hash", 0)),
            "role": str(param.get("role", "")), "parent": int(param.get("parent", 0)),
            "writable": bool(param.get("writable", False))}


def _metadata(device):
    needed = ("address", "name", "fieldCount", "generation", "serial", "hardwareVersion",
              "softwareVersion", "parameterVersion")
    if any(type(device.get(key)) is not int and key != "name" for key in needed):
        raise ValueError("Remote returned incomplete device metadata")
    if not isinstance(device.get("name"), str):
        raise ValueError("Remote returned an invalid device name")
    return {key: device[key] for key in needed}


def _device_role(address):
    if address == ROLE_ADDRESS["tx"]:
        return "tx"
    if address == ROLE_ADDRESS["rx"]:
        return "rx"
    return f"device:{address:02x}"


def _find_device(response, address):
    return next((d for d in response["devices"] if d.get("address") == address), None)


def _assert_selector_safety(response):
    if not response.get("maintenance"):
        raise ValueError("output selector sweep requires active maintenance")
    if response.get("radioReviewPending"):
        raise ValueError("finish the pending radio review before selector sweep")
    if not response.get("rfKnown") or not response.get("connected"):
        raise ValueError("output selector sweep requires a known connected receiver")
    if not response.get("armedKnown") or response.get("armed"):
        raise ValueError("output selector sweep requires known disarmed status")
    if type(response.get("statusAgeMs")) is not int or response["statusAgeMs"] > 1000:
        raise ValueError("output selector sweep requires status no older than one second")


def _fresh_metadata(client, address):
    response = client.devices()
    device = _find_device(response, address)
    if device is None:
        raise ValueError(f"device 0x{address:02x} disappeared")
    return response, _metadata(device)


def _write_selector(client, address, field, desired):
    response = client.status()
    raw = _find_device(response, address)
    if raw is None:
        raise ValueError(f"device 0x{address:02x} disappeared")
    meta = _metadata(raw)
    _assert_selector_safety(response)
    param = client.read_parameter(address, field, meta["generation"])
    if param.get("type") != 9 or not param.get("writable") or param.get("hidden"):
        raise ValueError("Output Ch is not an available writable selection")
    if param["value"] == desired:
        return param
    observed = client.write_parameter(param, meta["generation"], desired)
    if observed.get("value") != desired:
        raise ValueError("Output Ch selector readback did not match")
    return observed


def _capture_output_contexts(client, device, output_field, initial_selector, issues):
    address = device["address"]
    options = str(initial_selector.get("options", "")).split(";")
    minimum, maximum = int(initial_selector.get("minimum", 0)), int(initial_selector.get("maximum", len(options) - 1))
    selections = [(raw, label) for raw, label in enumerate(options) if label and minimum <= raw <= maximum]
    if not selections or len(selections) > MAX_OUTPUTS:
        issues.append({"device": address, "field": output_field, "reason": "output count is unavailable or exceeds 16"})
        return []
    labels = [label for _, label in selections]
    if len(labels) != len(set(labels)):
        issues.append({"device": address, "field": output_field,
                       "reason": "output labels are ambiguous"})
        return []
    original, original_label = _field_value(initial_selector)
    contexts = []
    dependent_fields = {}
    unrelated_persistent = {}
    duplicate_context_name = None
    for field in range(1, device["fieldCount"] + 1):
        try:
            meta_response, fresh = _fresh_metadata(client, address)
            p = client.read_parameter(address, field, fresh["generation"])
        except ValueError:
            continue
        if _name_key(p.get("name")) in SELECTOR_GROUP:
            context_key = _name_key(p["name"])
            if context_key in dependent_fields:
                duplicate_context_name = context_key
            dependent_fields[context_key] = field
        elif field != output_field and p.get("type") in PERSISTENT_TYPES and not p.get("hidden"):
            unrelated_persistent[field] = _captured_field(p)
    missing = REQUIRED_CONTEXT_FIELDS - set(dependent_fields)
    if duplicate_context_name:
        issues.append({"device": address, "field": output_field,
                       "reason": f"selector context has duplicate {duplicate_context_name} fields"})
        return []
    if missing:
        issues.append({"device": address, "field": output_field,
                       "reason": "selector context is missing " + ", ".join(sorted(missing))})
        return []
    try:
        for raw, label in selections:
            _write_selector(client, address, output_field, raw)
            response, meta = _fresh_metadata(client, address)
            _assert_selector_safety(response)
            selector = client.read_parameter(address, output_field, meta["generation"])
            if selector.get("value") != raw:
                raise ValueError(f"output selector did not reach {label}")
            captured = {}
            for key in sorted(dependent_fields):
                response, meta = _fresh_metadata(client, address)
                p = client.read_parameter(address, dependent_fields[key], meta["generation"])
                if _name_key(p.get("name")) != key:
                    raise ValueError(f"selector-dependent field {key} changed identity")
                captured[key] = _captured_field(p)
            for field, before in unrelated_persistent.items():
                response, meta = _fresh_metadata(client, address)
                p = client.read_parameter(address, field, meta["generation"])
                current = _captured_field(p)
                if current != before:
                    issue = f"unsupported selector-dependent persistent field {p.get('name')!r} varies by output"
                    if not any(issue == item.get("reason") for item in issues):
                        issues.append({"device": address, "field": field, "reason": issue})
            contexts.append({"selectorRaw": raw, "selectorLabel": label, "fields": captured})
    except ValueError as exc:
        issues.append({"device": address, "field": output_field, "reason": "Output Ch sweep failed: " + str(exc)})
    finally:
        try:
            _write_selector(client, address, output_field, original)
        except ValueError as exc:
            issues.append({"device": address, "field": output_field,
                           "reason": "failed to restore original Output Ch cursor; outcome unknown: " + str(exc)})
    return contexts


def capture_snapshot(client: RadioClient, selector_sweep: bool = False, discover: bool = True):
    response = client.discover() if discover else client.status()
    devices = response["devices"]
    snapshot = {"format": SNAPSHOT_FORMAT, "schemaVersion": SCHEMA_VERSION,
                "capturedAt": _iso(_utc_now()), "selectedTXmodelID": response.get("modelId"),
                "complete": True, "devices": [], "notCaptured": []}
    if not devices:
        snapshot["notCaptured"].append({"role": "radio", "reason": "no compatible devices discovered"})
    roles = {_device_role(device.get("address")) for device in devices}
    for required in ("tx", "rx"):
        if required not in roles:
            snapshot["notCaptured"].append({"role": required, "reason": "device was not discovered"})
    if len(devices) > MAX_DEVICES:
        raise ValueError("discovered device count exceeds the four-device snapshot limit")
    addresses = [device.get("address") for device in devices]
    if len(addresses) != len(set(addresses)):
        raise ValueError("Remote returned duplicate device addresses")
    for raw_device in devices:
        meta = _metadata(raw_device)
        role = _device_role(meta["address"])
        record = {**meta, "role": role, "fields": [], "information": [], "outputContexts": [], "excluded": [],
                  "notCaptured": [], "complete": True}
        output_selector = None
        possible_context_fields = []
        duplicate_output_selector = False
        for field in range(1, meta["fieldCount"] + 1):
            try:
                param = client.read_parameter(meta["address"], field, meta["generation"])
            except ValueError as exc:
                record["notCaptured"].append({"field": field, "reason": str(exc)})
                record["complete"] = False
                continue
            name = _name_key(param.get("name"))
            kind = param.get("type")
            if kind == 12:
                record["information"].append({"field": field, "name": param.get("name", ""),
                                               "info": param.get("info", "")})
                continue
            if kind in (11, 13) or param.get("commandAllowed") or name == "transientinfo":
                record["excluded"].append({"field": field, "type": kind,
                                           "reason": "folder, command, or transient information excluded"})
                continue
            if type(kind) is not int or kind not in PERSISTENT_TYPES:
                record["notCaptured"].append({"field": field, "reason": f"persistent parameter type {kind!r} is unsupported"})
                record["complete"] = False
                continue
            if name in SELECTOR_NAMES and kind == 9:
                if output_selector is not None:
                    duplicate_output_selector = True
                output_selector = param
                record["excluded"].append({"field": field, "type": kind,
                                           "reason": "selector cursor is captured as output contexts"})
                continue
            if name in SELECTOR_GROUP:
                possible_context_fields.append(param)
                continue
            if len(record["fields"]) >= MAX_FIELDS_PER_DEVICE:
                record["notCaptured"].append({"field": field, "reason": "64 persistent field limit exceeded"})
                record["complete"] = False
                continue
            try:
                record["fields"].append(_captured_field(param))
            except (ValueError, TypeError) as exc:
                record["notCaptured"].append({"field": field, "reason": str(exc)})
                record["complete"] = False
        for param in possible_context_fields:
            if output_selector is not None:
                record["excluded"].append({"field": param["field"], "type": param["type"],
                                           "reason": "selector-dependent value is captured per output"})
            else:
                try:
                    if len(record["fields"]) >= MAX_FIELDS_PER_DEVICE:
                        raise ValueError("64 persistent field limit exceeded")
                    record["fields"].append(_captured_field(param))
                except (ValueError, TypeError) as exc:
                    record["notCaptured"].append({"field": param["field"], "reason": str(exc)})
                    record["complete"] = False
        if duplicate_output_selector:
            record["notCaptured"].append({"reason": "multiple Output Ch selectors are ambiguous"})
            record["complete"] = False
        elif output_selector is not None:
            try:
                record["selector"] = _captured_field(output_selector)
            except (ValueError, TypeError) as exc:
                record["notCaptured"].append({"field": output_selector["field"], "reason": str(exc)})
                record["complete"] = False
            if selector_sweep:
                record["sweepStartGeneration"] = meta["generation"]
                _assert_selector_safety(client.status())
                record["outputContexts"] = _capture_output_contexts(
                    client, meta, output_selector["field"], output_selector, record["notCaptured"])
                try:
                    _, after_sweep = _fresh_metadata(client, meta["address"])
                    if not _same_target(meta, after_sweep):
                        raise ValueError("receiver identity or firmware changed during output sweep")
                    record.update(after_sweep)
                except ValueError as exc:
                    record["notCaptured"].append({"field": output_selector["field"],
                                                  "reason": "final output-sweep metadata unavailable: " + str(exc)})
            else:
                record["notCaptured"].append({"field": output_selector["field"],
                                              "reason": "selector-dependent outputs not swept; rerun with --selector-sweep"})
            if not record["outputContexts"]:
                record["complete"] = False
        if record["notCaptured"]:
            record["complete"] = False
            snapshot["complete"] = False
        snapshot["devices"].append(record)
    final = client.devices()
    tx = next((d for d in snapshot["devices"] if d["role"] == "tx"), None)
    if tx is not None:
        live = _find_device(final, ROLE_ADDRESS["tx"])
        model_id = final.get("modelId")
        if (live is None or not _same_target(tx, _metadata(live)) or
                live.get("generation") != tx["generation"] or type(model_id) is not int or not 0 <= model_id <= 63):
            tx["notCaptured"].append({"reason": "TX identity/generation changed or selected model ID remains unknown"})
            tx["complete"] = snapshot["complete"] = False
        else:
            snapshot["selectedTXmodelID"] = model_id
    if snapshot["notCaptured"]:
        snapshot["complete"] = False
    encoded = _canonical(snapshot)
    if len(encoded) > MAX_FILE_BYTES:
        raise ValueError("radio snapshot exceeds the 262144-byte file limit")
    snapshot["sha256"] = _sha256(encoded)
    return snapshot


def _validate_snapshot(snapshot):
    if (not isinstance(snapshot, dict) or snapshot.get("format") != SNAPSHOT_FORMAT or
            snapshot.get("schemaVersion") != SCHEMA_VERSION):
        raise ValueError("unsupported radio snapshot format or schema")
    devices = snapshot.get("devices")
    if not isinstance(devices, list) or len(devices) > MAX_DEVICES:
        raise ValueError("snapshot device list exceeds its bound")
    claimed = snapshot.get("sha256")
    unsigned = dict(snapshot)
    unsigned.pop("sha256", None)
    if not isinstance(claimed, str) or claimed != _sha256(_canonical(unsigned)):
        raise ValueError("snapshot checksum does not match its contents")
    addresses = [device.get("address") for device in devices if isinstance(device, dict)]
    if len(addresses) != len(devices) or len(addresses) != len(set(addresses)):
        raise ValueError("snapshot has duplicate or invalid device records")
    roles = [device.get("role") for device in devices]
    if snapshot.get("complete") and (snapshot.get("notCaptured") or
                                      any(not device.get("complete") or device.get("notCaptured") for device in devices) or
                                      "tx" not in roles or "rx" not in roles):
        raise ValueError("snapshot is marked complete despite missing device or field data")
    for device in devices:
        if not isinstance(device, dict) or not isinstance(device.get("fields"), list) or len(device["fields"]) > MAX_FIELDS_PER_DEVICE:
            raise ValueError("snapshot has an invalid device or more than 64 fields")
        if not isinstance(device.get("outputContexts", []), list) or len(device.get("outputContexts", [])) > MAX_OUTPUTS:
            raise ValueError("snapshot exceeds 16 selector contexts")
        seen_fields = set()
        for field in device["fields"]:
            if (not isinstance(field, dict) or type(field.get("field")) is not int or
                    type(field.get("type")) is not int or field["type"] not in PERSISTENT_TYPES or
                    type(field.get("value")) is not int or not isinstance(field.get("name"), str) or
                    not isinstance(field.get("unit", ""), str)):
                raise ValueError("snapshot contains an invalid persistent field")
            if field["field"] in seen_fields:
                raise ValueError("snapshot contains duplicate field IDs")
            seen_fields.add(field["field"])
    return devices


def _source_value(source):
    value = source.get("value")
    if type(value) is not int:
        raise ValueError(f"snapshot field {source.get('name')} has an invalid raw value")
    return value


def _target_choice(source, target):
    if source.get("type") != target.get("type") or source.get("unit", "") != target.get("unit", ""):
        raise ValueError(f"field {source.get('name')} changed type or unit")
    desired = _source_value(source)
    label = source.get("valueLabel")
    if source.get("type") != 9:
        if desired < int(target.get("minimum", 0)) or desired > int(target.get("maximum", 0)):
            raise ValueError(f"field {source.get('name')} value is outside the live target range")
        return desired, None
    if not isinstance(label, str) or not label:
        raise ValueError(f"selection field {source.get('name')} has no preserved source label")
    options = str(target.get("options", "")).split(";")
    minimum, maximum = int(target.get("minimum", 0)), int(target.get("maximum", len(options) - 1))
    matches = [i for i, option in enumerate(options) if option == label and minimum <= i <= maximum]
    if len(matches) != 1:
        raise ValueError(f"selection {label!r} is unavailable or ambiguous in the live target")
    return matches[0], label


def _match_field(source, targets):
    exact = [target for target in targets if target.get("type") == source.get("type") and
             target.get("unit", "") == source.get("unit", "") and
             target.get("name", "") == source.get("name", "")]
    role = source.get("role")
    by_role = [target for target in targets if role and target.get("role") == role and
               target.get("type") == source.get("type") and target.get("unit", "") == source.get("unit", "")]
    matches = by_role if by_role else exact
    if len(matches) != 1:
        if not matches:
            raise ValueError(f"no live field matches {source.get('name')!r} and unit {source.get('unit', '')!r}")
        raise ValueError(f"field match for {source.get('name')!r} is ambiguous")
    return matches[0]


def _capability_hash(fields):
    stable = []
    for field in sorted(fields, key=lambda item: item["field"]):
        item = {key: field.get(key) for key in
                ("field", "type", "name", "unit", "minimum", "maximum", "options", "role", "parent", "writable")}
        stable.append(item)
    return _sha256(_canonical(stable))


def _read_live_device(client, metadata):
    params = []
    for field in range(1, metadata["fieldCount"] + 1):
        try:
            param = client.read_parameter(metadata["address"], field, metadata["generation"])
        except ValueError:
            continue
        if param.get("type") in PERSISTENT_TYPES:
            params.append(param)
        if len(params) > MAX_FIELDS_PER_DEVICE:
            raise ValueError("live target exceeds 64 persistent fields")
    has_selector = any(p.get("type") == 9 and _name_key(p.get("name")) in SELECTOR_NAMES for p in params)
    return [p for p in params if not has_selector or _name_key(p.get("name")) not in SELECTOR_NAMES | SELECTOR_GROUP]


def _context_fingerprint(field):
    return {key: field.get(key) for key in
            ("field", "type", "name", "unit", "minimum", "maximum", "options", "role", "parent", "writable")}


def _context_value(field):
    value = _source_value(field)
    label = field.get("valueLabel")
    if field.get("type") == 9:
        options = str(field.get("options", "")).split(";")
        if value < 0 or value >= len(options) or options[value] != label:
            raise ValueError(f"context selection {field.get('name')} has no exact value label")
    return value, label


def _build_output_plan(source, target):
    source_contexts = source.get("outputContexts", [])
    target_contexts = target.get("outputContexts", [])
    if not source_contexts or len(source_contexts) != len(target_contexts) or len(source_contexts) > MAX_OUTPUTS:
        raise ValueError("source and live receiver output contexts do not match")
    target_by_label = {}
    for row in target_contexts:
        label = row.get("selectorLabel")
        if not isinstance(label, str) or not label or label in target_by_label:
            raise ValueError("live output labels are missing or ambiguous")
        target_by_label[label] = row
    if {row.get("selectorLabel") for row in source_contexts} != set(target_by_label):
        raise ValueError("source and live receiver output labels differ")
    source_selector, target_selector = source.get("selector"), target.get("selector")
    if not isinstance(source_selector, dict) or not isinstance(target_selector, dict):
        raise ValueError("source or live receiver has no output selector descriptor")
    if (source_selector.get("type"), _name_key(source_selector.get("name")), source_selector.get("unit", "")) != (
            target_selector.get("type"), _name_key(target_selector.get("name")), target_selector.get("unit", "")):
        raise ValueError("source and live receiver output selector descriptors differ")
    for selector, contexts in ((source_selector, source_contexts), (target_selector, target_contexts)):
        labels_available = str(selector.get("options", "")).split(";")
        minimum = int(selector.get("minimum", 0))
        maximum = int(selector.get("maximum", len(labels_available) - 1))
        options = {label for raw, label in enumerate(labels_available)
                   if label and minimum <= raw <= maximum}
        labels = {row.get("selectorLabel") for row in contexts}
        if options != labels:
            raise ValueError("source or live output selector has unrepresented output rows")
    contexts = []
    selector_map = []
    for foreign in source_contexts:
        label = foreign["selectorLabel"]
        live = target_by_label[label]
        selector_map.append({"label": label, "raw": live["selectorRaw"]})
        source_fields, target_fields = foreign.get("fields", {}), live.get("fields", {})
        dependencies = {}
        for dependency in ("outputmode", "protocol", "pairedpin"):
            if (dependency in source_fields) != (dependency in target_fields):
                raise ValueError("Output Mode/Protocol or paired-pin dependency descriptors differ; update through the explicit UI, then preview again")
            if dependency in source_fields:
                sf, tf = source_fields[dependency], target_fields[dependency]
                if (_context_fingerprint(sf) != _context_fingerprint(tf) or
                        _context_value(sf) != _context_value(tf)):
                    raise ValueError("Output Mode/Protocol or paired-pin dependency differs; update through the explicit UI, then preview again")
                dependencies[dependency] = {"descriptor": _context_fingerprint(tf),
                                            "descriptorHash": tf.get("descriptorHash"),
                                            "value": tf.get("value"), "valueLabel": tf.get("valueLabel")}
        changes = []
        prior = {}
        for key in ("inputch", "invert"):
            sf, tf = source_fields.get(key), target_fields.get(key)
            if not isinstance(sf, dict) or not isinstance(tf, dict):
                raise ValueError(f"output {label} is missing {key} context data")
            if (sf.get("type"), sf.get("unit"), _name_key(sf.get("name"))) != (
                    tf.get("type"), tf.get("unit"), _name_key(tf.get("name"))):
                raise ValueError(f"output {label} {key} descriptor changed type or unit")
            desired, desired_label = _target_choice(sf, tf)
            current, current_label = _context_value(tf)
            prior[key] = {"descriptor": _context_fingerprint(tf), "descriptorHash": tf.get("descriptorHash"),
                          "value": current, "valueLabel": current_label}
            if desired != current:
                if not tf.get("writable"):
                    raise ValueError(f"output {label} {key} is not writable")
                changes.append({"key": key, "field": tf["field"], "type": tf["type"],
                                "name": tf["name"], "unit": tf.get("unit", ""),
                                "expected": current, "expectedLabel": current_label,
                                "value": desired, "valueLabel": desired_label,
                                "descriptorHash": tf.get("descriptorHash")})
        contexts.append({"selectorLabel": label, "selectorRaw": live["selectorRaw"],
                         "dependencies": dependencies,
                         "prior": prior, "changes": changes})
    return {"contexts": contexts, "selectorMap": selector_map,
            "selector": target.get("selector"),
            "contextSha256": _sha256(_canonical([
                {"selectorLabel": row["selectorLabel"], "dependencies": row["dependencies"],
                 "fields": row["prior"]} for row in contexts]))}


def make_preview(client: RadioClient, snapshot: dict, device_role: str = "tx",
                 selector_sweep: bool = False):
    source_devices = _validate_snapshot(snapshot)
    if device_role not in {"tx", "rx"}:
        raise ValueError("select exactly one device group: tx or rx")
    source_matches = [d for d in source_devices if d.get("role") == device_role]
    if len(source_matches) != 1:
        raise ValueError(f"snapshot has no unique {device_role.upper()} device")
    source = source_matches[0]
    if not source.get("complete"):
        raise ValueError(f"{device_role.upper()} capture is incomplete; review its notCaptured fields first")
    if device_role == "rx" and source.get("outputContexts") and not selector_sweep:
        raise ValueError("RX output-context preview requires the explicit --selector-sweep option")
    response = client.status()
    address = ROLE_ADDRESS[device_role]
    target_raw = _find_device(response, address)
    if target_raw is None:
        raise ValueError(f"target {device_role.upper()} was not discovered")
    target_meta = _metadata(target_raw)
    if device_role == "tx" and type(response.get("modelId")) is not int:
        raise ValueError("TX selected model ID is unknown; select and read a profile in the Remote UI first")
    if device_role == "tx" and snapshot.get("selectedTXmodelID") != response.get("modelId"):
        raise ValueError("source and target TX model IDs differ; select the intended profile in the Remote UI, then recapture")
    output_plan = None
    if device_role == "rx" and source.get("outputContexts"):
        _assert_selector_safety(response)
        start_generation = target_meta["generation"]
        swept = capture_snapshot(client, selector_sweep=True, discover=False)
        target_rx = next((d for d in swept["devices"] if d.get("role") == "rx"), None)
        if not target_rx or not target_rx.get("complete"):
            raise ValueError("live receiver output-context capture is incomplete; review notCaptured and preview again")
        if not _same_target(target_meta, target_rx):
            raise ValueError("receiver identity or firmware changed during output-context preview")
        response = client.status()
        current_raw = _find_device(response, address)
        if current_raw is None:
            raise ValueError("receiver disappeared during output-context preview")
        target_meta = _metadata(current_raw)
        if target_meta["generation"] != target_rx["generation"]:
            raise ValueError("receiver generation changed after output-context preview sweep")
        output_plan = _build_output_plan(source, target_rx)
        output_plan["startGeneration"] = start_generation
        output_plan["finalGeneration"] = target_meta["generation"]
    live_params = _read_live_device(client, target_meta)
    source_fields = [field for field in source["fields"] if not source.get("selector") or
                     _name_key(field.get("name")) not in SELECTOR_NAMES | SELECTOR_GROUP]
    changes, prior_values = [], []
    seen_target_fields = set()
    for foreign in source_fields:
        live = _match_field(foreign, live_params)
        if live["field"] in seen_target_fields:
            raise ValueError(f"multiple source fields map to target field {live['field']}")
        seen_target_fields.add(live["field"])
        desired, label = _target_choice(foreign, live)
        current, current_label = _field_value(live)
        prior_values.append({"field": live["field"], "type": live["type"], "name": live["name"],
                             "unit": live.get("unit", ""), "value": current, "valueLabel": current_label,
                             "descriptorHash": live.get("hash", 0), "role": live.get("role", "")})
        if current == desired:
            continue
        if not live.get("writable") or live.get("hidden") or live.get("type") not in PERSISTENT_TYPES:
            raise ValueError(f"changed field {live['name']!r} is not a supported writable parameter")
        role = str(live.get("role", ""))
        if role in CRITICAL_ROLES:
            raise ValueError(f"{role} changes require the explicit Remote UI; change one there, then capture and preview again")
        changes.append({"device": address, "field": live["field"], "type": live["type"],
                        "name": live["name"], "unit": live.get("unit", ""), "role": role,
                        "expected": current, "expectedLabel": current_label, "value": desired,
                        "valueLabel": label, "hash": live.get("hash", 0)})
    if output_plan and changes:
        raise ValueError("combine receiver output-context changes with other RX settings in separate previews")
    dependency_order = {"power": 0, "telemetry": 1, "dynamic": 2}
    changes.sort(key=lambda change: (dependency_order.get(change["role"], 3), change["field"]))
    captured_at = _utc_now()
    preview = {"format": PREVIEW_FORMAT, "schemaVersion": SCHEMA_VERSION,
               "createdAt": _iso(captured_at), "expiresAt": int(captured_at.timestamp()) + 300,
               "snapshotSha256": _sha256(_canonical(snapshot)), "scope": device_role,
               "target": {**target_meta, "role": device_role, "modelId": response.get("modelId"),
                          "capabilitySha256": _capability_hash(live_params)},
               "priorValues": prior_values, "changes": changes,
               "completeScope": True,
               "note": "One device group only; CRSF writes are sequential with readback and best-effort rollback."}
    if output_plan:
        preview["outputPlan"] = output_plan
    encoded = _canonical(preview)
    if len(encoded) > MAX_FILE_BYTES:
        raise ValueError("radio preview exceeds 262144-byte file limit")
    preview["previewSha256"] = _sha256(encoded)
    return preview


def _validate_preview(preview, confirmed):
    if not confirmed:
        raise ValueError("apply requires explicit operator confirmation (--confirmed)")
    if not isinstance(preview, dict) or preview.get("format") != PREVIEW_FORMAT or preview.get("schemaVersion") != SCHEMA_VERSION:
        raise ValueError("unsupported preview format or schema")
    claimed = preview.get("previewSha256")
    unsigned = dict(preview)
    unsigned.pop("previewSha256", None)
    if not isinstance(claimed, str) or claimed != _sha256(_canonical(unsigned)):
        raise ValueError("preview checksum does not match its contents; preview again")
    if type(preview.get("expiresAt")) is not int or preview["expiresAt"] < int(time.time()):
        raise ValueError("preview expired; capture and preview again")
    if preview.get("scope") not in {"tx", "rx"} or not isinstance(preview.get("changes"), list):
        raise ValueError("invalid preview scope")
    if len(preview["changes"]) > MAX_FIELDS_PER_DEVICE:
        raise ValueError("preview exceeds 64-field device limit")
    if any(change.get("role") in CRITICAL_ROLES for change in preview["changes"]):
        raise ValueError("critical RF changes require the explicit Remote UI and a new preview")
    plan = preview.get("outputPlan")
    if plan is not None:
        if preview["scope"] != "rx" or not isinstance(plan, dict) or not isinstance(plan.get("contexts"), list):
            raise ValueError("invalid receiver output-context plan")
        if not 0 < len(plan["contexts"]) <= MAX_OUTPUTS:
            raise ValueError("preview exceeds 16 receiver output contexts")
        if any(not isinstance(row.get("changes"), list) or len(row["changes"]) > 2
               for row in plan["contexts"]):
            raise ValueError("invalid receiver output-context changes")


def _safe_for_apply(response):
    if not response.get("maintenance"):
        raise ValueError("apply requires active maintenance")
    if response.get("radioReviewPending"):
        raise ValueError("apply blocked until pending radio review is complete")
    if not response.get("armedKnown") or response.get("armed"):
        raise ValueError("apply requires known disarmed status")
    if type(response.get("statusAgeMs")) is not int or response["statusAgeMs"] > 1000:
        raise ValueError("apply requires status no older than one second")
    if not response.get("rfKnown"):
        raise ValueError("apply requires known current RF connection status")


def _same_connection_context(response, context):
    if (response.get("rfKnown"), response.get("connected")) != context:
        raise ValueError("RF connection changed during apply; stopped to avoid stale rollback")


def _same_target(expected, actual):
    return all(expected.get(key) == actual.get(key) for key in
               ("address", "serial", "hardwareVersion", "softwareVersion", "parameterVersion"))


def _fresh_field_map(client, target):
    response, metadata = _fresh_metadata(client, target["address"])
    if not _same_target(target, metadata):
        raise ValueError("target device identity or firmware changed; preview again")
    fields = _read_live_device(client, metadata)
    return response, metadata, {field["field"]: field for field in fields}, _capability_hash(fields)


def _confirm_preview_state(client, preview, target):
    response = client.status()
    _safe_for_apply(response)
    raw = _find_device(response, target["address"])
    if raw is None:
        raise ValueError("target device disconnected; preview again")
    metadata = _metadata(raw)
    if not _same_target(target, metadata):
        raise ValueError("target device identity or firmware changed; preview again")
    if metadata["generation"] != target["generation"] or response.get("modelId") != target.get("modelId"):
        raise ValueError("target generation or selected model changed; preview again")
    params = _read_live_device(client, metadata)
    if _capability_hash(params) != target.get("capabilitySha256"):
        raise ValueError("target descriptors or options changed; preview again")
    by_field = {p["field"]: p for p in params}
    for before in preview["priorValues"]:
        current = by_field.get(before["field"])
        if not current or current.get("value") != before["value"] or current.get("hash") != before["descriptorHash"]:
            raise ValueError("target prior values changed; preview again")
    return response, metadata, by_field


def _recheck_change(change, live):
    if (not live or live.get("type") != change["type"] or live.get("unit", "") != change["unit"] or
            live.get("name", "") != change["name"] or live.get("role", "") != change["role"]):
        raise ValueError(f"field {change['name']} descriptor changed; stopped before write")
    if change["type"] == 9:
        source = {"type": 9, "unit": change["unit"], "name": change["name"],
                  "value": change["value"], "valueLabel": change["valueLabel"]}
        desired, _ = _target_choice(source, live)
        if desired != change["value"]:
            raise ValueError(f"choice {change['valueLabel']!r} moved; preview again")
    elif not int(live["minimum"]) <= change["value"] <= int(live["maximum"]):
        raise ValueError(f"field {change['name']} value is outside the fresh target range")


def _verify_output_contexts(preview, live_rx):
    plan = preview["outputPlan"]
    selector = live_rx.get("selector")
    expected_selector = plan.get("selector")
    if not isinstance(selector, dict) or not isinstance(expected_selector, dict):
        raise ValueError("receiver output selector descriptor is unavailable")
    if (_context_fingerprint(selector) != _context_fingerprint(expected_selector) or
            selector.get("descriptorHash") != expected_selector.get("descriptorHash") or
            selector.get("value") != expected_selector.get("value") or
            selector.get("valueLabel") != expected_selector.get("valueLabel")):
        raise ValueError("receiver output selector descriptor or cursor changed; preview again")
    by_label = {row.get("selectorLabel"): row for row in live_rx.get("outputContexts", [])}
    if len(by_label) != len(live_rx.get("outputContexts", [])) or set(by_label) != {
            row.get("selectorLabel") for row in plan["contexts"]}:
        raise ValueError("receiver output-context labels changed; preview again")
    for expected in plan["contexts"]:
        live = by_label[expected["selectorLabel"]]
        if live.get("selectorRaw") != expected["selectorRaw"]:
            raise ValueError("receiver output selector mapping changed; preview again")
        for key, before in expected["prior"].items():
            current = live.get("fields", {}).get(key)
            if (not current or _context_fingerprint(current) != before["descriptor"] or
                    current.get("descriptorHash") != before["descriptorHash"] or
                    current.get("value") != before["value"] or
                    current.get("valueLabel") != before["valueLabel"]):
                raise ValueError("receiver output-context descriptors or prior values changed; preview again")
        for key, before in expected.get("dependencies", {}).items():
            current = live.get("fields", {}).get(key)
            if (not current or _context_fingerprint(current) != before["descriptor"] or
                    current.get("descriptorHash") != before["descriptorHash"] or
                    current.get("value") != before["value"] or
                    current.get("valueLabel") != before["valueLabel"]):
                raise ValueError("receiver output-context descriptors or prior values changed; preview again")


def _read_context_fields(client, address, row):
    values = {}
    for key, before in row["prior"].items():
        _, metadata = _fresh_metadata(client, address)
        param = client.read_parameter(address, before["descriptor"]["field"], metadata["generation"])
        captured = _captured_field(param)
        if _context_fingerprint(captured) != before["descriptor"]:
            raise ValueError(f"receiver {row['selectorLabel']} {key} descriptor changed")
        values[key] = (param, captured, metadata)
    return values


def _apply_output_plan(client, preview, target, connection_context):
    plan = preview["outputPlan"]
    address = target["address"]
    selector_field = plan["selector"]["field"]
    original_selector = None
    expected_generation = target["generation"]
    applied, rollback, failures = [], [], []
    observed_states = []
    outcome_unknown = False
    restore_failed = False
    try:
        live_capture = capture_snapshot(client, selector_sweep=True, discover=False)
        live_rx = next((d for d in live_capture["devices"] if d.get("role") == "rx"), None)
        if live_rx and isinstance(live_rx.get("selector"), dict):
            original_selector = live_rx["selector"].get("value")
        if not live_rx or not live_rx.get("complete"):
            raise ValueError("receiver output-context preflight is incomplete")
        if live_rx.get("sweepStartGeneration") != expected_generation:
            raise ValueError("receiver generation changed before output-context preflight")
        if not _same_target(target, live_rx):
            raise ValueError("receiver identity or firmware changed; preview again")
        _verify_output_contexts(preview, live_rx)
        _, current_meta = _fresh_metadata(client, address)
        expected_generation = current_meta["generation"]
        target["generation"] = expected_generation
        for row in plan["contexts"]:
            response = client.status()
            _safe_for_apply(response)
            _assert_selector_safety(response)
            _same_connection_context(response, connection_context)
            response, metadata = _fresh_metadata(client, address)
            if metadata["generation"] != expected_generation or not _same_target(target, metadata):
                raise ValueError("receiver generation or identity changed before output selection")
            selected = _write_selector(client, address, selector_field, row["selectorRaw"])
            if selected.get("value") != row["selectorRaw"]:
                raise ValueError(f"failed to select {row['selectorLabel']}")
            _, metadata = _fresh_metadata(client, address)
            expected_generation = target["generation"] = metadata["generation"]
            context_fields = _read_context_fields(client, address, row)
            for change in row["changes"]:
                response = client.status()
                _safe_for_apply(response)
                _assert_selector_safety(response)
                _same_connection_context(response, connection_context)
                fresh = _read_context_fields(client, address, row)
                live, captured, metadata = fresh[change["key"]]
                prior = row["prior"][change["key"]]
                if (metadata["generation"] != expected_generation or
                        captured.get("descriptorHash") != prior["descriptorHash"] or
                        live.get("value") != change["expected"]):
                    raise ValueError(f"receiver {row['selectorLabel']} {change['name']} changed before write")
                desired_source = {"type": change["type"], "unit": change["unit"],
                                  "name": change["name"], "value": change["value"],
                                  "valueLabel": change["valueLabel"]}
                desired, _ = _target_choice(desired_source, live)
                observed = client.write_parameter(live, metadata["generation"], desired)
                if observed.get("value") != desired:
                    raise OperationOutcomeUnknown(f"readback mismatch for {change['name']}")
                applied.append({"row": row, "change": change})
                observed_states.append({"output": row["selectorLabel"], "field": change["field"],
                                        "value": desired, "valueLabel": change["valueLabel"],
                                        "readback": "verified"})
                _, metadata = _fresh_metadata(client, address)
                expected_generation = target["generation"] = metadata["generation"]
    except OperationOutcomeUnknown as exc:
        outcome_unknown = True
        failures.append(str(exc))
        if "change" in locals() and "row" in locals():
            observed_states.append({"output": row.get("selectorLabel"), "field": change.get("field"),
                                    "readback": "outcome-unknown"})
    except ValueError as exc:
        failures.append(str(exc))
    if failures and applied and not outcome_unknown:
        for item in reversed(applied):
            row, change = item["row"], item["change"]
            try:
                response = client.status()
                _safe_for_apply(response)
                _assert_selector_safety(response)
                _same_connection_context(response, connection_context)
                _, metadata = _fresh_metadata(client, address)
                if metadata["generation"] != expected_generation or not _same_target(target, metadata):
                    raise ValueError("receiver generation or identity changed before rollback")
                _write_selector(client, address, selector_field, row["selectorRaw"])
                _, metadata = _fresh_metadata(client, address)
                expected_generation = target["generation"] = metadata["generation"]
                live_values = _read_context_fields(client, address, row)
                live, captured, metadata = live_values[change["key"]]
                if live.get("value") != change["value"]:
                    raise ValueError("current value no longer matches this receiver operation")
                observed = client.write_parameter(live, metadata["generation"], change["expected"])
                if observed.get("value") != change["expected"]:
                    raise OperationOutcomeUnknown("receiver rollback readback mismatch")
                _, metadata = _fresh_metadata(client, address)
                expected_generation = target["generation"] = metadata["generation"]
                rollback.append({"output": row["selectorLabel"], "field": change["field"], "status": "restored"})
                observed_states.append({"output": row["selectorLabel"], "field": change["field"],
                                        "value": change["expected"], "valueLabel": change["expectedLabel"],
                                        "readback": "rollback-verified"})
            except (ValueError, OperationOutcomeUnknown) as exc:
                rollback.append({"output": row["selectorLabel"], "field": change["field"],
                                 "status": "outcome-unknown", "reason": str(exc)})
                outcome_unknown = True
                break
    if original_selector is not None:
        try:
            response = client.status()
            _safe_for_apply(response)
            _assert_selector_safety(response)
            _same_connection_context(response, connection_context)
            _write_selector(client, address, selector_field, original_selector)
            _, metadata = _fresh_metadata(client, address)
            target["generation"] = metadata["generation"]
        except ValueError as exc:
            restore_failed = True
            failures.append("failed to restore original Output Ch cursor; outcome unknown: " + str(exc))
            outcome_unknown = True
    if failures:
        status = "outcome-unknown" if outcome_unknown or restore_failed else (
            "rolled-back" if applied and len(rollback) == len(applied) and
            all(row["status"] == "restored" for row in rollback) else "partially-changed" if applied else "not-applied")
        result = {"status": status, "target": target,
                  "applied": [{"output": item["row"]["selectorLabel"], "field": item["change"]["field"]}
                              for item in applied],
                  "failures": failures, "rollback": rollback, "observedState": observed_states,
                  "message": "Output-context writes are sequential with readback; this operation is not atomic."}
        raise ApplyError(result)
    return {"status": "complete", "target": target,
            "applied": [{"output": item["row"]["selectorLabel"], "field": item["change"]["field"]}
                        for item in applied], "failures": [], "rollback": [], "observedState": observed_states,
            "message": "Output-context writes are sequential with readback; this operation is not atomic."}


def apply_preview(client: RadioClient, preview: dict, confirmed: bool = False,
                  receiver_confirmed: bool = False):
    _validate_preview(preview, confirmed)
    target = preview.get("target")
    if not isinstance(target, dict):
        raise ValueError("preview has no target binding")
    if preview["scope"] == "rx" and not receiver_confirmed:
        raise ValueError("receiver changes require explicit physical receiver acknowledgment")
    response, metadata, fields = _confirm_preview_state(client, preview, target)
    if preview.get("outputPlan") is not None:
        if not response.get("connected"):
            raise ValueError("receiver apply requires the receiver to remain connected")
        return _apply_output_plan(client, preview, target,
                                  (response.get("rfKnown"), response.get("connected")))
    if preview["scope"] == "rx" and not response.get("connected"):
        raise ValueError("receiver apply requires the receiver to remain connected")
    connection_context = (response.get("rfKnown"), response.get("connected"))
    expected_generation = metadata["generation"]
    applied = []
    failures = []
    outcome_unknown = False
    for change in preview["changes"]:
        try:
            response = client.status()
            _safe_for_apply(response)
            _same_connection_context(response, connection_context)
            response, metadata, fresh_fields, _ = _fresh_field_map(client, target)
            if metadata["generation"] != expected_generation:
                raise ValueError("target generation changed during apply; stopped before write")
            live = fresh_fields.get(change["field"])
            if (not live or live.get("type") != change["type"] or live.get("value") != change["expected"] or
                    live.get("unit", "") != change["unit"] or live.get("name", "") != change["name"]):
                raise ValueError(f"field {change['name']} changed after preview; stopped before write")
            _recheck_change(change, live)
            if not live.get("writable") or live.get("hidden"):
                raise ValueError(f"field {change['name']} is no longer writable")
            observed = client.write_parameter(live, metadata["generation"], change["value"])
            if observed.get("value") != change["value"]:
                raise ValueError(f"readback mismatch for {change['name']}")
            applied.append(change)
            _, target_meta = _fresh_metadata(client, target["address"])
            expected_generation = target_meta["generation"]
            target["generation"] = expected_generation
        except OperationOutcomeUnknown as exc:
            outcome_unknown = True
            failures.append(str(exc))
            # A timed-out acknowledgment is never retried blindly. Read once to
            # determine whether this value was applied before considering rollback.
            try:
                response, metadata, fresh_fields, _ = _fresh_field_map(client, target)
                current = fresh_fields[change["field"]]
                if current["value"] == change["value"]:
                    applied.append(change)
                    expected_generation = target["generation"] = metadata["generation"]
                    outcome_unknown = False
                elif current["value"] == change["expected"]:
                    expected_generation = target["generation"] = metadata["generation"]
                else:
                    outcome_unknown = True
            except (ValueError, KeyError):
                outcome_unknown = True
            break
        except ValueError as exc:
            failures.append(str(exc))
            break
    rollback = []
    partially_changed = False
    if failures and applied and not outcome_unknown:
        for change in reversed(applied):
            try:
                response = client.status()
                _safe_for_apply(response)
                _same_connection_context(response, connection_context)
                response, metadata, fresh_fields, _ = _fresh_field_map(client, target)
                if metadata["generation"] != expected_generation:
                    raise ValueError("target generation changed before rollback")
                _safe_for_apply(response)
                live = fresh_fields.get(change["field"])
                if not live or live.get("value") != change["value"]:
                    rollback.append({"field": change["field"], "status": "not-restored",
                                     "reason": "current value no longer matches this operation"})
                    partially_changed = True
                    continue
                observed = client.write_parameter(live, metadata["generation"], change["expected"])
                if observed.get("value") != change["expected"]:
                    raise ValueError("rollback readback mismatch")
                _, target_meta = _fresh_metadata(client, target["address"])
                expected_generation = target["generation"] = target_meta["generation"]
                rollback.append({"field": change["field"], "status": "restored"})
            except ValueError as exc:
                rollback.append({"field": change["field"], "status": "outcome-unknown", "reason": str(exc)})
                outcome_unknown = True
                break
    observed_state = []
    if failures:
        try:
            _, current_meta = _fresh_metadata(client, target["address"])
            for field in sorted({change["field"] for change in preview["changes"]}):
                try:
                    param = client.read_parameter(target["address"], field, current_meta["generation"])
                    raw, label = _field_value(param)
                    observed_state.append({"field": field, "value": raw, "valueLabel": label,
                                           "readback": "verified"})
                except ValueError:
                    observed_state.append({"field": field, "readback": "unavailable"})
        except ValueError:
            observed_state = [{"field": change["field"], "readback": "unavailable"}
                              for change in preview["changes"]]
    if not failures:
        status = "complete"
    elif outcome_unknown:
        status = "outcome-unknown"
    elif partially_changed:
        status = "partially-changed"
    elif applied and len(rollback) == len(applied) and all(row["status"] == "restored" for row in rollback):
        status = "rolled-back"
    else:
        status = "not-applied"
    result = {"status": status,
              "target": target, "applied": [item["field"] for item in applied],
              "failures": failures, "rollback": rollback, "observedState": observed_state,
              "message": "Sequential writes with readback; this operation is not atomic."}
    if failures:
        raise ApplyError(result)
    return result


class ApplyError(ValueError):
    def __init__(self, result):
        self.result = result
        super().__init__(json.dumps(result, ensure_ascii=False, sort_keys=True))


def read_document(path: Path, suffix: str):
    path = Path(path)
    if not path.name.casefold().endswith(suffix.casefold()):
        raise ValueError(f"choose a {suffix} file")
    if not path.is_file() or path.is_symlink():
        raise ValueError("input file must be a regular local file")
    if path.stat().st_size > MAX_FILE_BYTES:
        raise ValueError("input file exceeds 262144-byte limit")
    document = _json_bytes(path.read_bytes())
    if suffix.casefold() == ".remote-radio.json":
        _validate_snapshot(document)
    elif suffix.casefold() == ".remote-radio-preview.json":
        _validate_preview(document, confirmed=True)
    return document


def write_document(path: Path, document, suffix: str):
    path = Path(path)
    if not path.name.casefold().endswith(suffix.casefold()):
        raise ValueError(f"output path must end in {suffix}")
    data = _canonical(document) + b"\n"
    if len(data) > MAX_FILE_BYTES:
        raise ValueError("output exceeds 262144-byte limit")
    path.write_bytes(data)


def _parser():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    capture = commands.add_parser("capture", help="capture a radio snapshot")
    capture.add_argument("--host", required=True, help="explicit Remote AP origin, e.g. http://192.168.4.1")
    capture.add_argument("--out", required=True, type=Path)
    capture.add_argument("--selector-sweep", action="store_true", help="explicitly sweep RX output cursor (requires maintenance and connected disarmed RX)")
    preview = commands.add_parser("preview", help="compare one device group against live descriptors")
    preview.add_argument("--host", required=True)
    preview.add_argument("--snapshot", required=True, type=Path)
    preview.add_argument("--out", required=True, type=Path)
    preview.add_argument("--device-role", choices=("tx", "rx"), required=True)
    preview.add_argument("--selector-sweep", action="store_true", help="explicitly sweep receiver output contexts for an RX preview")
    apply = commands.add_parser("apply", help="apply a saved preview with verified readback")
    apply.add_argument("--host", required=True)
    apply.add_argument("--preview", required=True, type=Path)
    apply.add_argument("--confirmed", action="store_true", help="confirm this exact saved preview")
    apply.add_argument("--receiver-confirmed", action="store_true", help="confirm the physical receiver is connected and safe for output updates")
    return parser


def main(argv=None):
    args = _parser().parse_args(argv)
    try:
        client = RadioClient(args.host)
        client.open_session()
        if args.command == "capture":
            snapshot = capture_snapshot(client, selector_sweep=args.selector_sweep)
            write_document(args.out, snapshot, ".remote-radio.json")
            print(f"Captured {len(snapshot['devices'])} device(s); complete={str(snapshot['complete']).lower()}; sha256={snapshot['sha256']}")
            return 0 if snapshot["complete"] else 2
        if args.command == "preview":
            snapshot = read_document(args.snapshot, ".remote-radio.json")
            preview = make_preview(client, snapshot, args.device_role, selector_sweep=args.selector_sweep)
            write_document(args.out, preview, ".remote-radio-preview.json")
            print(f"Previewed {preview['scope']} group: {len(preview['changes'])} change(s); expires at {preview['expiresAt']}")
            return 0
        preview = read_document(args.preview, ".remote-radio-preview.json")
        result = apply_preview(client, preview, args.confirmed, receiver_confirmed=args.receiver_confirmed)
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
        return 0
    except ApplyError as exc:
        _print_error(str(exc))
        return 3
    except (OSError, ValueError) as exc:
        _print_error(str(exc))
        return 2


def _print_error(message):
    if re.search(r"password|credential|secret|token|ssid", message, re.I):
        print("Request failed; sensitive response details masked", file=sys.stderr)
    else:
        print(message, file=sys.stderr)


if __name__ == "__main__":
    raise SystemExit(main())
