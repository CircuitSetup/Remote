#include "elrs_input_model.h"

namespace {

static long clampLong(long value, long lo, long hi)
{
    return (value < lo) ? lo : ((value > hi) ? hi : value);
}

static int16_t mapAxisSegment(int16_t raw, int16_t inMin, int16_t inMax, int16_t outMin, int16_t outMax)
{
    long mapped;
    long outLo = (outMin < outMax) ? outMin : outMax;
    long outHi = (outMin > outMax) ? outMin : outMax;
    long numerator;
    long denominator;

    if(inMin == inMax) {
        return outMin;
    }

    numerator = (long)(raw - inMin) * (long)(outMax - outMin);
    denominator = (long)(inMax - inMin);

    if((numerator >= 0 && denominator > 0) || (numerator <= 0 && denominator < 0)) {
        numerator += denominator / 2;
    } else {
        numerator -= denominator / 2;
    }

    mapped = numerator / denominator + outMin;
    mapped = clampLong(mapped, outLo, outHi);

    return (int16_t)mapped;
}

static int16_t moveToward(int16_t start, int16_t target, uint16_t amount)
{
    long delta = (long)target - start;

    if(delta > 0) {
        return (int16_t)(start + (int16_t)clampLong((long)amount, 0, delta));
    }

    return (int16_t)(start - (int16_t)clampLong((long)amount, 0, -delta));
}

}

int16_t elrsInputModelAxisToUs(const ELRSInputAxisProfile &profile, int16_t raw)
{
    const int16_t midUs = ELRS_INPUT_US_MID;
    int16_t lowUs = profile.reverse ? ELRS_INPUT_US_MAX : ELRS_INPUT_US_MIN;
    int16_t highUs = profile.reverse ? ELRS_INPUT_US_MIN : ELRS_INPUT_US_MAX;
    int16_t lowThreshold;
    int16_t highThreshold;
    int16_t deadbandLo;
    int16_t deadbandHi;
    int16_t deadbandMin;
    int16_t deadbandMax;

    if(profile.minimum == profile.center ||
       profile.maximum == profile.center ||
       profile.minimum == profile.maximum) {
        return ELRS_INPUT_US_MID;
    }

    deadbandLo = moveToward(profile.center, profile.minimum, profile.deadband);
    deadbandHi = moveToward(profile.center, profile.maximum, profile.deadband);
    deadbandMin = (deadbandLo < deadbandHi) ? deadbandLo : deadbandHi;
    deadbandMax = (deadbandLo > deadbandHi) ? deadbandLo : deadbandHi;

    if(raw >= deadbandMin && raw <= deadbandMax) {
        return midUs;
    }

    lowThreshold = deadbandLo;
    highThreshold = deadbandHi;

    if((profile.minimum < profile.center) ? (raw < lowThreshold) : (raw > lowThreshold)) {
        int16_t mapped = mapAxisSegment(raw, lowThreshold, profile.minimum, midUs, lowUs);

        if(mapped == midUs) {
            mapped = (lowUs < midUs) ? (int16_t)(midUs - 1) : (int16_t)(midUs + 1);
        }

        return mapped;
    }

    // Task 1 keeps expo explicit but inactive until a later task defines the curve behavior.
    int16_t mapped = mapAxisSegment(raw, highThreshold, profile.maximum, midUs, highUs);

    if(mapped == midUs) {
        mapped = (highUs > midUs) ? (int16_t)(midUs + 1) : (int16_t)(midUs - 1);
    }

    return mapped;
}

int16_t elrsInputModelThrottleToUs(const ELRSInputAxisProfile &profile, int16_t raw, uint16_t idleDeadband)
{
    if(!idleDeadband || !elrsIsValidInputAxisProfile(profile)) {
        return elrsInputModelAxisToUs(profile, raw);
    }
    ELRSInputAxisProfile adjusted = profile;
    int16_t &idle = adjusted.reverse ? adjusted.maximum : adjusted.minimum;
    int span = (int)idle - adjusted.center;
    if(span < 0) span = -span;
    uint16_t amount = (idleDeadband < span) ? idleDeadband : (uint16_t)(span - 1);
    idle = moveToward(idle, adjusted.center, amount);
    int otherSpan = (int)(adjusted.reverse ? adjusted.minimum : adjusted.maximum) - adjusted.center;
    if(otherSpan < 0) otherSpan = -otherSpan;
    int smallerSpan = (span - amount < otherSpan) ? span - amount : otherSpan;
    // A center deadband must leave a nonzero mapping segment at both endpoints.
    adjusted.deadband = (uint16_t)clampLong(adjusted.deadband, 0, smallerSpan - 1);
    return elrsInputModelAxisToUs(adjusted, raw);
}

uint16_t elrsInputUsToCrsfTicks(int16_t us)
{
    long clampedUs = clampLong(us, ELRS_INPUT_US_MIN, ELRS_INPUT_US_MAX);
    long spanUs = ELRS_INPUT_US_MAX - ELRS_INPUT_US_MIN;
    long spanTicks = ELRS_CRSF_CHANNEL_MAX - ELRS_CRSF_CHANNEL_MIN;
    long ticks = (clampedUs - ELRS_INPUT_US_MIN) * spanTicks + (spanUs / 2);

    ticks = ticks / spanUs + ELRS_CRSF_CHANNEL_MIN;
    return (uint16_t)ticks;
}

ELRSInputAxisProfile elrsDefaultInputAxisProfile()
{
    ELRSInputAxisProfile profile;

    profile.minimum = 0;
    profile.center = 1024;
    profile.maximum = 2047;
    profile.reverse = 0;
    profile.deadband = 0;
    profile.expo = 0;

    return profile;
}

ELRSGimbalRouting elrsDefaultGimbalRouting()
{
    ELRSGimbalRouting routing;

    routing.aileronChannel = 1;
    routing.elevatorChannel = 2;
    routing.throttleChannel = 3;
    routing.rudderChannel = 4;

    return routing;
}

bool elrsIsValidInputAxisProfile(const ELRSInputAxisProfile &profile)
{
    return (profile.minimum >= 0) &&
           (profile.center >= 0) &&
           (profile.maximum >= 0) &&
           (((profile.minimum < profile.center) &&
             (profile.center < profile.maximum)) ||
            ((profile.maximum < profile.center) &&
             (profile.center < profile.minimum)));
}

ELRSInputAxisProfile elrsSanitizeInputAxisProfile(const ELRSInputAxisProfile &profile)
{
    if(elrsIsValidInputAxisProfile(profile)) {
        return profile;
    }

    return elrsDefaultInputAxisProfile();
}

bool elrsIsValidGimbalRouting(const ELRSGimbalRouting &routing)
{
    return (routing.aileronChannel >= 1 && routing.aileronChannel <= 4) &&
           (routing.elevatorChannel >= 1 && routing.elevatorChannel <= 4) &&
           (routing.throttleChannel >= 1 && routing.throttleChannel <= 4) &&
           (routing.rudderChannel >= 1 && routing.rudderChannel <= 4) &&
           (routing.aileronChannel != routing.elevatorChannel) &&
           (routing.aileronChannel != routing.throttleChannel) &&
           (routing.aileronChannel != routing.rudderChannel) &&
           (routing.elevatorChannel != routing.throttleChannel) &&
           (routing.elevatorChannel != routing.rudderChannel) &&
           (routing.throttleChannel != routing.rudderChannel);
}

ELRSGimbalRouting elrsSanitizeGimbalRouting(const ELRSGimbalRouting &routing)
{
    if(elrsIsValidGimbalRouting(routing)) {
        return routing;
    }

    return elrsDefaultGimbalRouting();
}

ELRSSwitchRouting elrsDefaultSwitchRouting()
{
    ELRSSwitchRouting routing;
    for(int i = 0; i < ELRS_SWITCH_INPUT_COUNT; i++) routing.channels[i] = 5 + i;
    return routing;
}

bool elrsIsValidSwitchRouting(const ELRSSwitchRouting &routing)
{
    uint16_t used = 0;
    for(uint8_t channel : routing.channels) {
        if(channel < 5 || channel > 16) return false;
        uint16_t bit = (uint16_t)1 << (channel - 5);
        if(used & bit) return false;
        used |= bit;
    }
    return true;
}

ELRSSwitchRouting elrsSanitizeSwitchRouting(const ELRSSwitchRouting &routing)
{
    return elrsIsValidSwitchRouting(routing) ? routing : elrsDefaultSwitchRouting();
}
