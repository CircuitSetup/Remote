"""Exercise converted transmitter models through the production CRSF codec."""
import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "test"))
sys.path.insert(0, str(ROOT / "tools"))
from check_crsf_adc import HEADERS, compile_and_run  # noqa: E402
from model_import import convert_model  # noqa: E402

HEADERS['FS.h'] = '#pragma once\nclass String {}; class File {};\n'

CASES = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif
#include "crsf_model_files.h"
bool elrsIsValidRadioPreferences(const ELRSRadioPreferences &p) {
    return p.modelId <= 63 && p.telemetry[0] && p.dynamic[0] && p.maxPower.label[0] &&
        memchr(p.telemetry, 0, sizeof(p.telemetry)) && memchr(p.dynamic, 0, sizeof(p.dynamic)) &&
        memchr(p.maxPower.label, 0, sizeof(p.maxPower.label)) && memchr(p.maxPower.unit, 0, sizeof(p.maxPower.unit));
}
int main(int argc, char **argv) {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    assert(argc == 6);
    const uint8_t ids[] = {0, 12, 63, 12, 63};
    for (int i = 1; i < argc; ++i) {
        FILE *input = fopen(argv[i], "rb"); assert(input);
        fseek(input, 0, SEEK_END); long length = ftell(input); rewind(input);
        assert(length > 0 && length < REMOTE_MODEL_MAX_BYTES);
        char json[REMOTE_MODEL_MAX_BYTES]; assert(fread(json, 1, length, input) == (size_t)length); fclose(input);
        DynamicJsonDocument doc(16384); assert(!deserializeJson(doc, json, length));
        RemoteSettingsSnapshot target = {}; target.newBoard = true;
        target.crsf = remoteDefaultCrsfSettings();
        target.crsf.axisProfile[0] = {111, 999, 1999, 0, 0, 0};
        RemoteSettingsSnapshot model; RemoteTransferError error;
        if (!remoteDecodeTransfer(doc.as<JsonVariantConst>(), RemoteTransferKind::ModelProfile,
                                 target, model, error)) {
            fprintf(stderr, "converted profile rejected: %s %s\\n", error.path, error.message); return 1;
        }
        assert(model.radio.modelId == ids[i - 1]);
        assert(model.crsf.axisProfile[0].minimum == 111); // Portable models retain target calibration.
        assert(model.crsf.axisProfile[0].center == 999 && model.crsf.axisProfile[0].maximum == 1999);
        assert(elrsIsValidModelReferences(model.crsf.logic, model.crsf.mixes));
        ELRSLogicInputs inputs = {}; inputs.axesValid = true; inputs.validSwitches = 0x0fff;
        for (int16_t value : {1000, 1250, 1500, 1750, 2000}) {
            for (auto &axis : inputs.axesUs) axis = value;
            auto logical = elrsEvaluateLogic(model.crsf.logic, inputs);
            int16_t base[16]; for (auto &channel : base) channel = 1000;
            auto mixed = elrsEvaluateMixes(model.crsf.mixes, inputs, logical, base, 0);
            assert(!mixed.invalidChannels);
        }
        if (strstr(argv[i], "switch_mix")) {
            for (uint16_t state : {0, 1}) {
                inputs.switchStates = state; int16_t base[16];
                for (auto &channel : base) channel = 1500;
                for (unsigned sw = 0; sw < ELRS_SWITCH_INPUT_COUNT; ++sw) {
                    const uint8_t channel = model.crsf.switchRouting.channels[sw];
                    if (channel) base[channel - 1] = (state & (1u << sw)) ? 2000 : 1000;
                }
                auto mixed = elrsEvaluateMixes(model.crsf.mixes, inputs,
                    elrsEvaluateLogic(model.crsf.logic, inputs), base, 0);
                assert(!(mixed.invalidChannels & (1u << 4)) && mixed.valuesUs[4] == 1750);
            }
        }
        if (i == 1) {
            doc["inputs"][0]["unrecognized"] = 1;
            RemoteSettingsSnapshot rejected;
            assert(!remoteDecodeTransfer(doc.as<JsonVariantConst>(), RemoteTransferKind::ModelProfile,
                                         target, rejected, error));
            assert(error.code == RemoteTransferCode::InvalidValue && !strcmp(error.path, "inputs"));
        }
    }
    puts("converted EdgeTX/OpenTX profiles decode and evaluate through the production CRSF codec");
}
'''

def main():
    fixture = ROOT / "test" / "fixtures" / "model_import"
    sources = [
        (fixture / "edgetx" / "2.10.6" / "four_axis.yml", "Boxer", "2.10.6", {}),
        (fixture / "edgetx" / "2.12.4" / "four_axis.yml", "TX16S", "2.12.4", {}),
        (fixture / "edgetx" / "2.12.4" / "switch_mix.yml", "TX16S", "2.12.4",
         {"switches": [5] + [0] * 11}),
        (fixture / "companion" / "multi_model.etx", "TX16S", "2.12.4", {"model": "models/four_axis.yml"}),
        (fixture / "edgetx" / "2.12.4" / "logical_gaps.yml", "TX16S", "2.12.4", {}),
    ]
    axes = {"axes": {"Ail": 1, "Ele": 2, "Rud": 3, "Thr": 4}, "gimbals": [1, 2, 3, 4], "switches": [0] * 12}
    with tempfile.TemporaryDirectory(prefix="crsf-import-") as temp:
        generated = []
        for index, (path, radio, version, extra) in enumerate(sources):
            mapping = {**axes, **extra, "switchMap": {("SF" if version == "2.12.4" and radio == "TX16S" else "SA"):
                {"input": 0, "reverse": True, "positions": 2}}}
            result = convert_model(path, radio, version, mapping)
            assert result.errors == [] and result.profile is not None, (path, result.errors)
            result.profile["radio"]["modelId"] = (0, 12, 63, 12, 63)[index]
            output = Path(temp) / f"profile-{index}.json"
            output.write_text(json.dumps(result.profile, ensure_ascii=False), encoding="utf-8")
            generated.append(output)
        compile_and_run(CASES, ["elrs_input_model.cpp", "crsf_model_files.cpp"],
                        ["-I" + str(ROOT / "src/src/CRSF"),
                         "-I" + str(ROOT / ".pio/libdeps/esp32dev/ArduinoJson/src"), "-lbcrypt"], generated)

if __name__ == "__main__":
    main()
