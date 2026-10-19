#ifndef _ELRS_DEVICE_CONFIG_H
#define _ELRS_DEVICE_CONFIG_H

#include "elrs_crsf_transport.h"

enum class ELRSOperationState : uint8_t {
    Idle, Queued, Waiting, ConfirmationRequired, Running, Complete, Cancelled, Failed, OutcomeUnknown
};
enum class ELRSDeviceError : uint8_t {
    None, Busy, Stale, Malformed, TooLarge, Unsupported, Timeout, Unsafe, Rejected, NotConnected
};
struct ELRSConfigSafety {
    bool maintenance = false, automaticPreferenceApply = false, radioReviewPending = false;
    bool fakePower = false, calibrating = false, selfTest = false, storageBusy = false;
    bool rfConnectionKnown = false, rfConnected = false, armedKnown = false, armed = false;
    bool modelMatched = false;
    uint16_t finalCh5Ticks = 172;
    uint32_t statusAgeMs = UINT32_MAX, generation = 0;
};
struct ELRSDeviceInfo {
    uint8_t address = 0, fieldCount = 0, parameterVersion = 0;
    uint32_t serial = 0, hardwareVersion = 0, softwareVersion = 0, generation = 0;
    char name[48] = {};
};
struct ELRSParameterValue {
    uint8_t type = 0;
    int32_t raw = 0;
    uint32_t descriptorHash = 0;
    ELRSParameterValue() {}
    ELRSParameterValue(uint8_t t, int32_t v) : type(t), raw(v) {}
};
struct ELRSParameterView {
    uint8_t device = 0, field = 0, parent = 0, type = 0, flags = 0;
    uint32_t generation = 0;
    ELRSParameterValue value, minimum, maximum, defaultValue;
    uint16_t dataLength = 0, nameOffset = 2, optionsOffset = 0, unitOffset = 0, infoOffset = 0;
    uint8_t precision = 0, commandStatus = 0, timeoutRaw = 0;
    bool writable = false;
    uint8_t data[256] = {};
    const char *name() const { return (const char *)data + nameOffset; }
    const char *options() const { return optionsOffset ? (const char *)data + optionsOffset : ""; }
    const char *unit() const { return unitOffset ? (const char *)data + unitOffset : ""; }
    const char *info() const { return infoOffset ? (const char *)data + infoOffset : ""; }
    bool hidden() const { return flags != 0; }
    bool optionAt(uint8_t index, const char *&text, size_t &length) const;
};
struct ELRSOperationStatus {
    uint32_t id = 0, generation = 0, deadlineAtMs = 0;
    ELRSOperationState state = ELRSOperationState::Idle;
    ELRSDeviceError error = ELRSDeviceError::None;
    uint8_t device = 0, field = 0, commandStatus = 0, timeoutRaw = 0;
    char message[96] = {};
};

bool elrsMatchSelection(const ELRSParameterView &, const ELRSSelectionPreference &, uint8_t &index);

class ELRSDeviceConfig {
 public:
    void reset();
    void tick(ELRSCrsfTransport &, const ELRSConfigSafety &, uint32_t nowMs);
    bool onFrame(uint8_t type, const uint8_t *payload, size_t length, uint32_t nowMs);
    bool discover(uint32_t operationId);
    uint8_t deviceCount() const { return _deviceCount; }
    const ELRSDeviceInfo *deviceAt(uint8_t index) const { return index < _deviceCount ? &_devices[index] : NULL; }
    bool read(uint8_t device, uint8_t field, uint32_t generation, uint32_t operationId);
    bool readRadioStatus(uint32_t operationId);
    bool write(uint8_t device, uint8_t field, uint32_t generation,
               const ELRSParameterValue &expected, const ELRSParameterValue &value, uint32_t operationId);
    bool command(uint8_t device, uint8_t field, uint32_t generation, uint8_t status, uint32_t operationId);
    bool selectModel(uint8_t modelId, uint32_t generation, uint32_t operationId);
    bool assignReceiver(uint8_t rxDevice, uint8_t modelId, uint32_t generation, uint32_t operationId);
    const ELRSOperationStatus &status() const { return _status; }
    const ELRSParameterView *parameter() const { return _viewReady ? &_view : NULL; }
    bool busy() const;
    bool mutationSupported() const { return _viewReady && !_view.hidden() && (_view.writable || _view.type==13) && supportedRole(); }
    ELRSConfigSafety radioStatus(uint32_t nowMs) const;
    uint8_t modelId() const { return _modelId; }
    bool modelIdKnown() const { return _modelIdKnown; }

 private:
    enum Request : uint8_t { None, Discover, Read, Write, Command, SelectModel, AssignReceiver, RadioStatus };
    bool begin(Request, uint8_t device, uint8_t field, uint32_t generation, uint32_t operationId);
    int deviceIndex(uint8_t address) const;
    bool parseField();
    bool deviceInfo(const uint8_t *, size_t);
    void fail(ELRSDeviceError);
    void sent(uint32_t nowMs);
    bool queue(uint8_t type, uint8_t device, const uint8_t *payload, size_t length);
    bool stringEnd(size_t start, size_t &end) const;
    bool safe(const ELRSConfigSafety &, uint32_t nowMs) const;
    bool critical() const;
    bool refreshesDependents() const;
    bool supportedRole() const;
    bool readModelId(uint8_t &id) const;
    void invalidateDescriptors();
    static int32_t integer(const uint8_t *data, uint8_t width, bool sign);
    ELRSCrsfTransport *_transport = NULL;
    ELRSDeviceInfo _devices[4];
    uint8_t _parents[4][256] = {}, _parentKnown[4][32] = {};
    uint8_t _deviceCount = 0, _chunk = 0, _chunksRemain = 0, _retries = 0;
    uint32_t _generation = 1, _queuedCount = 0, _startedAt = 0;
    bool _queued = false, _chunkActive = false, _viewReady = false, _hasStarted = false;
    bool _queuedMutation = false, _probingRadio = false, _haveRadio = false;
    bool _rfConnected = false, _armed = false, _modelMatched = false, _modelIdKnown = false;
    uint8_t _phase = 0, _modelId = 0, _modelMatchField = 0;
    uint8_t _queuedType = 0, _lastSentType = 0, _queuedChunk = 0, _lastSentChunk = 0;
    uint32_t _radioAt = 0, _settleUntil = 0;
    uint32_t _nextPollAt = 0, _commandReplyAt = 0;
    bool _commandSent = false;
    uint8_t _receiverModelFields[4] = {}, _assignDevice = 0, _assignModelId = 0;
    ELRSParameterValue _expected, _desired;
    Request _request = None;
    ELRSOperationStatus _status;
    ELRSParameterView _view;
};

#endif
