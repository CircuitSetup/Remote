"""Fail-closed EdgeTX YAML subset converter for Remote model profiles."""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
import re
import stat
import zipfile

import yaml
from yaml.tokens import AliasToken, AnchorToken, TagToken


MAX_INPUT_BYTES = 8 * 1024 * 1024
MAX_ZIP_BYTES = 16 * 1024 * 1024
MAX_ZIP_FILES = 128
VERSIONS = {"2.10.6", "2.12.4"}
KNOWN_SWITCH_POSITIONS = {
    "tx16s": {"SA": 3, "SB": 3, "SC": 3, "SD": 3, "SE": 3, "SG": 3, "SF": 2},
    "boxer": {"SA": 2, "SB": 3, "SC": 3, "SD": 2},
}
MODULE_TYPES = {
    "TYPE_NONE", "TYPE_PPM", "TYPE_XJT_PXX1", "TYPE_ISRM_PXX2", "TYPE_DSM2",
    "TYPE_CROSSFIRE", "TYPE_MULTIMODULE", "TYPE_R9M_PXX1", "TYPE_R9M_PXX2",
    "TYPE_R9M_LITE_PXX1", "TYPE_R9M_LITE_PXX2", "TYPE_GHOST",
    "TYPE_R9M_LITE_PRO_PXX2", "TYPE_SBUS", "TYPE_XJT_LITE_PXX2",
    "TYPE_FLYSKY_AFHDS2A", "TYPE_FLYSKY_AFHDS3", "TYPE_LEMON_DSMP",
}
ROOT_KEYS = {
    "semver", "header", "timers", "noGlobalFunctions", "thrTrim", "trimInc",
    "displayTrims", "ignoreSensorIds", "showInstanceIds", "disableThrottleWarning",
    "enableCustomThrottleWarning", "customThrottleWarningPosition", "beepANACenter",
    "extendedLimits", "extendedTrims", "throttleReversed", "checklistInteractive",
    "flightModeData", "mixData", "limitData", "inputNames", "expoData", "curves",
    "points", "logicalSw", "customFn", "swashR", "thrTraceSrc", "switchWarning",
    "switchWarningState", "thrTrimSw", "potsWarnMode", "potsWarnEnabled",
    "jitterFilter", "potsWarnPosition", "displayChecklist", "gvars", "telemetryProtocol",
    "screens", "varioData", "rssiSource", "voltsSource", "altitudeSource", "rfAlarms",
    "disableTelemetryWarning", "moduleData", "failsafeChannels", "trainerData", "scriptsData",
    "telemetrySensors", "toplcdTimer", "screenData", "topbarData", "topbarWidgetWidth",
    "view", "modelRegistrationID", "hatsMode", "customSwitches", "cfsGroupOn",
    "usbJoystickExtMode", "usbJoystickIfMode", "usbJoystickCircularCut", "usbJoystickCh",
    "radioThemesDisabled", "radioGFDisabled", "radioTrainerDisabled", "modelHeliDisabled",
    "modelFMDisabled", "modelCurvesDisabled", "modelGVDisabled", "modelLSDisabled",
    "modelSFDisabled", "modelCustomScriptsDisabled", "modelTelemetryDisabled",
}
MIX_KEYS = {
    "destCh", "srcRaw", "weight", "swtch", "curve", "delayPrec", "delayUp", "delayDown",
    "speedPrec", "speedUp", "speedDown", "carryTrim", "mltpx", "mixWarn", "flightModes",
    "offset", "name",
}
EXPO_KEYS = {"srcRaw", "scale", "mode", "chn", "swtch", "flightModes", "weight", "offset",
             "curve", "trimSource", "carryTrim", "name"}
LIMIT_KEYS = {"min", "max", "offset", "ppmCenter", "symetrical", "revert", "curve", "name",
              "failsafe"}


@dataclass
class ConversionError:
    path: str
    message: str


@dataclass
class ConversionResult:
    profile: dict | None = None
    errors: list[ConversionError] = field(default_factory=list)
    report: list[str] = field(default_factory=list)


class _StrictLoader(yaml.SafeLoader):
    pass


def _mapping(loader, node, deep=False):
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if not isinstance(key, (str, int)) or key in result:
            raise ValueError(f"duplicate or unsupported mapping key: {key!r}")
        result[key] = loader.construct_object(value_node, deep=deep)
    return result


_StrictLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _mapping)


def _fail(errors, path, message):
    errors.append(ConversionError(path, message))


def _source_radio(source_radio):
    name = str(source_radio).casefold().replace(" ", "")
    return name in {"tx16s", "boxer"}


def _mapping_ok(mapping, source_radio, errors):
    if not isinstance(mapping, dict):
        _fail(errors, "mapping", "A complete physical input mapping is required")
        return False
    axes = mapping.get("axes")
    gimbals, switches = mapping.get("gimbals"), mapping.get("switches")
    if (not isinstance(axes, dict) or set(axes) != {"Ail", "Ele", "Rud", "Thr"} or
            any(type(v) is not int for v in axes.values()) or sorted(axes.values()) != [1, 2, 3, 4]):
        _fail(errors, "mapping.axes", "Map Ail, Ele, Rud and Thr exactly once to native axis IDs 1–4")
    if not isinstance(gimbals, list) or len(gimbals) != 4 or any(type(x) is not int or not 0 <= x <= 16 for x in gimbals):
        _fail(errors, "mapping.gimbals", "Four output destinations in the range 0–16 are required")
    if isinstance(gimbals, list) and len(gimbals) == 4 and len([x for x in gimbals if x]) != len(set(x for x in gimbals if x)):
        _fail(errors, "mapping.gimbals", "Enabled physical axes must use distinct output channels")
    if not isinstance(switches, list) or len(switches) != 12 or any(type(x) is not int or not 0 <= x <= 16 for x in switches):
        _fail(errors, "mapping.switches", "Twelve explicit switch destinations in the range 0–16 are required")
    if isinstance(gimbals, list) and isinstance(switches, list) and len(gimbals) == 4 and len(switches) == 12:
        assigned = [x for x in [*gimbals, *switches] if x]
        if len(assigned) != len(set(assigned)):
            _fail(errors, "mapping", "Enabled physical inputs must use distinct output channels")
    switch_map = mapping.get("switchMap")
    if not isinstance(switch_map, dict):
        _fail(errors, "mapping.switchMap", "Explicit two-position switch mappings are required")
        switch_map = {}
    for name, item in switch_map.items():
        radio_key = str(source_radio).casefold().replace(" ", "")
        known_positions = KNOWN_SWITCH_POSITIONS.get(radio_key, {})
        if not isinstance(name, str) or name not in known_positions:
            _fail(errors, f"mapping.switchMap.{name}", f"{source_radio} switch identity is unknown or unsupported")
            continue
        if known_positions[name] != 2:
            _fail(errors, f"mapping.switchMap.{name}",
                  f"{name} is a known three-position {source_radio} control and cannot be reduced to two positions")
        if (not isinstance(item, dict) or type(item.get("input")) is not int or
                not 0 <= item["input"] < 12 or type(item.get("reverse")) is not bool or
                type(item.get("positions")) is not int or item.get("positions") != known_positions[name]):
            _fail(errors, f"mapping.switchMap.{name}", "Specify a native input index, reversal and verified two-position mapping")
    return not errors


def _read_yaml(data):
    if len(data) > MAX_INPUT_BYTES:
        raise ValueError("input exceeds the 8 MiB expanded size limit")
    if any(isinstance(t, (AliasToken, AnchorToken, TagToken)) for t in yaml.scan(data)):
        raise ValueError("YAML aliases, anchors and explicit tags are not admitted")
    doc = yaml.load(data, Loader=_StrictLoader)
    if not isinstance(doc, dict):
        raise ValueError("YAML document root must be a mapping")
    return doc


def _read_bounded(path, limit):
    with Path(path).open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f"input exceeds the {limit // (1024 * 1024)} MiB read limit")
    return data


def _load_source(path, mapping, source_version, errors):
    path = Path(path)
    if path.suffix.casefold() != ".etx":
        try:
            return _read_yaml(_read_bounded(path, MAX_INPUT_BYTES)), path.name
        except Exception as exc:
            _fail(errors, "source", str(exc))
            return None, path.name
    try:
        if path.stat().st_size > MAX_ZIP_BYTES:
            raise ValueError("ZIP file exceeds the 16 MiB archive size limit")
        with zipfile.ZipFile(path) as archive:
            infos = archive.infolist()
            if len(infos) > MAX_ZIP_FILES:
                raise ValueError("ZIP contains too many entries")
            seen = set()
            total = 0
            yaml_files = []
            for info in infos:
                raw = info.filename
                posix = PurePosixPath(raw)
                norm = str(posix).casefold()
                if (not raw or "\x00" in raw or "\\" in raw or posix.is_absolute() or ".." in posix.parts or
                        (posix.parts and ":" in posix.parts[0]) or norm in seen):
                    raise ValueError(f"unsafe or duplicate ZIP path: {raw}")
                if stat.S_IFMT(info.external_attr >> 16) == stat.S_IFLNK:
                    raise ValueError(f"symbolic-link ZIP member is unsupported: {raw}")
                seen.add(norm)
                if info.flag_bits & 1:
                    raise ValueError(f"encrypted ZIP member is unsupported: {raw}")
                total += info.file_size
                if total > MAX_ZIP_BYTES:
                    raise ValueError("ZIP expanded data exceeds 16 MiB")
                if not info.is_dir() and PurePosixPath(raw).suffix.casefold() in {".yml", ".yaml"}:
                    yaml_files.append(raw)
            models = [n for n in yaml_files if PurePosixPath(n).name.casefold() not in {"radio.yml", "radio.yaml"}]
            selected = mapping.get("model") if isinstance(mapping, dict) else None
            if selected is None and len(models) == 1:
                selected = models[0]
            if selected not in models:
                raise ValueError("select one model by exact archive path; model selection is ambiguous or missing")
            info = archive.getinfo(selected)
            if info.file_size > MAX_INPUT_BYTES:
                raise ValueError("selected YAML exceeds 8 MiB")
            with archive.open(info) as stream:
                data = stream.read(MAX_INPUT_BYTES + 1)
            return _read_yaml(data), selected
    except Exception as exc:
        _fail(errors, "source.zip", str(exc))
        return None, str(path)


def _int(value, path, errors, lo, hi):
    if type(value) is not int or not lo <= value <= hi:
        _fail(errors, path, f"Expected an integer from {lo} through {hi}")
        return None
    return value


def _index_key(value):
    if type(value) is int and value >= 0:
        return value
    if isinstance(value, str) and value.isdigit():
        return int(value)
    return None


def _constant_int(value):
    if type(value) is int:
        return value
    if isinstance(value, str) and re.fullmatch(r"-?\d+", value):
        return int(value)
    return None


def _flight_modes_unconditional(row):
    value = row.get("flightModes", 0)
    return type(value) is int and value == 0 or value == "000000000"


def _crsf_model_id(doc, errors):
    modules = doc.get("moduleData", {})
    if not isinstance(modules, dict):
        _fail(errors, "moduleData", "Cannot determine the active CRSF module from moduleData")
        return None
    indexed = {}
    for key, module in modules.items():
        index = _index_key(key)
        if index not in (0, 1) or index in indexed or not isinstance(module, dict):
            _fail(errors, "moduleData", "Invalid moduleData; expected internal and external module records")
            return None
        kind = module.get("type", "TYPE_NONE")
        if not isinstance(kind, str) or kind not in MODULE_TYPES:
            _fail(errors, f"moduleData.{index}.type", "Invalid module type in moduleData")
            return None
        indexed[index] = kind
    crsf = [index for index, kind in indexed.items() if kind == "TYPE_CROSSFIRE"]
    if len(crsf) != 1:
        _fail(errors, "moduleData", "Cannot determine one active CRSF module")
        return None
    module_id = crsf[0]
    header = doc.get("header")
    if not isinstance(header, dict):
        _fail(errors, "header.modelId", "Cannot determine the active CRSF module model ID")
        return None
    if "modelId" not in header:
        return 0
    ids = header["modelId"]
    id_indexes = [_index_key(key) for key in ids] if isinstance(ids, dict) else []
    if (not isinstance(ids, dict) or len(set(id_indexes)) != len(ids) or
            any(index not in (0, 1) for index in id_indexes)):
        _fail(errors, "header.modelId", "Cannot determine the active CRSF module model ID")
        return None
    missing = object()
    entry = next((value for key, value in ids.items() if _index_key(key) == module_id), missing)
    if entry is missing:
        return 0
    if not isinstance(entry, dict):
        _fail(errors, f"header.modelId.{module_id}", "Invalid model ID record for the active CRSF module")
        return None
    value = entry.get("val", 0)
    if type(value) is not int or not 0 <= value <= 63:
        _fail(errors, f"header.modelId.{module_id}.val", "Invalid or missing model ID for the active CRSF module")
        return None
    return value


def _source(value, mapping, path, errors):
    if not isinstance(value, str):
        _fail(errors, path, "Expected a supported physical control source")
        return None
    reverse = value.startswith("!")
    tag = value[1:] if reverse else value
    axes = mapping["axes"]
    if tag == "MAX":
        if reverse:
            _fail(errors, path, "Inverted constants are unsupported; use a negative mix weight explicitly")
        return 25
    if tag in axes:
        if reverse:
            _fail(errors, path, "Inverted analog sources are unsupported")
            return None
        return axes[tag]
    input_source = re.fullmatch(r"I(\d+)", tag)
    if input_source and int(input_source.group(1)) in mapping.get("_inputMap", {}):
        if reverse:
            _fail(errors, path, "Inverted input sources are unsupported")
            return None
        source = mapping["_inputMap"][int(input_source.group(1))]
        return source
    switches = mapping["switchMap"]
    if tag in switches:
        item = switches[tag]
        return (5 + item["input"]) | (0x80 if bool(item["reverse"]) ^ reverse else 0)
    logical = re.fullmatch(r"ls\(([1-9]|1[0-6])\)", tag)
    if logical:
        index = int(logical.group(1)) - 1
        if index in mapping.get("_logicalMap", {}):
            return mapping["_logicalMap"][index] | (0x80 if reverse else 0)
    _fail(errors, path, f"Unsupported source {value!r}; feedback, timers, telemetry and nonmapped controls are rejected")
    return None


def _condition(value, mapping, path, errors):
    if not isinstance(value, str):
        _fail(errors, path, "Expected a mapped two-position switch or earlier logical switch condition")
        return None
    reverse = value.startswith("!")
    tag = value[1:] if reverse else value
    logical = re.fullmatch(r"L0?([1-9]|1[0-6])", tag)
    if logical:
        index = int(logical.group(1)) - 1
        if index in mapping.get("_logicalMap", {}):
            return mapping["_logicalMap"][index] | (0x80 if reverse else 0)
        _fail(errors, path, f"Unsupported or forward-referenced logical switch {value!r}")
        return None
    switch = re.fullmatch(r"([A-Z][A-Z0-9]*?)([012])", tag)
    if switch and switch.group(1) in mapping["switchMap"]:
        if switch.group(2) == "1":
            _fail(errors, path, "Middle-position switch conditions are unsupported")
            return None
        item = mapping["switchMap"][switch.group(1)]
        invert_position = switch.group(2) == "0"
        return (5 + item["input"]) | (0x80 if bool(item["reverse"]) ^ reverse ^ invert_position else 0)
    _fail(errors, path, f"Unsupported switch condition {value!r}; use a mapped two-position control or earlier logical switch")
    return None


def _default_profile(source_radio, source_version, source_name, mapping):
    return {
        "format": "remote-model", "schemaVersion": 1, "sourceContractVersion": 2,
        "source": {"firmware": f"EdgeTX {source_version}", "mapping": f"{source_radio}: {source_name}"},
        "inputs": [{"reverse": False, "deadband": 0, "expo": 0, "limits": [1000, 2000]} for _ in range(4)],
        "routing": {"gimbals": list(mapping["gimbals"]), "switches": list(mapping["switches"])},
        "tolerances": {"throttleIdleDeadband": 0}, "localActions": [0] * 12,
        "logical": [[0, 0, 0, 0, 0] for _ in range(16)],
        "mixes": [[0, 0, 0, 0, 0, 0] for _ in range(16)],
        "display": {"source": 14, "decimalPlaces": 255, "multiplier": 1, "offset": 0},
        "vehicle": {"motorPoles": 4, "gearRatio": 6.55, "tireDiameterMm": 64, "scaleFactor": 10, "rpmType": 0},
        "speedUnits": "kmh",
        "radio": {"crsfUpdateHz": 250, "modelId": 0, "telemetry": "Std", "maxPower": {"label": "250", "unit": "mW"}, "dynamic": "Off"},
    }


def _convert(doc, source_radio, source_version, source_name, mapping, errors):
    if doc.get("semver") != source_version:
        _fail(errors, "semver", "YAML semver must exactly match the selected supported adapter version")
    for key in doc:
        if key not in ROOT_KEYS:
            _fail(errors, key, "Unknown model field")
    for key in ("timers", "flightModeData", "gvars", "customFn", "scriptsData", "telemetrySensors", "customSwitches", "customScreens", "screenData", "topbarData", "trainerData", "failsafeChannels", "curves", "points"):
        if doc.get(key):
            _fail(errors, key, "This model behavior is not representable by the Remote")
    for key in ("thrTrim", "throttleReversed", "extendedLimits", "extendedTrims"):
        if doc.get(key) not in (None, 0, False):
            _fail(errors, key, "This global or throttle behavior is unsupported")
    header = doc.get("header")
    if not isinstance(header, dict):
        _fail(errors, "header", "Expected EdgeTX model header")
    elif set(header) - {"name", "bitmap", "labels", "modelId"}:
        for key in set(header) - {"name", "bitmap", "labels", "modelId"}:
            _fail(errors, f"header.{key}", "Unknown header field")
    profile = _default_profile(source_radio, source_version, source_name, mapping)
    profile["radio"]["modelId"] = _crsf_model_id(doc, errors) or 0

    for axis in ("Ail", "Ele", "Rud", "Thr"):
        if axis not in mapping["axes"]:
            _fail(errors, f"mapping.axes.{axis}", "Physical axis mapping is required")
    expo = doc.get("expoData", [])
    if not isinstance(expo, list):
        _fail(errors, "expoData", "Expected a sequence")
        expo = []
    seen_expo = set()
    mapping["_inputMap"] = {}
    for i, row in enumerate(expo):
        path = f"expoData[{i}]"
        if not isinstance(row, dict):
            _fail(errors, path, "Expected an expo mapping")
            continue
        for key in set(row) - EXPO_KEYS:
            _fail(errors, f"{path}.{key}", "Unknown expo field")
        source = _source(row.get("srcRaw"), mapping, f"{path}.srcRaw", errors)
        input_channel = row.get("chn")
        if source is None or not 1 <= source <= 4 or source in seen_expo:
            _fail(errors, path, "Each physical axis can have one unconditional expo row")
            continue
        if type(input_channel) is not int or not 0 <= input_channel <= 15:
            _fail(errors, f"{path}.chn", "Input channel must be a zero-based index from 0 through 15")
            continue
        if input_channel in mapping["_inputMap"]:
            _fail(errors, f"{path}.chn", "Multiple expo rows for one input require conditional-input support")
            continue
        seen_expo.add(source)
        curve = row.get("curve", {"type": 0, "value": 0})
        expo_value = 0
        if isinstance(curve, dict) and curve.get("type") == 1:
            expo_value = curve.get("value")
            if type(expo_value) is not int or not 0 <= expo_value <= 100:
                _fail(errors, f"{path}.curve.value", "Only positive constant expo from 0 through 100 is supported")
        elif curve not in ({"type": 0, "value": 0}, 0, None):
            _fail(errors, f"{path}.curve", "Differential and custom curves are unsupported")
        if (row.get("swtch", "NONE") != "NONE" or not _flight_modes_unconditional(row) or
                row.get("weight", 100) != 100 or row.get("offset", 0) != 0 or
                row.get("trimSource", row.get("carryTrim", 0)) not in (0, None) or
                row.get("mode", 3) != 3 or row.get("scale", 0) not in (0, None)):
            _fail(errors, path, "Conditional, trimmed, weighted or curved expo is unsupported")
        if type(expo_value) is int and 0 <= expo_value <= 100:
            if source == mapping["axes"]["Thr"] and expo_value:
                _fail(errors, f"{path}.curve", "Throttle expo behavior is not certified")
            elif expo_value:
                _fail(errors, f"{path}.curve", "Positive centered expo awaits version-matched Companion output reference vectors")
            mapping["_inputMap"][input_channel] = source

    limits = doc.get("limitData", {})
    if not isinstance(limits, dict):
        _fail(errors, "limitData", "Expected a channel-indexed mapping")
        limits = {}
    seen_limits = set()
    for channel, row in limits.items():
        path = f"limitData.{channel}"
        out = _index_key(channel)
        if out is None or not 0 <= out < 16 or not isinstance(row, dict):
            _fail(errors, path, "Expected a zero-based output channel from 0 through 15")
            continue
        if out in seen_limits:
            _fail(errors, path, "Duplicate channel index")
            continue
        seen_limits.add(out)
        for key in row:
            if key not in LIMIT_KEYS:
                _fail(errors, f"{path}.{key}", "Unknown limit field")
            elif key in {"curve", "failsafe"} and row[key] not in (0, None):
                _fail(errors, f"{path}.{key}", "Curves and failsafe values are unsupported")
        if row.get("offset", 0) not in (0, "0") or row.get("ppmCenter", 0) not in (0, "0", None) or row.get("symetrical", 0) not in (0, "0", False):
            _fail(errors, path, "Subtrim, center and asymmetric limit behavior are unsupported")
        if row.get("revert", 0) not in (0, "0", False):
            _fail(errors, f"{path}.revert", "Per-channel output reversal is unsupported")
        if "min" in row or "max" in row:
            lo_raw = _constant_int(row.get("min", 0))
            hi_raw = _constant_int(row.get("max", 0))
            if lo_raw is None or hi_raw is None or not 0 <= lo_raw <= 1000 or not -1000 <= hi_raw <= 0:
                _fail(errors, path, "Output limits must be constant values within the Remote's ±100% range")
            else:
                lo = (lo_raw - 1000) / 10
                hi = (hi_raw + 1000) / 10
                for axis_i, dest in enumerate(mapping["gimbals"]):
                    if dest == out + 1:
                        profile["inputs"][axis_i]["limits"] = [round(1000 + 5 * (lo + 100)), round(2000 - 5 * (100 - hi))]
                if out + 1 not in mapping["gimbals"]:
                    _fail(errors, path, "Output limits on non-gimbal destinations cannot be represented")

    logical = doc.get("logicalSw", {})
    if not isinstance(logical, dict):
        _fail(errors, "logicalSw", "Expected a slot-indexed mapping")
        logical = {}
    logical_ids = {}
    mapping["_logicalMap"] = {}
    indexed_logical = []
    seen_logical = set()
    for slot, row in logical.items():
        index = _index_key(slot)
        if index is None or not 0 <= index < 16 or not isinstance(row, dict):
            _fail(errors, f"logicalSw.{slot}", "Expected logical slot 0 through 15")
            continue
        if index in seen_logical:
            _fail(errors, f"logicalSw.{slot}", "Duplicate logical slot")
            continue
        seen_logical.add(index)
        indexed_logical.append((index, slot, row))
    for i, slot, row in sorted(indexed_logical):
        path = f"logicalSw.{slot}"
        if set(row) - {"func", "def", "delay", "duration", "andsw", "lsPersist", "lsState", "name"}:
            _fail(errors, path, "Unknown logical-switch field")
        if row.get("delay", 0) or row.get("duration", 0) or row.get("lsPersist", 0) or row.get("lsState", 0):
            _fail(errors, path, "Delayed, timed, sticky or edge logic is unsupported")
        func = row.get("func")
        definition = row.get("def", "")
        parts = definition.split(",") if isinstance(definition, str) else []
        and_switch = row.get("andsw", "NONE")
        fn = {"FUNC_VPOS": 1, "FUNC_VNEG": 2, "FUNC_AND": 3, "FUNC_OR": 4, "FUNC_XOR": 5}.get(func)
        if fn is None or len(parts) != 2:
            _fail(errors, path, f"Unsupported logical function {func!r}")
            continue
        a = (_source(parts[0], mapping, f"{path}.def", errors) if fn <= 2 else
             _condition(parts[0], mapping, f"{path}.def", errors))
        b = 0
        threshold = 0
        if fn <= 2:
            threshold_value = re.fullmatch(r"-?\d+", parts[1])
            if not threshold_value or a is None or not 1 <= a <= 4:
                _fail(errors, f"{path}.def", "Axis threshold comparison required")
                continue
            raw = int(parts[1])
            if not -100 <= raw <= 100:
                _fail(errors, f"{path}.def", "Threshold must be from -100 through 100 percent")
                continue
            threshold = raw
        else:
            b = _condition(parts[1], mapping, f"{path}.def", errors)
            if a is None or b is None or a < 5 or b < 5:
                _fail(errors, f"{path}.def", "Boolean logic requires two mapped two-position switches or earlier logical slots")
                continue
        if and_switch != "NONE":
            and_switch = _condition(and_switch, mapping, f"{path}.andsw", errors)
            if and_switch is None or and_switch < 5:
                _fail(errors, f"{path}.andsw", "Only a mapped two-position switch is supported")
                continue
        else:
            and_switch = 0
        profile["logical"][i] = [fn, a, b, threshold, and_switch]
        logical_ids[i] = 17 + i + (1 if i >= 8 else 0)
        mapping["_logicalMap"][i] = logical_ids[i]
    for i, row in enumerate(profile["logical"]):
        if not row[0]:
            continue
        for field_i in (1, 2, 4):
            source = row[field_i] & 0x7F
            if source >= 17 and source != 25:
                slot = source - 17 - (1 if source > 25 else 0)
                if slot >= i or slot not in logical_ids:
                    _fail(errors, f"logical[{i}]", "Logical sources must reference an earlier populated slot")

    mixes = doc.get("mixData", [])
    if not isinstance(mixes, list):
        _fail(errors, "mixData", "Expected an ordered sequence")
        mixes = []
    if len(mixes) > 16:
        _fail(errors, "mixData", "Remote supports at most 16 ordered mix rows")
    foreign_lines = []
    for i, row in enumerate(mixes[:16]):
        path = f"mixData[{i}]"
        if not isinstance(row, dict):
            _fail(errors, path, "Expected a mix mapping")
            continue
        for key in row:
            if key not in MIX_KEYS:
                _fail(errors, f"{path}.{key}", "Unknown mix field")
        if set(row) - MIX_KEYS:
            continue
        if any(row.get(key, 0) for key in ("delayPrec", "delayUp", "delayDown", "speedPrec", "speedUp", "speedDown", "carryTrim", "mixWarn")):
            _fail(errors, path, "Delayed, timed, trim, warning and curve behavior is unsupported")
        if row.get("curve", {"type": 0, "value": 0}) not in ({"type": 0, "value": 0}, 0, None):
            _fail(errors, f"{path}.curve", "Nonlinear curves are unsupported")
        if not _flight_modes_unconditional(row):
            _fail(errors, f"{path}.flightModes", "Flight-mode-dependent mixes are unsupported")
        dest = _int(row.get("destCh"), f"{path}.destCh", errors, 0, 15)
        source = _source(row.get("srcRaw"), mapping, f"{path}.srcRaw", errors)
        weight = _int(row.get("weight", 100), f"{path}.weight", errors, -100, 100)
        offset = _int(row.get("offset", 0), f"{path}.offset", errors, -100, 100)
        switch = row.get("swtch", "NONE")
        if switch == "NONE":
            switch = 0
        else:
            switch = _condition(switch, mapping, f"{path}.swtch", errors)
            if switch is not None and switch < 5:
                _fail(errors, f"{path}.swtch", "Conditional mixes require a mapped two-position switch")
        mux = {"ADD": 0, "MUL": 1, "REPL": 2}.get(row.get("mltpx", "ADD"))
        if mux is None:
            _fail(errors, f"{path}.mltpx", "Unsupported multiplex operation")
        elif mux in (1, 2):
            _fail(errors, f"{path}.mltpx", "Multiply and Replace await version-matched Companion output reference vectors")
        if None not in (dest, source, weight, offset, switch, mux):
            foreign_lines.append([dest + 1, source, switch, weight, offset, mux])

    changed_channels = {line[0] for line in foreign_lines}
    for axis, destination in enumerate(profile["routing"]["gimbals"], 1):
        if destination in changed_channels:
            profile["routing"]["gimbals"][axis - 1] = 0
    profile["routing"]["switches"] = [0 if destination in changed_channels else destination
                                      for destination in profile["routing"]["switches"]]
    for channel, row in limits.items():
        out = _index_key(channel)
        if out is None or not isinstance(row, dict) or out + 1 not in changed_channels:
            continue
        lo_raw = _constant_int(row.get("min", 0))
        hi_raw = _constant_int(row.get("max", 0))
        if lo_raw is not None and hi_raw is not None and (lo_raw != 0 or hi_raw != 0):
            _fail(errors, f"limitData.{channel}", "Nondefault limits on mixed outputs are unsupported because Remote mix rows bypass axis output limits")
    profile["mixes"][:len(foreign_lines)] = foreign_lines

    return profile


def _adapt_2106(doc, source_radio, source_name, mapping, errors):
    return _convert(doc, source_radio, "2.10.6", source_name, mapping, errors)


def _adapt_2124(doc, source_radio, source_name, mapping, errors):
    return _convert(doc, source_radio, "2.12.4", source_name, mapping, errors)


_ADAPTERS = {"2.10.6": _adapt_2106, "2.12.4": _adapt_2124}


def convert_model(path, source_radio, source_version, mapping) -> ConversionResult:
    """Convert one supported EdgeTX YAML file or `.etx` ZIP into a native profile."""
    result = ConversionResult()
    if isinstance(mapping, dict):
        mapping = dict(mapping)
    if not _source_radio(source_radio):
        _fail(result.errors, "source_radio", "Only TX16S and Boxer source radio adapters are enabled")
    if not isinstance(source_version, str) or source_version not in VERSIONS:
        _fail(result.errors, "source_version", "Supported YAML adapters are EdgeTX 2.10.6 and 2.12.4")
    _mapping_ok(mapping, source_radio, result.errors)
    if result.errors:
        return result
    doc, source_name = _load_source(path, mapping, source_version, result.errors)
    if doc is None:
        return result
    result.profile = _ADAPTERS[source_version](doc, source_radio, source_name, mapping, result.errors)
    if result.errors:
        result.profile = None
    return result
