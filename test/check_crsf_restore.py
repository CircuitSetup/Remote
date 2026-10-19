"""Exercise the CRSF model codec and preview/apply path from production source."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "test"))
from check_crsf_adc import HEADERS, compile_and_run  # noqa: E402

HEADERS["FS.h"] = r'''
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <memory>
class String {};
class File {
    std::vector<uint8_t> *bytes = nullptr;
    size_t position = 0;
public:
    File() = default;
    explicit File(std::vector<uint8_t> *data): bytes(data) {}
    explicit operator bool() const { return bytes != nullptr; }
    size_t size() const { return bytes ? bytes->size() : 0; }
    bool seek(size_t p) { if(!bytes || p > bytes->size()) return false; position = p; return true; }
    int read() { return bytes && position < bytes->size() ? (*bytes)[position++] : -1; }
    size_t read(uint8_t *out, size_t n) {
        if(!bytes) return 0;
        n = std::min(n, bytes->size() - std::min(position, bytes->size()));
        memcpy(out, bytes->data() + position, n); position += n; return n;
    }
    void close() { bytes = nullptr; }
};
namespace fs {
class FS {
public:
    std::map<std::string, std::vector<uint8_t>> files;
    bool exists(const char *path) const { return files.count(path) != 0; }
    File open(const char *path, const char *mode = "r") {
        if(mode[0] == 'w') files[path].clear();
        auto found = files.find(path);
        return found == files.end() ? File() : File(&found->second);
    }
};
}
extern fs::FS LittleFS, SD;
'''
HEADERS["LittleFS.h"] = '#pragma once\n#include "FS.h"\n'
HEADERS["SD.h"] = '#pragma once\n#include "FS.h"\n'

runtime_source = (ROOT / "src/src/CRSF/crsf_model_restore.cpp").read_text(encoding="utf-8")
runtime = runtime_source[
    runtime_source.index("static uint32_t settingsGeneration=1;"):
    runtime_source.rindex("\n#endif")
]

CASES = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
#include "crsf_model_files.h"
#include "crsf_settings.h"
#include <LittleFS.h>
#include <SD.h>

extern bool haveFS, haveSD, haveNewBoard, FlashROMode;
extern int myRemID;
extern unsigned long testNow;
inline unsigned long millis() { return testNow; }
ELRSCrsfSettingsBlob snapshotELRSSettings();
void testTransferService();
bool opModeCRSF = false;
struct ReviewRadioConfig {
    bool known = true;
    uint8_t id = 9;
    bool modelIdKnown() const { return known; }
    uint8_t modelId() const { return id; }
};
struct { ReviewRadioConfig config; ReviewRadioConfig &deviceConfig() { return config; } } elrsMode;
ELRSRadioPreferences remoteRadioPreferences;

#define REMOTE_TRANSFER_TEST
'''

CASES += runtime + r'''
fs::FS LittleFS, SD;
bool haveFS = true, haveSD = true, haveNewBoard = true, FlashROMode = false;
int myRemID = 17;
unsigned long testNow = 100;
ELRSCrsfSettingsBlob liveCrsf;
ELRSPortalPreferences livePortal;
bool crsfSettingsSaving = false, crsfSettingsRecoveryBlocked = false;
unsigned saveCalls = 0;
bool savedRequireSafe = false;
ELRSCrsfSettingsBlob savedCrsf;
ELRSPortalPreferences savedPortal;
void testTransferService() {}
ELRSCrsfSettingsBlob snapshotELRSSettings() { return liveCrsf; }
ELRSPortalPreferences snapshotELRSPortalPreferences() { return livePortal; }
bool saveELRSSettings(const ELRSCrsfSettingsBlob &model, const ELRSPortalPreferences &portal, bool requireSafe) {
    ++saveCalls; savedRequireSafe = requireSafe; savedCrsf = model; savedPortal = portal;
    liveCrsf = model; livePortal = portal; return true;
}
static void putFile(fs::FS &storage, const char *path, const std::string &text) {
    storage.files[path] = std::vector<uint8_t>(text.begin(), text.end());
}
static std::string encodeModel(const RemoteSettingsSnapshot &source, int expo, int modelId) {
    DynamicJsonDocument doc(REMOTE_TRANSFER_JSON_CAPACITY); RemoteTransferError error;
    assert(remoteEncodeModel(doc.to<JsonObject>(), source, error));
    doc["inputs"][0]["expo"] = expo;
    doc["radio"]["modelId"] = modelId;
    doc["radio"]["crsfUpdateHz"] = 500;
    doc["speedUnits"] = "mph";
    std::string out; serializeJson(doc, out); return out;
}
static RemoteSettingsSnapshot capture() {
    RemoteSettingsSnapshot snapshot; RemoteTransferError error;
    assert(captureRemoteSettings(snapshot, error)); return snapshot;
}
static RemoteRestorePreview preview(const std::string &json, RemoteSettingsSnapshot &target, RemoteTransferKind kind = RemoteTransferKind::ModelProfile) {
    target = capture(); putFile(remoteUploadStorage(), remoteUploadPath(), json);
    File file = remoteUploadStorage().open(remoteUploadPath()); RemoteRestorePreview result; RemoteTransferError error;
    const bool accepted = validateRemoteTransfer(file, kind, target, result, error);
    if(!accepted) fprintf(stderr, "preview rejected: code=%u path=%s message=%s\n", (unsigned)error.code, error.path, error.message);
    assert(accepted && result.valid);
    file.close(); return result;
}
static void expectStale(RemoteRestorePreview &p) {
    RemoteTransferError error; const unsigned calls = saveCalls;
    assert(!applyRemoteTransfer(p, error));
    assert(error.code == RemoteTransferCode::StalePreview && saveCalls == calls);
}
static void testFileValidation() {
    auto target = capture(); const std::string valid = encodeModel(target, 35, 12);
    RemoteRestorePreview p = preview(valid, target);
    assert(p.valid && p.proposedCrsf.axisProfile[0].expo == 35 && p.proposedRadio.modelId == 12);
    assert(p.settingsGeneration == target.generation && p.deviceGeneration == target.deviceGeneration);
    assert(p.capabilityFingerprint == target.capabilityFingerprint);

    std::string duplicate = valid; duplicate.insert(duplicate.size() - 1, ",\"format\":\"remote-model\"");
    putFile(LittleFS, remoteUploadPath(), duplicate);
    File file = LittleFS.open(remoteUploadPath()); RemoteTransferError error; RemoteRestorePreview rejected;
    assert(!validateRemoteTransfer(file, RemoteTransferKind::ModelProfile, target, rejected, error));
    assert(error.code == RemoteTransferCode::InvalidValue); file.close();

    std::string escaped = valid; const auto key = escaped.find("\"format\""); assert(key != std::string::npos);
    escaped.replace(key, 8, "\"\\u0066ormat\""); putFile(LittleFS, remoteUploadPath(), escaped);
    file = LittleFS.open(remoteUploadPath());
    assert(!validateRemoteTransfer(file, RemoteTransferKind::ModelProfile, target, rejected, error));
    assert(error.code == RemoteTransferCode::InvalidValue); file.close();

    putFile(LittleFS, remoteUploadPath(), "{\"x\":[[[[[[[[[0]]]]]]]]]}"); file = LittleFS.open(remoteUploadPath());
    assert(!validateRemoteTransfer(file, RemoteTransferKind::ModelProfile, target, rejected, error));
    assert(error.code == RemoteTransferCode::InvalidValue); file.close();

    LittleFS.files[remoteUploadPath()] = std::vector<uint8_t>(REMOTE_MODEL_MAX_BYTES + 1, ' ');
    file = LittleFS.open(remoteUploadPath());
    assert(!validateRemoteTransfer(file, RemoteTransferKind::ModelProfile, target, rejected, error));
    assert(error.code == RemoteTransferCode::TooLarge); file.close();
}
static void testImportsCannotLeaveCrsfDisabled() {
    const unsigned calls = saveCalls;
    const auto original = livePortal;
    const auto originalExpo = liveCrsf.axisProfile[0].expo;
    for(unsigned mode = 0; mode <= 2; ++mode) {
        livePortal.opMode = mode;
        auto target = capture();
        RemoteRestorePreview p;
        RemoteTransferError error;
        if(mode == 0) {
            p = preview(encodeModel(target, 42, 9), target);
            assert(!applyRemoteTransfer(p, error));
            assert(error.code == RemoteTransferCode::UnsupportedBehavior && !strcmp(error.path, "opMode"));
        }
        auto source = target;
        strcpy(source.settings.opMode, "0");
        DynamicJsonDocument doc(REMOTE_TRANSFER_JSON_CAPACITY);
        assert(remoteEncodeBackup(doc.to<JsonObject>(), source, error));
        std::string json; serializeJson(doc, json);
        p = preview(json, target, RemoteTransferKind::DeviceBackup);
        assert(!applyRemoteTransfer(p, error));
        assert(error.code == RemoteTransferCode::UnsupportedBehavior && !strcmp(error.path, "opMode"));
        assert(saveCalls == calls && !remoteRadioReviewPending());
        assert(livePortal.opMode == mode && liveCrsf.axisProfile[0].expo == originalExpo);
    }
    livePortal = original;
}
static void testPreviewBindingAndApply() {
    auto target = capture(); const std::string valid = encodeModel(target, 42, 9);
    auto p = preview(valid, target);
    std::string changed = valid + " "; putFile(LittleFS, remoteUploadPath(), changed); expectStale(p);

    p = preview(valid, target); liveCrsf.axisProfile[0].expo++; expectStale(p); liveCrsf.axisProfile[0].expo--;

    p = preview(valid, target); remoteSettingsChanged(); expectStale(p);
    --settingsGeneration;

    target = capture(); target.deviceGeneration = 21;
    putFile(LittleFS, remoteUploadPath(), valid); File file = LittleFS.open(remoteUploadPath());
    RemoteRestorePreview deviceBound; RemoteTransferError error;
    assert(validateRemoteTransfer(file, RemoteTransferKind::ModelProfile, target, deviceBound, error)); file.close();
    assert(deviceBound.deviceGeneration == 21); expectStale(deviceBound);

    p = preview(valid, target); p.expiresAt = millis(); expectStale(p);

    p = preview(valid, target);
    assert(applyRemoteTransfer(p, error));
    assert(saveCalls == 1 && savedRequireSafe && savedPortal.radioReview == 1);
    assert(savedCrsf.axisProfile[0].expo == 42 && savedPortal.radio.modelId == 9);
    assert(savedPortal.cadence == 4 && savedPortal.speedUnits == 1);
    assert(remoteRadioReviewPending());
}
static void testRadioReviewRequiresActiveCrsfAndReadback() {
    livePortal.radioReview = 1;
    remoteRadioPreferences = livePortal.radio;
    remoteSetMaintenance(true);
    unsigned calls = saveCalls;
    assert(!remoteCompleteRadioReview());
    assert(livePortal.radioReview == 1 && saveCalls == calls);

    opModeCRSF = true;
    assert(!remoteCompleteRadioReview());
    assert(livePortal.radioReview == 1 && saveCalls == calls);
    const char *labels[] = {livePortal.radio.maxPower.label, livePortal.radio.telemetry, livePortal.radio.dynamic};
    const char *units[] = {livePortal.radio.maxPower.unit, " (1:64)", ""};
    for(unsigned i = 0; i < 3; ++i) {
        auto &observation = rfObservations[i + 5];
        observation.generation = 0;
        observation.at = millis();
        strcpy(observation.choice.label, labels[i]);
        strcpy(observation.choice.unit, units[i]);
    }
    strcpy(rfObservations[5].choice.unit, "W");
    assert(!remoteCompleteRadioReview());
    strcpy(rfObservations[5].choice.unit, livePortal.radio.maxPower.unit);
    strcpy(rfObservations[6].choice.label, "Off");
    assert(!remoteCompleteRadioReview());
    strcpy(rfObservations[6].choice.label, livePortal.radio.telemetry);
    for(const char *unit : {" (1:64)", " (2000bps)", ""}) {
        strcpy(rfObservations[6].choice.unit, unit);
        livePortal.radioReview = 1;
        assert(remoteCompleteRadioReview());
        assert(livePortal.radioReview == 0 && saveCalls == ++calls);
    }
    opModeCRSF = false;
}
static void testCapabilityBindingAndStorage() {
    livePortal.radioReview = 0;
    ELRSParameterView choice; choice.device = 0xee; choice.type = 9; choice.field = 3;
    choice.optionsOffset = 64; choice.value.raw = 0; choice.value.descriptorHash = 77;
    strcpy((char *)choice.data + 2, "Max Power"); strcpy((char *)choice.data + 64, "100mW;250mW");
    remoteObserveRfChoice(choice, 1);
    auto target = capture(); assert(target.capabilityFingerprint != 0);
    auto p = preview(encodeModel(target, 30, 7), target);
    assert(p.capabilityFingerprint == target.capabilityFingerprint);
    choice.value.raw = 1; remoteObserveRfChoice(choice, 2); expectStale(p);

    haveFS = true; haveSD = true; FlashROMode = true;
    assert(remoteUploadAvailable() && &remoteUploadStorage() == &SD);
    FlashROMode = false; haveFS = false;
    assert(remoteUploadAvailable() && &remoteUploadStorage() == &SD);
    haveSD = false; assert(!remoteUploadAvailable());
    haveFS = true; haveSD = true;
    assert(&remoteUploadStorage() == &LittleFS);
}
int main() {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    liveCrsf = remoteDefaultCrsfSettings();
    livePortal.opMode = 1;
    livePortal.radio.modelId = 2; strcpy(livePortal.radio.telemetry, "Std");
    strcpy(livePortal.radio.dynamic, "Off"); strcpy(livePortal.radio.maxPower.label, "100mW");
    strcpy(livePortal.radio.maxPower.unit, "mW");
    testFileValidation(); testImportsCannotLeaveCrsfDisabled(); testPreviewBindingAndApply(); testRadioReviewRequiresActiveCrsfAndReadback(); testCapabilityBindingAndStorage();
    puts("CRSF model bounds, preview binding/expiry, upload selection and CRSF-only review pass");
}
'''

compile_and_run(
    CASES,
    ["elrs_input_model.cpp", "elrs_crsf_core.cpp", "elrs_crsf_transport.cpp", "crsf_model_files.cpp"],
    ["-I" + str(ROOT / "src/src/CRSF"), "-I" + str(ROOT / ".pio/libdeps/esp32dev/ArduinoJson/src"), "-lbcrypt"],
)
