"""Convert supported transmitter inputs, then exercise the production native codec and evaluator."""
import json
import ast
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "test"))
sys.path.insert(0, str(ROOT / "tools"))
from check_crsf_adc import HEADERS, ROOT as REPO, compile_and_run  # noqa: E402
from model_import import convert_model  # noqa: E402

def _restore_header_literals():
    tree = ast.parse((REPO / "test" / "check_crsf_restore.py").read_text(encoding="utf-8"))
    found = {}
    for node in ast.walk(tree):
        if not isinstance(node, ast.Assign):
            continue
        for target in node.targets:
            if (isinstance(target, ast.Subscript) and isinstance(target.value, ast.Name)
                    and target.value.id == "HEADERS"):
                key = target.slice.value if isinstance(target.slice, ast.Constant) else None
                if key in {"FS.h", "remote_transfer_test_io.h"}:
                    found[key] = ast.literal_eval(node.value)
    assert set(found) == {"FS.h", "remote_transfer_test_io.h"}
    return found

HEADERS.update(_restore_header_literals())

MAPPING = {
    "axes": {"Ail": 1, "Ele": 2, "Rud": 3, "Thr": 4},
    "gimbals": [1, 2, 3, 4],
    "switches": [0] * 12,
    "switchMap": {"SF": {"input": 0, "reverse": True, "positions": 2}},
}
BOXER_MAPPING = {**MAPPING, "switchMap": {"SA": {"input": 0, "reverse": True, "positions": 2}}}
CASES = r'''
#define REMOTE_TRANSFER_TEST
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include "remote_settings_restore.cpp"
fs::FS LittleFS, SD;
unsigned testStep=0, testFailAt=0, testCrashAt=0, testSDUnavailableAt=0;
unsigned long testNow=100;
bool haveFS=true, haveSD=true, FlashROMode=false, remoteSettingsRecoveryBlocked=false;
ELRSRadioPreferences remoteRadioPreferences;
bool remoteRadioVersioned=false;
RemoteSettingsSnapshot current;
RemoteSecondarySettings secSettings={};
RemoteTertiarySettings terSettings={};
uint32_t secSettingsHash=111, terSettingsHash=222;
const char *secCfgName="/secondary", *terCfgName="/tertiary";
bool cachedWritePass=true;
void testTransferService() {}
bool captureRemoteSettings(RemoteSettingsSnapshot &snapshot, RemoteTransferError &) { snapshot=current; return true; }
void remoteSettingsChanged() { ++current.generation; }
bool saveConfigFile(const char *, uint8_t *, int, int) { return cachedWritePass; }
bool writeFileToSD(const char *, uint8_t *, int) { return cachedWritePass; }
bool writeFileToFS(const char *, uint8_t *, int) { return cachedWritePass; }

static void checkProfile(const char *path, uint8_t modelId) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string json = buffer.str();
    DynamicJsonDocument doc(16384);
    assert(!deserializeJson(doc, json));

    RemoteSettingsSnapshot target = {};
    target.crsf = remoteDefaultCrsfSettings();
    target.identity = 77;
    target.newBoard = true;
    strcpy(target.settings.ssid, "target-wifi");
    strcpy(target.settings.pass, "target-secret");
    target.crsf.axisProfile[0] = {123, 1011, 1987, 1, 4, 0};
    for (uint8_t i = 0; i < 7; ++i) {
        target.stores[i].state = RemoteStoreState::Present;
        target.stores[i].medium = RemoteStoreMedium::Flash;
    }

    RemoteSettingsSnapshot imported;
    RemoteTransferError error;
    assert(remoteDecodeTransfer(doc.as<JsonVariantConst>(), RemoteTransferKind::ModelProfile,
                                target, imported, error));
    assert(imported.identity == target.identity);
    assert(imported.radio.modelId == modelId);
    assert(imported.crsf.axisProfile[0].minimum == 123);
    assert(!strcmp(imported.settings.ssid, "target-wifi"));
    assert(!strcmp(imported.settings.pass, "target-secret"));

    // Seed the old native main/CRSF stores, then run the real local save transaction.
    RemoteSettingsSnapshot before = imported;
    before.stores[0].state = before.stores[6].state = RemoteStoreState::Present;
    before.stores[0].medium = before.stores[6].medium = RemoteStoreMedium::Flash;
    RemoteSettingsSnapshot candidate = before;
    strcpy(candidate.settings.hostName, "ordinary-save");
    current = before;
    LittleFS.files.clear(); SD.files.clear(); testStep=0; testFailAt=testCrashAt=0;
    uint8_t bytes[16384];
    size_t length = 0;
    for (uint8_t id : {0, 6}) {
        assert(remoteSerializeStore(before, id, bytes, sizeof(bytes), length, error,
                                    RemoteSettingsEncoding::StoredFile));
        LittleFS.files[remoteStorePath(id)] = std::vector<uint8_t>(bytes, bytes + length);
    }
    const auto oldFiles = LittleFS.files;
    auto invalidBlob = candidate;
    invalidBlob.crsf = {};
    assert(!remoteCommitELRSSettings(before, invalidBlob, error));
    assert(error.code == RemoteTransferCode::InvalidValue && !remoteSettingsBusy());
    assert(LittleFS.files == oldFiles);
    auto absentBlob = candidate;
    absentBlob.stores[6].state = RemoteStoreState::Absent;
    assert(!remoteCommitELRSSettings(before, absentBlob, error));
    assert(error.code == RemoteTransferCode::InvalidValue && !remoteSettingsBusy());
    assert(LittleFS.files == oldFiles);
    assert(remoteCommitELRSSettings(before, candidate, error));
    assert(!remoteRadioReviewPending());
    assert(recoverRemoteSettingsRestore(error));
    assert(remoteSettingsRestoreLoaded(error));
    assert(!remoteSettingsBusy() && !remoteRadioReviewPending());

    // Rebuild the export candidate from the bytes the transaction actually persisted.
    auto &mainBytes = LittleFS.files[remoteStorePath(0)];
    DynamicJsonDocument storedMain(8192);
    assert(!deserializeJson(storedMain, mainBytes.data(), mainBytes.size()));
    Settings savedSettings = target.settings;
    ELRSRadioPreferences savedRadio;
    assert(remoteDecodeSettings(storedMain.as<JsonObjectConst>(), savedSettings, savedRadio, false, error));
    assert(!strcmp(savedSettings.hostName, "ordinary-save"));
    assert(savedRadio.modelId == modelId);
    auto &crsfBytes = LittleFS.files[remoteStorePath(6)];
    ELRSCrsfSettingsBlob savedCrsf;
    assert(remoteDecodeCrsf(crsfBytes.data(), crsfBytes.size(), savedCrsf, error));
    assert(savedCrsf.axisProfile[0].minimum == 123);
    assert(!memcmp(&savedCrsf.gimbalRouting, &imported.crsf.gimbalRouting, sizeof(savedCrsf.gimbalRouting)));
    assert(!memcmp(&savedCrsf.switchRouting, &imported.crsf.switchRouting, sizeof(savedCrsf.switchRouting)));
    assert(!memcmp(&savedCrsf.logic, &imported.crsf.logic, sizeof(savedCrsf.logic)));
    assert(!memcmp(&savedCrsf.mixes, &imported.crsf.mixes, sizeof(savedCrsf.mixes)));
    assert(!memcmp(savedCrsf.outputLimits, imported.crsf.outputLimits, sizeof(savedCrsf.outputLimits)));

    RemoteSettingsSnapshot saved = candidate;
    saved.settings = savedSettings;
    saved.crsf = savedCrsf;
    DynamicJsonDocument exported(16384);
    assert(remoteEncodeModel(exported.to<JsonObject>(), saved, error));
    std::string exportText;
    serializeJson(exported, exportText);
    DynamicJsonDocument fileDoc(16384);
    assert(!deserializeJson(fileDoc, exportText));
    RemoteSettingsSnapshot restoreTarget = {};
    restoreTarget.crsf = remoteDefaultCrsfSettings();
    restoreTarget.identity = 91;
    restoreTarget.newBoard = true;
    strcpy(restoreTarget.settings.ssid, "restore-wifi");
    strcpy(restoreTarget.settings.pass, "restore-secret");
    restoreTarget.crsf.axisProfile[0] = {111, 999, 1999, 0, 0, 0};
    RemoteSettingsSnapshot restored;
    if (!remoteDecodeTransfer(fileDoc.as<JsonVariantConst>(), RemoteTransferKind::ModelProfile,
                              restoreTarget, restored, error)) {
        fprintf(stderr, "restore profile rejected: %s %s\\n", error.path, error.message);
        abort();
    }
    assert(restored.identity == 91);
    assert(restored.radio.modelId == modelId);
    assert(!strcmp(restored.settings.ssid, "restore-wifi"));
    assert(!strcmp(restored.settings.pass, "restore-secret"));
    assert(restored.crsf.axisProfile[0].minimum == 111);
    assert(restored.crsf.axisProfile[0].center == 999);
    assert(restored.crsf.axisProfile[0].maximum == 1999);
    assert(!memcmp(&restored.crsf.gimbalRouting, &saved.crsf.gimbalRouting, sizeof(saved.crsf.gimbalRouting)));
    assert(!memcmp(&restored.crsf.switchRouting, &saved.crsf.switchRouting, sizeof(saved.crsf.switchRouting)));
    assert(!memcmp(&restored.crsf.logic, &saved.crsf.logic, sizeof(saved.crsf.logic)));
    assert(!memcmp(&restored.crsf.mixes, &saved.crsf.mixes, sizeof(saved.crsf.mixes)));
    assert(!memcmp(restored.crsf.outputLimits, saved.crsf.outputLimits, sizeof(saved.crsf.outputLimits)));

    if (std::string(path).find("switch_mix") != std::string::npos) {
        assert(restored.crsf.switchRouting.channels[0] == 0);
        ELRSLogicInputs inputs = {};
        inputs.axesValid = true;
        inputs.validSwitches = 0x0fff;
        for (int16_t &axis : inputs.axesUs) axis = 1500;
        for (uint16_t state : {0, 1}) {
            inputs.switchStates = state;
            int16_t base[16] = {};
            for (unsigned channel = 0; channel < 16; ++channel) base[channel] = 1500;
            for (unsigned sw = 0; sw < ELRS_SWITCH_INPUT_COUNT; ++sw) {
                const uint8_t channel = restored.crsf.switchRouting.channels[sw];
                if (channel) base[channel - 1] = (state & (1u << sw)) ? 2000 : 1000;
            }
            const auto result = elrsEvaluateMixes(restored.crsf.mixes, inputs,
                elrsEvaluateLogic(restored.crsf.logic, inputs), base, 0);
            assert(!(result.invalidChannels & (1u << 4)));
            assert(result.valuesUs[4] == 1750);
        }
    }
}

int main(int argc, char **argv) {
    assert(argc == 6);
    const uint8_t modelIds[] = {0, 12, 63, 12, 63};
    for (int i = 1; i < argc; ++i) checkProfile(argv[i], modelIds[i - 1]);
    puts("converted YAML/.etx profiles survive native decode, StoredFile encode, model export/restore; target credentials, identity and calibration remain local");
}
'''

def _settings_functions():
    source = (REPO / "src" / "remote_settings.cpp").read_text(encoding="utf-8")
    signatures = [
        "uint32_t calcHash(uint8_t *buf, int len)",
        "static bool saveSecSettings(bool useCache)",
        "static bool saveTerSettings(bool useCache)",
        "static bool writeJSONCfgFile(const JsonDocument& json, const char *fn, bool useSD, uint32_t oldHash = 0, uint32_t *newHash = NULL)",
    ]
    return "\n".join(source[source.index(sig + "\n{"):source.index("\n}", source.index(sig + "\n{")) + 2]
                     for sig in signatures) + "\n"


def main():
    fixture = ROOT / "test" / "fixtures" / "model_import"
    sources = [
        (fixture / "edgetx" / "2.10.6" / "four_axis.yml", "Boxer", "2.10.6", BOXER_MAPPING),
        (fixture / "edgetx" / "2.12.4" / "four_axis.yml", "TX16S", "2.12.4", MAPPING),
        (fixture / "edgetx" / "2.12.4" / "switch_mix.yml", "TX16S", "2.12.4",
         {**MAPPING, "switches": [5] + [0] * 11}),
        (fixture / "companion" / "multi_model.etx", "TX16S", "2.12.4",
         {**MAPPING, "model": "models/four_axis.yml"}),
        (fixture / "edgetx" / "2.12.4" / "logical_gaps.yml", "TX16S", "2.12.4", MAPPING),
    ]
    with tempfile.TemporaryDirectory(prefix="crsf-save-roundtrip-") as temp:
        generated = []
        for index, (path, radio, version, mapping) in enumerate(sources):
            result = convert_model(path, radio, version, mapping)
            assert result.errors == [] and result.profile is not None, (path, result.errors)
            result.profile["radio"]["modelId"] = (0, 12, 63, 12, 63)[index]
            output = Path(temp) / f"profile-{index}.json"
            output.write_text(json.dumps(result.profile, ensure_ascii=False), encoding="utf-8")
            generated.append(output)
        source = CASES.replace("int main(int argc, char **argv) {", _settings_functions() + "\nint main(int argc, char **argv) {")
        compile_and_run(source,
            ["elrs_input_model.cpp", "elrs_crsf_core.cpp", "elrs_crsf_transport.cpp",
             "../../remote_settings_transfer.cpp"],
            ["-I" + str(ROOT / ".pio/libdeps/esp32dev/ArduinoJson/src"), "-lbcrypt"], generated)


if __name__ == "__main__":
    main()
