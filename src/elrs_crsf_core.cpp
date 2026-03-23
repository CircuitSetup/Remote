#include "elrs_crsf_core.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint16_t CRSF_CHANNEL_MIN = 172;
constexpr uint16_t CRSF_CHANNEL_MID = 992;
constexpr uint16_t CRSF_CHANNEL_MAX = 1811;

constexpr uint8_t CRSF_FRAME_GPS = 0x02;
constexpr uint8_t CRSF_FRAME_BATTERY = 0x08;
constexpr uint8_t CRSF_FRAME_AIRSPEED = 0x0A;
constexpr uint8_t CRSF_FRAME_LINK_STATS = 0x14;

constexpr unsigned long CRSF_DISPLAY_INTERVAL = 200;
constexpr unsigned long CRSF_INPUT_STALE_MS = 100;
constexpr unsigned long CRSF_COMM_OVERLAY_MS = 1000;
constexpr unsigned long CRSF_COMM_NSY_OVERLAY_MS = 1500;
constexpr unsigned long BATTERY_BANNER_INTERVAL = 30000;
constexpr unsigned long BATTERY_BANNER_DURATION = 1000;

constexpr uint8_t AXIS_THROTTLE = 0;
constexpr uint8_t AXIS_YAW = 1;
constexpr uint8_t AXIS_PITCH = 2;
constexpr uint8_t AXIS_ROLL = 3;

static long clampLong(long value, long lo, long hi)
{
    return (value < lo) ? lo : ((value > hi) ? hi : value);
}

}

ELRSCrsfCore::ELRSCrsfCore()
{
    for(int i = 0; i < 16; i++) {
        _channels[i] = CRSF_CHANNEL_MID;
    }

    for(int i = 0; i < ELRS_GIMBAL_AXIS_COUNT; i++) {
        _rawAxes[i] = 1024;
        _axisCal[i].minimum = 0;
        _axisCal[i].center = 1024;
        _axisCal[i].maximum = 2047;
    }

    memset(_overlayText, 0, sizeof(_overlayText));
    memset(_commOverlayText, 0, sizeof(_commOverlayText));
}

bool ELRSCrsfCore::begin(ELRSCrsfHost &host, const ELRSCrsfCoreConfig &config, unsigned long now)
{
    return begin(host, config, now, now * 1000UL);
}

bool ELRSCrsfCore::begin(ELRSCrsfHost &host, const ELRSCrsfCoreConfig &config, unsigned long now, unsigned long nowUs)
{
    _config = config;
    _config.transport.packetRateHz = elrsPacketRateOrDefault(_config.transport.packetRateHz);
    _transport = ELRSCrsfTransport();
    _transport.setSink(this);

    _startedAt = now;
    _selfTestUntil = 0;
    _lastAxisAttemptAt = 0;
    _lastGoodAxesAt = 0;
    _lastGoodPackAt = 0;
    _lastDisplay = 0;
    _lastTelemetry = 0;
    _lastLinkStats = 0;
    _lastGpsSpeed = 0;
    _lastAirspeed = 0;
    _batteryBlinkAt = 0;
    _batteryBannerAt = 0;
    _overlayUntil = 0;
    _commOverlayUntil = 0;
    _linkQuality = 0;
    _remoteBattery = 0;
    _remoteBatteryVoltage = 0.0f;
    _faultFlags = ELRS_FAULT_NONE;
    _gpsSpeed10 = 0;
    _airspeed10 = 0;
    _activeSpeedSource = SPEED_SOURCE_NONE;
    _haveAds = false;
    _fakePowerOn = false;
    _selfTestActive = false;
    _hasValidPackState = false;
    _lastPackStates = 0;
    _adcFaultActive = false;
    _buttonPackFaultActive = false;
    _lastCommCode = ELRS_COMM_NONE;
    _calibRaw = false;
    _calibPressed = false;
    _calibLongSent = false;
    _calibDebounceAt = 0;
    _calibPressedAt = 0;
    _calStage = CAL_IDLE;
    memset(_overlayText, 0, sizeof(_overlayText));
    memset(_commOverlayText, 0, sizeof(_commOverlayText));

    host.loadCalibration(_axisCal, ELRS_GIMBAL_AXIS_COUNT);

    log(host, "ELRS/CRSF: basic external module mode");
    log(host, "ELRS/CRSF: ELRS Lua/config menu unsupported");
    log(host, "ELRS/CRSF: TX=GPIO2 RX=GPIO34 OE=GPIO0");
    logf(host, "ELRS/CRSF: packet rate %uHz (module must match externally)", (unsigned)_config.transport.packetRateHz);
    log(host, "ELRS/CRSF: CH1 Roll CH2 Pitch CH3 Throttle CH4 Yaw CH5 Stop CH6 FakePower CH7 O.O CH8 RESET CH9-16 ButtonPack");
    log(host, "ELRS/CRSF: display GPS speed, then airspeed, then link quality");

    sampleAxes(host, now, true);

    for(int i = 0; i < 16; i++) {
        _channels[i] = CRSF_CHANNEL_MID;
    }

    _fakePowerOn = host.readFakePowerSwitch();
    applyIdleOutputs(host, _fakePowerOn);
    host.setStopLed(false);

    _transport.begin(host, _config.transport, now, nowUs);

    showOverlay("ELR", now, 1000);
    if(!_haveAds) {
        _faultFlags |= ELRS_FAULT_ADC_MISSING;
        _adcFaultActive = true;
        _channels[0] = CRSF_CHANNEL_MID;
        _channels[1] = CRSF_CHANNEL_MID;
        _channels[2] = CRSF_CHANNEL_MIN;
        _channels[3] = CRSF_CHANNEL_MID;
        log(host, "ELRS/CRSF: ADC missing");
        showOverlay("ADC", now, 1000);
    }
    updateInputFaults(host, now);

    return true;
}

void ELRSCrsfCore::loop(ELRSCrsfHost &host, unsigned long now, int battWarn)
{
    loop(host, now, now * 1000UL, battWarn);
}

void ELRSCrsfCore::loop(ELRSCrsfHost &host, unsigned long now, unsigned long nowUs, int battWarn)
{
    bool fakePower = host.readFakePowerSwitch();
    bool stopOn = host.readStopSwitch();
    bool buttonAOn = host.readButtonA();
    bool buttonBOn = host.readButtonB();
    uint8_t packStates = samplePackStates(host, now);

    _fakePowerOn = fakePower;
    host.setStopLed(stopOn);

    sampleAxes(host, now);
    updateInputFaults(host, now);

    if(_selfTestActive && _selfTestUntil && now >= _selfTestUntil) {
        stopSelfTest();
        log(host, "ELRS/CRSF: self-test stopped");
    }

    if(!_selfTestActive) {
        updateCalibrationButton(host, now, battWarn);
    }

    updateChannels(now, fakePower, stopOn, buttonAOn, buttonBOn, packStates);
    _transport.setChannels(_channels);
    _transport.loop(host, now, nowUs);

    updateBatteryWarning(host, now, battWarn, fakePower);
    updateBenchState(host, now);
    updateDisplay(host, now, battWarn);
}

void ELRSCrsfCore::startSelfTest(unsigned long now, unsigned long durationMs)
{
    _selfTestActive = true;
    _selfTestUntil = now + durationMs;
}

void ELRSCrsfCore::stopSelfTest()
{
    _selfTestActive = false;
    _selfTestUntil = 0;
}

bool ELRSCrsfCore::selfTestActive() const
{
    return _selfTestActive;
}

bool ELRSCrsfCore::isCalibrating() const
{
    return (_calStage != CAL_IDLE);
}

bool ELRSCrsfCore::fakePowerOn() const
{
    return _fakePowerOn;
}

uint32_t ELRSCrsfCore::baudRate() const
{
    return _transport.status().baudRate;
}

uint16_t ELRSCrsfCore::channelAt(uint8_t index) const
{
    return (index < 16) ? _channels[index] : 0;
}

uint8_t ELRSCrsfCore::linkQuality() const
{
    return _linkQuality;
}

uint8_t ELRSCrsfCore::remoteBatteryPercent() const
{
    return _remoteBattery;
}

float ELRSCrsfCore::remoteBatteryVoltage() const
{
    return _remoteBatteryVoltage;
}

uint16_t ELRSCrsfCore::gpsSpeed10() const
{
    return _gpsSpeed10;
}

uint16_t ELRSCrsfCore::airspeed10() const
{
    return _airspeed10;
}

bool ELRSCrsfCore::telemetryActive() const
{
    return _transport.status().telemetryActive;
}

bool ELRSCrsfCore::replyActive() const
{
    return _transport.status().replyActive;
}

bool ELRSCrsfCore::synced() const
{
    return _transport.status().synced;
}

ELRSCrsfCore::SpeedSource ELRSCrsfCore::activeSpeedSource() const
{
    return _activeSpeedSource;
}

ELRSCrsfStatus ELRSCrsfCore::getStatus() const
{
    const ELRSCrsfTransportStatus &transportStatus = _transport.status();
    ELRSCrsfStatus status;

    status.baudRate = transportStatus.baudRate;
    status.packetRateHz = transportStatus.packetRateHz;
    status.telemetryActive = transportStatus.telemetryActive;
    status.replyActive = transportStatus.replyActive;
    status.synced = transportStatus.synced;
    status.activeSpeedSource = (uint8_t)_activeSpeedSource;
    status.linkQuality = _linkQuality;
    status.remoteBatteryPercent = _remoteBattery;
    status.remoteBatteryVoltage = _remoteBatteryVoltage;
    status.faultFlags = _faultFlags;
    status.commCode = transportStatus.commCode;
    status.everReplied = transportStatus.everReplied;
    status.everSynced = transportStatus.everSynced;
    status.invertLine = transportStatus.invertLine;
    status.debugEnabled = transportStatus.debugEnabled;
    status.lastTxAt = transportStatus.lastTxAt;
    status.lastReplyAt = transportStatus.lastReplyAt;
    status.lastRxAt = transportStatus.lastRxAt;
    status.lastReplyTimeoutAt = transportStatus.lastReplyTimeoutAt;
    status.lastRawFrameSyncByte = transportStatus.lastRawFrame.syncByte;
    status.lastRawFrameType = transportStatus.lastRawFrame.type;
    status.lastRawFrameLength = transportStatus.lastRawFrame.length;
    status.lastRawFrameCrcValid = transportStatus.lastRawFrame.crcValid;
    status.fakePowerOn = _fakePowerOn;
    status.calibrating = (_calStage != CAL_IDLE);
    status.selfTestActive = _selfTestActive;

    return status;
}

bool ELRSCrsfCore::sampleAxes(ELRSCrsfHost &host, unsigned long now, bool force)
{
    int16_t axes[ELRS_GIMBAL_AXIS_COUNT];
    unsigned long sampleIntervalMs = axisSampleIntervalMs(_config.transport.packetRateHz);

    if(!force && (now - _lastAxisAttemptAt < sampleIntervalMs)) {
        return _haveAds;
    }

    _lastAxisAttemptAt = now;
    if(!host.sampleAxes(axes)) {
        return false;
    }

    for(int i = 0; i < ELRS_GIMBAL_AXIS_COUNT; i++) {
        _rawAxes[i] = axes[i];
    }
    _haveAds = true;
    _lastGoodAxesAt = now;

    return true;
}

uint8_t ELRSCrsfCore::samplePackStates(ELRSCrsfHost &host, unsigned long now)
{
    uint8_t packStates = _hasValidPackState ? _lastPackStates : 0;
    uint8_t sampledStates = 0;

    if(!_config.haveButtonPack) {
        return 0;
    }

    if(host.samplePackStates(sampledStates)) {
        _lastPackStates = sampledStates;
        _lastGoodPackAt = now;
        _hasValidPackState = true;
        packStates = sampledStates;
    }

    return packStates;
}

bool ELRSCrsfCore::onCrsfFrame(uint8_t type, const uint8_t *payload, size_t payloadLen, unsigned long now)
{
    bool supportedTelemetry = false;

    switch(type) {
    case CRSF_FRAME_LINK_STATS:
        if(payloadLen >= 10) {
            _linkQuality = payload[2];
            _lastLinkStats = now;
            supportedTelemetry = true;
        }
        break;
    case CRSF_FRAME_BATTERY:
        if(payloadLen >= 8) {
            _remoteBatteryVoltage = (float)readBE16(payload) * 0.00001f;
            _remoteBattery = payload[7];
            supportedTelemetry = true;
        }
        break;
    case CRSF_FRAME_GPS:
        if(payloadLen >= 15) {
            _gpsSpeed10 = readBE16(payload + 8) / 10;
            _lastGpsSpeed = now;
            supportedTelemetry = true;
        }
        break;
    case CRSF_FRAME_AIRSPEED:
        if(payloadLen >= 2) {
            _airspeed10 = readBE16(payload);
            _lastAirspeed = now;
            supportedTelemetry = true;
        }
        break;
    default:
        break;
    }

    if(supportedTelemetry) {
        _lastTelemetry = now;
    }

    return supportedTelemetry;
}

void ELRSCrsfCore::updateCalibrationButton(ELRSCrsfHost &host, unsigned long now, int battWarn)
{
    bool raw = host.readCalibrationButton();

    if(raw != _calibRaw) {
        _calibRaw = raw;
        _calibDebounceAt = now;
    }

    if(now - _calibDebounceAt < 50) {
        return;
    }

    if(raw != _calibPressed) {
        _calibPressed = raw;
        if(raw) {
            _calibPressedAt = now;
            _calibLongSent = false;
        } else if(!_calibLongSent) {
            handleCalibrationShort(host, now, battWarn);
        }
    }

    if(_calibPressed && !_calibLongSent && (now - _calibPressedAt >= 2000)) {
        _calibLongSent = true;
        handleCalibrationLong(host, now, battWarn);
    }
}

void ELRSCrsfCore::handleCalibrationShort(ELRSCrsfHost &host, unsigned long now, int battWarn)
{
    if(_fakePowerOn || _selfTestActive) {
        return;
    }

    if(battWarn) {
        showOverlay("BAT", now, BATTERY_BANNER_DURATION);
        return;
    }

    if(_calStage == CAL_IDLE) {
        return;
    }

    sampleAxes(host, now, true);

    switch(_calStage) {
    case CAL_CENTER:
        for(int i = 0; i < ELRS_GIMBAL_AXIS_COUNT; i++) {
            _axisCal[i].center = _rawAxes[i];
        }
        _calStage = CAL_TLOW;
        break;
    case CAL_TLOW:
        _axisCal[AXIS_THROTTLE].minimum = _rawAxes[AXIS_THROTTLE];
        _calStage = CAL_THIGH;
        break;
    case CAL_THIGH:
        _axisCal[AXIS_THROTTLE].maximum = _rawAxes[AXIS_THROTTLE];
        _calStage = CAL_YLOW;
        break;
    case CAL_YLOW:
        _axisCal[AXIS_YAW].minimum = _rawAxes[AXIS_YAW];
        _calStage = CAL_YHIGH;
        break;
    case CAL_YHIGH:
        _axisCal[AXIS_YAW].maximum = _rawAxes[AXIS_YAW];
        _calStage = CAL_PLOW;
        break;
    case CAL_PLOW:
        _axisCal[AXIS_PITCH].minimum = _rawAxes[AXIS_PITCH];
        _calStage = CAL_PHIGH;
        break;
    case CAL_PHIGH:
        _axisCal[AXIS_PITCH].maximum = _rawAxes[AXIS_PITCH];
        _calStage = CAL_RLOW;
        break;
    case CAL_RLOW:
        _axisCal[AXIS_ROLL].minimum = _rawAxes[AXIS_ROLL];
        _calStage = CAL_RHIGH;
        break;
    case CAL_RHIGH:
        _axisCal[AXIS_ROLL].maximum = _rawAxes[AXIS_ROLL];
        _calStage = CAL_IDLE;
        host.saveCalibration(_axisCal, ELRS_GIMBAL_AXIS_COUNT);
        showOverlay("CAL", now, 1000);
        break;
    default:
        _calStage = CAL_IDLE;
        break;
    }
}

void ELRSCrsfCore::handleCalibrationLong(ELRSCrsfHost &host, unsigned long now, int battWarn)
{
    if(_fakePowerOn || _selfTestActive) {
        return;
    }

    if(battWarn) {
        showOverlay("BAT", now, BATTERY_BANNER_DURATION);
        return;
    }

    if(_calStage == CAL_IDLE) {
        sampleAxes(host, now, true);
        _calStage = CAL_CENTER;
    } else {
        _calStage = CAL_IDLE;
        showOverlay("CAN", now, 1000);
    }
}

const char *ELRSCrsfCore::getCalibrationPrompt() const
{
    switch(_calStage) {
    case CAL_CENTER: return "CEN";
    case CAL_TLOW:   return "TLO";
    case CAL_THIGH:  return "THI";
    case CAL_YLOW:   return "YLO";
    case CAL_YHIGH:  return "YHI";
    case CAL_PLOW:   return "PLO";
    case CAL_PHIGH:  return "PHI";
    case CAL_RLOW:   return "RLO";
    case CAL_RHIGH:  return "RHI";
    default:         return "";
    }
}

void ELRSCrsfCore::updateChannels(unsigned long now, bool fakePowerOn, bool stopOn, bool buttonAOn, bool buttonBOn, uint8_t packStates)
{
    (void)now;

    if(_selfTestActive) {
        _channels[0] = CRSF_CHANNEL_MID;
        _channels[1] = CRSF_CHANNEL_MID;
        _channels[2] = CRSF_CHANNEL_MIN;
        _channels[3] = CRSF_CHANNEL_MID;
        _channels[4] = CRSF_CHANNEL_MAX;
        for(int i = 5; i < 16; i++) {
            _channels[i] = CRSF_CHANNEL_MIN;
        }
        return;
    }

    if(adcFaultActive(now)) {
        _channels[0] = CRSF_CHANNEL_MID;
        _channels[1] = CRSF_CHANNEL_MID;
        _channels[2] = CRSF_CHANNEL_MIN;
        _channels[3] = CRSF_CHANNEL_MID;
    } else {
        _channels[0] = axisToTicks(AXIS_ROLL);
        _channels[1] = axisToTicks(AXIS_PITCH);
        _channels[2] = axisToTicks(AXIS_THROTTLE);
        _channels[3] = axisToTicks(AXIS_YAW);
    }

    _channels[4] = stopOn ? CRSF_CHANNEL_MAX : CRSF_CHANNEL_MIN;
    _channels[5] = fakePowerOn ? CRSF_CHANNEL_MAX : CRSF_CHANNEL_MIN;
    _channels[6] = buttonAOn ? CRSF_CHANNEL_MAX : CRSF_CHANNEL_MIN;
    _channels[7] = buttonBOn ? CRSF_CHANNEL_MAX : CRSF_CHANNEL_MIN;

    for(int i = 0; i < 8; i++) {
        _channels[8 + i] = (packStates & (1 << i)) ? CRSF_CHANNEL_MAX : CRSF_CHANNEL_MIN;
    }
}

uint16_t ELRSCrsfCore::axisToTicks(uint8_t axis) const
{
    const ELRSAxisCalibrationData &cal = _axisCal[axis];
    int16_t raw = _rawAxes[axis];
    bool onMinSide;

    if(cal.center == cal.minimum || cal.center == cal.maximum || cal.minimum == cal.maximum) {
        return CRSF_CHANNEL_MID;
    }

    onMinSide = (cal.minimum < cal.center) ? (raw <= cal.center) : (raw >= cal.center);

    if(onMinSide) {
        return mapTicks(raw, cal.center, cal.minimum, CRSF_CHANNEL_MID, CRSF_CHANNEL_MIN);
    }

    return mapTicks(raw, cal.center, cal.maximum, CRSF_CHANNEL_MID, CRSF_CHANNEL_MAX);
}

uint16_t ELRSCrsfCore::mapTicks(int16_t raw, int16_t inMin, int16_t inMax, uint16_t outMin, uint16_t outMax) const
{
    long mapped;
    long outLo = (outMin < outMax) ? outMin : outMax;
    long outHi = (outMin > outMax) ? outMin : outMax;

    if(inMin == inMax) {
        return outMin;
    }

    mapped = (long)(raw - inMin) * (long)(outMax - outMin) / (long)(inMax - inMin) + outMin;
    mapped = clampLong(mapped, outLo, outHi);

    return (uint16_t)mapped;
}

void ELRSCrsfCore::applyIdleOutputs(ELRSCrsfHost &host, bool fakePowerOn)
{
    if(_config.usePowerLed) {
        host.setPowerLed(_config.powerLedOnFakePower ? fakePowerOn : !fakePowerOn);
    }

    if(_config.useLevelMeter) {
        host.setLevelMeter(_config.levelMeterOnFakePower ? fakePowerOn : !fakePowerOn);
    }
}

void ELRSCrsfCore::updateBatteryWarning(ELRSCrsfHost &host, unsigned long now, int battWarn, bool fakePowerOn)
{
    if(!battWarn) {
        applyIdleOutputs(host, fakePowerOn);
        return;
    }

    if(_config.useLevelMeter) {
        host.setLevelMeter(false);
    }

    if(_config.usePowerLed) {
        if(now - _batteryBlinkAt >= 1000) {
            _batteryBlinkAt = now;
            host.setPowerLed(!host.getPowerLed());
        }
        return;
    }

    if(now - _batteryBannerAt >= BATTERY_BANNER_INTERVAL) {
        _batteryBannerAt = now;
        showOverlay("BAT", now, BATTERY_BANNER_DURATION);
    }
}

void ELRSCrsfCore::updateInputFaults(ELRSCrsfHost &host, unsigned long now)
{
    bool adcMissing = !_haveAds && (_lastAxisAttemptAt >= _startedAt);
    bool adcStale = _haveAds && (now - _lastGoodAxesAt > CRSF_INPUT_STALE_MS);
    bool adcFault = adcMissing || adcStale;
    bool buttonPackStale = false;

    if(adcMissing) {
        _faultFlags = (uint8_t)((_faultFlags | ELRS_FAULT_ADC_MISSING) & ~ELRS_FAULT_ADC_STALE);
    } else if(adcStale) {
        _faultFlags = (uint8_t)((_faultFlags | ELRS_FAULT_ADC_STALE) & ~ELRS_FAULT_ADC_MISSING);
    } else {
        _faultFlags = (uint8_t)(_faultFlags & ~(ELRS_FAULT_ADC_MISSING | ELRS_FAULT_ADC_STALE));
    }

    if(adcFault != _adcFaultActive) {
        _adcFaultActive = adcFault;
        if(adcFault) {
            log(host, adcMissing ? "ELRS/CRSF: ADC missing" : "ELRS/CRSF: ADC stale");
            showOverlay("ADC", now, 1000);
        } else {
            log(host, "ELRS/CRSF: ADC recovered");
        }
    }

    if(_config.haveButtonPack) {
        buttonPackStale = _hasValidPackState ? (now - _lastGoodPackAt > CRSF_INPUT_STALE_MS)
                                             : (now - _startedAt >= CRSF_INPUT_STALE_MS);
    }

    if(buttonPackStale) {
        _faultFlags |= ELRS_FAULT_BUTTONPACK_STALE;
    } else {
        _faultFlags = (uint8_t)(_faultFlags & ~ELRS_FAULT_BUTTONPACK_STALE);
    }

    if(buttonPackStale != _buttonPackFaultActive) {
        _buttonPackFaultActive = buttonPackStale;
        if(buttonPackStale) {
            log(host, "ELRS/CRSF: button pack stale");
            showOverlay("BPK", now, 1000);
        } else {
            log(host, "ELRS/CRSF: button pack recovered");
        }
    }
}

void ELRSCrsfCore::updateBenchState(ELRSCrsfHost &host, unsigned long now)
{
    const ELRSCrsfTransportStatus &transportStatus = _transport.status();
    SpeedSource speedSource = SPEED_SOURCE_NONE;
    uint8_t commCode = transportStatus.commCode;

    getDisplaySpeed10(now, &speedSource);

    if(commCode != _lastCommCode) {
        if(commCode != ELRS_COMM_NONE) {
            const char *text = commCodeText(commCode);
            unsigned long durationMs = (commCode == ELRS_COMM_NRY) ? CRSF_COMM_NSY_OVERLAY_MS : CRSF_COMM_OVERLAY_MS;
            showCommOverlay(text, now, durationMs);
            logf(host, "ELRS/CRSF: communication state %s", text);
        }
        _lastCommCode = commCode;
    }

    if(speedSource != _activeSpeedSource) {
        _activeSpeedSource = speedSource;
        if(speedSource == SPEED_SOURCE_NONE) {
            if(_lastLinkStats && (now - _lastLinkStats < _config.transport.telemetryTimeoutMs)) {
                log(host, "ELRS/CRSF: no recent speed telemetry, displaying link quality");
            }
        } else {
            logf(host, "ELRS/CRSF: displaying %s speed", speedSourceName(speedSource));
        }
    }
}

void ELRSCrsfCore::updateDisplay(ELRSCrsfHost &host, unsigned long now, int battWarn)
{
    char buf[8];
    SpeedSource speedSource = SPEED_SOURCE_NONE;
    uint16_t speed10 = 0;

    if(_overlayUntil > now) {
        host.displayOn();
        host.displaySetText(_overlayText);
        host.displayShow();
        return;
    }

    if(_calStage != CAL_IDLE) {
        host.displayOn();
        host.displaySetText(getCalibrationPrompt());
        host.displayShow();
        return;
    }

    if(battWarn && !_config.usePowerLed && now - _batteryBannerAt < BATTERY_BANNER_DURATION) {
        host.displayOn();
        host.displaySetText("BAT");
        host.displayShow();
        return;
    }

    if(_commOverlayUntil > now) {
        host.displayOn();
        host.displaySetText(_commOverlayText);
        host.displayShow();
        return;
    }

    if(now - _lastDisplay < CRSF_DISPLAY_INTERVAL) {
        return;
    }
    _lastDisplay = now;

    host.displayOn();
    speed10 = getDisplaySpeed10(now, &speedSource);
    if(speedSource != SPEED_SOURCE_NONE) {
        host.displaySetSpeed((int)speed10);
    } else {
        snprintf(buf, sizeof(buf), "%3d", (_lastLinkStats && (now - _lastLinkStats < _config.transport.telemetryTimeoutMs)) ? _linkQuality : 0);
        host.displaySetText(buf);
    }
    host.displayShow();
}

bool ELRSCrsfCore::hasRecentTelemetry(unsigned long now) const
{
    (void)now;
    return _transport.status().telemetryActive;
}

bool ELRSCrsfCore::adcFaultActive(unsigned long now) const
{
    if(_faultFlags & (ELRS_FAULT_ADC_MISSING | ELRS_FAULT_ADC_STALE)) {
        return true;
    }

    if(!_haveAds) {
        return true;
    }

    return (now - _lastGoodAxesAt > CRSF_INPUT_STALE_MS);
}

bool ELRSCrsfCore::buttonPackFaultActive(unsigned long now) const
{
    if(_faultFlags & ELRS_FAULT_BUTTONPACK_STALE) {
        return true;
    }

    if(!_config.haveButtonPack) {
        return false;
    }

    if(!_hasValidPackState) {
        return (now - _startedAt >= CRSF_INPUT_STALE_MS);
    }

    return (now - _lastGoodPackAt > CRSF_INPUT_STALE_MS);
}

uint16_t ELRSCrsfCore::getDisplaySpeed10(unsigned long now, SpeedSource *source) const
{
    SpeedSource activeSource = SPEED_SOURCE_NONE;
    uint16_t speed10 = 0;

    if(_lastGpsSpeed && (now - _lastGpsSpeed < _config.transport.telemetryTimeoutMs)) {
        activeSource = SPEED_SOURCE_GPS;
        speed10 = _gpsSpeed10;
    } else if(_lastAirspeed && (now - _lastAirspeed < _config.transport.telemetryTimeoutMs)) {
        activeSource = SPEED_SOURCE_AIRSPEED;
        speed10 = _airspeed10;
    }

    if(source) {
        *source = activeSource;
    }

    return speed10;
}

void ELRSCrsfCore::showOverlay(const char *text, unsigned long now, unsigned long durationMs)
{
    if(!text) {
        return;
    }

    memset(_overlayText, 0, sizeof(_overlayText));
    strncpy(_overlayText, text, sizeof(_overlayText) - 1);
    _overlayUntil = now + durationMs;
}

void ELRSCrsfCore::showCommOverlay(const char *text, unsigned long now, unsigned long durationMs)
{
    if(!text) {
        return;
    }

    memset(_commOverlayText, 0, sizeof(_commOverlayText));
    strncpy(_commOverlayText, text, sizeof(_commOverlayText) - 1);
    _commOverlayUntil = now + durationMs;
}

void ELRSCrsfCore::log(ELRSCrsfHost &host, const char *message) const
{
    if(message && *message) {
        host.logMessage(message);
    }
}

void ELRSCrsfCore::logf(ELRSCrsfHost &host, const char *fmt, ...) const
{
    char buffer[192];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    host.logMessage(buffer);
}

uint16_t ELRSCrsfCore::readBE16(const uint8_t *data)
{
    return ((uint16_t)data[0] << 8) | data[1];
}

uint8_t ELRSCrsfCore::crc8D5(const uint8_t *data, size_t len)
{
    return ELRSCrsfTransport::crc8D5(data, len);
}

size_t ELRSCrsfCore::packRcChannelsFrame(const uint16_t channels[16], uint8_t *frame, size_t frameSize)
{
    return ELRSCrsfTransport::packRcChannelsFrame(channels, frame, frameSize);
}

const char *ELRSCrsfCore::speedSourceName(SpeedSource source)
{
    switch(source) {
    case SPEED_SOURCE_GPS:
        return "GPS";
    case SPEED_SOURCE_AIRSPEED:
        return "airspeed";
    default:
        return "none";
    }
}

const char *ELRSCrsfCore::commCodeText(uint8_t code)
{
    switch(code) {
    case ELRS_COMM_NRY:
        return "NRY";
    case ELRS_COMM_RLS:
        return "RLS";
    case ELRS_COMM_CRC:
        return "CRC";
    case ELRS_COMM_FRM:
        return "FRM";
    default:
        return "";
    }
}

unsigned long ELRSCrsfCore::axisSampleIntervalMs(uint16_t packetRateHz)
{
    packetRateHz = elrsPacketRateOrDefault(packetRateHz);
    return (1000UL + packetRateHz - 1) / packetRateHz;
}
