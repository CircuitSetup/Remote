#include "elrs_device_config.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

ELRSRadioPreferences elrsRadioPreferencesFromLegacy(uint8_t telemetry, uint8_t power, uint8_t dynamic)
{
    ELRSRadioPreferences result;
    snprintf(result.telemetry, sizeof(result.telemetry), "%s", elrsTelemetryRatioLabel(telemetry));
    snprintf(result.maxPower.label, sizeof(result.maxPower.label), "%u", (unsigned)elrsMaxPowerMilliwatts(power));
    snprintf(result.dynamic, sizeof(result.dynamic), "%s", elrsDynamicPowerLabel(dynamic));
    return result;
}

bool elrsIsValidRadioPreferences(const ELRSRadioPreferences &p)
{
    return p.modelId <= 63 && p.telemetry[0] && p.dynamic[0] && p.maxPower.label[0] &&
        memchr(p.telemetry, 0, sizeof(p.telemetry)) && memchr(p.dynamic, 0, sizeof(p.dynamic)) &&
        memchr(p.maxPower.label, 0, sizeof(p.maxPower.label)) && memchr(p.maxPower.unit, 0, sizeof(p.maxPower.unit));
}

bool elrsMatchSelection(const ELRSParameterView &view, const ELRSSelectionPreference &preference, uint8_t &index)
{
    if(view.type != 9 || !memchr(preference.label, 0, sizeof(preference.label)) ||
       !memchr(preference.unit, 0, sizeof(preference.unit)) || strcmp(view.unit(), preference.unit)) return false;
    bool found = false;
    for(int i = view.minimum.raw; i <= view.maximum.raw; i++) {
        const char *label;
        size_t length;
        if(!view.optionAt((uint8_t)i, label, length) || strlen(preference.label) != length || memcmp(label, preference.label, length)) continue;
        if(found) return false;
        found = true;
        index = i;
    }
    return found;
}

bool ELRSParameterView::optionAt(uint8_t index, const char *&text, size_t &length) const
{
    if(type != 9 || !optionsOffset) return false;
    text = options();
    for(unsigned int i = 0; i < index; i++) {
        text = strchr(text, ';');
        if(!text) return false;
        text++;
    }
    const char *end = strchr(text, ';');
    length = end ? (size_t)(end - text) : strlen(text);
    return length != 0;
}

bool ELRSDeviceConfig::busy() const
{
    return _status.state == ELRSOperationState::Queued || _status.state == ELRSOperationState::Waiting ||
           _status.state == ELRSOperationState::Running || _status.state == ELRSOperationState::ConfirmationRequired;
}

void ELRSDeviceConfig::reset()
{
    if(_queued && _transport) _transport->cancelServiceFrame();
    _generation++;
    _deviceCount = 0;
    _request = None;
    _queued = _chunkActive = _viewReady = false;
    _status = ELRSOperationStatus();
    _haveRadio = _modelIdKnown = false;
    _modelMatchField = 0;
    memset(_parents, 0, sizeof(_parents));
    memset(_parentKnown, 0, sizeof(_parentKnown));
}

int ELRSDeviceConfig::deviceIndex(uint8_t address) const
{
    for(uint8_t i = 0; i < _deviceCount; i++) if(_devices[i].address == address) return i;
    return -1;
}

void ELRSDeviceConfig::fail(ELRSDeviceError error)
{
    if(_queued && _transport && _transport->serviceTxCount() == _queuedCount) _transport->cancelServiceFrame();
    _queued = false;
    _viewReady = false;
    _status.error = error;
    _status.state = _phase == 2 && error != ELRSDeviceError::Rejected &&
        (_request == Write || _request == SelectModel || _request == AssignReceiver) ? ELRSOperationState::OutcomeUnknown :
        _request == Command && _commandSent ? ELRSOperationState::OutcomeUnknown : ELRSOperationState::Failed;
}

bool ELRSDeviceConfig::begin(Request request, uint8_t device, uint8_t field, uint32_t generation, uint32_t id)
{
    if(busy()) return false;
    _status = ELRSOperationStatus();
    _status.id = id;
    _status.device = device;
    _status.field = field;
    _status.generation = generation;
    _viewReady = _chunkActive = _queued = _hasStarted = false;
    _view = ELRSParameterView();
    _chunk = _retries = 0;
    _phase = 0;
    _commandSent = false;
    _lastSentType = 0;
    _queuedMutation = _probingRadio = false;
    _request = request;
    if(request == Write || request == SelectModel || request == Command || request == AssignReceiver) {
        int tx = deviceIndex(0xEE);
        if(tx < 0 || _devices[tx].serial != 0x454C5253UL) { fail(ELRSDeviceError::Unsupported); return false; }
    }
    int index = deviceIndex(device);
    if(request != Discover && (index < 0 || generation != _devices[index].generation)) {
        fail(ELRSDeviceError::Stale);
        return false;
    }
    if(request != Discover && request != RadioStatus && (!field || field > _devices[index].fieldCount)) {
        fail(ELRSDeviceError::Unsupported);
        return false;
    }
    _status.state = ELRSOperationState::Queued;
    return true;
}

bool ELRSDeviceConfig::discover(uint32_t id)
{
    if(busy()) return false;
    reset();
    return begin(Discover, 0, 0, _generation, id);
}

bool ELRSDeviceConfig::read(uint8_t device, uint8_t field, uint32_t generation, uint32_t id)
{
    return begin(Read, device, field, generation, id);
}

bool ELRSDeviceConfig::readRadioStatus(uint32_t id)
{
    int index = deviceIndex(0xEE);
    return index >= 0 && _devices[index].serial == 0x454C5253UL && begin(RadioStatus, 0xEE, 0, _devices[index].generation, id);
}

bool ELRSDeviceConfig::write(uint8_t device, uint8_t field, uint32_t generation,
                            const ELRSParameterValue &expected, const ELRSParameterValue &value, uint32_t id)
{
    if(busy()) return false;
    _expected = expected;
    _desired = value;
    return begin(Write, device, field, generation, id);
}

bool ELRSDeviceConfig::selectModel(uint8_t model, uint32_t generation, uint32_t id)
{
    if(busy() || model > 63 || !_modelMatchField) return false;
    _desired = ELRSParameterValue(9, model);
    return begin(SelectModel, 0xEE, _modelMatchField, generation, id);
}

bool ELRSDeviceConfig::command(uint8_t device, uint8_t field, uint32_t generation, uint8_t action, uint32_t id)
{
    if(busy()) {
        if(_request != Command || _status.id != id || _status.device != device || _status.field != field ||
           _status.generation != generation || _queued || (action != 5 &&
           !(action == 4 && _status.state == ELRSOperationState::ConfirmationRequired))) return false;
        _desired = ELRSParameterValue(13, action);
        _phase = 1;
        _status.state = ELRSOperationState::Queued;
        return true;
    }
    if(action != 1) return false;
    _desired = ELRSParameterValue(13, action);
    return begin(Command, device, field, generation, id);
}

bool ELRSDeviceConfig::assignReceiver(uint8_t rx, uint8_t model, uint32_t generation, uint32_t id)
{
    const int index = deviceIndex(rx);
    if(busy() || rx != 0xEC || index < 0 || !_receiverModelFields[index] || !_modelIdKnown ||
       model > 63 || model != _modelId || !_modelMatchField) return false;
    _assignDevice = rx;
    _assignModelId = model;
    _desired = ELRSParameterValue(9, 1);
    return begin(AssignReceiver, 0xEE, _modelMatchField, generation, id);
}

ELRSConfigSafety ELRSDeviceConfig::radioStatus(uint32_t now) const
{
    ELRSConfigSafety result;
    result.statusAgeMs = _haveRadio ? (uint32_t)(now - _radioAt) : UINT32_MAX;
    result.rfConnectionKnown = result.armedKnown = _haveRadio && result.statusAgeMs <= 1000;
    result.rfConnected = _rfConnected;
    result.armed = _armed;
    result.modelMatched = _modelMatched;
    result.generation = _generation;
    return result;
}

bool ELRSDeviceConfig::critical() const
{
    const char *name = _view.name();
    return _request == SelectModel || !strcmp(name, "RF Band") || !strcmp(name, "Packet Rate") ||
        !strcmp(name, "Antenna Mode") || !strcmp(name, "Switch Mode") || !strcmp(name, "Model Match");
}

bool ELRSDeviceConfig::refreshesDependents() const
{
    return critical() || !strcmp(_view.name(), "Output Ch") || !strcmp(_view.name(), "Output Mode") ||
        !strcmp(_view.name(), "Protocol") || !strcmp(_view.name(), "Protocol2");
}

bool ELRSDeviceConfig::safe(const ELRSConfigSafety &local, uint32_t now) const
{
    auto radio = radioStatus(now);
    const bool legacy = local.legacyPreferenceApply && !local.radioReviewPending && _status.device == 0xEE &&
        (!strcmp(_view.name(), "Telem Ratio") || !strcmp(_view.name(), "Max Power") || !strcmp(_view.name(), "Dynamic"));
    if((!local.maintenance && !legacy) || local.fakePower || local.calibrating || local.selfTest ||
       local.storageBusy || local.finalCh5Ticks > 992 || !radio.armedKnown || radio.armed || !radio.rfConnectionKnown) return false;
    if(_request == AssignReceiver || (_request == Command &&
        (!strcmp(_view.name(), "Enable Rx WiFi") || !strcmp(_view.name(), "Enable RX WiFi")))) return radio.rfConnected;
    return _status.device == 0xEC ? radio.rfConnected : !critical() || !radio.rfConnected;
}

bool ELRSDeviceConfig::supportedRole() const
{
    if(_view.type == 13) {
        const char *names = _status.device == 0xEE ?
            "Bind\0Enable WiFi\0Enable Rx WiFi\0Enable RX WiFi\0Enable Backpack WiFi\0Enable VRx WiFi\0BLE Joystick\0\0" :
            _status.device == 0xEC ? "Set Failsafe Pos\0Enter Bind Mode\0Loan Model\0Return Model\0\0" : "";
        for(const char *name = names; *name; name += strlen(name) + 1) if(!strcmp(name, _view.name())) return true;
        return false;
    }
    const char *names = _status.device == 0xEE ?
        "RF Band\0Packet Rate\0Antenna Mode\0Switch Mode\0Model Match\0Telem Ratio\0Max Power\0Dynamic\0Fan Thresh\0Backpack\0DVR Rec\0DVR Srt Dly\0DVR Stp Dly\0HT Enable\0HT Start Channel\0Telemetry\0\0" :
        _status.device == 0xEC ? "Protocol\0Protocol2\0SBUS failsafe\0Tlm Power\0Ant. Mode\0Ant. Group\0Receiver Mode\0Init Rate\0Output Ch\0Input Ch\0Output Mode\0Invert\0Channel\0Position\0Bind Storage\0\0" : "";
    for(const char *name = names; *name; name += strlen(name) + 1) if(!strcmp(name, _view.name())) return true;
    return false;
}

bool ELRSDeviceConfig::readModelId(uint8_t &id) const
{
    const char *start = strstr(_view.unit(), "(ID:");
    if(!start) return false;
    start += 4;
    while(*start == ' ') start++;
    if(*start < '0' || *start > '9') return false;
    unsigned int value = 0;
    while(*start >= '0' && *start <= '9') { value = value * 10 + *start++ - '0'; if(value > 63) return false; }
    if(*start != ')') return false;
    id = value;
    return true;
}

void ELRSDeviceConfig::invalidateDescriptors()
{
    _generation++;
    for(uint8_t i = 0; i < _deviceCount; i++) _devices[i].generation = _generation;
    memset(_parentKnown, 0, sizeof(_parentKnown));
    _view.generation = _status.generation = _generation;
}

bool ELRSDeviceConfig::queue(uint8_t type, uint8_t device, const uint8_t *payload, size_t length)
{
    if(!_transport || _transport->hasPendingServiceFrame() || length > 58) return false;
    uint8_t frame[64] = {0xC8, (uint8_t)(length + 4), type, device, (uint8_t)(type == 0x28 ? 0xEA : 0xEF)};
    if(length) memcpy(frame + 5, payload, length);
    if(type == 0x32 && length >= 1) {
        uint8_t crc = 0;
        for(size_t i = 2; i < length + 4; i++) {
            crc ^= frame[i];
            for(int bit = 0; bit < 8; bit++) crc = (crc & 0x80) ? (crc << 1) ^ 0xBA : crc << 1;
        }
        frame[length + 4] = crc;
    }
    frame[length + 5] = ELRSCrsfTransport::crc8D5(frame + 2, length + 3);
    if(!_transport->queueServiceFrame(frame, length + 6)) return false;
    _queuedCount = _transport->serviceTxCount();
    _queuedType = type;
    _queuedChunk = _chunk;
    _queued = true;
    return true;
}

void ELRSDeviceConfig::sent(uint32_t now)
{
    if(!_queued || !_transport || _transport->serviceTxCount() == _queuedCount) return;
    _queued = false;
    _lastSentType = _queuedType;
    _lastSentChunk = _queuedChunk;
    _status.state = ELRSOperationState::Waiting;
    _status.deadlineAtMs = _transport->lastServiceTxAt() + (_status.device == 0xEC ? 2000 : 500);
    if(_queuedMutation) {
        _queuedMutation = false;
        _phase = _request == Command ? 3 : 2;
        _settleUntil = _transport->lastServiceTxAt() + 100;
        _status.state = _request == Command ? ELRSOperationState::Running : ELRSOperationState::Queued;
        if(_request == Command) { _commandSent = true; _commandReplyAt = now; _nextPollAt = now + 200; }
        _view.dataLength = 0;
        _chunk = 0;
        _chunkActive = false;
        if(_request == AssignReceiver) {
            _status.device = _assignDevice;
            _status.field = _receiverModelFields[deviceIndex(_assignDevice)];
        }
    }
    if(_request == Command && _phase == 3) _status.state = ELRSOperationState::Running;
    if(!_hasStarted) { _startedAt = now; _hasStarted = true; }
}

void ELRSDeviceConfig::tick(ELRSCrsfTransport &transport, const ELRSConfigSafety &local, uint32_t now)
{
    _transport = &transport;
    sent(now);
    if(_queued && _queuedMutation && !safe(local, now)) { fail(ELRSDeviceError::Unsafe); return; }
    if(_request == Command && _phase == 3 && busy()) {
        if(_status.state == ELRSOperationState::ConfirmationRequired) {
            if((uint32_t)(now - _startedAt) >= 300000) fail(ELRSDeviceError::Timeout);
            return;
        }
        if((uint32_t)(now - _commandReplyAt) >= 2500 || (uint32_t)(now - _startedAt) >= 300000) {
            fail(ELRSDeviceError::Timeout); return;
        }
        if(!_queued && (int32_t)(now - _nextPollAt) >= 0) {
            if(!_chunkActive) { _view.dataLength = 0; _chunk = 0; }
            const uint8_t payload[] = {_status.field, 6};
            if(queue(0x2D, _status.device, payload, 2)) _nextPollAt = now + 200;
        }
        return;
    }
    if((_request == Read || _request == Write || _request == SelectModel || _request == AssignReceiver) && busy() && _hasStarted && (uint32_t)(now - _startedAt) >= 30000) {
        fail(ELRSDeviceError::Timeout);
        return;
    }
    if(_status.state == ELRSOperationState::Queued && !_queued) {
        if(_request == RadioStatus) { const uint8_t data[] = {0, 0}; queue(0x2D, 0xEE, data, 2); return; }
        if(_phase == 1) {
            const auto radio = radioStatus(now);
            if(!radio.armedKnown || !radio.rfConnectionKnown) {
                uint8_t probe[] = {0, 0};
                if(queue(0x2D, 0xEE, probe, 2)) _probingRadio = true;
                return;
            }
            if(!safe(local, now)) { fail(ELRSDeviceError::Unsafe); return; }
            uint8_t payload[4] = {_status.field, 0, 0, 0};
            uint8_t length = 2, type = 0x2D;
            if(_request == SelectModel) {
                type = 0x32;
                payload[0] = 0x10;
                payload[1] = 0x05;
                payload[2] = _desired.raw;
                length = 4;
            } else if(_view.type == 2 || _view.type == 3) {
                payload[1] = (uint16_t)_desired.raw >> 8;
                payload[2] = (uint16_t)_desired.raw & 0xff;
                length = 3;
            } else payload[1] = (uint8_t)_desired.raw;
            if(queue(type, _status.device, payload, length)) _queuedMutation = true;
            return;
        }
        if(_phase == 2 && (int32_t)(now - _settleUntil) < 0) return;
        uint8_t payload[] = {_status.field, _chunk};
        queue(_request == Discover ? 0x28 : 0x2C, _status.device, payload, _request == Discover ? 0 : 2);
    } else if(_status.state == ELRSOperationState::Waiting && (int32_t)(now - _status.deadlineAtMs) >= 0) {
        if(_request == Discover && _deviceCount) _status.state = ELRSOperationState::Complete;
        else if(_retries >= 2 || (uint32_t)(now - _startedAt) >= 30000) fail(ELRSDeviceError::Timeout);
        else if(!transport.hasPendingServiceFrame()) {
            _retries++;
            _status.state = ELRSOperationState::Queued;
        }
    }
}

bool ELRSDeviceConfig::deviceInfo(const uint8_t *payload, size_t length)
{
    if(length < 17 || payload[1] == 0 || payload[1] == 0xEA || payload[1] == 0xEF) return false;
    const uint8_t *end = (const uint8_t *)memchr(payload + 2, 0, length - 2);
    if(!end || end - (payload + 2) >= 48 || (size_t)(end - payload) + 15 > length) return false;
    int index = deviceIndex(payload[1]);
    const uint8_t *info = end + 1;
    ELRSDeviceInfo next;
    next.address = payload[1];
    memcpy(next.name, payload + 2, end - (payload + 2));
    next.serial = (uint32_t)integer(info, 4, false);
    next.hardwareVersion = (uint32_t)integer(info + 4, 4, false);
    next.softwareVersion = (uint32_t)integer(info + 8, 4, false);
    next.fieldCount = info[12];
    next.parameterVersion = info[13];
    if(index < 0) {
        if(_deviceCount == 4) { fail(ELRSDeviceError::TooLarge); return false; }
        index = _deviceCount++;
    } else if(strcmp(next.name, _devices[index].name) || next.serial != _devices[index].serial ||
              next.hardwareVersion != _devices[index].hardwareVersion || next.softwareVersion != _devices[index].softwareVersion ||
              next.fieldCount != _devices[index].fieldCount || next.parameterVersion != _devices[index].parameterVersion) {
        _generation++;
        _haveRadio = false;
        if(next.address == 0xEE) { _modelIdKnown = false; _modelMatchField = 0; }
        for(uint8_t i = 0; i < _deviceCount; i++) _devices[i].generation = _generation;
        memset(_parentKnown, 0, sizeof(_parentKnown));
        if(busy() && _request != Discover) fail(ELRSDeviceError::Stale);
    }
    next.generation = _generation;
    _devices[index] = next;
    return true;
}

bool ELRSDeviceConfig::onFrame(uint8_t type, const uint8_t *payload, size_t length, uint32_t now)
{
    if(!payload || length < 2 || (payload[0] != 0xEA && payload[0] != 0xEF)) return false;
    sent(now);
    if((_request == Read || _request == Write || _request == SelectModel || _request == AssignReceiver) && busy() && _hasStarted && (uint32_t)(now - _startedAt) >= 30000) {
        fail(ELRSDeviceError::Timeout);
        return false;
    }
    if(type == 0x29) return deviceInfo(payload, length);
    const int tx = deviceIndex(0xEE);
    if(type == 0x2E && payload[1] == 0xEE && tx >= 0 && _devices[tx].serial == 0x454C5253UL &&
       length >= 7 && memchr(payload + 6, 0, length - 6)) {
        _haveRadio = true;
        _radioAt = now;
        _rfConnected = payload[5] & 1;
        _armed = payload[5] & 8;
        _modelMatched = _rfConnected && !(payload[5] & 4);
        if(_request == RadioStatus && busy()) _status.state = ELRSOperationState::Complete;
        if(_probingRadio && busy()) { _probingRadio = false; _status.state = ELRSOperationState::Queued; _retries = 0; }
        return true;
    }
    if(type != 0x2B || !busy() || (_request != Read && _request != Write && _request != SelectModel && _request != Command && _request != AssignReceiver) || length < 5 ||
       payload[1] != _status.device || payload[2] != _status.field || _queuedMutation ||
       (_request == Command && _phase == 3 ? _lastSentType != 0x2D : _lastSentType != 0x2C) || _lastSentChunk != _chunk) return false;
    if(_chunkActive && payload[3] != (uint8_t)(_chunksRemain - 1)) return false;
    if(_queued) {
        if(_transport->serviceTxCount() == _queuedCount) _transport->cancelServiceFrame();
        _queued = false;
    }
    size_t bytes = length - 4;
    if(_view.dataLength + bytes > sizeof(_view.data)) { fail(ELRSDeviceError::TooLarge); return false; }
    memcpy(_view.data + _view.dataLength, payload + 4, bytes);
    _view.dataLength += bytes;
    _chunksRemain = payload[3];
    if(_chunksRemain) {
        _chunkActive = true;
        _chunk++;
        _retries = 0;
        _status.state = _request == Command && _phase == 3 ? ELRSOperationState::Running : ELRSOperationState::Queued;
        if(_request == Command && _phase == 3) { _commandReplyAt = now; _nextPollAt = now; }
        return true;
    }
    if(!parseField()) { fail(ELRSDeviceError::Malformed); return false; }
    _viewReady = true;
    if(_request == Command) {
        if(_view.type != 13 || _view.hidden() || !supportedRole()) { fail(ELRSDeviceError::Unsupported); return false; }
        if(_phase == 0) { _phase = 1; _status.state = ELRSOperationState::Queued; return true; }
        _commandReplyAt = now;
        _chunk = 0;
        _chunkActive = false;
        _status.commandStatus = _view.commandStatus;
        _status.timeoutRaw = _view.timeoutRaw;
        snprintf(_status.message, sizeof(_status.message), "%s", _view.info());
        _status.state = _view.commandStatus == 3 ? ELRSOperationState::ConfirmationRequired :
            _view.commandStatus == 0 ? (_desired.raw == 5 ? ELRSOperationState::Cancelled : ELRSOperationState::Complete) :
            ELRSOperationState::Running;
        if(_status.state==ELRSOperationState::Complete && (!strcmp(_view.name(),"Bind") || !strcmp(_view.name(),"Enter Bind Mode")))
            strcpy(_status.message,"Command acknowledged; verify receiver link and Model Match");
        _nextPollAt = now + 200;
        return true;
    }
    if(_request == Write || _request == SelectModel || _request == AssignReceiver) {
        if(_phase == 0) {
            if(!_view.writable || _view.hidden() || !supportedRole()) { fail(ELRSDeviceError::Unsupported); return false; }
            if(_request == Write) {
                if(_expected.type != _view.type || _expected.raw != _view.value.raw ||
                   (_expected.descriptorHash && _expected.descriptorHash != _view.value.descriptorHash)) {
                    fail(ELRSDeviceError::Stale); return false;
                }
                const char *option;
                size_t optionLength;
                if(_desired.type != _view.type || _desired.raw < _view.minimum.raw || _desired.raw > _view.maximum.raw ||
                   (_view.type == 9 && !_view.optionAt((uint8_t)_desired.raw, option, optionLength))) {
                    fail(ELRSDeviceError::Unsupported); return false;
                }
            }
            if(_request == AssignReceiver && (!_modelIdKnown || _modelId != _assignModelId || strcmp(_view.name(), "Model Match"))) {
                fail(ELRSDeviceError::Stale); return false;
            }
            _phase = 1;
            _status.state = ELRSOperationState::Queued;
            return true;
        }
        uint8_t selected;
        char *end = NULL;
        const long receiverId = _request == AssignReceiver ? strtol(_view.info(), &end, 10) : 0;
        if(_phase != 2 || (_request == Write && _view.value.raw != _desired.raw) ||
           (_request == SelectModel && (!readModelId(selected) || selected != _desired.raw)) ||
           (_request == AssignReceiver && (_view.type != 12 || !_view.info()[0] || !end || *end || receiverId != _assignModelId))) {
            fail(ELRSDeviceError::Rejected); _viewReady = true; return false;
        }
        if(_request == AssignReceiver || refreshesDependents()) invalidateDescriptors();
    }
    _status.state = ELRSOperationState::Complete;
    return true;
}

int32_t ELRSDeviceConfig::integer(const uint8_t *data, uint8_t width, bool sign)
{
    uint32_t value = 0;
    for(uint8_t i = 0; i < width; i++) value = (value << 8) | data[i];
    if(sign && width < 4 && (value & (1UL << (width * 8 - 1)))) value |= UINT32_MAX << (width * 8);
    return (int32_t)value;
}

bool ELRSDeviceConfig::stringEnd(size_t start, size_t &end) const
{
    if(start >= _view.dataLength) return false;
    const uint8_t *zero = (const uint8_t *)memchr(_view.data + start, 0, _view.dataLength - start);
    if(!zero) return false;
    end = zero - _view.data;
    return true;
}

bool ELRSDeviceConfig::parseField()
{
    if(_view.dataLength < 3) return false;
    _view.device = _status.device;
    _view.field = _status.field;
    _view.generation = _status.generation;
    _view.parent = _view.data[0];
    _view.flags = _view.data[1] & 0xC0;
    _view.type = _view.data[1] == 127 ? 127 : _view.data[1] & 0x3f;
    int device = deviceIndex(_view.device);
    if(_view.parent > _devices[device].fieldCount || _view.parent == _view.field) return false;
    uint8_t parent = _view.parent;
    for(int depth = 0; parent && depth <= 255; depth++) {
        if(parent == _view.field) return false;
        if(!(_parentKnown[device][parent / 8] & (1 << (parent % 8)))) break;
        parent = _parents[device][parent];
        if(depth == 255) return false;
    }
    size_t end, offset;
    if(!stringEnd(2, end)) return false;
    offset = end + 1;
    _view.value.type = _view.minimum.type = _view.maximum.type = _view.defaultValue.type = _view.type;
    if(_view.type == 9) {
        _view.optionsOffset = offset;
        if(!stringEnd(offset, end)) return false;
        offset = end + 1;
        if(offset + 4 >= _view.dataLength) return false;
        _view.value.raw = _view.data[offset];
        _view.minimum.raw = _view.data[offset + 1];
        _view.maximum.raw = _view.data[offset + 2];
        _view.defaultValue.raw = _view.data[offset + 3];
        unsigned int count = 1;
        for(const char *text = _view.options(); *text; text++) if(*text == ';') count++;
        if(_view.minimum.raw > _view.maximum.raw || _view.maximum.raw >= (int32_t)count ||
           _view.value.raw < _view.minimum.raw || _view.value.raw > _view.maximum.raw) return false;
        _view.unitOffset = offset + 4;
        if(!stringEnd(_view.unitOffset, end)) return false;
        _view.writable = true;
    } else if(_view.type <= 3 || _view.type == 8) {
        const uint8_t width = _view.type == 8 ? 4 : _view.type < 2 ? 1 : 2;
        const bool sign = _view.type == 1 || _view.type == 3 || _view.type == 8;
        const uint8_t extra = _view.type == 8 ? 5 : 0;
        if(offset + width * 4 + extra >= _view.dataLength) return false;
        _view.value.raw = integer(_view.data + offset, width, sign);
        _view.minimum.raw = integer(_view.data + offset + width, width, sign);
        _view.maximum.raw = integer(_view.data + offset + width * 2, width, sign);
        _view.defaultValue.raw = integer(_view.data + offset + width * 3, width, sign);
        if(_view.minimum.raw > _view.maximum.raw || _view.value.raw < _view.minimum.raw || _view.value.raw > _view.maximum.raw) return false;
        if(extra) { _view.precision = _view.data[offset + 16]; if(_view.precision > 9) return false; }
        _view.unitOffset = offset + width * 4 + extra;
        if(!stringEnd(_view.unitOffset, end)) return false;
        _view.writable = _view.type <= 3;
    } else if(_view.type == 10 || _view.type == 12) {
        _view.infoOffset = offset;
        if(!stringEnd(offset, end)) return false;
    } else if(_view.type == 13) {
        if(offset + 2 >= _view.dataLength || _view.data[offset] > 6) return false;
        _view.commandStatus = _view.data[offset];
        _view.timeoutRaw = _view.data[offset + 1];
        _view.infoOffset = offset + 2;
        if(!stringEnd(_view.infoOffset, end)) return false;
    } else if(_view.type == 11 && offset < _view.dataLength) {
        while(offset < _view.dataLength && _view.data[offset] != 0xFF) {
            if(!_view.data[offset] || _view.data[offset] > _devices[device].fieldCount || _view.data[offset] == _view.field) return false;
            offset++;
        }
        if(offset == _view.dataLength) return false;
    }
    _parents[device][_view.field] = _view.parent;
    _parentKnown[device][_view.field / 8] |= 1 << (_view.field % 8);
    uint32_t hash = 2166136261UL;
    for(size_t i = 0; i < _view.dataLength; i++) hash = (hash ^ _view.data[i]) * 16777619UL;
    _view.value.descriptorHash = hash;
    if(_view.device == 0xEE && !strcmp(_view.name(), "Model Match")) {
        _modelMatchField = _view.field;
        _modelIdKnown = readModelId(_modelId);
    }
    if(_view.device == 0xEC && _view.type == 12 && (!strcmp(_view.name(), "Model Id") || !strcmp(_view.name(), "Model ID")))
        _receiverModelFields[device] = _view.field;
    return true;
}
