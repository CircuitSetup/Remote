#include "elrs_crsf_core.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint16_t CRSF_CHANNEL_MIN = 172;
constexpr uint16_t CRSF_CHANNEL_MID = 992;
constexpr uint16_t CRSF_CHANNEL_MAX = 1811;

constexpr uint8_t CRSF_FRAME_RC_CHANNELS_PACKED = 0x16;
constexpr uint8_t CRSF_FRAME_GPS = 0x02;
constexpr uint8_t CRSF_FRAME_BATTERY = 0x08;
constexpr uint8_t CRSF_FRAME_AIRSPEED = 0x0A;
constexpr uint8_t CRSF_FRAME_LINK_STATS = 0x14;

constexpr uint8_t CRSF_SYNC_BYTE = 0xC8;
constexpr size_t CRSF_MAX_FRAME = 64;
constexpr unsigned long CRSF_SEND_INTERVAL = 10;
constexpr unsigned long CRSF_DISPLAY_INTERVAL = 200;
constexpr unsigned long CRSF_TELEMETRY_TIMEOUT = 2000;
constexpr unsigned long CRSF_BAUD_FALLBACK_MS = 3000;
constexpr unsigned long CRSF_INPUT_STALE_MS = 100;
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

    memset(_rxFrame, 0, sizeof(_rxFrame));
    memset(_overlayText, 0, sizeof(_overlayText));
}

bool ELRSCrsfCore::begin(ELRSCrsfHost &host, const ELRSCrsfCoreConfig &config, unsigned long now)
{
    _config = config;
    _startedAt = now;
    _lastAxisAttemptAt = 0;
    _lastGoodAxesAt = 0;
    _lastGoodPackAt = 0;
    _lastTx = 0;
    _lastDisplay = 0;
    _lastTelemetry = 0;
    _lastLinkStats = 0;
    _lastGpsSpeed = 0;
    _lastAirspeed = 0;
    _batteryBlinkAt = 0;
    _batteryBannerAt = 0;
    _overlayUntil = 0;
    _linkQuality = 0;
    _gpsSpeed10 = 0;
    _airspeed10 = 0;
    _remoteBattery = 0;
    _remoteBatteryVoltage = 0.0f;
    _faultFlags = ELRS_FAULT_NONE;
    _haveAds = false;
    _synced = false;
    _fallbackApplied = false;
    _baudRate = 420000;
    _calStage = CAL_IDLE;
    _telemetryActive = false;
    _activeSpeedSource = SPEED_SOURCE_NONE;
    _hasValidPackState = false;
    _lastPackStates = 0;
    _adcFaultActive = false;
    _buttonPackFaultActive = false;
    _calibRaw = false;
    _calibPressed = false;
    _calibLongSent = false;
    _calibDebounceAt = 0;
    _calibPressedAt = 0;
    _rxFrameLen = 0;
    memset(_overlayText, 0, sizeof(_overlayText));

    host.loadCalibration(_axisCal, ELRS_GIMBAL_AXIS_COUNT);

    log(host, "ELRS/CRSF: basic external module mode");
    log(host, "ELRS/CRSF: ELRS Lua/config menu unsupported");
    log(host, "ELRS/CRSF: TX=GPIO2 RX=GPIO34 OE=GPIO0");
    log(host, "ELRS/CRSF: CH1 Roll CH2 Pitch CH3 Throttle CH4 Yaw CH5 Stop CH6 FakePower CH7 O.O CH8 RESET CH9-16 ButtonPack");
    log(host, "ELRS/CRSF: display GPS speed, then airspeed, then link quality");

    sampleAxes(host, now, true);

    for(int i = 0; i < 16; i++) {
        _channels[i] = CRSF_CHANNEL_MID;
    }

    beginSerial(host, _baudRate);
    _fakePowerOn = host.readFakePowerSwitch();
    applyIdleOutputs(host, _fakePowerOn);
    host.setStopLed(false);
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
    bool fakePower = host.readFakePowerSwitch();
    bool stopOn = host.readStopSwitch();
    bool buttonAOn = host.readButtonA();
    bool buttonBOn = host.readButtonB();
    uint8_t packStates = samplePackStates(host, now);

    _fakePowerOn = fakePower;
    host.setStopLed(stopOn);

    sampleAxes(host, now);
    updateInputFaults(host, now);
    updateCalibrationButton(host, now, battWarn);
    updateChannels(now, fakePower, stopOn, buttonAOn, buttonBOn, packStates);
    pollTelemetry(host, now);

    if(!_synced && !_fallbackApplied && (now - _startedAt > CRSF_BAUD_FALLBACK_MS)) {
        _fallbackApplied = true;
        _faultFlags |= ELRS_FAULT_FALLBACK_BAUD;
        log(host, "ELRS/CRSF: no telemetry sync at 420000, falling back to 400000");
        beginSerial(host, 400000);
        host.discardSerialInput();
    }

    if(now - _lastTx >= CRSF_SEND_INTERVAL) {
        sendChannels(host, now);
    }

    updateBatteryWarning(host, now, battWarn, fakePower);
    updateBenchState(host, now);
    updateDisplay(host, now, battWarn);
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
    return _baudRate;
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
    return _telemetryActive;
}

bool ELRSCrsfCore::synced() const
{
    return _synced;
}

ELRSCrsfCore::SpeedSource ELRSCrsfCore::activeSpeedSource() const
{
    return _activeSpeedSource;
}

ELRSCrsfStatus ELRSCrsfCore::getStatus() const
{
    ELRSCrsfStatus status;

    status.baudRate = _baudRate;
    status.telemetryActive = _telemetryActive;
    status.synced = _synced;
    status.activeSpeedSource = (uint8_t)_activeSpeedSource;
    status.linkQuality = _linkQuality;
    status.remoteBatteryPercent = _remoteBattery;
    status.remoteBatteryVoltage = _remoteBatteryVoltage;
    status.faultFlags = _faultFlags;
    status.fakePowerOn = _fakePowerOn;
    status.calibrating = (_calStage != CAL_IDLE);

    return status;
}

bool ELRSCrsfCore::sampleAxes(ELRSCrsfHost &host, unsigned long now, bool force)
{
    int16_t axes[ELRS_GIMBAL_AXIS_COUNT];

    if(!force && (now - _lastAxisAttemptAt < CRSF_SEND_INTERVAL)) {
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
    if(_fakePowerOn) {
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
    if(_fakePowerOn) {
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

void ELRSCrsfCore::beginSerial(ELRSCrsfHost &host, uint32_t baud)
{
    _baudRate = baud;
    host.startSerial(_baudRate);
    host.setDriverEnabled(false);
    logf(host, "ELRS/CRSF: UART at %lu baud", (unsigned long)_baudRate);
}

void ELRSCrsfCore::sendChannels(ELRSCrsfHost &host, unsigned long now)
{
    uint8_t frame[26];
    size_t frameLen = packRcChannelsFrame(_channels, frame, sizeof(frame));

    if(!frameLen) {
        return;
    }

    host.setDriverEnabled(true);
    host.serialWrite(frame, frameLen);
    host.serialFlush();
    host.setDriverEnabled(false);

    _lastTx = now;
}

void ELRSCrsfCore::pollTelemetry(ELRSCrsfHost &host, unsigned long now)
{
    while(host.serialAvailable()) {
        int ch = host.serialRead();
        if(ch < 0) {
            break;
        }

        if(_rxFrameLen == 0) {
            if((uint8_t)ch != CRSF_SYNC_BYTE) {
                continue;
            }
            _rxFrame[_rxFrameLen++] = (uint8_t)ch;
            continue;
        }

        if(_rxFrameLen >= sizeof(_rxFrame)) {
            _rxFrameLen = 0;
            if((uint8_t)ch != CRSF_SYNC_BYTE) {
                continue;
            }
        }

        _rxFrame[_rxFrameLen++] = (uint8_t)ch;

        if(_rxFrameLen == 2) {
            if(_rxFrame[1] < 2 || _rxFrame[1] > 62) {
                resyncRxBuffer();
            }
            continue;
        }

        if(_rxFrameLen >= 2) {
            size_t expectLen = _rxFrame[1] + 2;
            if(expectLen > sizeof(_rxFrame)) {
                resyncRxBuffer();
                continue;
            }
            if(_rxFrameLen == expectLen) {
                if(crc8D5(_rxFrame + 2, expectLen - 3) == _rxFrame[expectLen - 1]) {
                    handleFrame(host, _rxFrame, expectLen, now);
                    _rxFrameLen = 0;
                } else {
                    resyncRxBuffer();
                }
            }
        }
    }
}

void ELRSCrsfCore::resyncRxBuffer(size_t startIndex)
{
    size_t syncIndex = startIndex;

    while(syncIndex < _rxFrameLen) {
        if(_rxFrame[syncIndex] == CRSF_SYNC_BYTE) {
            break;
        }
        syncIndex++;
    }

    if(syncIndex >= _rxFrameLen) {
        _rxFrameLen = 0;
        return;
    }

    memmove(_rxFrame, _rxFrame + syncIndex, _rxFrameLen - syncIndex);
    _rxFrameLen -= syncIndex;
}

void ELRSCrsfCore::handleFrame(ELRSCrsfHost &host, const uint8_t *frame, size_t frameSize, unsigned long now)
{
    uint8_t type = frame[2];
    const uint8_t *payload = frame + 3;
    size_t payloadLen = frameSize - 4;

    (void)host;
    _synced = true;
    _lastTelemetry = now;

    switch(type) {
    case CRSF_FRAME_LINK_STATS:
        if(payloadLen >= 10) {
            _linkQuality = payload[2];
            _lastLinkStats = now;
        }
        break;
    case CRSF_FRAME_BATTERY:
        if(payloadLen >= 8) {
            _remoteBatteryVoltage = (float)readBE16(payload) * 0.00001f;
            _remoteBattery = payload[7];
        }
        break;
    case CRSF_FRAME_GPS:
        if(payloadLen >= 15) {
            _gpsSpeed10 = readBE16(payload + 8) / 10;
            _lastGpsSpeed = now;
        }
        break;
    case CRSF_FRAME_AIRSPEED:
        if(payloadLen >= 2) {
            _airspeed10 = readBE16(payload);
            _lastAirspeed = now;
        }
        break;
    default:
        break;
    }
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
    bool telemetryActive = hasRecentTelemetry(now);
    SpeedSource speedSource = SPEED_SOURCE_NONE;

    getDisplaySpeed10(now, &speedSource);

    if(telemetryActive != _telemetryActive) {
        _telemetryActive = telemetryActive;
        if(telemetryActive) {
            logf(host, "ELRS/CRSF: telemetry sync at %lu baud", (unsigned long)_baudRate);
        } else {
            log(host, "ELRS/CRSF: telemetry timeout, still transmitting RC");
        }
    }

    if(speedSource != _activeSpeedSource) {
        _activeSpeedSource = speedSource;
        if(speedSource == SPEED_SOURCE_NONE) {
            if(_lastLinkStats && (now - _lastLinkStats < CRSF_TELEMETRY_TIMEOUT)) {
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

    if(now - _lastDisplay < CRSF_DISPLAY_INTERVAL) {
        return;
    }
    _lastDisplay = now;

    host.displayOn();
    speed10 = getDisplaySpeed10(now, &speedSource);
    if(speedSource != SPEED_SOURCE_NONE) {
        host.displaySetSpeed((int)speed10);
    } else {
        snprintf(buf, sizeof(buf), "%3d", (_lastLinkStats && (now - _lastLinkStats < CRSF_TELEMETRY_TIMEOUT)) ? _linkQuality : 0);
        host.displaySetText(buf);
    }
    host.displayShow();
}

bool ELRSCrsfCore::hasRecentTelemetry(unsigned long now) const
{
    return (_lastTelemetry && (now - _lastTelemetry < CRSF_TELEMETRY_TIMEOUT));
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

    if(_lastGpsSpeed && (now - _lastGpsSpeed < CRSF_TELEMETRY_TIMEOUT)) {
        activeSource = SPEED_SOURCE_GPS;
        speed10 = _gpsSpeed10;
    } else if(_lastAirspeed && (now - _lastAirspeed < CRSF_TELEMETRY_TIMEOUT)) {
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

void ELRSCrsfCore::log(ELRSCrsfHost &host, const char *message) const
{
    if(message && *message) {
        host.logMessage(message);
    }
}

void ELRSCrsfCore::logf(ELRSCrsfHost &host, const char *fmt, ...) const
{
    char buffer[160];
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
    uint8_t crc = 0;

    while(len--) {
        crc ^= *data++;
        for(uint8_t i = 0; i < 8; i++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0xD5) : (uint8_t)(crc << 1);
        }
    }

    return crc;
}

size_t ELRSCrsfCore::packRcChannelsFrame(const uint16_t channels[16], uint8_t *frame, size_t frameSize)
{
    uint8_t payload[22];
    uint32_t bitbuf = 0;
    uint8_t bits = 0;
    uint8_t idx = 0;

    if(frameSize < 26) {
        return 0;
    }

    memset(payload, 0, sizeof(payload));

    for(int i = 0; i < 16; i++) {
        bitbuf |= ((uint32_t)(channels[i] & 0x07ff)) << bits;
        bits += 11;
        while(bits >= 8) {
            payload[idx++] = bitbuf & 0xff;
            bitbuf >>= 8;
            bits -= 8;
        }
    }
    if(idx < sizeof(payload)) {
        payload[idx++] = bitbuf & 0xff;
    }

    frame[0] = CRSF_SYNC_BYTE;
    frame[1] = 24;
    frame[2] = CRSF_FRAME_RC_CHANNELS_PACKED;
    memcpy(frame + 3, payload, sizeof(payload));
    frame[25] = crc8D5(frame + 2, 23);

    return 26;
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
