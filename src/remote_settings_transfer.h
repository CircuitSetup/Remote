#ifndef REMOTE_SETTINGS_TRANSFER_H
#define REMOTE_SETTINGS_TRANSFER_H
#ifndef ARDUINOJSON_VERSION_MAJOR
#define ARDUINOJSON_USE_LONG_LONG 0
#define ARDUINOJSON_USE_DOUBLE 1
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 0
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 0
#define ARDUINOJSON_DECODE_UNICODE 1
#define ARDUINOJSON_ENABLE_STD_STREAM 0
#ifndef _WIN32
#define ARDUINOJSON_ENABLE_STD_STRING 0
#endif
#define ARDUINOJSON_ENABLE_NAN 0
#endif
#include <ArduinoJson.h>
#include "remote_global.h"
#include "remote_settings.h"
#include "src/CRSF/elrs_input_model.h"
#include "src/CRSF/elrs_device_config.h"

enum class RemoteTransferKind : uint8_t { DeviceBackup, ModelProfile };
enum class RemoteSettingsEncoding : uint8_t { Transfer, StoredFile };
enum class RemoteStoreState : uint8_t { Absent, Present, Disabled };
enum class RemoteStoreMedium : uint8_t { Flash, SD };
enum class RemoteTransferCode : uint8_t {
    None, TooLarge, UnsupportedSchema, MissingSection, InvalidValue, DanglingSource,
    UnsupportedBehavior, StalePreview, MediaUnavailable, WriteFailed, RollbackRequired,
    RecoveryAnchorUnavailable, Busy, IdentityChoiceRequired
};
struct RemoteTransferError {
    RemoteTransferCode code = RemoteTransferCode::None;
    char path[96] = {}, message[128] = {};
};
struct [[gnu::packed]] RemoteSecondarySettings {
    int32_t up, dn, zero;
    uint8_t brightness, curVolume, movieMode, displayTCDSMode, showUpdAvail, updateV, updateR, carMode;
};
struct [[gnu::packed]] RemoteTertiarySettings { uint16_t visMode; uint8_t musFolderNum, mpShuffle; };
static_assert(sizeof(RemoteSecondarySettings)==20 && sizeof(RemoteTertiarySettings)==4,"Preserve stored settings");
struct RemoteStoreRecord {
    RemoteStoreState state = RemoteStoreState::Absent;
    RemoteStoreMedium medium = RemoteStoreMedium::Flash;
    uint16_t byteLength = 0;
    char sha256[65] = {};
};
struct RemoteRfChoice {
    char role[24] = {}, label[64] = {}, unit[24] = {};
    uint32_t descriptorHash = 0;
};
struct RemoteSettingsSnapshot {
    Settings settings;
    IPSettings ip;
    uint32_t identity = 0;
    RemoteSecondarySettings secondary = {};
    RemoteTertiarySettings tertiary = {};
    ELRSCrsfSettingsBlob crsf = {};
    ELRSRadioPreferences radio;
    RemoteStoreRecord stores[7];
    bool newBoard = false, flashRO = false, sdAvailable = false;
    uint32_t generation = 0;
    RemoteRfChoice rfChoices[8];
    uint8_t rfChoiceCount = 0;
    uint32_t deviceGeneration = 0, capabilityFingerprint = 0;
};
struct RemoteRestorePreview {
    RemoteTransferKind kind = RemoteTransferKind::ModelProfile;
    char fileHash[65] = {}, settingsHash[65] = {};
    uint32_t expiresAt = 0, settingsGeneration = 0, deviceGeneration = 0, capabilityFingerprint = 0;
    bool valid = false, cloneIdentity = false, restart = true;
    RemoteStoreRecord stores[7];
    uint32_t proposedIdentity = 0;
    char hostname[32] = {}, ip[20] = {};
    ELRSCrsfSettingsBlob currentCrsf = {}, proposedCrsf = {};
    ELRSRadioPreferences currentRadio, proposedRadio;
    uint16_t currentCadence = 250, proposedCadence = 250;
    uint8_t changedStores = 0;
};
constexpr size_t REMOTE_BACKUP_MAX_BYTES = 32768, REMOTE_MODEL_MAX_BYTES = 16384;
constexpr size_t REMOTE_TRANSFER_JSON_CAPACITY = 16384;
extern ELRSRadioPreferences remoteRadioPreferences;
extern bool remoteRadioVersioned;
extern bool remoteSettingsRecoveryBlocked;
const char *remoteTransferCodeName(RemoteTransferCode code);
bool remoteTransferFail(RemoteTransferError &, RemoteTransferCode, const char *, const char *);
ELRSCrsfSettingsBlob remoteDefaultCrsfSettings();
uint8_t remoteFrameChecksum(const uint8_t *, size_t);
void remoteSha256(const uint8_t *, size_t, char hash[65]);
bool remoteDecodeCrsf(const uint8_t *, size_t, ELRSCrsfSettingsBlob &, RemoteTransferError &);
bool remoteEncodeSettings(JsonObject, const Settings &, const ELRSRadioPreferences &, bool mqtt,
    RemoteSettingsEncoding encoding=RemoteSettingsEncoding::Transfer, bool newBoard=true);
bool remoteDecodeSettings(JsonObjectConst, Settings &, ELRSRadioPreferences &, bool mqtt, RemoteTransferError &);
bool remoteDecodeRadioPreferences(JsonObjectConst, ELRSRadioPreferences &);
bool remoteEncodeModel(JsonObject, const RemoteSettingsSnapshot &, RemoteTransferError &);
bool remoteEncodeBackup(JsonObject, const RemoteSettingsSnapshot &, RemoteTransferError &);
bool remoteDecodeTransfer(JsonVariantConst, RemoteTransferKind, const RemoteSettingsSnapshot &, RemoteSettingsSnapshot &, RemoteTransferError &, bool cloneIdentity=false);
bool remoteSerializeStore(const RemoteSettingsSnapshot &, uint8_t id, uint8_t *, size_t capacity, size_t &length, RemoteTransferError &,
    RemoteSettingsEncoding encoding=RemoteSettingsEncoding::Transfer);
bool captureRemoteSettings(RemoteSettingsSnapshot &, RemoteTransferError &);
bool validateRemoteTransfer(File &, RemoteTransferKind, const RemoteSettingsSnapshot &, RemoteRestorePreview &, RemoteTransferError &);
bool applyRemoteTransfer(const RemoteRestorePreview &, RemoteTransferError &);
bool remoteCommitELRSSettings(const RemoteSettingsSnapshot &, const RemoteSettingsSnapshot &, RemoteTransferError &);
bool elrsIsValidCrsfSettingsBlob(const ELRSCrsfSettingsBlob &);
bool recoverRemoteSettingsRestore(RemoteTransferError &);
bool remoteSettingsRestoreLoaded(RemoteTransferError &);
bool remoteMaintenanceActive();
void remoteSetMaintenance(bool enabled);
const char *remoteParameterRole(const ELRSParameterView &);
void remoteObserveRfChoice(const ELRSParameterView &,uint32_t operationId);
void remoteCaptureRfChoices(RemoteSettingsSnapshot &);
bool remoteSettingsBusy();
bool remoteRadioReviewPending();
bool remoteCompleteRadioReview();
void remoteSettingsChanged();
const char *remoteStoreName(uint8_t);
const char *remoteStorePath(uint8_t);
bool remoteCheckedSettingsWrite(const char *,const uint8_t *,size_t, bool useSD);
#endif
