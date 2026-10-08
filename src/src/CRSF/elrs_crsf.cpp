#include "../../remote_global.h"

#ifdef HAVE_CRSF

#include <Arduino.h>
#include <Wire.h>
#ifdef REMOTE_DBG
#include <WiFi.h>
#endif

#include "elrs_crsf.h"
#include "crsf_kludge.h"
#include "crsf_settings.h"

namespace {

constexpr uint8_t CRSF_RX_PIN = 34;
constexpr uint8_t CRSF_TX_PIN = 2;
constexpr uint8_t CRSF_OE_PIN = 0;
constexpr uint8_t ADS1015_ADDR = 0x48;
constexpr uint8_t ADS_REG_CONVERT = 0x00;
constexpr uint8_t ADS_REG_CONFIG = 0x01;
#ifdef REMOTE_DBG
constexpr int16_t ADS_LOG_DELTA_THRESHOLD = 20;
constexpr uint32_t ADS_LOG_INTERVAL_MS = 200;
#endif
constexpr uint8_t ADS_FILTER_SHIFT = 2;
constexpr uint32_t ADS_CONVERSION_US = 500;
constexpr uint32_t ADS_RETRY_MS = 20;
// At 400kHz a config write takes 90us; a register read takes 113us.
// Reserve overhead as well and do at most one of these per service pass.
constexpr uint32_t ADS_SERVICE_BUDGET_US = 200;

}

ELRSCrsfMode elrsMode;

ELRSCrsfMode::ELRSCrsfMode() : _serial(1)
{
}

bool ELRSCrsfMode::begin(
    uint16_t packetRateHz,
    uint8_t speedDisplayUnits,
    uint8_t telemetryRatio,
    uint8_t maxPower,
    uint8_t dynamicPower,
    const ELRSInputAxisProfile *axisProfiles,
    const ELRSGimbalRouting &inputRouting,
    ButtonPack *buttonPack,
    bool haveButtonPack,
    remDisplay *display,
    remLED *powerLed,
    remLED *levelMeter,
    remLED *stopLed,
    bool usePowerLed,
    bool useLevelMeter,
    bool powerLedOnFakePower,
    bool levelMeterOnFakePower,
    void (*fpOnWifiHandler)(bool),
    uint16_t adcHysteresis,
    uint16_t throttleIdleDeadband,
    const ELRSSwitchRouting *switchRouting,
    const ELRSOutputLimits *outputLimits,
    const uint8_t *localActions,
    bool propControls,
    const ELRSDisplayConfig *displayConfig,
    const ELRSVehicleConfig *vehicleConfig)
{
    ELRSCrsfCoreConfig config;
    config.propControls = propControls;
    if(displayConfig) config.displayConfig = *displayConfig;
    if(vehicleConfig) config.vehicleConfig = *vehicleConfig;
    _speedDisplayUnits = elrsSpeedUnitsOrDefault(speedDisplayUnits);

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
    _haveAds = false;
    _oeActiveLow = true;
    #ifdef REMOTE_DBG
    _haveLoggedAxes = false;
    _lastAxesLogAt = 0;
    _lastProbeLogAt = (uint32_t)millis() - ADS_LOG_INTERVAL_MS;
    #endif
    _haveFilteredAxes = false;
    _adsState = ADS_IDLE;
    _adsChannel = 0;
    _adsReadyAtUs = 0;
    _adsRetryPending = false;
    _lastAdsProbeAt = 0;
    memset(_stagingAxes, 0, sizeof(_stagingAxes));
    for(int i = 0; i < ELRS_GIMBAL_AXIS_COUNT; i++) {
        _rawAxes[i] = 1024;
        _filteredAxes[i] = 1024;
        #ifdef REMOTE_DBG
        _lastLoggedAxes[i] = 1024;
        #endif
    }

    _fpOnWifiHandler = fpOnWifiHandler;

    pinMode(FPOWER_IO_PIN, INPUT_PULLUP);
    pinMode(STOPS_IO_PIN, INPUT);
    pinMode(CALIBB_IO_PIN, INPUT_PULLUP);
    pinMode(BUTA_IO_PIN, INPUT_PULLUP);
    pinMode(BUTB_IO_PIN, INPUT_PULLUP);

    digitalWrite(CRSF_OE_PIN, _oeActiveLow ? HIGH : LOW);
    pinMode(CRSF_OE_PIN, OUTPUT);
    setDriverEnabled(false);

    _haveAds = initAds1015();

    config.haveButtonPack = _haveButtonPack;
    config.usePowerLed = _usePowerLed;
    config.useLevelMeter = _useLevelMeter;
    config.powerLedOnFakePower = _powerLedOnFakePower;
    config.levelMeterOnFakePower = _levelMeterOnFakePower;
    config.speedDisplayUnits = elrsSpeedUnitsOrDefault(speedDisplayUnits);
    config.telemetryRatio = elrsTelemetryRatioOrDefault(telemetryRatio);
    config.maxPower = elrsMaxPowerOrDefault(maxPower);
    config.dynamicPower = elrsDynamicPowerOrDefault(dynamicPower);
    config.adcHysteresis = adcHysteresis;
    config.throttleIdleDeadband = throttleIdleDeadband;
    if(switchRouting) config.switchRouting = *switchRouting;
    if(localActions) memcpy(config.localActions, localActions, sizeof(config.localActions));
    for(int i = 0; i < ELRS_GIMBAL_AXIS_COUNT; i++) {
        if(axisProfiles) {
            config.axisProfiles[i] = elrsSanitizeInputAxisProfile(axisProfiles[i]);
        } else {
            config.axisProfiles[i] = elrsDefaultInputAxisProfile();
        }
        config.outputLimits[i] = outputLimits ? outputLimits[i] : elrsDefaultOutputLimits();
    }
    config.inputRouting = inputRouting;
    config.transport.baudRate = elrsCrsfRecommendedBaudRate(packetRateHz);
    config.transport.invertLine = false;
    config.transport.packetRateHz = packetRateHz;
    config.transport.frameIntervalMs = (uint16_t)((1000UL + elrsPacketRateOrDefault(config.transport.packetRateHz) - 1) /
                                                  elrsPacketRateOrDefault(config.transport.packetRateHz));
    config.transport.telemetryTimeoutMs = 2000;
    config.transport.replyTimeoutMs = elrsCrsfModuleReplyTimeoutMs(packetRateHz);
    #ifdef REMOTE_DBG
    config.transport.debugEnabled = true;
    #else
    config.transport.debugEnabled = false;
    #endif
    #ifdef REMOTE_DBG_CRSF_RAW
    config.transport.rawFrameDebugEnabled = true;
    #else
    config.transport.rawFrameDebugEnabled = false;
    #endif
    config.transport.oeActiveLow = _oeActiveLow;

    #ifdef REMOTE_DBG
    Serial.printf("ELRS/CRSF: WiFi mode=%u status=%u STA=%s AP=%s\n",
                  (unsigned)WiFi.getMode(), (unsigned)WiFi.status(),
                  WiFi.localIP().toString().c_str(), WiFi.softAPIP().toString().c_str());
    #endif
    return _core.begin(*this, config, millis(), micros());
}

void ELRSCrsfMode::loop(int battWarn)
{
    _core.loop(*this, millis(), micros(), battWarn);
}

void ELRSCrsfMode::scanLocalSwitches(uint16_t states, uint16_t validMask)
{
    queueCRSFLocalSwitches(states, validMask);
}

bool ELRSCrsfMode::isCalibrating() const
{
    return _core.isCalibrating();
}

bool ELRSCrsfMode::fakePowerOn() const
{
    return _core.fakePowerOn();
}

ELRSCrsfStatus ELRSCrsfMode::getStatus() const
{
    return _core.getStatus();
}

ELRSTelemetrySample ELRSCrsfMode::telemetrySample(uint8_t source, uint32_t now, const ELRSVehicleConfig *vehicleConfig) const
{
    return _core.telemetrySample(source, now, vehicleConfig);
}

bool ELRSCrsfMode::telemetryDisplayAssigned() const { return _core.telemetryDisplayAssigned(); }
void ELRSCrsfMode::renderAssignedDisplay(uint32_t now, int battWarn) { _core.renderAssignedDisplay(*this, now, battWarn); }
uint8_t ELRSCrsfMode::speedDisplayUnits() const { return _speedDisplayUnits; }

void ELRSCrsfMode::requestModuleConfigUpdate(uint8_t telemetryRatio, uint8_t maxPower, uint8_t dynamicPower)
{
    _core.requestModuleConfigUpdate(telemetryRatio, maxPower, dynamicPower, millis());
}

bool ELRSCrsfMode::readCurrentRawAxes(int16_t axes[ELRS_GIMBAL_AXIS_COUNT])
{
    if(!axes) {
        return false;
    }

    if(!_haveAds || !_haveFilteredAxes) {
        return false;
    }

    // Share the control loop's filtering and jitter hold with portal captures.
    return _core.readFilteredAxes(axes);
}

bool ELRSCrsfMode::initAds1015()
{
    Wire.beginTransmission(ADS1015_ADDR);
    bool ok = (Wire.endTransmission(true) == 0);
    _lastAdsProbeAt = (uint32_t)millis();
    _adsRetryPending = !ok;
    #ifdef REMOTE_DBG
    const uint32_t now = millis();
    if((uint32_t)(now - _lastProbeLogAt) >= ADS_LOG_INTERVAL_MS) {
        _lastProbeLogAt = now;
        Serial.printf("ELRS/CRSF ADC: ADS1015 probe %s @0x%02X\n", ok ? "ok" : "failed", ADS1015_ADDR);
    }
    #endif
    return ok;
}

bool ELRSCrsfMode::startAdsChannel(uint8_t channel)
{
    Wire.beginTransmission(ADS1015_ADDR);
    Wire.write(ADS_REG_CONFIG);
    Wire.write(elrsAds1015SingleEndedConfigHighByte(channel));
    Wire.write(0xE3);
    return Wire.endTransmission(true) == 0;
}

bool ELRSCrsfMode::readAdsRegister(uint8_t reg, uint16_t &value)
{
    Wire.beginTransmission(ADS1015_ADDR);
    Wire.write(reg);
    if(Wire.endTransmission(false) || Wire.requestFrom((uint8_t)ADS1015_ADDR, (uint8_t)2) != 2) {
        return false;
    }
    const uint8_t high = Wire.read();
    const uint8_t low = Wire.read();
    value = ((uint16_t)high << 8) | low;
    return true;
}

ELRSAxesResult ELRSCrsfMode::failAdsSweep()
{
    _adsState = ADS_IDLE;
    _adsChannel = 0;
    _haveAds = false;
    _haveFilteredAxes = false;
    _adsRetryPending = true;
    _lastAdsProbeAt = (uint32_t)millis();
    return ELRS_AXES_ERROR;
}

void ELRSCrsfMode::logMessage(const char *message)
{
    Serial.println(message);
}

void ELRSCrsfMode::startSerial(uint32_t baud, bool invert)
{
    _serial.end();
    _serial.begin((unsigned long)baud, SERIAL_8N1, CRSF_RX_PIN, CRSF_TX_PIN, invert);
    setDriverEnabled(false);
    discardSerialInput();
}

void ELRSCrsfMode::stopSerial()
{
    _serial.end();
}

int ELRSCrsfMode::serialAvailable()
{
    return _serial.available();
}

int ELRSCrsfMode::serialRead()
{
    return _serial.read();
}

size_t ELRSCrsfMode::serialWrite(const uint8_t *data, size_t len)
{
    return _serial.write(data, len);
}

void ELRSCrsfMode::serialFlush()
{
    _serial.flush();
}

void ELRSCrsfMode::setDriverEnabled(bool enabled)
{
    if(!enabled) {
        delayMicroseconds(elrsCrsfDriverDisableHoldUs());
    }

    bool level = enabled ? !_oeActiveLow : _oeActiveLow;
    digitalWrite(CRSF_OE_PIN, level ? HIGH : LOW);
    delayMicroseconds(enabled ? elrsCrsfDriverEnableSetupUs() : elrsCrsfDriverReleaseGuardUs());
}

void ELRSCrsfMode::discardSerialInput()
{
    while(_serial.available()) {
        _serial.read();
    }
}

unsigned long ELRSCrsfMode::microsNow()
{
    return micros();
}

unsigned long ELRSCrsfMode::millisNow()
{
    return millis();
}

ELRSAxesResult ELRSCrsfMode::sampleAxes(int16_t axes[ELRS_GIMBAL_AXIS_COUNT], uint32_t &completedAt,
                                      ELRSAxesRequest request, uint32_t txBudgetUs)
{
    if(!_haveAds) {
        if(txBudgetUs < ADS_SERVICE_BUDGET_US ||
           (_adsRetryPending && (uint32_t)((uint32_t)millis() - _lastAdsProbeAt) < ADS_RETRY_MS)) {
            return ELRS_AXES_ERROR;
        }
        _haveAds = initAds1015();
        if(!_haveAds) return ELRS_AXES_ERROR;
        _adsState = ADS_DRAIN;
        _adsChannel = 0;
        _adsReadyAtUs = (uint32_t)micros();
        return ELRS_AXES_PENDING;
    }
    if(request == ELRS_AXES_RESTART) {
        // ADS1015 ignores OS starts while busy. Discard the abandoned conversion
        // before starting A0; do no extra I2C in the button's pass.
        _adsState = ADS_DRAIN;
        _adsChannel = 0;
        _adsReadyAtUs = (uint32_t)micros();
        return ELRS_AXES_PENDING;
    }
    if(_adsState == ADS_IDLE) {
        if(request != ELRS_AXES_START) return ELRS_AXES_PENDING;
        _adsState = ADS_START;
        _adsChannel = 0;
    }
    if(txBudgetUs < ADS_SERVICE_BUDGET_US) return ELRS_AXES_PENDING;

    uint16_t value;
    switch(_adsState) {
    case ADS_START:
        if(!startAdsChannel(_adsChannel)) return failAdsSweep();
        _adsReadyAtUs = (uint32_t)micros() + ADS_CONVERSION_US;
        _adsState = ADS_WAIT;
        return ELRS_AXES_PENDING;
    case ADS_DRAIN:
    case ADS_WAIT:
        if((int32_t)((uint32_t)micros() - _adsReadyAtUs) < 0) return ELRS_AXES_PENDING;
        if(!readAdsRegister(ADS_REG_CONFIG, value)) return failAdsSweep();
        if(value & 0x8000) _adsState = _adsState == ADS_DRAIN ? ADS_START : ADS_COLLECT;
        else _adsReadyAtUs = (uint32_t)micros() + ADS_CONVERSION_US;
        return ELRS_AXES_PENDING;
    case ADS_COLLECT:
        if(!readAdsRegister(ADS_REG_CONVERT, value)) return failAdsSweep();
        _stagingAxes[_adsChannel] = ((int16_t)value) >> 4;
        if(_stagingAxes[_adsChannel] < 0) _stagingAxes[_adsChannel] = 0;
        if(++_adsChannel < ELRS_GIMBAL_AXIS_COUNT) {
            _adsState = ADS_START;
            return ELRS_AXES_PENDING;
        }
        break;
    default:
        return ELRS_AXES_PENDING;
    }
    _adsState = ADS_IDLE;
    completedAt = (uint32_t)millis();
    for(int i = 0; i < ELRS_GIMBAL_AXIS_COUNT; i++) {
        _filteredAxes[i] = !_haveFilteredAxes ? _stagingAxes[i] :
            elrsIirFilterStep(_filteredAxes[i], _stagingAxes[i], ADS_FILTER_SHIFT);
        _rawAxes[i] = axes[i] = _filteredAxes[i];
    }
    _haveFilteredAxes = true;

    #ifdef REMOTE_DBG
    const uint32_t now = millis();
    if(!_haveLoggedAxes || ((uint32_t)(now - _lastAxesLogAt) >= ADS_LOG_INTERVAL_MS &&
       elrsAxesChanged(_rawAxes, _lastLoggedAxes, ELRS_GIMBAL_AXIS_COUNT, ADS_LOG_DELTA_THRESHOLD))) {
        memcpy(_lastLoggedAxes, _rawAxes, sizeof(_lastLoggedAxes));
        _haveLoggedAxes = true;
        _lastAxesLogAt = now;
        Serial.printf("ELRS/CRSF ADC raw: A0=%d A1=%d A2=%d A3=%d\n",
                      _rawAxes[0], _rawAxes[1], _rawAxes[2], _rawAxes[3]);
    }
    #endif

    return ELRS_AXES_READY;
}

bool ELRSCrsfMode::readFakePowerSwitch()
{
    return (digitalRead(FPOWER_IO_PIN) == LOW);
}

bool ELRSCrsfMode::readStopSwitch()
{
    return (digitalRead(STOPS_IO_PIN) == HIGH);
}

bool ELRSCrsfMode::readButtonA()
{
    return (digitalRead(BUTA_IO_PIN) == LOW);
}

bool ELRSCrsfMode::readButtonB()
{
    return (digitalRead(BUTB_IO_PIN) == LOW);
}

bool ELRSCrsfMode::readCalibrationButton()
{
    return (digitalRead(CALIBB_IO_PIN) == LOW);
}

bool ELRSCrsfMode::samplePackStates(uint8_t &states)
{
    if(!_haveButtonPack || !_buttonPack) {
        states = 0;
        return false;
    }

    return _buttonPack->sampleStates(states);
}

void ELRSCrsfMode::displayOn()
{
    if(_display) {
        _display->on();
    }
}

void ELRSCrsfMode::displaySetText(const char *text)
{
    if(_display) {
        _display->setText(text);
    }
}

void ELRSCrsfMode::displaySetSpeed(int speed)
{
    if(_display) {
        _display->setSpeed(speed);
    }
}

void ELRSCrsfMode::displayShow()
{
    if(_display) {
        _display->show();
    }
}

void ELRSCrsfMode::setPowerLed(bool state)
{
    if(_powerLed) {
        _powerLed->setState(state);
    }
}

bool ELRSCrsfMode::getPowerLed() const
{
    return _powerLed ? _powerLed->getState() : false;
}

void ELRSCrsfMode::setLevelMeter(bool state)
{
    if(_levelMeter) {
        _levelMeter->setState(state);
    }
}

bool ELRSCrsfMode::getLevelMeter() const
{
    return _levelMeter ? _levelMeter->getState() : false;
}

void ELRSCrsfMode::setStopLed(bool state)
{
    if(_stopLed) {
        _stopLed->setState(state);
    }
}

void ELRSCrsfMode::loadCalibration(ELRSAxisCalibrationData *cal, int count)
{
    loadELRSCalibration(cal, count);
}

bool ELRSCrsfMode::saveCalibration(const ELRSAxisCalibrationData *cal, int count)
{
    return saveELRSCalibration(cal, count);
}

#endif
