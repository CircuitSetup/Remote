#include "remote_global.h"

#include <Arduino.h>
#include <Wire.h>

#include "elrs_crsf.h"

namespace {

constexpr uint8_t ADS1015_ADDR = 0x48;
constexpr uint8_t ADS_REG_CONVERT = 0x00;
constexpr uint8_t ADS_REG_CONFIG = 0x01;

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
constexpr unsigned long BATTERY_BANNER_INTERVAL = 30000;
constexpr unsigned long BATTERY_BANNER_DURATION = 1000;

constexpr uint8_t AXIS_THROTTLE = 0;
constexpr uint8_t AXIS_YAW = 1;
constexpr uint8_t AXIS_PITCH = 2;
constexpr uint8_t AXIS_ROLL = 3;

}

ELRSCrsfMode elrsMode;

ELRSCrsfMode::ELRSCrsfMode() : _serial(1)
{
}

bool ELRSCrsfMode::begin(
    ButtonPack *buttonPack,
    bool haveButtonPack,
    remDisplay *display,
    remLED *powerLed,
    remLED *levelMeter,
    remLED *stopLed,
    bool usePowerLed,
    bool useLevelMeter,
    bool powerLedOnFakePower,
    bool levelMeterOnFakePower)
{
    _buttonPack = buttonPack;
    _haveButtonPack = haveButtonPack;
    _display = display;
    _powerLed = powerLed;
    _levelMeter = levelMeter;
    _stopLed = stopLed;
    _usePowerLed = usePowerLed;
    _useLevelMeter = useLevelMeter;
    _powerLedOnFakePower = powerLedOnFakePower;
    _levelMeterOnFakePower = levelMeterOnFakePower;
    _startedAt = millis();
    _lastDisplay = 0;
    _lastTx = 0;
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
    _synced = false;
    _fallbackApplied = false;
    _baudRate = 420000;
    _calStage = CAL_IDLE;
    _telemetryActive = false;
    _activeSpeedSource = SPEED_SOURCE_NONE;

    loadELRSCalibration(_axisCal);

    Serial.println("ELRS/CRSF: basic external module mode");
    Serial.println("ELRS/CRSF: ELRS Lua/config menu unsupported");
    Serial.println("ELRS/CRSF: TX=GPIO2 RX=GPIO34 OE=GPIO0");
    Serial.println("ELRS/CRSF: CH1 Roll CH2 Pitch CH3 Throttle CH4 Yaw CH5 Stop CH6 FakePower CH7 O.O CH8 RESET CH9-16 ButtonPack");
    Serial.println("ELRS/CRSF: display GPS speed, then airspeed, then link quality");

    pinMode(FPOWER_IO_PIN, INPUT_PULLUP);
    pinMode(STOPS_IO_PIN, INPUT);
    pinMode(CALIBB_IO_PIN, INPUT_PULLUP);
    pinMode(BUTA_IO_PIN, INPUT_PULLUP);
    pinMode(BUTB_IO_PIN, INPUT_PULLUP);

    digitalWrite(CRSF_OE_PIN, CRSF_OE_DISABLE_LEVEL);
    pinMode(CRSF_OE_PIN, OUTPUT);
    setDriverEnabled(false);

    _haveAds = initAds1015();
    if(!_haveAds) {
        Serial.println("ELRS/CRSF: ADS1015 not found");
    }

    sampleAxes(true);

    for(int i = 0; i < 16; i++) {
        _channels[i] = CRSF_CHANNEL_MID;
    }

    beginSerial(_baudRate);
    _fakePowerOn = fakePowerOn();
    applyIdleOutputs(_fakePowerOn);
    showOverlay("ELR", millis(), 1000);

    return true;
}

void ELRSCrsfMode::loop(int battWarn)
{
    unsigned long now = millis();
    bool fakePower = fakePowerOn();
    bool stopOn = (digitalRead(STOPS_IO_PIN) == HIGH);
    bool buttonAOn = (digitalRead(BUTA_IO_PIN) == LOW);
    bool buttonBOn = (digitalRead(BUTB_IO_PIN) == LOW);
    uint8_t packStates = (_haveButtonPack && _buttonPack) ? _buttonPack->readStates() : 0;

    _fakePowerOn = fakePower;

    if(_stopLed) {
        _stopLed->setState(stopOn);
    }

    sampleAxes();
    updateCalibrationButton(now, fakePower, battWarn);
    updateChannels(fakePower, stopOn, buttonAOn, buttonBOn, packStates);
    pollTelemetry(now);

    if(!_synced && !_fallbackApplied && (now - _startedAt > CRSF_BAUD_FALLBACK_MS)) {
        _fallbackApplied = true;
        Serial.println("ELRS/CRSF: no telemetry sync at 420000, falling back to 400000");
        beginSerial(400000);
    }

    if(now - _lastTx >= CRSF_SEND_INTERVAL) {
        sendChannels(now);
    }

    updateBatteryWarning(now, battWarn, fakePower);
    updateBenchState(now);
    updateDisplay(now, battWarn);
}

bool ELRSCrsfMode::isCalibrating() const
{
    return (_calStage != CAL_IDLE);
}

bool ELRSCrsfMode::fakePowerOn() const
{
    return (digitalRead(FPOWER_IO_PIN) == LOW);
}

bool ELRSCrsfMode::initAds1015()
{
    Wire.beginTransmission(ADS1015_ADDR);
    return (Wire.endTransmission(true) == 0);
}

int16_t ELRSCrsfMode::readAdsChannel(uint8_t channel)
{
    static const uint8_t muxBits[ELRS_GIMBAL_AXIS_COUNT] = { 0x04, 0x05, 0x06, 0x07 };
    uint8_t cfg[3];
    uint8_t raw[2];
    int value = 0;

    if(channel >= ELRS_GIMBAL_AXIS_COUNT) {
        return 1024;
    }

    cfg[0] = ADS_REG_CONFIG;
    cfg[1] = 0x80 | (muxBits[channel] << 4) | 0x04 | 0x01;
    cfg[2] = 0xE3;

    Wire.beginTransmission(ADS1015_ADDR);
    Wire.write(cfg[0]);
    Wire.write(cfg[1]);
    Wire.write(cfg[2]);
    if(Wire.endTransmission(true)) {
        return _rawAxes[channel];
    }

    delayMicroseconds(500);

    Wire.beginTransmission(ADS1015_ADDR);
    Wire.write(ADS_REG_CONVERT);
    if(Wire.endTransmission(false)) {
        return _rawAxes[channel];
    }

    if(Wire.requestFrom((uint8_t)ADS1015_ADDR, (uint8_t)2) != 2) {
        return _rawAxes[channel];
    }

    raw[0] = Wire.read();
    raw[1] = Wire.read();

    value = ((int16_t)((raw[0] << 8) | raw[1])) >> 4;
    if(value < 0) {
        value = 0;
    }

    return (int16_t)value;
}

void ELRSCrsfMode::sampleAxes(bool force)
{
    unsigned long now = millis();

    if(!_haveAds) {
        return;
    }

    if(!force && (now - _lastAxisSample < CRSF_SEND_INTERVAL)) {
        return;
    }

    _lastAxisSample = now;
    for(int i = 0; i < ELRS_GIMBAL_AXIS_COUNT; i++) {
        _rawAxes[i] = readAdsChannel(i);
    }
}

void ELRSCrsfMode::updateCalibrationButton(unsigned long now, bool fakePower, int battWarn)
{
    bool raw = (digitalRead(CALIBB_IO_PIN) == LOW);

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
            handleCalibrationShort(now, fakePower, battWarn);
        }
    }

    if(_calibPressed && !_calibLongSent && (now - _calibPressedAt >= 2000)) {
        _calibLongSent = true;
        handleCalibrationLong(now, fakePower, battWarn);
    }
}

void ELRSCrsfMode::handleCalibrationShort(unsigned long now, bool fakePower, int battWarn)
{
    if(fakePower) {
        return;
    }

    if(battWarn) {
        showOverlay("BAT", now, BATTERY_BANNER_DURATION);
        return;
    }

    if(_calStage == CAL_IDLE) {
        return;
    }

    sampleAxes(true);

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
        saveELRSCalibration(_axisCal);
        showOverlay("CAL", now, 1000);
        break;
    default:
        _calStage = CAL_IDLE;
        break;
    }
}

void ELRSCrsfMode::handleCalibrationLong(unsigned long now, bool fakePower, int battWarn)
{
    if(fakePower) {
        return;
    }

    if(battWarn) {
        showOverlay("BAT", now, BATTERY_BANNER_DURATION);
        return;
    }

    if(_calStage == CAL_IDLE) {
        sampleAxes(true);
        _calStage = CAL_CENTER;
    } else {
        _calStage = CAL_IDLE;
        showOverlay("CAN", now, 1000);
    }
}

const char *ELRSCrsfMode::getCalibrationPrompt() const
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

void ELRSCrsfMode::updateChannels(bool fakePower, bool stopOn, bool buttonAOn, bool buttonBOn, uint8_t packStates)
{
    _channels[0] = axisToTicks(AXIS_ROLL);
    _channels[1] = axisToTicks(AXIS_PITCH);
    _channels[2] = axisToTicks(AXIS_THROTTLE);
    _channels[3] = axisToTicks(AXIS_YAW);

    _channels[4] = stopOn ? CRSF_CHANNEL_MAX : CRSF_CHANNEL_MIN;
    _channels[5] = fakePower ? CRSF_CHANNEL_MAX : CRSF_CHANNEL_MIN;
    _channels[6] = buttonAOn ? CRSF_CHANNEL_MAX : CRSF_CHANNEL_MIN;
    _channels[7] = buttonBOn ? CRSF_CHANNEL_MAX : CRSF_CHANNEL_MIN;

    for(int i = 0; i < 8; i++) {
        _channels[8 + i] = (packStates & (1 << i)) ? CRSF_CHANNEL_MAX : CRSF_CHANNEL_MIN;
    }
}

uint16_t ELRSCrsfMode::axisToTicks(uint8_t axis) const
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

uint16_t ELRSCrsfMode::mapTicks(int16_t raw, int16_t inMin, int16_t inMax, uint16_t outMin, uint16_t outMax) const
{
    long mapped;
    long outLo = min((long)outMin, (long)outMax);
    long outHi = max((long)outMin, (long)outMax);

    if(inMin == inMax) {
        return outMin;
    }

    mapped = (long)(raw - inMin) * (long)(outMax - outMin) / (long)(inMax - inMin) + outMin;
    mapped = constrain(mapped, outLo, outHi);

    return (uint16_t)mapped;
}

void ELRSCrsfMode::beginSerial(uint32_t baud)
{
    _serial.end();
    _baudRate = baud;
    _serial.begin((unsigned long)baud, SERIAL_8N1, CRSF_RX_PIN, CRSF_TX_PIN);
    setDriverEnabled(false);
    Serial.printf("ELRS/CRSF: UART at %lu baud\n", (unsigned long)_baudRate);
}

void ELRSCrsfMode::setDriverEnabled(bool enabled)
{
    digitalWrite(CRSF_OE_PIN, enabled ? CRSF_OE_ENABLE_LEVEL : CRSF_OE_DISABLE_LEVEL);
}

void ELRSCrsfMode::sendChannels(unsigned long now)
{
    uint8_t frame[26];
    uint8_t payload[22];
    uint32_t bitbuf = 0;
    uint8_t bits = 0;
    uint8_t idx = 0;

    memset(payload, 0, sizeof(payload));

    for(int i = 0; i < 16; i++) {
        bitbuf |= ((uint32_t)(_channels[i] & 0x07ff)) << bits;
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

    setDriverEnabled(true);
    _serial.write(frame, sizeof(frame));
    _serial.flush();
    setDriverEnabled(false);

    _lastTx = now;
}

void ELRSCrsfMode::pollTelemetry(unsigned long now)
{
    static uint8_t frame[CRSF_MAX_FRAME];
    static size_t frameLen = 0;

    while(_serial.available()) {
        int ch = _serial.read();
        if(ch < 0) {
            break;
        }

        if(frameLen >= sizeof(frame)) {
            frameLen = 0;
        }

        frame[frameLen++] = (uint8_t)ch;

        if(frameLen == 2) {
            if(frame[1] < 2 || frame[1] > 62) {
                frame[0] = frame[1];
                frameLen = 1;
            }
            continue;
        }

        if(frameLen >= 2) {
            size_t expectLen = frame[1] + 2;
            if(expectLen > sizeof(frame)) {
                frameLen = 0;
                continue;
            }
            if(frameLen == expectLen) {
                if(crc8D5(frame + 2, expectLen - 3) == frame[expectLen - 1]) {
                    handleFrame(frame, expectLen, now);
                }
                frameLen = 0;
            }
        }
    }
}

void ELRSCrsfMode::handleFrame(const uint8_t *frame, size_t frameSize, unsigned long now)
{
    uint8_t type = frame[2];
    const uint8_t *payload = frame + 3;
    size_t payloadLen = frameSize - 4;

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

void ELRSCrsfMode::applyIdleOutputs(bool fakePower)
{
    if(_usePowerLed && _powerLed) {
        _powerLed->setState(_powerLedOnFakePower ? fakePower : !fakePower);
    }

    if(_useLevelMeter && _levelMeter) {
        _levelMeter->setState(_levelMeterOnFakePower ? fakePower : !fakePower);
    }
}

void ELRSCrsfMode::updateBatteryWarning(unsigned long now, int battWarn, bool fakePower)
{
    if(!battWarn) {
        applyIdleOutputs(fakePower);
        return;
    }

    if(_useLevelMeter && _levelMeter) {
        _levelMeter->setState(false);
    }

    if(_usePowerLed && _powerLed) {
        if(now - _batteryBlinkAt >= 1000) {
            _batteryBlinkAt = now;
            _powerLed->setState(!_powerLed->getState());
        }
        return;
    }

    if(now - _batteryBannerAt >= BATTERY_BANNER_INTERVAL) {
        _batteryBannerAt = now;
        showOverlay("BAT", now, BATTERY_BANNER_DURATION);
    }
}

void ELRSCrsfMode::updateBenchState(unsigned long now)
{
    bool telemetryActive = hasRecentTelemetry(now);
    SpeedSource speedSource = SPEED_SOURCE_NONE;

    getDisplaySpeed10(now, &speedSource);

    if(telemetryActive != _telemetryActive) {
        _telemetryActive = telemetryActive;
        if(telemetryActive) {
            Serial.printf("ELRS/CRSF: telemetry sync at %lu baud\n", (unsigned long)_baudRate);
        } else {
            Serial.println("ELRS/CRSF: telemetry timeout, still transmitting RC");
        }
    }

    if(speedSource != _activeSpeedSource) {
        _activeSpeedSource = speedSource;
        if(speedSource == SPEED_SOURCE_NONE) {
            if(_lastLinkStats && (now - _lastLinkStats < CRSF_TELEMETRY_TIMEOUT)) {
                Serial.println("ELRS/CRSF: no recent speed telemetry, displaying link quality");
            }
        } else {
            Serial.printf("ELRS/CRSF: displaying %s speed\n", speedSourceName(speedSource));
        }
    }
}

void ELRSCrsfMode::updateDisplay(unsigned long now, int battWarn)
{
    char buf[8];
    SpeedSource speedSource = SPEED_SOURCE_NONE;
    uint16_t speed10 = 0;

    if(!_display) {
        return;
    }

    if(_overlayUntil > now) {
        _display->on();
        _display->setText(_overlayText);
        _display->show();
        return;
    }

    if(_calStage != CAL_IDLE) {
        _display->on();
        _display->setText(getCalibrationPrompt());
        _display->show();
        return;
    }

    if(battWarn && !_usePowerLed && now - _batteryBannerAt < BATTERY_BANNER_DURATION) {
        _display->on();
        _display->setText("BAT");
        _display->show();
        return;
    }

    if(now - _lastDisplay < CRSF_DISPLAY_INTERVAL) {
        return;
    }
    _lastDisplay = now;

    _display->on();
    speed10 = getDisplaySpeed10(now, &speedSource);
    if(speedSource != SPEED_SOURCE_NONE) {
        _display->setSpeed((int)speed10);
    } else {
        snprintf(buf, sizeof(buf), "%3d", (_lastLinkStats && (now - _lastLinkStats < CRSF_TELEMETRY_TIMEOUT)) ? _linkQuality : 0);
        _display->setText(buf);
    }
    _display->show();
}

bool ELRSCrsfMode::hasRecentTelemetry(unsigned long now) const
{
    return (_lastTelemetry && (now - _lastTelemetry < CRSF_TELEMETRY_TIMEOUT));
}

uint16_t ELRSCrsfMode::getDisplaySpeed10(unsigned long now, SpeedSource *source) const
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

void ELRSCrsfMode::showOverlay(const char *text, unsigned long now, unsigned long durationMs)
{
    if(!text) {
        return;
    }

    memset(_overlayText, 0, sizeof(_overlayText));
    strncpy(_overlayText, text, sizeof(_overlayText) - 1);
    _overlayUntil = now + durationMs;
}

uint16_t ELRSCrsfMode::readBE16(const uint8_t *data)
{
    return ((uint16_t)data[0] << 8) | data[1];
}

uint8_t ELRSCrsfMode::crc8D5(const uint8_t *data, size_t len)
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

const char *ELRSCrsfMode::speedSourceName(SpeedSource source)
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
