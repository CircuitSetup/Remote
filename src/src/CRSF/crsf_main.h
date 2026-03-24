#ifndef _CRSF_MAIN_H
#define _CRSF_MAIN_H

#ifdef HAVE_CRSF

#include "../../display.h"
#include "../../input.h"

bool crsf_main_mode_enabled();
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
    bool levelMeterOnFakePower
);
void crsf_main_loop(int battWarn);
bool crsf_main_fake_power_on();
bool crsf_main_is_calibrating();

#endif

#endif
