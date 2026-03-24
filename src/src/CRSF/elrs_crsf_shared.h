#ifndef _ELRS_CRSF_SHARED_H
#define _ELRS_CRSF_SHARED_H

#include <stdint.h>

/*
 * Define HAVE_CRSF at build time to enable the ELRS/CRSF integration in the main
 * firmware. When undefined, the main program falls back to legacy-only mode.
 */

#define ELRS_GIMBAL_AXIS_COUNT 4

#define ELRS_PACKET_RATE_50HZ  50
#define ELRS_PACKET_RATE_100HZ 100
#define ELRS_PACKET_RATE_150HZ 150
#define ELRS_PACKET_RATE_250HZ 250
#define ELRS_PACKET_RATE_DEFAULT ELRS_PACKET_RATE_250HZ

#define ELRS_SPEED_UNITS_KMH 0
#define ELRS_SPEED_UNITS_MPH 1
#define ELRS_SPEED_UNITS_DEFAULT ELRS_SPEED_UNITS_KMH

struct ELRSAxisCalibrationData {
    int16_t minimum;
    int16_t center;
    int16_t maximum;
};

static inline bool elrsPacketRateSupported(uint16_t packetRateHz)
{
    return (packetRateHz == ELRS_PACKET_RATE_50HZ ||
            packetRateHz == ELRS_PACKET_RATE_100HZ ||
            packetRateHz == ELRS_PACKET_RATE_150HZ ||
            packetRateHz == ELRS_PACKET_RATE_250HZ);
}

static inline uint16_t elrsPacketRateOrDefault(uint16_t packetRateHz)
{
    return elrsPacketRateSupported(packetRateHz) ? packetRateHz : (uint16_t)ELRS_PACKET_RATE_DEFAULT;
}

static inline bool elrsSpeedUnitsSupported(uint8_t speedUnits)
{
    return (speedUnits == ELRS_SPEED_UNITS_KMH ||
            speedUnits == ELRS_SPEED_UNITS_MPH);
}

static inline uint8_t elrsSpeedUnitsOrDefault(uint8_t speedUnits)
{
    return elrsSpeedUnitsSupported(speedUnits) ? speedUnits : (uint8_t)ELRS_SPEED_UNITS_DEFAULT;
}

#endif
