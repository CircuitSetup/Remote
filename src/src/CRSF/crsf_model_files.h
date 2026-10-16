#pragma once
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
#include <FS.h>
#include "elrs_input_model.h"
#include "elrs_device_config.h"

enum class RemoteTransferKind : uint8_t {DeviceBackup,ModelProfile};
enum class RemoteTransferCode : uint8_t {None,TooLarge,UnsupportedSchema,MissingSection,InvalidValue,DanglingSource,UnsupportedBehavior,StalePreview,MediaUnavailable,WriteFailed,RollbackRequired,RecoveryAnchorUnavailable,Busy,IdentityChoiceRequired};
struct RemoteTransferError {RemoteTransferCode code=RemoteTransferCode::None;char path[96]={},message[128]={};};
struct RemoteCrsfParameters {char opMode[2]="0",crsfap[2]="1",elrsPktRate[2]="3",elrsSpdUnit[2]="0";};
struct RemoteRfChoice {char role[24]={},label[64]={},unit[24]={};uint32_t descriptorHash=0;};
struct RemoteSettingsSnapshot {
    ELRSCrsfSettingsBlob crsf={};ELRSRadioPreferences radio;RemoteCrsfParameters settings;
    uint32_t identity=0,generation=0,deviceGeneration=0,capabilityFingerprint=0;
    bool newBoard=false;RemoteRfChoice rfChoices[8];uint8_t rfChoiceCount=0;
};
struct RemoteRestorePreview {
    RemoteTransferKind kind=RemoteTransferKind::ModelProfile;char fileHash[65]={},settingsHash[65]={};
    uint32_t expiresAt=0,settingsGeneration=0,deviceGeneration=0,capabilityFingerprint=0;
    RemoteCrsfParameters currentSettings,proposedSettings;
    bool valid=false;ELRSCrsfSettingsBlob currentCrsf={},proposedCrsf={};
    ELRSRadioPreferences currentRadio,proposedRadio;uint16_t currentCadence=250,proposedCadence=250;
};
constexpr size_t REMOTE_MODEL_MAX_BYTES=16384,REMOTE_BACKUP_MAX_BYTES=16384,REMOTE_TRANSFER_JSON_CAPACITY=16384;
const char *remoteTransferCodeName(RemoteTransferCode);
bool remoteTransferFail(RemoteTransferError &,RemoteTransferCode,const char *,const char *);
void remoteSha256(const uint8_t *,size_t,char[65]);
ELRSCrsfSettingsBlob remoteDefaultCrsfSettings();
bool elrsIsValidCrsfSettingsBlob(const ELRSCrsfSettingsBlob &);
bool remoteEncodeModel(JsonObject,const RemoteSettingsSnapshot &,RemoteTransferError &);
bool remoteEncodeBackup(JsonObject,const RemoteSettingsSnapshot &,RemoteTransferError &);
bool remoteDecodeTransfer(JsonVariantConst,RemoteTransferKind,const RemoteSettingsSnapshot &,RemoteSettingsSnapshot &,RemoteTransferError &);
bool captureRemoteSettings(RemoteSettingsSnapshot &,RemoteTransferError &);
bool validateRemoteTransfer(File &,RemoteTransferKind,const RemoteSettingsSnapshot &,RemoteRestorePreview &,RemoteTransferError &);
bool applyRemoteTransfer(const RemoteRestorePreview &,RemoteTransferError &);
bool remoteFileActivationSafe(RemoteTransferError &);
bool remoteMaintenanceActive();void remoteSetMaintenance(bool);
const char *remoteParameterRole(const ELRSParameterView &);
void remoteObserveRfChoice(const ELRSParameterView &,uint32_t);void remoteCaptureRfChoices(RemoteSettingsSnapshot &);
bool remoteSettingsBusy();bool remoteRadioReviewPending();bool remoteCompleteRadioReview();void remoteSettingsChanged();

namespace fs {class FS;}
bool remoteUploadAvailable();fs::FS &remoteUploadStorage();const char *remoteUploadPath();
