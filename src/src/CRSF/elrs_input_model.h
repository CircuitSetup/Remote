#ifndef _ELRS_INPUT_MODEL_H
#define _ELRS_INPUT_MODEL_H

#include <stdint.h>

#include "elrs_crsf_shared.h"

enum ELRSGimbalInput : uint8_t {
    ELRS_GIMBAL_INPUT_AILERON = 0,
    ELRS_GIMBAL_INPUT_ELEVATOR = 1,
    ELRS_GIMBAL_INPUT_RUDDER = 2,
    ELRS_GIMBAL_INPUT_THROTTLE = 3
};

struct ELRSInputAxisProfile {
    int16_t minimum;
    int16_t center;
    int16_t maximum;
    uint16_t reverse;
    uint16_t deadband;
    uint8_t expo; // Independent response curve strength, 0 (linear) through 100 (cubic).
};

struct ELRSGimbalRouting {
    uint8_t aileronChannel;
    uint8_t elevatorChannel;
    uint8_t throttleChannel;
    uint8_t rudderChannel;
};

struct ELRSOutputLimits {
    uint16_t minimumUs;
    uint16_t maximumUs;
};

constexpr uint8_t ELRS_SWITCH_INPUT_COUNT = 12;
struct ELRSSwitchRouting {
    uint8_t channels[ELRS_SWITCH_INPUT_COUNT]; // Stop, FakePower, O.O, RESET, ButtonPack 1-8.
};

constexpr uint16_t ELRS_INPUT_TOLERANCE_DEFAULT = 5;
constexpr uint16_t ELRS_INPUT_TOLERANCE_MAX = 32;

int16_t elrsInputModelApplyExpo(int16_t linearUs, uint8_t expo, bool centered);
int16_t elrsInputModelAxisToUs(const ELRSInputAxisProfile &profile, int16_t raw);
int16_t elrsInputModelThrottleToUs(const ELRSInputAxisProfile &profile, int16_t raw, uint16_t idleDeadband);
ELRSOutputLimits elrsDefaultOutputLimits();
bool elrsIsValidOutputLimits(const ELRSOutputLimits &limits);
ELRSOutputLimits elrsSanitizeOutputLimits(const ELRSOutputLimits &limits);
int16_t elrsApplyOutputLimits(const ELRSOutputLimits &limits, int16_t us);
uint16_t elrsInputUsToCrsfTicks(int16_t us);
ELRSInputAxisProfile elrsDefaultInputAxisProfile();
ELRSGimbalRouting elrsDefaultGimbalRouting();
bool elrsIsValidInputAxisProfile(const ELRSInputAxisProfile &profile);
ELRSInputAxisProfile elrsSanitizeInputAxisProfile(const ELRSInputAxisProfile &profile);
bool elrsIsValidGimbalRouting(const ELRSGimbalRouting &routing);
ELRSGimbalRouting elrsSanitizeGimbalRouting(const ELRSGimbalRouting &routing);
ELRSSwitchRouting elrsDefaultSwitchRouting();
bool elrsIsValidSwitchRouting(const ELRSSwitchRouting &routing);
ELRSSwitchRouting elrsSanitizeSwitchRouting(const ELRSSwitchRouting &routing);
bool elrsIsValidInputRouting(const ELRSGimbalRouting &gimbals, const ELRSSwitchRouting &switches);
void elrsSanitizeInputRouting(ELRSGimbalRouting &gimbals, ELRSSwitchRouting &switches);

#endif
