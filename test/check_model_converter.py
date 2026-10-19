"""Focused offline EdgeTX model conversion and .etx safety checks."""
import json
import sys
import tempfile
import zipfile
from pathlib import Path
import yaml

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from model_import import convert_model  # noqa: E402


FIXTURES = ROOT / "test" / "fixtures" / "model_import"
MAPPING = {
    "axes": {"Ail": 1, "Ele": 2, "Rud": 3, "Thr": 4},
    "gimbals": [1, 2, 3, 4],
    "switches": [0] * 12,
    "switchMap": {
        "SF": {"input": 0, "reverse": True, "positions": 2},
    },
}
BOXER_MAPPING = {**MAPPING, "switchMap": {"SA": {"input": 0, "reverse": True, "positions": 2}}}


def _write_source(path, text):
    doc = yaml.safe_load(text)
    doc.setdefault("header", {"name": "Synthetic model"})
    doc["header"].setdefault("modelId", {0: {"val": 12}, 1: {"val": 42}})
    doc.setdefault("moduleData", {0: {"type": "TYPE_NONE"}, 1: {"type": "TYPE_CROSSFIRE"}})
    path.write_text(yaml.safe_dump(doc, sort_keys=False))


def test_flight_modes_accept_only_unconditional_zero_masks():
    with tempfile.TemporaryDirectory() as temp:
        path = Path(temp) / "flight-modes.yml"
        for field in ("", "flightModes: 0", 'flightModes: "000000000"'):
            for row_kind in ("expo", "mix"):
                suffix = ", " + field if field else ""
                row = ("expoData: [{srcRaw: Ail, chn: 0, mode: 3" + suffix + "}]\n" if row_kind == "expo"
                       else "mixData: [{destCh: 0, srcRaw: Ail" + suffix + "}]\n")
                _write_source(path, "semver: 2.12.4\n" + row)
                result = convert_model(path, "TX16S", "2.12.4", MAPPING)
                assert result.errors == [], (row_kind, field, result.errors)
        for field in ('"111111111"', '"000000001"', '"bad"', "1"):
            for row_kind in ("expo", "mix"):
                row = ("expoData: [{srcRaw: Ail, chn: 0, mode: 3, flightModes: " + field + "}]\n" if row_kind == "expo"
                       else "mixData: [{destCh: 0, srcRaw: Ail, flightModes: " + field + "}]\n")
                _write_source(path, "semver: 2.12.4\n" + row)
                result = convert_model(path, "TX16S", "2.12.4", MAPPING)
                assert result.profile is None and any(row_kind == "expo" and "expoData[0]" in error.path or
                                                      row_kind == "mix" and "flightModes" in error.path
                                                      for error in result.errors)


def test_crsf_model_id_comes_from_unique_active_module():
    with tempfile.TemporaryDirectory() as temp:
        path = Path(temp) / "module.yml"
        def convert(source):
            _write_source(path, "semver: 2.12.4\n" + source)
            return convert_model(path, "TX16S", "2.12.4", MAPPING)
        result = convert("header: {name: external, modelId: {0: {val: 12}, 1: {val: 42}}}\n"
                         "moduleData: {0: {type: TYPE_NONE}, 1: {type: TYPE_CROSSFIRE}}\n")
        assert result.errors == [] and result.profile["radio"]["modelId"] == 42
        result = convert("header: {name: internal, modelId: {0: {val: 12}, 1: {val: 42}}}\n"
                         "moduleData: {0: {type: TYPE_CROSSFIRE}, 1: {type: TYPE_NONE}}\n")
        assert result.errors == [] and result.profile["radio"]["modelId"] == 12
        path.write_text("semver: 2.12.4\nheader: {name: missing module, modelId: {0: {val: 12}, 1: {val: 42}}}\n")
        result = convert_model(path, "TX16S", "2.12.4", MAPPING)
        assert result.profile is None and any(error.path == "moduleData" for error in result.errors)
        for source, error_path in (
            ("header: {name: invalid id, modelId: {0: {val: 12}, 1: {val: 64}}}\n"
             "moduleData: {0: {type: TYPE_NONE}, 1: {type: TYPE_CROSSFIRE}}\n", "header.modelId.1.val"),
            ("header: {name: ambiguous, modelId: {0: {val: 12}, 1: {val: 42}}}\n"
             "moduleData: {0: {type: TYPE_CROSSFIRE}, 1: {type: TYPE_CROSSFIRE}}\n", "moduleData"),
            ("header: {name: invalid, modelId: {0: {val: 12}, 1: {val: 42}}}\n"
             "moduleData: {0: {type: TYPE_UNKNOWN}, 1: {type: TYPE_CROSSFIRE}}\n", "moduleData.0.type"),
        ):
            result = convert(source)
            assert result.profile is None and any(error.path == error_path for error in result.errors)


def test_sparse_crsf_module_exports_use_edge_tx_zero_defaults():
    with tempfile.TemporaryDirectory() as temp:
        path = Path(temp) / "sparse.yml"
        for version in ("2.10.6", "2.12.4"):
            for module, yaml_module, model_ids, expected_id in (
                ("external", "1: {type: TYPE_CROSSFIRE}", "1: {val: 42}", 42),
                ("internal", "0: {type: TYPE_CROSSFIRE}", None, 0),
            ):
                header = (f"header: {{name: {module}}}\n" if model_ids is None else
                          f"header: {{name: {module}, modelId: {{{model_ids}}}}}\n")
                path.write_text(f"semver: {version}\n" + header + f"moduleData: {{{yaml_module}}}\n")
                result = convert_model(path, "TX16S", version, MAPPING)
                assert result.errors == [] and result.profile["radio"]["modelId"] == expected_id
            path.write_text(f"semver: {version}\nheader: {{name: omitted ID}}\n"
                            "moduleData: {1: {type: TYPE_CROSSFIRE}}\n")
            result = convert_model(path, "TX16S", version, MAPPING)
            assert result.errors == [] and result.profile["radio"]["modelId"] == 0
            path.write_text(f"semver: {version}\nheader: {{name: omitted value, modelId: {{1: {{}}}}}}\n"
                            "moduleData: {1: {type: TYPE_CROSSFIRE}}\n")
            result = convert_model(path, "TX16S", version, MAPPING)
            assert result.errors == [] and result.profile["radio"]["modelId"] == 0
            path.write_text(f"semver: {version}\nheader: {{name: default module type}}\n"
                            "moduleData: {0: {type: TYPE_CROSSFIRE}, 1: {}}\n")
            result = convert_model(path, "TX16S", version, MAPPING)
            assert result.errors == [] and result.profile["radio"]["modelId"] == 0


def test_two_position_switch_conditions_and_logical_sources_use_distinct_syntax():
    with tempfile.TemporaryDirectory() as temp:
        path = Path(temp) / "switches.yml"
        _write_source(path, 'semver: 2.12.4\nlogicalSw:\n'
                      '  0: {func: FUNC_AND, def: "SF0,SF2"}\n'
                      '  1: {func: FUNC_OR, def: "!L01,SF0"}\n'
                      'mixData: [{destCh: 0, srcRaw: "!ls(1)", swtch: SF2}]\n')
        result = convert_model(path, "TX16S", "2.12.4", MAPPING)
        assert result.errors == []
        assert result.profile["logical"][0] == [3, 5, 5 | 0x80, 0, 0]
        assert result.profile["logical"][1] == [4, 17 | 0x80, 5, 0, 0]
        assert result.profile["mixes"][0] == [1, 17 | 0x80, 5 | 0x80, 100, 0, 0]
        _write_source(path, 'semver: 2.12.4\nlogicalSw: {0: {func: FUNC_AND, def: "SF1,SF2"}}\n')
        result = convert_model(path, "TX16S", "2.12.4", MAPPING)
        assert result.profile is None and result.errors
        for version, radio, mapping, switch in (("2.10.6", "Boxer", BOXER_MAPPING, "SA0"),
                                                 ("2.12.4", "TX16S", MAPPING, "SF2")):
            _write_source(path, f'semver: {version}\nmixData: [{{destCh: 0, srcRaw: Ail, swtch: {switch}}}]\n')
            result = convert_model(path, radio, version, mapping)
            assert result.errors == [], (version, radio, result.errors)
        for source in ("SF0", "SF2"):
            _write_source(path, f'semver: 2.12.4\nmixData: [{{destCh: 0, srcRaw: {source}}}]\n')
            result = convert_model(path, "TX16S", "2.12.4", MAPPING)
            assert result.profile is None and any(error.path == "mixData[0].srcRaw" for error in result.errors)
        for source, native_source in (("SF", 5 | 0x80), ("!SF", 5)):
            _write_source(path, f'semver: 2.12.4\nmixData: [{{destCh: 0, srcRaw: "{source}"}}]\n')
            result = convert_model(path, "TX16S", "2.12.4", MAPPING)
            assert result.errors == [] and result.profile["mixes"][0][1] == native_source


def test_axis_threshold_functions_use_percent_and_reject_source_comparisons():
    with tempfile.TemporaryDirectory() as temp:
        path = Path(temp) / "thresholds.yml"
        for func, native_fn in (("FUNC_VPOS", 1), ("FUNC_VNEG", 2)):
            _write_source(path, f'semver: 2.12.4\nlogicalSw: {{0: {{func: {func}, def: "Ail,-50"}}}}\n')
            result = convert_model(path, "TX16S", "2.12.4", MAPPING)
            assert result.errors == [] and result.profile["logical"][0] == [native_fn, 1, 0, -50, 0]
        for func in ("FUNC_GREATER", "FUNC_LESS"):
            _write_source(path, f'semver: 2.12.4\nlogicalSw: {{0: {{func: {func}, def: "Ail,Rud"}}}}\n')
            result = convert_model(path, "TX16S", "2.12.4", MAPPING)
            assert result.profile is None and result.errors

def test_mixed_switch_destination_has_no_implicit_switch_base():
    result = convert_model(FIXTURES / 'edgetx' / '2.12.4' / 'switch_mix.yml', 'TX16S', '2.12.4',
                           {**MAPPING, 'switches': [5] + [0] * 11})
    assert result.errors == []
    assert result.profile['routing']['switches'][0] == 0
    assert result.profile['mixes'][0] == [5, 25, 0, 50, 0, 0]

def test_converted_power_uses_descriptor_label_and_unit_separately():
    result = convert_model(FIXTURES / 'edgetx' / '2.12.4' / 'four_axis.yml', 'TX16S', '2.12.4', MAPPING)
    assert result.errors == []
    assert result.profile['radio']['maxPower'] == {'label': '250', 'unit': 'mW'}


def test_converts_supported_source():
    result = convert_model(FIXTURES / "edgetx" / "2.12.4" / "four_axis.yml",
                           "TX16S", "2.12.4", MAPPING)
    assert result.errors == []
    profile = result.profile
    assert profile["format"] == "remote-model"
    assert profile["sourceContractVersion"] == 2
    assert profile["inputs"][0]["expo"] == 0
    assert profile["inputs"] == [
        {"reverse": False, "deadband": 0, "expo": 0, "limits": [1000, 2000]},
        {"reverse": False, "deadband": 0, "expo": 0, "limits": [1000, 2000]},
        {"reverse": False, "deadband": 0, "expo": 0, "limits": [1000, 2000]},
        {"reverse": False, "deadband": 0, "expo": 0, "limits": [1000, 2000]},
    ]
    assert len(profile["logical"]) == len(profile["mixes"]) == 16
    assert profile["routing"]["gimbals"] == [0, 2, 3, 4]
    assert profile["mixes"][0] == [1, 1, 0, 100, 0, 0]
    assert profile["mixes"][1:] == [[0, 0, 0, 0, 0, 0]] * 15
    assert MAPPING["gimbals"] == [1, 2, 3, 4]
    wrong_version = convert_model(FIXTURES / "edgetx" / "2.12.4" / "four_axis.yml",
                                  "TX16S", "2.10.6", MAPPING)
    assert wrong_version.profile is None and wrong_version.errors


def test_errors_never_return_profile_and_report_paths():
    result = convert_model(FIXTURES / "edgetx" / "2.10.6" / "unsupported.yml",
                           "Boxer", "2.10.6", BOXER_MAPPING)
    assert result.profile is None
    assert any("timers" in error.path for error in result.errors)


def test_stable_logical_ids_and_reversed_two_position_switches():
    result = convert_model(FIXTURES / "edgetx" / "2.12.4" / "logical_gaps.yml",
                           "TX16S", "2.12.4", MAPPING)
    assert result.errors == []
    assert result.profile["logical"][8] == [3, 5 | 0x80, 5 | 0x80, 0, 0]
    assert result.profile["logical"][15] == [4, 26, 5, 0, 0]


def test_second_versioned_radio_adapter():
    result = convert_model(FIXTURES / "edgetx" / "2.10.6" / "four_axis.yml",
                           "Boxer", "2.10.6", BOXER_MAPPING)
    assert result.errors == [] and result.profile["inputs"][0]["expo"] == 0


def test_known_three_position_switch_cannot_be_declared_two_position():
    unsafe_mapping = {**MAPPING, "switchMap": {"SA": {"input": 0, "reverse": False, "positions": 2}}}
    result = convert_model(FIXTURES / "edgetx" / "2.12.4" / "four_axis.yml",
                           "TX16S", "2.12.4", unsafe_mapping)
    assert result.profile is None
    assert any("SA" in error.path and "three-position" in error.message.lower()
               for error in result.errors)


def test_positive_centered_expo_waits_for_companion_reference_vectors():
    with tempfile.TemporaryDirectory() as temp:
        path = Path(temp) / "expo.yml"
        _write_source(path, "semver: 2.12.4\nheader: {name: expo}\n"
                        "expoData: [{srcRaw: Ail, chn: 0, mode: 3, curve: {type: 1, value: 20}}]\n")
        result = convert_model(path, "TX16S", "2.12.4", MAPPING)
        assert result.profile is None
        assert any(error.path == "expoData[0].curve" and "reference vector" in error.message.lower()
                   for error in result.errors)


def test_ordered_add_multiply_replace_and_switch_conditions():
    result = convert_model(FIXTURES / "edgetx" / "2.12.4" / "mix_ops.yml",
                           "TX16S", "2.12.4", MAPPING)
    assert result.profile is None
    assert {error.path for error in result.errors if error.path.endswith(".mltpx")} == {
        "mixData[1].mltpx", "mixData[2].mltpx"}


def test_limits_are_crsf_pulses_and_nondefault_limits_reject_mixed_outputs():
    with tempfile.TemporaryDirectory() as temp:
        path = Path(temp) / "direct-limit.yml"
        _write_source(path, "semver: 2.12.4\nheader: {name: direct limit}\nlimitData: {0: {min: 500, max: -500}}\n")
        result = convert_model(path, "TX16S", "2.12.4", MAPPING)
        assert result.errors == []
        assert result.profile["inputs"][0]["limits"] == [1250, 1750]
        assert all(row["limits"] == [1000, 2000] for row in result.profile["inputs"][1:])

        _write_source(path, "semver: 2.12.4\nheader: {name: mixed limit}\n"
                        "limitData: {0: {min: 500, max: -500}}\n"
                        "mixData: [{destCh: 0, srcRaw: Ail, weight: 50}]\n")
        result = convert_model(path, "TX16S", "2.12.4", MAPPING)
        assert result.profile is None
        assert any(error.path == "limitData.0" for error in result.errors)


def test_mixed_gimbal_destination_has_no_invented_base_mix():
    with tempfile.TemporaryDirectory() as temp:
        path = Path(temp) / "partial-gimbal.yml"
        _write_source(path, "semver: 2.12.4\nheader: {name: partial gimbal}\n"
                        "mixData:\n"
                        "  - {destCh: 0, srcRaw: Ele, weight: 50, swtch: SF2, mltpx: ADD}\n"
                        "  - {destCh: 0, srcRaw: MAX, weight: 100, mltpx: ADD}\n")
        result = convert_model(path, "TX16S", "2.12.4", MAPPING)
        assert result.errors == []
        assert result.profile["routing"]["gimbals"] == [0, 2, 3, 4]
        assert result.profile["mixes"][:2] == [[1, 2, 5 | 0x80, 50, 0, 0], [1, 25, 0, 100, 0, 0]]
        assert result.profile["mixes"][2:] == [[0, 0, 0, 0, 0, 0]] * 14


def test_etx_selects_explicit_model_and_rejects_hostile_zip_entries():
    archive = FIXTURES / "companion" / "multi_model.etx"
    result = convert_model(archive, "TX16S", "2.12.4",
                           {**MAPPING, "model": "models/four_axis.yml"})
    assert result.errors == [] and result.profile
    assert "wifiPassword" not in json.dumps(result.profile)
    assert result.profile["radio"]["modelId"] == 42

    with tempfile.TemporaryDirectory() as temp:
        bad = Path(temp) / "bad.etx"
        with zipfile.ZipFile(bad, "w") as zf:
            zf.writestr("../escape.yml", "semver: 2.12.4")
        result = convert_model(bad, "TX16S", "2.12.4", MAPPING)
        assert result.profile is None
        assert any("zip" in error.path.lower() for error in result.errors)

        bad.write_bytes(b"semver: 2.12.4\nheader: {name: dup}\nheader: {name: duplicate}\n")
        result = convert_model(bad, "TX16S", "2.12.4", MAPPING)
        assert result.profile is None and result.errors
        bad.write_bytes(b"semver: 2.12.4\nheader: &h {name: alias}\nother: *h\n")
        result = convert_model(bad, "TX16S", "2.12.4", MAPPING)
        assert result.profile is None and result.errors
        bad.write_bytes(b"!!python/object/apply:os.system ['echo unsafe']")
        result = convert_model(bad, "TX16S", "2.12.4", MAPPING)
        assert result.profile is None and result.errors

        with zipfile.ZipFile(bad, "w") as zf:
            zf.writestr("models/a.yml", "semver: 2.12.4")
            zf.writestr("models/A.yml", "semver: 2.12.4")
        result = convert_model(bad, "TX16S", "2.12.4", MAPPING)
        assert result.profile is None and result.errors

        with zipfile.ZipFile(bad, "w", zipfile.ZIP_DEFLATED) as zf:
            zf.writestr("models/large.yml", b"#" * (16 * 1024 * 1024 + 1))
        result = convert_model(bad, "TX16S", "2.12.4", MAPPING)
        assert result.profile is None and result.errors

        bad.write_bytes(b"\x7fELF fake firmware image")
        result = convert_model(bad, "TX16S", "2.12.4", MAPPING)
        assert result.profile is None and result.errors
        binary = Path(temp) / "firmware.bin"
        binary.write_bytes(b"\x7fELF fake firmware image")
        result = convert_model(binary, "TX16S", "2.12.4", MAPPING)
        assert result.profile is None and result.errors
        opentx = Path(temp) / "model.otx"
        opentx.write_bytes(b"binary OpenTX EEPROM")
        result = convert_model(opentx, "TX16S", "2.12.4", MAPPING)
        assert result.profile is None and result.errors

        result = convert_model(archive, "TX16S", "2.12.4", MAPPING)
        assert result.profile is None and result.errors

        bad.write_bytes(b"semver: 2.12.4\n" + b"#" * (8 * 1024 * 1024 + 1))
        result = convert_model(bad, "TX16S", "2.12.4", MAPPING)
        assert result.profile is None and result.errors


def test_malformed_enum_values_return_errors():
    with tempfile.TemporaryDirectory() as temp:
        path = Path(temp) / "enum.yml"
        for version in ("2.10.6", "2.12.4"):
            for value in ("[]", "{}", "null", "true", "1", "UNKNOWN"):
                for body, error_path in (
                    (f"mixData: [{{destCh: 0, srcRaw: Ail, mltpx: {value}}}]\n", "mixData[0].mltpx"),
                    (f"logicalSw: {{0: {{func: {value}, def: 'Ail,0'}}}}\n", "logicalSw.0"),
                ):
                    _write_source(path, f"semver: {version}\n" + body)
                    result = convert_model(path, "TX16S", version, MAPPING)
                    assert result.profile is None
                    assert any(error.path == error_path for error in result.errors), result.errors


def test_rejection_matrix_and_translated_row_capacity():
    cases = {
        "srcRaw": "mixData: [{destCh: 4, srcRaw: 'ch(0)'}]\n",
        "mixData[0].srcRaw": "mixData: [{destCh: 4, srcRaw: '!Ail'}]\n",
        "futureFeature": "futureFeature: enabled\n",
        "curves": "curves: {'0': {points: [0, 100]}}\n",
        "flightModeData": "flightModeData: {'0': {name: flight}}\n",
        "expoData": "expoData: [{srcRaw: Thr, chn: 3, mode: 3, curve: {type: 1, value: 20}}]\n",
    }
    with tempfile.TemporaryDirectory() as temp:
        path = Path(temp) / "case.yml"
        for expected_path, body in cases.items():
            _write_source(path, "semver: 2.12.4\nheader: {name: reject}\n" + body)
            result = convert_model(path, "TX16S", "2.12.4", MAPPING)
            assert result.profile is None
            assert any(expected_path in error.path for error in result.errors), (expected_path, result.errors)

        _write_source(path, "semver: 2.12.4\nheader: {name: bad id, modelId: {1: {val: 64}}}\n")
        result = convert_model(path, "TX16S", "2.12.4", MAPPING)
        assert result.profile is None and any(e.path == "header.modelId.1.val" for e in result.errors)

        for rows, should_pass in ((16, True), (17, False)):
            body = "mixData:\n" + "".join(
                "  - {destCh: 4, srcRaw: MAX, weight: 100, mltpx: ADD}\n" for _ in range(rows))
            _write_source(path, "semver: 2.12.4\nheader: {name: capacity}\n" + body)
            result = convert_model(path, "TX16S", "2.12.4", MAPPING)
            assert (result.profile is not None) == should_pass, (rows, result.profile is not None, result.errors,
                result.profile["routing"]["gimbals"] if result.profile else None,
                sum(bool(row[0]) for row in result.profile["mixes"]) if result.profile else None)

        for rows, should_pass in ((16, True), (17, False)):
            body = "mixData:\n" + "".join(
                "  - {destCh: 0, srcRaw: MAX, weight: 100, mltpx: ADD}\n" for _ in range(rows))
            _write_source(path, "semver: 2.12.4\nheader: {name: translated capacity}\n" + body)
            result = convert_model(path, "TX16S", "2.12.4", MAPPING)
            assert (result.profile is not None) == should_pass, (rows, result.profile is not None, result.errors)



if __name__ == "__main__":
    test_malformed_enum_values_return_errors()
    test_mixed_switch_destination_has_no_implicit_switch_base()
    test_converted_power_uses_descriptor_label_and_unit_separately()
    test_converts_supported_source()
    test_errors_never_return_profile_and_report_paths()
    test_stable_logical_ids_and_reversed_two_position_switches()
    test_second_versioned_radio_adapter()
    test_known_three_position_switch_cannot_be_declared_two_position()
    test_positive_centered_expo_waits_for_companion_reference_vectors()
    test_ordered_add_multiply_replace_and_switch_conditions()
    test_limits_are_crsf_pulses_and_nondefault_limits_reject_mixed_outputs()
    test_mixed_gimbal_destination_has_no_invented_base_mix()
    test_rejection_matrix_and_translated_row_capacity()
    test_etx_selects_explicit_model_and_rejects_hostile_zip_entries()
    test_flight_modes_accept_only_unconditional_zero_masks()
    test_crsf_model_id_comes_from_unique_active_module()
    test_sparse_crsf_module_exports_use_edge_tx_zero_defaults()
    test_two_position_switch_conditions_and_logical_sources_use_distinct_syntax()
    test_axis_threshold_functions_use_percent_and_reject_source_comparisons()
    print("model converter fixtures pass")
