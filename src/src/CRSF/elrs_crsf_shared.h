#ifndef _ELRS_CRSF_SHARED_H
#define _ELRS_CRSF_SHARED_H

#include <stddef.h>
#include <stdint.h>
#include <cmath>

/*
 * Define HAVE_CRSF at build time to enable the ELRS/CRSF integration in the main
 * firmware. When undefined, the main program falls back to legacy-only mode.
 */

#define ELRS_GIMBAL_AXIS_COUNT 4

#define ELRS_INPUT_US_MIN 1000
#define ELRS_INPUT_US_MID 1500
#define ELRS_INPUT_US_MAX 2000

#define ELRS_CRSF_CHANNEL_MIN 172
#define ELRS_CRSF_CHANNEL_MID 992
#define ELRS_CRSF_CHANNEL_MAX 1811

#define ELRS_PACKET_RATE_50HZ  50
#define ELRS_PACKET_RATE_100HZ 100
#define ELRS_PACKET_RATE_150HZ 150
#define ELRS_PACKET_RATE_250HZ 250
#define ELRS_PACKET_RATE_500HZ 500
#define ELRS_PACKET_RATE_DEFAULT ELRS_PACKET_RATE_250HZ

#define ELRS_SPEED_UNITS_KMH 0
#define ELRS_SPEED_UNITS_MPH 1
#define ELRS_SPEED_UNITS_DEFAULT ELRS_SPEED_UNITS_KMH

#define ELRS_TLM_RATIO_STD   0
#define ELRS_TLM_RATIO_1_2   1
#define ELRS_TLM_RATIO_1_4   2
#define ELRS_TLM_RATIO_1_8   3
#define ELRS_TLM_RATIO_1_16  4
#define ELRS_TLM_RATIO_1_32  5
#define ELRS_TLM_RATIO_OFF   6
#define ELRS_TLM_RATIO_DEFAULT ELRS_TLM_RATIO_STD

#define ELRS_MAX_POWER_10MW    0
#define ELRS_MAX_POWER_25MW    1
#define ELRS_MAX_POWER_100MW   2
#define ELRS_MAX_POWER_250MW   3
#define ELRS_MAX_POWER_500MW   4
#define ELRS_MAX_POWER_1000MW  5
#define ELRS_MAX_POWER_DEFAULT ELRS_MAX_POWER_250MW

#define ELRS_DYNAMIC_POWER_OFF     0
#define ELRS_DYNAMIC_POWER_DYNAMIC 1
#define ELRS_DYNAMIC_POWER_DEFAULT ELRS_DYNAMIC_POWER_OFF

struct ELRSAxisCalibrationData {
    int16_t minimum;
    int16_t center;
    int16_t maximum;
};

enum ELRSTelemetrySource : uint8_t {
    ELRS_DISPLAY_AUTO, ELRS_DISPLAY_GPS_SPEED, ELRS_DISPLAY_AIRSPEED,
    ELRS_DISPLAY_BATTERY_VOLTAGE, ELRS_DISPLAY_BATTERY_CURRENT,
    ELRS_DISPLAY_BATTERY_PERCENT, ELRS_DISPLAY_CAPACITY, ELRS_DISPLAY_GPS_ALTITUDE,
    ELRS_DISPLAY_GPS_HEADING, ELRS_DISPLAY_SATELLITES, ELRS_DISPLAY_LINK_QUALITY,
    ELRS_DISPLAY_RSSI, ELRS_DISPLAY_SNR, ELRS_DISPLAY_OFF, ELRS_DISPLAY_NONE,
    ELRS_DISPLAY_RPM_ACTUAL_MPH, ELRS_DISPLAY_RPM_SCALED_MPH
};

struct ELRSTelemetrySample {
    uint8_t source;
    float value;
    bool received;
    bool available;
    uint32_t ageMs;
};

#pragma pack(push, 1)
struct ELRSDisplayConfig {
    uint8_t source;
    uint8_t decimalPlaces;
    float multiplier;
    float offset;
};

enum ELRSRpmType : uint8_t { ELRS_RPM_ELECTRICAL, ELRS_RPM_SHAFT };

struct ELRSVehicleConfig {
    uint8_t motorPoles;
    float gearRatio;
    float tireDiameterMm;
    float scaleFactor;
    uint8_t rpmType;
};
#pragma pack(pop)
static_assert(sizeof(ELRSDisplayConfig) == 10, "Display settings must retain their stored layout");
static_assert(sizeof(ELRSVehicleConfig) == 14, "Vehicle settings append the RPM type");

static inline ELRSVehicleConfig elrsDefaultVehicleConfig()
{
    return {4, 6.55f, 64.0f, 10.0f, ELRS_RPM_ELECTRICAL};
}

static inline bool elrsIsValidVehicleConfig(const ELRSVehicleConfig &config)
{
    return config.rpmType <= ELRS_RPM_SHAFT &&
        config.motorPoles >= 2 && config.motorPoles <= 64 && config.motorPoles % 2 == 0 &&
        std::isfinite(config.gearRatio) && config.gearRatio >= 0.01f && config.gearRatio <= 1000 &&
        std::isfinite(config.tireDiameterMm) && config.tireDiameterMm >= 1 && config.tireDiameterMm <= 1000 &&
        std::isfinite(config.scaleFactor) && config.scaleFactor >= 0.01f && config.scaleFactor <= 1000;
}

static inline ELRSDisplayConfig elrsDefaultDisplayConfig()
{
    return {ELRS_DISPLAY_NONE, 255, 1.0f, 0.0f};
}

static inline bool elrsIsValidDisplayConfig(const ELRSDisplayConfig &config)
{
    return config.source <= ELRS_DISPLAY_RPM_SCALED_MPH &&
        (config.decimalPlaces <= 2 || config.decimalPlaces == 255) &&
        std::isfinite(config.multiplier) && config.multiplier >= -1000 && config.multiplier <= 1000 &&
        std::isfinite(config.offset) && config.offset >= -999 && config.offset <= 999;
}

static inline ELRSDisplayConfig elrsEffectiveDisplayConfig(const ELRSDisplayConfig &config, bool propControls)
{
    ELRSDisplayConfig effective = elrsIsValidDisplayConfig(config) ? config : elrsDefaultDisplayConfig();
    if(effective.source == ELRS_DISPLAY_NONE && !propControls) effective.source = ELRS_DISPLAY_AUTO;
    return effective;
}

void elrsFormatTelemetry(const ELRSDisplayConfig &config, const ELRSTelemetrySample &sample,
                         uint8_t speedUnits, char text[8]);

static inline const char *elrsTelemetrySourceLabel(uint8_t source)
{
    static const char * const labels[] = {
        "Auto", "GPS speed", "Airspeed", "Vehicle battery voltage", "Vehicle battery current",
        "Vehicle battery remaining", "Consumed capacity", "GPS altitude", "GPS heading",
        "GPS satellites", "Receiver uplink LQ", "Receiver RSSI (antenna 1)", "Receiver SNR", "Off", "None (normal display)",
        "Motor RPM: Actual mph", "Motor RPM: Scaled mph"
    };
    return source <= ELRS_DISPLAY_RPM_SCALED_MPH ? labels[source] : "Unknown";
}

static inline const char *elrsTelemetrySourceUnit(uint8_t source, uint8_t speedUnits)
{
    switch(source) {
    case ELRS_DISPLAY_RPM_ACTUAL_MPH: case ELRS_DISPLAY_RPM_SCALED_MPH: return "mph";
    case ELRS_DISPLAY_GPS_SPEED: case ELRS_DISPLAY_AIRSPEED: return speedUnits == ELRS_SPEED_UNITS_MPH ? "mph" : "km/h";
    case ELRS_DISPLAY_BATTERY_VOLTAGE: return "V";
    case ELRS_DISPLAY_BATTERY_CURRENT: return "A";
    case ELRS_DISPLAY_BATTERY_PERCENT: case ELRS_DISPLAY_LINK_QUALITY: return "%";
    case ELRS_DISPLAY_CAPACITY: return "mAh";
    case ELRS_DISPLAY_GPS_ALTITUDE: return "m";
    case ELRS_DISPLAY_GPS_HEADING: return "deg";
    case ELRS_DISPLAY_RSSI: return "dBm";
    case ELRS_DISPLAY_SNR: return "dB";
    default: return "";
    }
}

static inline uint8_t elrsTelemetrySourceDecimals(uint8_t source)
{
    return (source >= ELRS_DISPLAY_GPS_SPEED && source <= ELRS_DISPLAY_BATTERY_CURRENT) ||
        source == ELRS_DISPLAY_RPM_ACTUAL_MPH || source == ELRS_DISPLAY_RPM_SCALED_MPH ? 1 : 0;
}

static inline bool elrsPacketRateSupported(uint16_t packetRateHz)
{
    return (packetRateHz == ELRS_PACKET_RATE_50HZ ||
            packetRateHz == ELRS_PACKET_RATE_100HZ ||
            packetRateHz == ELRS_PACKET_RATE_150HZ ||
            packetRateHz == ELRS_PACKET_RATE_250HZ ||
            packetRateHz == ELRS_PACKET_RATE_500HZ);
}

static inline uint16_t elrsPacketRateOrDefault(uint16_t packetRateHz)
{
    return elrsPacketRateSupported(packetRateHz) ? packetRateHz : (uint16_t)ELRS_PACKET_RATE_DEFAULT;
}

static inline uint16_t elrsCrsfModuleReplyTimeoutMs(uint16_t packetRateHz)
{
    packetRateHz = elrsPacketRateOrDefault(packetRateHz);
    return (packetRateHz >= ELRS_PACKET_RATE_500HZ) ? 50U : 20U;
}

static inline uint32_t elrsCrsfRecommendedBaudRate(uint16_t packetRateHz)
{
    packetRateHz = elrsPacketRateOrDefault(packetRateHz);
    return (packetRateHz >= ELRS_PACKET_RATE_500HZ) ? 921600UL : 400000UL;
}

static inline uint16_t elrsCrsfDriverEnableSetupUs()
{
    return 40U;
}

static inline uint16_t elrsCrsfDriverDisableHoldUs()
{
    return 40U;
}

static inline uint16_t elrsCrsfDriverReleaseGuardUs()
{
    return 150U;
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

static inline bool elrsTelemetryRatioSupported(uint8_t telemetryRatio)
{
    return (telemetryRatio <= ELRS_TLM_RATIO_OFF);
}

static inline uint8_t elrsTelemetryRatioOrDefault(uint8_t telemetryRatio)
{
    return elrsTelemetryRatioSupported(telemetryRatio) ? telemetryRatio : (uint8_t)ELRS_TLM_RATIO_DEFAULT;
}

static inline const char *elrsTelemetryRatioLabel(uint8_t telemetryRatio)
{
    switch(elrsTelemetryRatioOrDefault(telemetryRatio)) {
    case ELRS_TLM_RATIO_STD:  return "Std";
    case ELRS_TLM_RATIO_1_2:  return "1:2";
    case ELRS_TLM_RATIO_1_4:  return "1:4";
    case ELRS_TLM_RATIO_1_8:  return "1:8";
    case ELRS_TLM_RATIO_1_16: return "1:16";
    case ELRS_TLM_RATIO_1_32: return "1:32";
    default:                  return "Off";
    }
}

static inline bool elrsMaxPowerSupported(uint8_t maxPower)
{
    return (maxPower <= ELRS_MAX_POWER_1000MW);
}

static inline uint8_t elrsMaxPowerOrDefault(uint8_t maxPower)
{
    return elrsMaxPowerSupported(maxPower) ? maxPower : (uint8_t)ELRS_MAX_POWER_DEFAULT;
}

static inline uint16_t elrsMaxPowerMilliwatts(uint8_t maxPower)
{
    switch(elrsMaxPowerOrDefault(maxPower)) {
    case ELRS_MAX_POWER_10MW:   return 10;
    case ELRS_MAX_POWER_25MW:   return 25;
    case ELRS_MAX_POWER_100MW:  return 100;
    case ELRS_MAX_POWER_250MW:  return 250;
    case ELRS_MAX_POWER_500MW:  return 500;
    default:                    return 1000;
    }
}

static inline bool elrsDynamicPowerSupported(uint8_t dynamicPower)
{
    return (dynamicPower == ELRS_DYNAMIC_POWER_OFF ||
            dynamicPower == ELRS_DYNAMIC_POWER_DYNAMIC);
}

static inline uint8_t elrsDynamicPowerOrDefault(uint8_t dynamicPower)
{
    return elrsDynamicPowerSupported(dynamicPower) ? dynamicPower : (uint8_t)ELRS_DYNAMIC_POWER_DEFAULT;
}

static inline const char *elrsDynamicPowerLabel(uint8_t dynamicPower)
{
    return (elrsDynamicPowerOrDefault(dynamicPower) == ELRS_DYNAMIC_POWER_DYNAMIC) ? "Dyn" : "Off";
}

static inline uint8_t elrsAds1015SingleEndedConfigHighByte(uint8_t channel)
{
    static const uint8_t muxBits[ELRS_GIMBAL_AXIS_COUNT] = { 0x04, 0x05, 0x06, 0x07 };
    const uint8_t adsPga4v096 = 0x02;

    if(channel >= ELRS_GIMBAL_AXIS_COUNT) {
        channel = 0;
    }

    return (uint8_t)(0x80 | (muxBits[channel] << 4) | adsPga4v096 | 0x01);
}

static inline bool elrsAxesChanged(const int16_t *current, const int16_t *previous, size_t count, int16_t threshold = 0)
{
    if(!current || !previous) {
        return false;
    }

    for(size_t i = 0; i < count; i++) {
        int16_t delta = (int16_t)(current[i] - previous[i]);
        if(delta < 0) {
            delta = (int16_t)(-delta);
        }
        if(delta > threshold) {
            return true;
        }
    }

    return false;
}

static inline int16_t elrsIirFilterStep(int16_t previous, int16_t sample, uint8_t shift = 2)
{
    int16_t delta;
    int16_t divisor;

    if(!shift) {
        return sample;
    }

    delta = (int16_t)(sample - previous);
    divisor = (int16_t)(1U << shift);

    int16_t step = delta / divisor;
    // Steady samples must converge; the core applies the configured jitter hold.
    if(!step && delta) step = (delta > 0) ? 1 : -1;
    return (int16_t)(previous + step);
}

#endif
