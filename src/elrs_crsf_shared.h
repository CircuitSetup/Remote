#ifndef _ELRS_CRSF_SHARED_H
#define _ELRS_CRSF_SHARED_H

#include <stdint.h>

#define ELRS_GIMBAL_AXIS_COUNT 4

struct ELRSAxisCalibrationData {
    int16_t minimum;
    int16_t center;
    int16_t maximum;
};

#endif
