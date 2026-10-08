#ifndef _ELRS_CRSF_H
#define _ELRS_CRSF_H

#ifdef HAVE_CRSF

#include <Arduino.h>
#include <HardwareSerial.h>

#include "../../display.h"
#include "elrs_crsf_core.h"
#include "../../input.h"
//#include "remote_settings.h"

class ELRSCrsfMode : private ELRSCrsfHost {

    public:
        ELRSCrsfMode();

        bool begin(
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
            uint16_t adcHysteresis = ELRS_INPUT_TOLERANCE_DEFAULT,
            uint16_t throttleIdleDeadband = ELRS_INPUT_TOLERANCE_DEFAULT,
            const ELRSSwitchRouting *switchRouting = NULL,
            const ELRSOutputLimits *outputLimits = NULL,
            const uint8_t *localActions = NULL,
            bool propControls = false,
            const ELRSDisplayConfig *displayConfig = NULL,
            const ELRSVehicleConfig *vehicleConfig = NULL
        );

        void loop(int battWarn);

        bool isCalibrating() const;
        bool fakePowerOn() const;
        ELRSCrsfStatus getStatus() const;
        ELRSTelemetrySample telemetrySample(uint8_t source, uint32_t now, const ELRSVehicleConfig *vehicleConfig = NULL) const;
        bool telemetryDisplayAssigned() const;
        void renderAssignedDisplay(uint32_t now, int battWarn);
        uint8_t speedDisplayUnits() const;
        void requestModuleConfigUpdate(uint8_t telemetryRatio, uint8_t maxPower, uint8_t dynamicPower);
        bool readCurrentRawAxes(int16_t axes[ELRS_GIMBAL_AXIS_COUNT]);

    private:
        bool initAds1015();
        bool startAdsChannel(uint8_t channel);
        bool readAdsRegister(uint8_t reg, uint16_t &value);
        ELRSAxesResult failAdsSweep();

        void logMessage(const char *message) override;

        void startSerial(uint32_t baud, bool invert) override;
        void stopSerial() override;
        int serialAvailable() override;
        int serialRead() override;
        size_t serialWrite(const uint8_t *data, size_t len) override;
        void serialFlush() override;
        void setDriverEnabled(bool enabled) override;
        void discardSerialInput() override;
        unsigned long microsNow() override;
        unsigned long millisNow() override;

        ELRSAxesResult sampleAxes(int16_t axes[ELRS_GIMBAL_AXIS_COUNT], uint32_t &completedAt,
                                 ELRSAxesRequest request, uint32_t txBudgetUs) override;
        bool readFakePowerSwitch() override;
        bool readStopSwitch() override;
        bool readButtonA() override;
        bool readButtonB() override;
        bool readCalibrationButton() override;
        bool samplePackStates(uint8_t &states) override;
        void scanLocalSwitches(uint16_t states, uint16_t validMask) override;

        void displayOn() override;
        void displaySetText(const char *text) override;
        void displaySetSpeed(int speed) override;
        void displayShow() override;

        void setPowerLed(bool state) override;
        bool getPowerLed() const override;
        void setLevelMeter(bool state) override;
        bool getLevelMeter() const override;
        void setStopLed(bool state) override;

        void loadCalibration(ELRSAxisCalibrationData *cal, int count) override;
        bool saveCalibration(const ELRSAxisCalibrationData *cal, int count) override;

        ELRSCrsfCore _core;
        uint8_t _speedDisplayUnits = ELRS_SPEED_UNITS_DEFAULT;
        HardwareSerial _serial;
        ButtonPack *_buttonPack = NULL;
        remDisplay *_display = NULL;
        remLED *_powerLed = NULL;
        remLED *_levelMeter = NULL;
        remLED *_stopLed = NULL;
        void (*_fpOnWifiHandler)(bool) = NULL;

        bool _haveButtonPack = false;
        bool _usePowerLed = false;
        bool _useLevelMeter = false;
        bool _powerLedOnFakePower = false;
        bool _levelMeterOnFakePower = false;
        bool _haveAds = false;
        bool _oeActiveLow = true;
        bool _haveFilteredAxes = false;
        enum AdsState : uint8_t { ADS_IDLE, ADS_DRAIN, ADS_START, ADS_WAIT, ADS_COLLECT };
        AdsState _adsState = ADS_IDLE;
        uint8_t _adsChannel = 0;
        uint32_t _adsReadyAtUs = 0;
        uint32_t _lastAdsProbeAt = 0;
        bool _adsRetryPending = false;
        int16_t _stagingAxes[ELRS_GIMBAL_AXIS_COUNT] = {};

        int16_t _rawAxes[ELRS_GIMBAL_AXIS_COUNT] = { 1024, 1024, 1024, 1024 };
        int16_t _filteredAxes[ELRS_GIMBAL_AXIS_COUNT] = { 1024, 1024, 1024, 1024 };
        #ifdef REMOTE_DBG
        bool _haveLoggedAxes = false;
        uint32_t _lastAxesLogAt = 0;
        uint32_t _lastProbeLogAt = 0;
        int16_t _lastLoggedAxes[ELRS_GIMBAL_AXIS_COUNT] = { 1024, 1024, 1024, 1024 };
        #endif
};

extern ELRSCrsfMode elrsMode;

#endif

#endif
