#ifndef _ELRS_CRSF_CORE_H
#define _ELRS_CRSF_CORE_H

#include <stddef.h>
#include <stdint.h>

#include "elrs_crsf_shared.h"

enum ELRSCrsfFaultFlags : uint8_t {
    ELRS_FAULT_NONE = 0,
    ELRS_FAULT_ADC_MISSING = (1 << 0),
    ELRS_FAULT_ADC_STALE = (1 << 1),
    ELRS_FAULT_BUTTONPACK_STALE = (1 << 2),
    ELRS_FAULT_FALLBACK_BAUD = (1 << 3)
};

enum ELRSCrsfCommCode : uint8_t {
    ELRS_COMM_NONE = 0,
    ELRS_COMM_FAL,
    ELRS_COMM_NSY,
    ELRS_COMM_LOS,
    ELRS_COMM_CRC,
    ELRS_COMM_FRM
};

struct ELRSCrsfStatus {
    uint32_t baudRate = 420000;
    bool telemetryActive = false;
    bool synced = false;
    uint8_t activeSpeedSource = 0;
    uint8_t linkQuality = 0;
    uint8_t remoteBatteryPercent = 0;
    float remoteBatteryVoltage = 0.0f;
    uint8_t faultFlags = ELRS_FAULT_NONE;
    uint8_t commCode = ELRS_COMM_NONE;
    bool everSynced = false;
    bool fakePowerOn = false;
    bool calibrating = false;
};

class ELRSCrsfHost {
    public:
        virtual ~ELRSCrsfHost() {}

        virtual void logMessage(const char *message) = 0;

        virtual void startSerial(uint32_t baud) = 0;
        virtual int serialAvailable() = 0;
        virtual int serialRead() = 0;
        virtual size_t serialWrite(const uint8_t *data, size_t len) = 0;
        virtual void serialFlush() = 0;
        virtual void setDriverEnabled(bool enabled) = 0;
        virtual void discardSerialInput() = 0;

        virtual bool sampleAxes(int16_t axes[ELRS_GIMBAL_AXIS_COUNT]) = 0;
        virtual bool readFakePowerSwitch() = 0;
        virtual bool readStopSwitch() = 0;
        virtual bool readButtonA() = 0;
        virtual bool readButtonB() = 0;
        virtual bool readCalibrationButton() = 0;
        virtual bool samplePackStates(uint8_t &states) = 0;

        virtual void displayOn() = 0;
        virtual void displaySetText(const char *text) = 0;
        virtual void displaySetSpeed(int speed) = 0;
        virtual void displayShow() = 0;

        virtual void setPowerLed(bool state) = 0;
        virtual bool getPowerLed() const = 0;
        virtual void setLevelMeter(bool state) = 0;
        virtual bool getLevelMeter() const = 0;
        virtual void setStopLed(bool state) = 0;

        virtual void loadCalibration(ELRSAxisCalibrationData *cal, int count) = 0;
        virtual void saveCalibration(const ELRSAxisCalibrationData *cal, int count) = 0;
};

struct ELRSCrsfCoreConfig {
    bool haveButtonPack = false;
    bool usePowerLed = false;
    bool useLevelMeter = false;
    bool powerLedOnFakePower = false;
    bool levelMeterOnFakePower = false;
};

class ELRSCrsfCore {
    public:
        enum SpeedSource : uint8_t {
            SPEED_SOURCE_NONE = 0,
            SPEED_SOURCE_GPS,
            SPEED_SOURCE_AIRSPEED
        };

        ELRSCrsfCore();

        bool begin(ELRSCrsfHost &host, const ELRSCrsfCoreConfig &config, unsigned long now);
        void loop(ELRSCrsfHost &host, unsigned long now, int battWarn);

        bool isCalibrating() const;
        bool fakePowerOn() const;

        uint32_t baudRate() const;
        uint16_t channelAt(uint8_t index) const;
        uint8_t linkQuality() const;
        uint8_t remoteBatteryPercent() const;
        float remoteBatteryVoltage() const;
        uint16_t gpsSpeed10() const;
        uint16_t airspeed10() const;
        bool telemetryActive() const;
        bool synced() const;
        SpeedSource activeSpeedSource() const;
        ELRSCrsfStatus getStatus() const;

        static uint8_t crc8D5(const uint8_t *data, size_t len);
        static size_t packRcChannelsFrame(const uint16_t channels[16], uint8_t *frame, size_t frameSize);

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

        bool sampleAxes(ELRSCrsfHost &host, unsigned long now, bool force = false);
        uint8_t samplePackStates(ELRSCrsfHost &host, unsigned long now);

        void updateCalibrationButton(ELRSCrsfHost &host, unsigned long now, int battWarn);
        void handleCalibrationShort(ELRSCrsfHost &host, unsigned long now, int battWarn);
        void handleCalibrationLong(ELRSCrsfHost &host, unsigned long now, int battWarn);
        const char *getCalibrationPrompt() const;

        void updateChannels(unsigned long now, bool fakePowerOn, bool stopOn, bool buttonAOn, bool buttonBOn, uint8_t packStates);
        uint16_t axisToTicks(uint8_t axis) const;
        uint16_t mapTicks(int16_t raw, int16_t inMin, int16_t inMax, uint16_t outMin, uint16_t outMax) const;

        void beginSerial(ELRSCrsfHost &host, uint32_t baud);
        void sendChannels(ELRSCrsfHost &host, unsigned long now);
        void pollTelemetry(ELRSCrsfHost &host, unsigned long now);
        void handleFrame(ELRSCrsfHost &host, const uint8_t *frame, size_t frameSize, unsigned long now);
        void resyncRxBuffer(size_t startIndex = 1);

        void applyIdleOutputs(ELRSCrsfHost &host, bool fakePowerOn);
        void updateBatteryWarning(ELRSCrsfHost &host, unsigned long now, int battWarn, bool fakePowerOn);
        void updateInputFaults(ELRSCrsfHost &host, unsigned long now);
        void updateBenchState(ELRSCrsfHost &host, unsigned long now);
        void updateDisplay(ELRSCrsfHost &host, unsigned long now, int battWarn);
        bool hasRecentTelemetry(unsigned long now) const;
        bool adcFaultActive(unsigned long now) const;
        bool buttonPackFaultActive(unsigned long now) const;
        uint16_t getDisplaySpeed10(unsigned long now, SpeedSource *source = NULL) const;
        void showOverlay(const char *text, unsigned long now, unsigned long durationMs);
        void showCommOverlay(const char *text, unsigned long now, unsigned long durationMs);
        void setCommCode(ELRSCrsfHost &host, uint8_t code, unsigned long now);
        void clearCommCode();
        void noteBadCrc(ELRSCrsfHost &host, unsigned long now);
        void noteFrameError(ELRSCrsfHost &host, unsigned long now);

        void log(ELRSCrsfHost &host, const char *message) const;
        void logf(ELRSCrsfHost &host, const char *fmt, ...) const;

        static uint16_t readBE16(const uint8_t *data);
        static const char *speedSourceName(SpeedSource source);
        static const char *commCodeText(uint8_t code);

        ELRSCrsfCoreConfig _config;

        bool _haveAds = false;
        bool _fakePowerOn = false;
        bool _synced = false;
        bool _fallbackApplied = false;

        uint32_t _baudRate = 420000;
        unsigned long _startedAt = 0;
        unsigned long _fallbackAt = 0;
        unsigned long _lastAxisAttemptAt = 0;
        unsigned long _lastGoodAxesAt = 0;
        unsigned long _lastGoodPackAt = 0;
        unsigned long _lastTx = 0;
        unsigned long _lastDisplay = 0;
        unsigned long _lastTelemetry = 0;
        unsigned long _lastLinkStats = 0;
        unsigned long _lastGpsSpeed = 0;
        unsigned long _lastAirspeed = 0;
        unsigned long _batteryBlinkAt = 0;
        unsigned long _batteryBannerAt = 0;
        unsigned long _overlayUntil = 0;
        unsigned long _commOverlayUntil = 0;
        unsigned long _crcBurstAt = 0;
        unsigned long _frameBurstAt = 0;

        uint16_t _channels[16];
        int16_t _rawAxes[ELRS_GIMBAL_AXIS_COUNT];
        ELRSAxisCalibrationData _axisCal[ELRS_GIMBAL_AXIS_COUNT];
        uint8_t _lastPackStates = 0;

        uint8_t _linkQuality = 0;
        uint8_t _remoteBattery = 0;
        float _remoteBatteryVoltage = 0.0f;
        uint8_t _faultFlags = ELRS_FAULT_NONE;
        uint8_t _commCode = ELRS_COMM_NONE;
        uint16_t _gpsSpeed10 = 0;
        uint16_t _airspeed10 = 0;
        uint8_t _crcBurstCount = 0;
        uint8_t _frameBurstCount = 0;
        bool _telemetryActive = false;
        SpeedSource _activeSpeedSource = SPEED_SOURCE_NONE;
        bool _hasValidPackState = false;
        bool _adcFaultActive = false;
        bool _buttonPackFaultActive = false;

        bool _calibRaw = false;
        bool _calibPressed = false;
        bool _calibLongSent = false;
        unsigned long _calibDebounceAt = 0;
        unsigned long _calibPressedAt = 0;
        CalStage _calStage = CAL_IDLE;

        uint8_t _rxFrame[64];
        size_t _rxFrameLen = 0;
        char _overlayText[4];
        char _commOverlayText[4];
};

#endif
