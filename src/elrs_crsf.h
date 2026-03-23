#ifndef _ELRS_CRSF_H
#define _ELRS_CRSF_H

#ifdef CRSF

#include <Arduino.h>
#include <HardwareSerial.h>

#include "display.h"
#include "elrs_crsf_core.h"
#include "input.h"
#include "remote_settings.h"

class ELRSCrsfMode : private ELRSCrsfHost {

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
        ELRSCrsfStatus getStatus() const;

    private:
        bool initAds1015();
        int16_t readAdsChannel(uint8_t channel);

        void logMessage(const char *message) override;

        void startSerial(uint32_t baud, bool invert) override;
        void stopSerial() override;
        int serialAvailable() override;
        int serialRead() override;
        size_t serialWrite(const uint8_t *data, size_t len) override;
        void serialFlush() override;
        void setDriverEnabled(bool enabled) override;
        void discardSerialInput() override;

        bool sampleAxes(int16_t axes[ELRS_GIMBAL_AXIS_COUNT]) override;
        bool readFakePowerSwitch() override;
        bool readStopSwitch() override;
        bool readButtonA() override;
        bool readButtonB() override;
        bool readCalibrationButton() override;
        bool samplePackStates(uint8_t &states) override;

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
        void saveCalibration(const ELRSAxisCalibrationData *cal, int count) override;

        ELRSCrsfCore _core;
        HardwareSerial _serial;
        ButtonPack *_buttonPack = NULL;
        remDisplay *_display = NULL;
        remLED *_powerLed = NULL;
        remLED *_levelMeter = NULL;
        remLED *_stopLed = NULL;

        bool _haveButtonPack = false;
        bool _usePowerLed = false;
        bool _useLevelMeter = false;
        bool _powerLedOnFakePower = false;
        bool _levelMeterOnFakePower = false;
        bool _haveAds = false;
        bool _oeActiveLow = true;

        int16_t _rawAxes[ELRS_GIMBAL_AXIS_COUNT] = { 1024, 1024, 1024, 1024 };
};

extern ELRSCrsfMode elrsMode;

#endif

#endif
