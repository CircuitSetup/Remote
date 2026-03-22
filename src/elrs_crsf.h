#ifndef _ELRS_CRSF_H
#define _ELRS_CRSF_H

#include <Arduino.h>
#include <HardwareSerial.h>

#include "display.h"
#include "input.h"
#include "remote_settings.h"

class ELRSCrsfMode {

    public:
        ELRSCrsfMode();

        bool begin(
            ButtonPack *buttonPack,
            bool haveButtonPack,
            remDisplay *display,
            remLED *powerLed,
            remLED *levelMeter,
            remLED *stopLed,
            bool usePowerLed,
            bool useLevelMeter,
            bool powerLedOnFakePower,
            bool levelMeterOnFakePower
        );

        void loop(int battWarn);

        bool isCalibrating() const;
        bool fakePowerOn() const;

    private:
        enum CalStage : uint8_t {
            CAL_IDLE = 0,
            CAL_CENTER,
            CAL_TLOW,
            CAL_THIGH,
            CAL_YLOW,
            CAL_YHIGH,
            CAL_PLOW,
            CAL_PHIGH,
            CAL_RLOW,
            CAL_RHIGH
        };

        enum SpeedSource : uint8_t {
            SPEED_SOURCE_NONE = 0,
            SPEED_SOURCE_GPS,
            SPEED_SOURCE_AIRSPEED
        };

        bool initAds1015();
        int16_t readAdsChannel(uint8_t channel);
        void sampleAxes(bool force = false);

        void updateCalibrationButton(unsigned long now, bool fakePowerOn, int battWarn);
        void handleCalibrationShort(unsigned long now, bool fakePowerOn, int battWarn);
        void handleCalibrationLong(unsigned long now, bool fakePowerOn, int battWarn);
        const char *getCalibrationPrompt() const;

        void updateChannels(bool fakePowerOn, bool stopOn, bool buttonAOn, bool buttonBOn, uint8_t packStates);
        uint16_t axisToTicks(uint8_t axis) const;
        uint16_t mapTicks(int16_t raw, int16_t inMin, int16_t inMax, uint16_t outMin, uint16_t outMax) const;

        void beginSerial(uint32_t baud);
        void setDriverEnabled(bool enabled);
        void sendChannels(unsigned long now);
        void pollTelemetry(unsigned long now);
        void handleFrame(const uint8_t *frame, size_t frameSize, unsigned long now);

        void applyIdleOutputs(bool fakePowerOn);
        void updateBatteryWarning(unsigned long now, int battWarn, bool fakePowerOn);
        void updateBenchState(unsigned long now);
        void updateDisplay(unsigned long now, int battWarn);
        bool hasRecentTelemetry(unsigned long now) const;
        uint16_t getDisplaySpeed10(unsigned long now, SpeedSource *source = NULL) const;
        void showOverlay(const char *text, unsigned long now, unsigned long durationMs);

        static uint16_t readBE16(const uint8_t *data);
        static uint8_t crc8D5(const uint8_t *data, size_t len);
        static const char *speedSourceName(SpeedSource source);

        HardwareSerial _serial;
        ButtonPack *_buttonPack = NULL;
        remDisplay *_display = NULL;
        remLED *_powerLed = NULL;
        remLED *_levelMeter = NULL;
        remLED *_stopLed = NULL;

        bool _haveButtonPack = false;
        bool _haveAds = false;
        bool _usePowerLed = false;
        bool _useLevelMeter = false;
        bool _powerLedOnFakePower = false;
        bool _levelMeterOnFakePower = false;

        bool _fakePowerOn = false;
        bool _synced = false;
        bool _fallbackApplied = false;

        uint32_t _baudRate = 420000;
        unsigned long _startedAt = 0;
        unsigned long _lastAxisSample = 0;
        unsigned long _lastTx = 0;
        unsigned long _lastDisplay = 0;
        unsigned long _lastTelemetry = 0;
        unsigned long _lastLinkStats = 0;
        unsigned long _lastGpsSpeed = 0;
        unsigned long _lastAirspeed = 0;
        unsigned long _batteryBlinkAt = 0;
        unsigned long _batteryBannerAt = 0;
        unsigned long _overlayUntil = 0;

        uint16_t _channels[16] = { 0 };
        int16_t _rawAxes[ELRS_GIMBAL_AXIS_COUNT] = { 1024, 1024, 1024, 1024 };
        ELRSAxisCalibrationData _axisCal[ELRS_GIMBAL_AXIS_COUNT];

        uint8_t _linkQuality = 0;
        uint8_t _remoteBattery = 0;
        float _remoteBatteryVoltage = 0.0f;
        uint16_t _gpsSpeed10 = 0;
        uint16_t _airspeed10 = 0;
        bool _telemetryActive = false;
        SpeedSource _activeSpeedSource = SPEED_SOURCE_NONE;

        bool _calibRaw = false;
        bool _calibPressed = false;
        bool _calibLongSent = false;
        unsigned long _calibDebounceAt = 0;
        unsigned long _calibPressedAt = 0;
        CalStage _calStage = CAL_IDLE;

        char _overlayText[4] = "";
};

extern ELRSCrsfMode elrsMode;

#endif
