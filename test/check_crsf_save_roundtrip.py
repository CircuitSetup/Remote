"""Check portable-model and complete CRSF-backup calibration round trips."""
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
    assert(argc == 6); const uint8_t ids[] = {0, 12, 63, 12, 63};
    for (int i = 1; i < argc; ++i) {
        FILE *input = fopen(argv[i], "rb"); assert(input);
        fseek(input, 0, SEEK_END); long length = ftell(input); rewind(input);
        assert(length > 0 && length < REMOTE_MODEL_MAX_BYTES);
        char json[REMOTE_MODEL_MAX_BYTES]; assert(fread(json, 1, length, input) == (size_t)length); fclose(input);
        DynamicJsonDocument importedDoc(16384); assert(!deserializeJson(importedDoc, json, length));
        RemoteSettingsSnapshot source = {}; source.newBoard = true; source.identity = 42;
        source.crsf = remoteDefaultCrsfSettings();
        source.crsf.axisProfile[0] = {101, 1024, 1900, 0, 4, 0};
        source.crsf.axisProfile[1] = {201, 1000, 1800, 1, 3, 0};
        source.crsf.adcHysteresis = 7;
        strcpy(source.settings.opMode, "2"); strcpy(source.settings.crsfap, "0");
        RemoteTransferError error; RemoteSettingsSnapshot converted;
        assert(remoteDecodeTransfer(importedDoc.as<JsonVariantConst>(), RemoteTransferKind::ModelProfile,
                                    source, converted, error));
        assert(converted.radio.modelId == ids[i - 1]);
        assert(!converted.crsf.timeTravel.enabled); // Old profiles omit this optional section.
        converted.crsf.timeTravel = {1,16};

        DynamicJsonDocument modelDoc(16384);
        assert(remoteEncodeModel(modelDoc.to<JsonObject>(), converted, error));
        assert(!strcmp(modelDoc["format"], "remote-model"));
        char encoded[REMOTE_MODEL_MAX_BYTES];
        size_t encodedLength = serializeJson(modelDoc, encoded, sizeof(encoded));
        assert(encodedLength > 0 && encodedLength < sizeof(encoded));
        modelDoc.clear(); assert(!deserializeJson(modelDoc, encoded, encodedLength));
        RemoteSettingsSnapshot portableTarget = {}; portableTarget.newBoard = true;
        portableTarget.crsf = remoteDefaultCrsfSettings();
        portableTarget.crsf.axisProfile[0] = {333, 1111, 1777, 0, 0, 0};
        portableTarget.crsf.axisProfile[1] = {444, 1222, 1666, 0, 0, 0};
        RemoteSettingsSnapshot portableRestored;
        if (!remoteDecodeTransfer(modelDoc.as<JsonVariantConst>(), RemoteTransferKind::ModelProfile,
                                  portableTarget, portableRestored, error)) {
            JsonVariantConst multiplier = modelDoc["display"]["multiplier"];
            fprintf(stderr, "multiplier isfloat=%d value=%f\n", multiplier.is<float>(), multiplier.as<float>());
            fprintf(stderr, "portable round trip rejected: %u %s %s\n", (unsigned)error.code, error.path, error.message);
            return 1;
        }
        assert(portableRestored.crsf.timeTravel.enabled && portableRestored.crsf.timeTravel.channel == 16);
        for(int bad : {0,1,3,17}) {
            modelDoc["timeTravel"]["channel"] = bad;
            assert(!remoteDecodeTransfer(modelDoc.as<JsonVariantConst>(), RemoteTransferKind::ModelProfile, portableTarget, portableRestored, error));
        }
        modelDoc["timeTravel"]["channel"] = 16;
        assert(remoteDecodeTransfer(modelDoc.as<JsonVariantConst>(), RemoteTransferKind::ModelProfile, portableTarget, portableRestored, error));
        assert(portableRestored.crsf.axisProfile[0].minimum == 333);
        assert(portableRestored.crsf.axisProfile[0].center == 1111);
        assert(portableRestored.crsf.axisProfile[1].minimum == 444);

        DynamicJsonDocument backupDoc(16384);
        assert(remoteEncodeBackup(backupDoc.to<JsonObject>(), converted, error));
        assert(!strcmp(backupDoc["format"], "remote-crsf-backup"));
        assert(backupDoc["schemaVersion"] == 1 && backupDoc["calibration"].size() == 4);
        encodedLength = serializeJson(backupDoc, encoded, sizeof(encoded));
        assert(encodedLength > 0 && encodedLength < sizeof(encoded));
        backupDoc.clear(); assert(!deserializeJson(backupDoc, encoded, encodedLength));
        RemoteSettingsSnapshot backupTarget = portableTarget;
        RemoteSettingsSnapshot backupRestored;
        assert(remoteDecodeTransfer(backupDoc.as<JsonVariantConst>(), RemoteTransferKind::DeviceBackup,
                                    backupTarget, backupRestored, error));
        assert(backupRestored.radio.modelId == ids[i - 1]);
        assert(backupRestored.crsf.timeTravel.enabled && backupRestored.crsf.timeTravel.channel == 16);
        assert(backupRestored.crsf.axisProfile[0].minimum == 101);
        assert(backupRestored.crsf.axisProfile[0].center == 1024);
        assert(backupRestored.crsf.axisProfile[0].maximum == 1900);
        assert(backupRestored.crsf.axisProfile[1].minimum == 201);
        assert(backupRestored.crsf.adcHysteresis == 7);
        assert(!strcmp(backupRestored.settings.opMode, "2") && !strcmp(backupRestored.settings.crsfap, "0"));
        assert(!memcmp(&backupRestored.crsf.gimbalRouting, &converted.crsf.gimbalRouting, sizeof(converted.crsf.gimbalRouting)));
        assert(!memcmp(&backupRestored.crsf.switchRouting, &converted.crsf.switchRouting, sizeof(converted.crsf.switchRouting)));
        assert(!memcmp(&backupRestored.crsf.logic, &converted.crsf.logic, sizeof(converted.crsf.logic)));
        assert(!memcmp(&backupRestored.crsf.mixes, &converted.crsf.mixes, sizeof(converted.crsf.mixes)));
        if (strstr(argv[i], "switch_mix")) {
            ELRSLogicInputs inputs = {}; inputs.axesValid = true; inputs.validSwitches = 0x0fff;
            for (auto &axis : inputs.axesUs) axis = 1500;
            for (uint16_t state : {0, 1}) {
                inputs.switchStates = state; int16_t base[16];
                for (auto &channel : base) channel = 1500;
                for (unsigned sw = 0; sw < ELRS_SWITCH_INPUT_COUNT; ++sw) {
                    const uint8_t channel = backupRestored.crsf.switchRouting.channels[sw];
                    if (channel) base[channel - 1] = (state & (1u << sw)) ? 2000 : 1000;
                }
                const auto result = elrsEvaluateMixes(backupRestored.crsf.mixes, inputs,
                    elrsEvaluateLogic(backupRestored.crsf.logic, inputs), base, 0);
                assert(!(result.invalidChannels & (1u << 4)) && result.valuesUs[4] == 1750);
            }
        }
    }
    puts("portable models retain target calibration; CRSF backups restore their own calibration, filtering and portal modes");
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
    mapping = {"axes": {"Ail": 1, "Ele": 2, "Rud": 3, "Thr": 4}, "gimbals": [1, 2, 3, 4], "switches": [0] * 12}
    with tempfile.TemporaryDirectory(prefix="crsf-backup-roundtrip-") as temp:
        generated = []
        for index, (path, radio, version, extra) in enumerate(sources):
            switch = "SF" if version == "2.12.4" and radio == "TX16S" else "SA"
            inputs = {**mapping, **extra, "switchMap": {switch: {"input": 0, "reverse": True, "positions": 2}}}
            result = convert_model(path, radio, version, inputs)
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
