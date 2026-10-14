"""Behavioral tests for the offline radio snapshot and apply client."""
import json
import hashlib
import contextlib
import io
import socket
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs

import sys
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from radio_snapshot import (RadioClient, capture_snapshot, make_preview, apply_preview,
                            read_document, write_document, _print_error,
                            ApplyError, OperationOutcomeUnknown, MAX_FILE_BYTES)  # noqa: E402


def parameter(device, field, name, value, kind=1, unit="", options="", role="", writable=True):
    return {"device": device, "field": field, "parent": 0, "generation": 1,
            "type": kind, "name": name, "value": value, "minimum": 0,
            "maximum": max(0, len(options.split(";")) - 1) if options else 100,
            "options": options, "unit": unit, "info": "", "precision": 0,
            "hash": 1000 + field, "hidden": False, "writable": writable,
            "commandAllowed": False, "role": role}


def reseal(snapshot):
    snapshot.pop("sha256", None)
    snapshot["sha256"] = hashlib.sha256(json.dumps(snapshot, ensure_ascii=False, sort_keys=True,
        separators=(",", ":"), allow_nan=False).encode()).hexdigest()


def reseal_preview(preview):
    preview.pop("previewSha256", None)
    preview["previewSha256"] = hashlib.sha256(json.dumps(preview, ensure_ascii=False, sort_keys=True,
        separators=(",", ":"), allow_nan=False).encode()).hexdigest()


class FakeRemote:
    def __init__(self, devices, params):
        self.devices = devices
        self.params = params
        self.local_ap = True
        self.maintenance = True
        self.connected = True
        self.armed = False
        self.status_age = 20
        self.fail_write_field = None
        self.unknown_write_field = None
        self.unreadable_fields = set()
        self.context_values = {236: {0: {2: 0, 3: 0, 4: 0}, 1: {2: 1, 3: 0, 4: 0}}}
        self.fail_write_context = None
        self.fail_selector_value = None
        self.actions = []
        self.operation = {"id": 0, "state": "idle"}
        self.next_id = 1
        self.model_id = 4
        self.model_id_known = True
        self.reset_model_on_discovery = False
        self.lose_write_response = None
        self.lose_poll_response = None
        self.invalid_write_readback = None

    def device(self, address):
        return next(d for d in self.devices if d["address"] == address)

    def run(self, action):
        self.actions.append(action)
        name = action["action"]
        if name == "read":
            d, f = int(action["device"]), int(action["field"])
            if (d, f) in self.unreadable_fields:
                raise ValueError("unavailable")
            if int(action["generation"]) != self.device(d)["generation"]:
                raise ValueError("stale generation")
            p = dict(self.params[d][f])
            if d == 238 and p.get('role') == 'modelMatch':
                self.model_id_known = True
            if d in self.context_values and f in next(iter(self.context_values[d].values()), {}):
                cursor = self.params[d][1]["value"]
                p["value"] = self.context_values[d][cursor][f]
                p["hash"] += cursor * 100
        elif name == "write":
            d, f = int(action["device"]), int(action["field"])
            if int(action["generation"]) != self.device(d)["generation"]:
                raise ValueError("stale generation")
            p = self.params[d][f]
            cursor = self.params[d].get(1, {}).get("value", -1)
            contextual = d in self.context_values and f in next(iter(self.context_values[d].values()), {})
            current_value = self.context_values[d][cursor][f] if contextual else p["value"]
            current_hash = p["hash"] + cursor * 100 if contextual else p["hash"]
            if int(action["hash"]) != current_hash or int(action["expected"]) != current_value:
                raise ValueError("stale descriptor")
            if f == self.fail_write_field or (d, cursor, f) == self.fail_write_context or (f == 1 and int(action["value"]) == self.fail_selector_value):
                self.operation = {"state": "failed", "error": "rejected", "device": d, "field": f}
                return
            if contextual:
                self.context_values[d][cursor][f] = int(action["value"])
                p = dict(p)
                p["value"] = int(action["value"])
            else:
                p["value"] = int(action["value"])
            self.device(d)["generation"] += 1
            for other in self.params[d].values():
                other["generation"] = self.device(d)["generation"]
            p = dict(p)
            if f == self.unknown_write_field:
                self.unreadable_fields.add((d, f))
                self.operation = {"state": "unknown", "error": "timeout", "device": d, "field": f}
                return
        elif name in {"discover", "status"}:
            if name == 'discover' and self.reset_model_on_discovery:
                self.model_id_known = False
            p = None
        else:
            raise AssertionError(f"unexpected API action: {name}")
        self.operation = {"state": "complete", "device": int(action.get("device", 0)),
                          "field": int(action.get("field", 0)), "parameter": p}
        if name == "write" and self.invalid_write_readback is not None:
            readback, unreadable = self.invalid_write_readback
            self.invalid_write_readback = None
            p.pop("value", None)
            p.update(readback)
            if unreadable:
                self.unreadable_fields.add((d, f))


class ApiHandler(BaseHTTPRequestHandler):
    remote = None

    def log_message(self, *_args):
        pass

    def send_json(self, obj, status=200):
        data = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def lose_response(self, mode):
        if mode == "drop":
            self.close_connection = True
            self.connection.shutdown(socket.SHUT_RDWR)
            self.connection.close()
        elif mode == "server-error":
            self.send_json({"message": "write acknowledgment unavailable"}, 500)
        else:
            self.send_json("unreadable operation response")

    def do_GET(self):
        if self.headers.get("Origin") != f"http://{self.headers.get('Host')}":
            self.send_json({"code": "ACCESS_DENIED", "path": "session", "message": "bad origin"}, 403)
            return
        if self.path == "/remote/devices":
            r = self.remote
            self.send_json({"devices": r.devices, "localAP": r.local_ap, "maintenance": r.maintenance,
                            "radioReviewPending": False, "rfKnown": True, "connected": r.connected,
                            "armedKnown": True, "armed": r.armed, "statusAgeMs": r.status_age,
                            "modelId": r.model_id if r.model_id_known else None})
        elif self.path == "/remote/operation":
            if self.remote.lose_poll_response:
                mode = self.remote.lose_poll_response
                self.remote.lose_poll_response = None
                self.lose_response(mode)
                return
            self.send_json(self.remote.operation)
        else:
            self.send_json({"message": "missing"}, 404)

    def do_POST(self):
        if self.headers.get("Origin") != f"http://{self.headers.get('Host')}":
            self.send_json({"code": "ACCESS_DENIED", "path": "session", "message": "bad origin"}, 403)
            return
        length = int(self.headers.get("Content-Length", "0"))
        values = {k: v[-1] for k, v in parse_qs(self.rfile.read(length).decode()).items()}
        if self.path == "/remote/session":
            self.send_json({"nonce": "a" * 32})
            return
        if self.path == "/remote/action":
            if self.headers.get("X-Remote-Nonce") != "a" * 32:
                self.send_json({"code": "ACCESS_DENIED", "path": "session", "message": "bad nonce"}, 403)
                return
            try:
                self.remote.run(values)
                operation_id = self.remote.next_id
                self.remote.next_id += 1
                self.remote.operation["id"] = operation_id
                if values["action"] == "write" and self.remote.lose_write_response:
                    endpoint, mode = self.remote.lose_write_response
                    self.remote.lose_write_response = None
                    if endpoint == "post":
                        self.lose_response(mode)
                        return
                    self.remote.lose_poll_response = mode
                self.send_json({"id": operation_id, "state": "queued"}, 202)
            except ValueError as exc:
                self.send_json({"code": "STALE", "path": "generation", "message": str(exc)}, 409)
            return
        self.send_json({"message": "missing"}, 404)


class RadioSnapshotTests(unittest.TestCase):
    def test_tx_model_id_is_captured_after_model_match_scan(self):
        self.fake.reset_model_on_discovery = True
        self.fake.params[238][3] = parameter(238, 3, 'Model Match (ID:4)', 1, 9, '', 'Off;On', 'modelMatch')
        snapshot = capture_snapshot(self.client)
        self.assertEqual(snapshot['selectedTXmodelID'], 4)
        self.assertEqual(make_preview(self.client, snapshot)['scope'], 'tx')

    def test_info_descriptors_do_not_make_persistent_capture_incomplete(self):
        self.fake.params[238][3] = parameter(238, 3, 'Version', 0, 12, writable=False)
        self.fake.params[238][3]['info'] = '4.1.0'
        snapshot = capture_snapshot(self.client)
        tx = next(d for d in snapshot['devices'] if d['role'] == 'tx')
        self.assertTrue(tx['complete'])
        self.assertEqual(tx['information'][0]['info'], '4.1.0')

    def test_serial_protocol_is_captured_and_included_in_live_preview(self):
        self.fake.devices[1]['fieldCount'] = 1
        self.fake.params[236] = {1: parameter(236, 1, 'Protocol', 1, 9, '', 'SBUS;CRSF')}
        self.fake.context_values.pop(236)
        snapshot = capture_snapshot(self.client)
        rx = next(d for d in snapshot['devices'] if d['role'] == 'rx')
        self.assertTrue(rx['complete'])
        self.assertEqual([f['name'] for f in rx['fields']], ['Protocol'])
        self.fake.params[236][1]['value'] = 0
        preview = make_preview(self.client, snapshot, device_role='rx')
        self.assertEqual(preview['changes'][0]['name'], 'Protocol')

    def test_non_pwm_rx_apply_requires_physical_receiver_confirmation(self):
        self.fake.devices[1]['fieldCount'] = 1
        self.fake.params[236] = {1: parameter(236, 1, 'Tlm Power', 1, 9, 'mW', '25;50')}
        self.fake.context_values.pop(236)
        snapshot = capture_snapshot(self.client)
        self.fake.params[236][1]['value'] = 0
        preview = make_preview(self.client, snapshot, device_role='rx')
        self.fake.actions.clear()
        with self.assertRaisesRegex(ValueError, 'physical receiver'):
            apply_preview(self.client, preview, confirmed=True)
        self.assertFalse(any(a['action'] == 'write' for a in self.fake.actions))
        self.assertEqual(apply_preview(self.client, preview, confirmed=True, receiver_confirmed=True)['status'], 'complete')

    def setUp(self):
        self.tx = {"address": 238, "name": "TX", "fieldCount": 4, "generation": 1,
                   "serial": 12, "hardwareVersion": 2, "softwareVersion": 5, "parameterVersion": 1}
        self.rx = {"address": 236, "name": "RX", "fieldCount": 4, "generation": 1,
                   "serial": 13, "hardwareVersion": 3, "softwareVersion": 6, "parameterVersion": 1}
        self.params = {
            238: {1: parameter(238, 1, "Max Power", 1, 9, "mW", "25/1000;250/1000", "power"),
                  2: parameter(238, 2, "Telemetry", 1, 9, "", "Std;Off", "telemetry"),
                  3: parameter(238, 3, "Folder", 0, 11),
                  4: parameter(238, 4, "Bind", 0, 13)},
            236: {1: parameter(236, 1, "Output Ch", 0, 9, "", "Output 1;Output 2"),
                  2: parameter(236, 2, "Input Ch", 0, 9, "", "CH1;CH2"),
                  3: parameter(236, 3, "Output Mode", 0, 9, "", "PWM;Serial"),
                  4: parameter(236, 4, "Invert", 0, 0)},
        }
        self.fake = FakeRemote([self.tx.copy(), self.rx.copy()], self.params)
        ApiHandler.remote = self.fake
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), ApiHandler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.client = RadioClient(f"http://127.0.0.1:{self.server.server_port}", poll_interval=0)
        self.client.open_session()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)

    def test_tx_only_capture_is_partial_and_excludes_commands_and_folders(self):
        self.fake.devices = [self.tx.copy()]
        self.fake.params = {238: self.params[238]}
        snapshot = capture_snapshot(self.client)
        self.assertFalse(snapshot["complete"])
        self.assertTrue(any(x["role"] == "rx" for x in snapshot["notCaptured"]))
        fields = snapshot["devices"][0]["fields"]
        self.assertEqual({field["name"] for field in fields}, {"Max Power", "Telemetry"})
        self.assertNotIn("Bind", json.dumps(snapshot))
        self.assertEqual(snapshot["selectedTXmodelID"], 4)
        self.assertEqual(fields[0]["descriptorHash"], 1001)
        self.assertFalse(any(action["action"] in {"command", "assign", "model"} for action in self.fake.actions))
        tx_preview = make_preview(self.client, snapshot, device_role="tx")
        self.assertEqual(tx_preview["scope"], "tx")
        self.assertFalse(snapshot["complete"])

    def test_client_requires_plain_http_origin_and_never_exposes_command_actions(self):
        with self.assertRaisesRegex(ValueError, "plain HTTP"):
            RadioClient("https://192.168.4.1")
        with self.assertRaisesRegex(ValueError, "never issue commands"):
            self.client.action(action="command", device=238, field=4)
        self.assertFalse(any(action["action"] == "command" for action in self.fake.actions))

    def test_client_uses_nonce_but_allows_lan_session(self):
        self.assertEqual(self.client.devices()["localAP"], True)
        self.fake.local_ap = False
        self.assertEqual(self.client.devices()["localAP"], False)
        self.client.action(action="status")
        self.client.nonce = ""
        with self.assertRaisesRegex(ValueError, "Open a Remote session first"):
            self.client.devices()
        with self.assertRaisesRegex(ValueError, "Open a Remote session first"):
            self.client.action(action="status")

    def test_error_output_masks_credentials_and_file_bound_is_enforced(self):
        output = io.StringIO()
        with contextlib.redirect_stderr(output):
            _print_error("ACCESS_DENIED wifiPassword: example-secret")
        self.assertNotIn("example-secret", output.getvalue())
        self.assertIn("masked", output.getvalue())
        with self.assertRaisesRegex(ValueError, "262144"):
            write_document(Path("too-large.remote-radio.json"), {"payload": "x" * MAX_FILE_BYTES},
                           ".remote-radio.json")

    def test_preview_maps_composite_power_by_exact_label_and_unit(self):
        source = capture_snapshot(self.client)
        target_power = parameter(238, 1, "Maximum Power", 0, 9, "mW", "25/1000;250/1000", "power")
        self.fake.params[238] = {1: target_power}
        self.tx["fieldCount"] = 1
        self.tx["generation"] = 1
        self.fake.devices[0].update(self.tx)
        source["devices"] = [dict(source["devices"][0], fields=[dict(source["devices"][0]["fields"][0], value=0, valueLabel="250/1000")])]
        source["complete"] = False
        source["notCaptured"] = []
        reseal(source)
        preview = make_preview(self.client, source, device_role="tx")
        self.assertEqual(preview["changes"][0]["field"], 1)
        self.assertEqual(preview["changes"][0]["value"], 1)
        target_power["options"] = "250/1000;250/1000"
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            make_preview(self.client, source, device_role="tx")

    def test_tx_model_id_mismatch_requires_manual_profile_selection(self):
        source = capture_snapshot(self.client)
        source["selectedTXmodelID"] = 0
        reseal(source)
        with self.assertRaisesRegex(ValueError, "model IDs differ"):
            make_preview(self.client, source, device_role="tx")
        self.assertFalse(any(action["action"] == "model" for action in self.fake.actions))

    def test_critical_rf_changes_and_rx_context_apply_are_rejected_for_ui_review(self):
        self.fake.params[238][2]["role"] = "rfRate"
        source = capture_snapshot(self.client)
        tx = next(device for device in source["devices"] if device["role"] == "tx")
        tx["fields"][1]["value"] = 0
        tx["fields"][1]["valueLabel"] = "Std"
        reseal(source)
        with self.assertRaisesRegex(ValueError, "explicit Remote UI"):
            make_preview(self.client, source, device_role="tx")

        rx_snapshot = capture_snapshot(self.client, selector_sweep=True)
        rx_preview = make_preview(self.client, rx_snapshot, device_role="rx", selector_sweep=True)
        self.assertTrue(all(not row["changes"] for row in rx_preview["outputPlan"]["contexts"]))

    def _rx_context_preview(self):
        self.fake.params[236][1]["value"] = 0
        self.fake.context_values[236] = {0: {2: 0, 3: 0, 4: 0}, 1: {2: 1, 3: 0, 4: 1}}
        source = capture_snapshot(self.client, selector_sweep=True)
        self.fake.context_values[236] = {0: {2: 1, 3: 0, 4: 1}, 1: {2: 0, 3: 0, 4: 0}}
        self.fake.devices[1]["generation"] += 1
        for param in self.fake.params[236].values():
            param["generation"] = self.fake.devices[1]["generation"]
        return source, make_preview(self.client, source, device_role="rx", selector_sweep=True)

    def test_rx_preview_translates_input_and_invert_for_each_output(self):
        source, preview = self._rx_context_preview()
        self.assertEqual(self.fake.params[236][1]["value"], 0)
        contexts = preview["outputPlan"]["contexts"]
        self.assertEqual(len(contexts), 2)
        self.assertEqual([row["selectorLabel"] for row in contexts], ["Output 1", "Output 2"])
        self.assertEqual([(change["key"], change["expected"], change["value"])
                          for row in contexts for change in row["changes"]],
                         [("inputch", 1, 0), ("invert", 1, 0),
                          ("inputch", 0, 1), ("invert", 0, 1)])
        self.assertFalse(any(change["key"] == "outputmode" for row in contexts for change in row["changes"]))

    def test_rx_mode_difference_rejects_entire_context_preview(self):
        self.fake.params[236][1]["value"] = 0
        source = capture_snapshot(self.client, selector_sweep=True)
        self.fake.context_values[236][1][3] = 1
        self.fake.devices[1]["generation"] += 1
        for param in self.fake.params[236].values():
            param["generation"] = self.fake.devices[1]["generation"]
        with self.assertRaisesRegex(ValueError, "Output Mode/Protocol"):
            make_preview(self.client, source, device_role="rx", selector_sweep=True)

    def test_rx_apply_requires_receiver_ack_and_restores_cursor_after_two_outputs(self):
        _, preview = self._rx_context_preview()
        with self.assertRaisesRegex(ValueError, "physical receiver"):
            apply_preview(self.client, preview, confirmed=True)
        self.fake.actions.clear()
        self.assertFalse(any(action["action"] == "write" for action in self.fake.actions))
        result = apply_preview(self.client, preview, confirmed=True, receiver_confirmed=True)
        self.assertEqual(result["status"], "complete")
        self.assertEqual(self.fake.params[236][1]["value"], 0)
        self.assertEqual(self.fake.context_values[236], {0: {2: 0, 3: 0, 4: 0}, 1: {2: 1, 3: 0, 4: 1}})

    def test_rx_failure_mid_second_output_rolls_back_and_restores_cursor(self):
        _, preview = self._rx_context_preview()
        self.fake.fail_write_context = (236, 1, 4)
        with self.assertRaises(Exception) as caught:
            apply_preview(self.client, preview, confirmed=True, receiver_confirmed=True)
        self.assertEqual(caught.exception.result["status"], "rolled-back")
        self.assertEqual(self.fake.params[236][1]["value"], 0)
        self.assertEqual(self.fake.context_values[236], {0: {2: 1, 3: 0, 4: 1}, 1: {2: 0, 3: 0, 4: 0}})

    def test_rx_apply_rejects_stale_generation_or_context_descriptor_before_changes(self):
        _, preview = self._rx_context_preview()
        self.fake.devices[1]["generation"] += 1
        for param in self.fake.params[236].values():
            param["generation"] = self.fake.devices[1]["generation"]
        with self.assertRaisesRegex(ValueError, "generation"):
            apply_preview(self.client, preview, confirmed=True, receiver_confirmed=True)
        self.assertEqual(self.fake.context_values[236][0][2], 1)

        _, preview = self._rx_context_preview()
        self.fake.params[236][3]["options"] = "PWM;Serial;I2C"
        with self.assertRaisesRegex(ValueError, "output-context descriptors"):
            apply_preview(self.client, preview, confirmed=True, receiver_confirmed=True)
        self.assertEqual(self.fake.context_values[236][0][2], 1)

    def test_rx_cursor_restore_failure_is_reported_as_outcome_unknown(self):
        _, preview = self._rx_context_preview()
        self.fake.fail_selector_value = 0
        with self.assertRaises(Exception) as caught:
            apply_preview(self.client, preview, confirmed=True, receiver_confirmed=True)
        self.assertEqual(caught.exception.result["status"], "outcome-unknown")
        self.assertEqual(self.fake.params[236][1]["value"], 1)

    def test_selector_sweep_captures_outputs_and_restores_original_cursor(self):
        self.fake.params[236][1]["value"] = 1
        self.fake.devices[1]["fieldCount"] = 4
        snapshot = capture_snapshot(self.client, selector_sweep=True)
        rx = next(device for device in snapshot["devices"] if device["role"] == "rx")
        self.assertEqual([row["selectorRaw"] for row in rx["outputContexts"]], [0, 1])
        self.assertEqual(self.fake.params[236][1]["value"], 1)
        self.assertEqual(self.fake.params[236][2]["value"], 0)
        self.assertEqual(len([a for a in self.fake.actions if a["action"] == "write"]), 2)

    def _numeric_receiver(self):
        self.fake.params[236][1].update(type=0, name="OutputCh", minimum=1, maximum=2,
                                       value=1, options="")
        self.fake.params[236][2].update(type=0, name="InputCh", minimum=1, maximum=16,
                                       value=1, options="")
        self.fake.context_values[236] = {1: {2: 1, 3: 0, 4: 0}, 2: {2: 16, 3: 0, 4: 1}}

    def test_numeric_selector_capture_requires_sweep(self):
        self._numeric_receiver()
        snapshot = capture_snapshot(self.client)
        rx = next(d for d in snapshot["devices"] if d["role"] == "rx")
        self.assertFalse(rx["complete"])
        self.assertFalse(snapshot["complete"])
        self.assertEqual(rx["fields"], [])
        with self.assertRaisesRegex(ValueError, "RX capture is incomplete"):
            make_preview(self.client, snapshot, device_role="rx")
        self.assertFalse(any(a["action"] == "write" for a in self.fake.actions))

    def test_numeric_selector_capture_preview_and_apply_multiple_outputs(self):
        self._numeric_receiver()
        source = capture_snapshot(self.client, selector_sweep=True)
        rx = next(d for d in source["devices"] if d["role"] == "rx")
        self.assertTrue(source["complete"])
        self.assertEqual([r["selectorRaw"] for r in rx["outputContexts"]], [1, 2])
        self.assertEqual([r["fields"]["inputch"]["value"] for r in rx["outputContexts"]], [1, 16])
        self.assertEqual(self.fake.params[236][1]["value"], 1)
        self.assertEqual([a["value"] for a in self.fake.actions if a["action"] == "write"], ["2", "1"])
        self.fake.context_values[236] = {1: {2: 16, 3: 0, 4: 1}, 2: {2: 1, 3: 0, 4: 0}}
        preview = make_preview(self.client, source, device_role="rx", selector_sweep=True)
        self.assertEqual(preview["changes"], [])
        self.assertEqual(preview["priorValues"], [])
        self.assertEqual([(c["key"], c["expected"], c["value"])
                          for r in preview["outputPlan"]["contexts"] for c in r["changes"]],
                         [("inputch", 16, 1), ("invert", 1, 0),
                          ("inputch", 1, 16), ("invert", 0, 1)])
        result = apply_preview(self.client, preview, confirmed=True, receiver_confirmed=True)
        self.assertEqual(result["status"], "complete")
        self.assertEqual(self.fake.context_values[236], {1: {2: 1, 3: 0, 4: 0}, 2: {2: 16, 3: 0, 4: 1}})
        self.assertEqual(self.fake.params[236][1]["value"], 1)

    def test_numeric_selector_invalid_ranges_never_write(self):
        self._numeric_receiver()
        for minimum, maximum in ((1, 17), (2, 1)):
            with self.subTest(minimum=minimum, maximum=maximum):
                self.fake.params[236][1].update(minimum=minimum, maximum=maximum)
                snapshot = capture_snapshot(self.client, selector_sweep=True)
                rx = next(d for d in snapshot["devices"] if d["role"] == "rx")
                self.assertFalse(rx["complete"])
                self.assertEqual(rx["outputContexts"], [])
                self.assertFalse(any(a["action"] == "write" for a in self.fake.actions))

    def test_numeric_selector_failure_restores_original_cursor(self):
        self._numeric_receiver()
        self.fake.context_values[236][2][2] = "unreadable"
        snapshot = capture_snapshot(self.client, selector_sweep=True)
        rx = next(d for d in snapshot["devices"] if d["role"] == "rx")
        self.assertFalse(rx["complete"])
        self.assertEqual(self.fake.params[236][1]["value"], 1)
        self.assertEqual([a["value"] for a in self.fake.actions if a["action"] == "write"], ["2", "1"])

    def test_numeric_selector_preview_rejects_unadvertised_context_or_input(self):
        self._numeric_receiver()
        source = capture_snapshot(self.client, selector_sweep=True)
        rx = next(d for d in source["devices"] if d["role"] == "rx")
        rx["outputContexts"][1]["selectorRaw"] = 3
        reseal(source)
        with self.assertRaisesRegex(ValueError, "unrepresented output rows"):
            make_preview(self.client, source, device_role="rx", selector_sweep=True)
        rx["outputContexts"][1]["selectorRaw"] = 2
        rx["outputContexts"][1]["fields"]["inputch"]["value"] = 17
        reseal(source)
        with self.assertRaisesRegex(ValueError, "outside the live target range"):
            make_preview(self.client, source, device_role="rx", selector_sweep=True)
        self.assertEqual(self.fake.context_values[236], {1: {2: 1, 3: 0, 4: 0}, 2: {2: 16, 3: 0, 4: 1}})
        self.assertEqual(self.fake.params[236][1]["value"], 1)

    def test_numeric_selector_apply_failure_rolls_back_and_restores_cursor(self):
        self._numeric_receiver()
        source = capture_snapshot(self.client, selector_sweep=True)
        self.fake.context_values[236] = {1: {2: 16, 3: 0, 4: 1}, 2: {2: 1, 3: 0, 4: 0}}
        preview = make_preview(self.client, source, device_role="rx", selector_sweep=True)
        self.fake.fail_write_context = (236, 2, 4)
        with self.assertRaises(ApplyError) as caught:
            apply_preview(self.client, preview, confirmed=True, receiver_confirmed=True)
        self.assertEqual(caught.exception.result["status"], "rolled-back")
        self.assertEqual(self.fake.context_values[236], {1: {2: 16, 3: 0, 4: 1}, 2: {2: 1, 3: 0, 4: 0}})
        self.assertEqual(self.fake.params[236][1]["value"], 1)

    def test_selector_sweep_marks_unrecognized_per_output_fields_incomplete(self):
        self.fake.params[236][5] = parameter(236, 5, "Vendor Output Function", 0, 9, "", "A;B")
        self.fake.devices[1]["fieldCount"] = 5
        self.fake.context_values[236] = {0: {2: 0, 3: 0, 4: 0, 5: 0},
                                         1: {2: 1, 3: 0, 4: 0, 5: 1}}
        snapshot = capture_snapshot(self.client, selector_sweep=True)
        rx = next(device for device in snapshot["devices"] if device["role"] == "rx")
        self.assertFalse(rx["complete"])
        self.assertTrue(any("unsupported selector-dependent persistent field" in issue["reason"]
                            for issue in rx["notCaptured"]))

    def _two_change_preview(self):
        self.tx["fieldCount"] = 2
        self.fake.devices[0]["fieldCount"] = 2
        self.fake.devices[0]["generation"] = 1
        self.fake.params[238] = {
            1: parameter(238, 1, "Telemetry", 1, 9, "", "Std;Off", "telemetry"),
            2: parameter(238, 2, "Dynamic", 1, 9, "", "Off;On", "dynamic"),
        }
        for field in self.fake.params[238].values():
            field["generation"] = 1
        source = capture_snapshot(self.client)
        tx = next(device for device in source["devices"] if device["role"] == "tx")
        for field in tx["fields"]:
            field["value"] = 0
            field["valueLabel"] = field["options"].split(";")[0]
        reseal(source)
        return make_preview(self.client, source, device_role="tx")

    def test_apply_rejects_stale_generation_and_changed_options_before_any_write(self):
        preview = self._two_change_preview()
        self.fake.devices[0]["generation"] += 1
        self.fake.params[238][1]["generation"] += 1
        self.fake.params[238][2]["generation"] += 1
        with self.assertRaisesRegex(ValueError, "generation"):
            apply_preview(self.client, preview, confirmed=True)
        self.assertFalse(any(action["action"] == "write" for action in self.fake.actions))

        preview = self._two_change_preview()
        self.fake.params[238][2]["options"] = "Different;Choices"
        with self.assertRaisesRegex(ValueError, "descriptors or options"):
            apply_preview(self.client, preview, confirmed=True)
        self.assertFalse(any(action["action"] == "write" for action in self.fake.actions))

    def test_apply_rolls_back_verified_prior_write_after_later_failure(self):
        preview = self._two_change_preview()
        self.fake.fail_write_field = 2
        with self.assertRaises(Exception) as caught:
            apply_preview(self.client, preview, confirmed=True)
        self.assertEqual(caught.exception.result["status"], "rolled-back")
        self.assertEqual(self.fake.params[238][1]["value"], 1)
        self.assertEqual(caught.exception.result["rollback"], [{"field": 1, "status": "restored"}])

    def test_confirmed_apply_writes_and_verifies_the_entire_preflighted_group(self):
        preview = self._two_change_preview()
        result = apply_preview(self.client, preview, confirmed=True)
        self.assertEqual(result["status"], "complete")
        self.assertEqual(result["applied"], [1, 2])
        self.assertEqual([self.fake.params[238][field]["value"] for field in (1, 2)], [0, 0])

    def test_lost_write_ack_with_unreadable_result_is_outcome_unknown(self):
        preview = self._two_change_preview()
        self.fake.unknown_write_field = 1
        with self.assertRaises(Exception) as caught:
            apply_preview(self.client, preview, confirmed=True)
        self.assertEqual(caught.exception.result["status"], "outcome-unknown")
        self.assertEqual(self.fake.params[238][1]["value"], 0)

    def test_lost_write_response_reconciles_and_rolls_back_without_retry(self):
        for endpoint in ("post", "poll"):
            for mode in ("drop", "unreadable", "server-error"):
                with self.subTest(endpoint=endpoint, mode=mode):
                    preview = self._two_change_preview()
                    self.fake.actions.clear()
                    self.fake.lose_write_response = (endpoint, mode)
                    with self.assertRaises(ApplyError) as caught:
                        apply_preview(self.client, preview, confirmed=True)
                    self.assertEqual(caught.exception.result["status"], "rolled-back")
                    self.assertEqual(caught.exception.result["applied"], [1])
                    self.assertEqual(self.fake.params[238][1]["value"], 1)
                    self.assertEqual(self.fake.params[238][2]["value"], 1)
                    self.assertEqual([(a["field"], a["value"]) for a in self.fake.actions if a["action"] == "write"],
                                     [("1", "0"), ("1", "1")])

    def test_lost_write_network_response_with_unreadable_result_is_unknown(self):
        for endpoint in ("post", "poll"):
            with self.subTest(endpoint=endpoint):
                self.fake.unreadable_fields.clear()
                preview = self._two_change_preview()
                self.fake.actions.clear()
                self.fake.unknown_write_field = 1
                self.fake.lose_write_response = (endpoint, "drop")
                with self.assertRaises(ApplyError) as caught:
                    apply_preview(self.client, preview, confirmed=True)
                self.assertEqual(caught.exception.result["status"], "outcome-unknown")
                self.assertEqual(self.fake.params[238][1]["value"], 0)
                self.assertEqual(self.fake.params[238][2]["value"], 1)
                self.assertEqual([(a["field"], a["value"]) for a in self.fake.actions if a["action"] == "write"],
                                 [("1", "0")])

    def test_definite_write_rejection_and_read_network_failure_stay_value_errors(self):
        param = dict(self.fake.params[238][1], hash=0)
        with self.assertRaises(ValueError) as caught:
            self.client.write_parameter(param, 1, 0)
        self.assertNotIsInstance(caught.exception, OperationOutcomeUnknown)
        self.assertEqual(self.fake.params[238][1]["value"], 1)
        self.fake.lose_poll_response = "drop"
        with self.assertRaises(ValueError) as caught:
            self.client.read_parameter(238, 1, 1)
        self.assertNotIsInstance(caught.exception, OperationOutcomeUnknown)
        self.assertEqual(self.client.read_parameter(238, 1, 1)["value"], 1)

    def test_invalid_completed_write_value_reconciles_without_replaying_post(self):
        for readback in ({}, {"value": "0"}, {"value": 0.0}, {"value": False}, {"value": 1}):
            for unreadable in (False, True):
                with self.subTest(readback=readback, unreadable=unreadable):
                    self.fake.unreadable_fields.clear()
                    preview = self._two_change_preview()
                    self.fake.actions.clear()
                    self.fake.invalid_write_readback = (readback, unreadable)
                    with self.assertRaises(ApplyError) as caught:
                        apply_preview(self.client, preview, confirmed=True)
                    result = caught.exception.result
                    if unreadable:
                        self.assertEqual(result["status"], "outcome-unknown")
                        self.assertEqual(self.fake.params[238][1]["value"], 0)
                        writes = [("1", "0")]
                    else:
                        self.assertEqual(result["status"], "rolled-back")
                        self.assertEqual(result["applied"], [1])
                        self.assertEqual(self.fake.params[238][1]["value"], 1)
                        writes = [("1", "0"), ("1", "1")]
                    self.assertEqual(self.fake.params[238][2]["value"], 1)
                    self.assertEqual([(a["field"], a["value"]) for a in self.fake.actions if a["action"] == "write"], writes)

    def test_selector_sweep_requires_maintenance_disarmed_connected_state(self):
        self.fake.armed = True
        with self.assertRaisesRegex(ValueError, "disarmed"):
            capture_snapshot(self.client, selector_sweep=True)
        self.assertFalse(any(action["action"] == "write" for action in self.fake.actions))

    def test_apply_requires_maintenance_fresh_disarmed_status(self):
        preview = self._two_change_preview()
        self.fake.maintenance = False
        with self.assertRaisesRegex(ValueError, "maintenance"):
            apply_preview(self.client, preview, confirmed=True)
        self.assertFalse(any(action["action"] == "write" for action in self.fake.actions))

    def test_snapshot_file_allowlist_and_checksum_are_enforced(self):
        snapshot = capture_snapshot(self.client)
        with tempfile.TemporaryDirectory() as directory:
            wrong = Path(directory) / "source.json"
            wrong.write_text(json.dumps(snapshot))
            with self.assertRaisesRegex(ValueError, "remote-radio.json"):
                read_document(wrong, ".remote-radio.json")
            good = Path(directory) / "model.remote-radio.json"
            write_document(good, snapshot, ".remote-radio.json")
            loaded = read_document(good, ".remote-radio.json")
            loaded["selectedTXmodelID"] = 0
            with self.assertRaisesRegex(ValueError, "checksum"):
                make_preview(self.client, loaded, device_role="tx")
            deep = Path(directory) / "deep.remote-radio.json"
            deep.write_text("{" * 33 + "}" * 33)
            with self.assertRaisesRegex(ValueError, "nesting"):
                read_document(deep, ".remote-radio.json")

    def test_selector_failure_still_attempts_to_restore_original_cursor(self):
        self.fake.params[236][1]["value"] = 1
        self.fake.fail_write_field = 1
        snapshot = capture_snapshot(self.client, selector_sweep=True)
        rx = next(device for device in snapshot["devices"] if device["role"] == "rx")
        self.assertEqual(self.fake.params[236][1]["value"], 1)
        self.assertFalse(rx["complete"])
        self.assertTrue(any("output" in issue["reason"].lower() for issue in rx["notCaptured"]))

    def test_unconfirmed_and_expired_previews_never_write(self):
        preview = self._two_change_preview()
        with self.assertRaisesRegex(ValueError, "confirmation"):
            apply_preview(self.client, preview, confirmed=False)
        preview["expiresAt"] = 0
        reseal_preview(preview)
        with self.assertRaisesRegex(ValueError, "expired"):
            apply_preview(self.client, preview, confirmed=True)
        self.assertFalse(any(action["action"] == "write" for action in self.fake.actions))

    def test_snapshot_field_and_context_caps_are_explicit(self):
        self.fake.devices = [self.tx.copy(), self.rx.copy(), {**self.rx, "address": 10},
                             {**self.rx, "address": 11}, {**self.rx, "address": 12}]
        with self.assertRaisesRegex(ValueError, "four-device"):
            capture_snapshot(self.client)

        self.fake.devices = [self.rx.copy()]
        self.fake.params = {236: {1: parameter(236, 1, "Output Ch", 0, 9, "", ";".join(map(str, range(17))))}}
        self.fake.devices[0]["fieldCount"] = 1
        self.fake.params[236][1]["maximum"] = 16
        snapshot = capture_snapshot(self.client, selector_sweep=True)
        rx = snapshot["devices"][0]
        self.assertFalse(rx["complete"])
        self.assertIn("exceeds 16", rx["notCaptured"][0]["reason"])
        self.assertFalse(any(action["action"] == "write" for action in self.fake.actions))

        self.fake.devices = [self.tx.copy()]
        self.fake.devices[0]["fieldCount"] = 65
        self.fake.params = {238: {field: parameter(238, field, f"Field {field}", 0)
                                  for field in range(1, 66)}}
        snapshot = capture_snapshot(self.client)
        tx = snapshot["devices"][0]
        self.assertEqual(len(tx["fields"]), 64)
        self.assertFalse(tx["complete"])
        self.assertTrue(any("64 persistent" in issue["reason"] for issue in tx["notCaptured"]))


if __name__ == "__main__":
    unittest.main()
