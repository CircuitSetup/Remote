#ifndef _ELRS_CRSF_SHARED_H
#define _ELRS_CRSF_SHARED_H

#include <stdint.h>

/*
 * Define HAVE_CRSF in remote_global.h to enable the ELRS/CRSF integration in
 * the main firmware.
 */

#define ELRS_GIMBAL_AXIS_COUNT 4

#define ELRS_PACKET_RATE_50HZ  50
#define ELRS_PACKET_RATE_100HZ 100
#define ELRS_PACKET_RATE_150HZ 150
#define ELRS_PACKET_RATE_250HZ 250
#define ELRS_PACKET_RATE_DEFAULT ELRS_PACKET_RATE_250HZ

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

#endif
