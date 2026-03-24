#include "../../remote_global.h"

#ifdef HAVE_CRSF

#include <Arduino.h>
#include <stdlib.h>

#include "../../remote_settings.h"
#include "crsf_main.h"
#include "elrs_crsf.h"

namespace {

bool crsfMainStarted = false;

uint16_t crsfPacketRate()
{
    return elrsPacketRateOrDefault((uint16_t)atoi(settings.elrsPktRate));
}

}

bool crsf_main_mode_enabled()
{
    return (atoi(settings.opMode) == 1);
}

bool crsf_main_setup(
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
    if(!crsf_main_mode_enabled()) {
        crsfMainStarted = false;
        return false;
    }

    if(buttonPack) {
        haveButtonPack = buttonPack->begin();
        if(haveButtonPack) {
            buttonPack->setScanInterval(50);
        } else {
            #ifdef REMOTE_DBG
            Serial.println("ButtonPack not detected");
            #endif
        }
    } else {
        haveButtonPack = false;
    }

    crsfMainStarted = elrsMode.begin(
        crsfPacketRate(),
        haveButtonPack ? buttonPack : NULL,
        haveButtonPack,
        display,
        powerLed,
        levelMeter,
        stopLed,
        usePowerLed,
        useLevelMeter,
        powerLedOnFakePower,
        levelMeterOnFakePower
    );

    return crsfMainStarted;
}

void crsf_main_loop(int battWarn)
{
    if(crsfMainStarted) {
        elrsMode.loop(battWarn);
    }
}

bool crsf_main_fake_power_on()
{
    return crsfMainStarted ? elrsMode.fakePowerOn() : false;
}

bool crsf_main_is_calibrating()
{
    return crsfMainStarted ? elrsMode.isCalibrating() : false;
}

#endif
