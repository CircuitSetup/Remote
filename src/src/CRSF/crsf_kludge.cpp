/*
 * -------------------------------------------------------------------
 * Remote Control
 * (C) 2026 Thomas Winischhofer (A10001986)
 * https://github.com/realA10001986/Remote
 * https://remote.out-a-ti.me
 *
 * CRSF kludge: Stuff that is called from the main prop firmware
 *
 * -------------------------------------------------------------------
 * License: MIT NON-AI
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the
 * Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * In addition, the following restrictions apply:
 *
 * 1. The Software and any modifications made to it may not be used
 * for the purpose of training or improving machine learning algorithms,
 * including but not limited to artificial intelligence, natural
 * language processing, or data mining. This condition applies to any
 * derivatives, modifications, or updates based on the Software code.
 * Any usage of the Software in an AI-training dataset is considered a
 * breach of this License.
 *
 * 2. The Software may not be included in any dataset used for
 * training or improving machine learning algorithms, including but
 * not limited to artificial intelligence, natural language processing,
 * or data mining.
 *
 * 3. Any person or organization found to be in violation of these
 * restrictions will be subject to legal action and may be held liable
 * for any damages resulting from such use.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "../../remote_global.h"
#ifdef HAVE_CRSF

#include <Arduino.h>
#include <math.h>
#include <LittleFS.h>
#include <SD.h>
#include "../../remote_settings.h"

#include "elrs_crsf_shared.h"
#include "elrs_crsf.h"
#include "crsf_kludge.h"
#include "crsf_settings.h"

// External
extern bool     saveConfigFile(const char *fn, uint8_t *buf, int len, int forcefs = 0);
extern uint32_t calcHash(uint8_t *buf, int len);

extern void     wifiOnFakePowerOn(bool showWait);

// CRSF settings
// Stored as a raw blob in /crsfcfg. Keep compatibility in mind when changing
// this layout because older builds may have written legacy calibration-only data.
struct [[gnu::packed]] ELRSCrsfSettingsBlob {
    ELRSInputAxisProfile axisProfile[ELRS_GIMBAL_AXIS_COUNT];
    ELRSGimbalRouting gimbalRouting;
    uint16_t adcHysteresis;
    uint16_t throttleIdleDeadband;
    ELRSSwitchRouting switchRouting;
    ELRSOutputLimits outputLimits[ELRS_GIMBAL_AXIS_COUNT];
    uint8_t localActions[ELRS_SWITCH_INPUT_COUNT];
    ELRSDisplayConfig displayConfig;
    ELRSVehicleConfig vehicleConfig;
};

static_assert(offsetof(ELRSCrsfSettingsBlob, outputLimits) == 68, "Preserve the legacy CRSF settings prefix");
static_assert(offsetof(ELRSCrsfSettingsBlob, localActions) == 84, "Preserve travel limits before local actions");
static_assert(offsetof(ELRSCrsfSettingsBlob, displayConfig) == 96, "Preserve the existing CRSF settings prefix");
static_assert(offsetof(ELRSCrsfSettingsBlob, vehicleConfig) == 106, "Preserve the complete display settings tail");
static_assert(sizeof(ELRSCrsfSettingsBlob) == 119, "CRSF settings include the complete vehicle tail");

struct [[gnu::packed]] ELRSCrsfLegacySettingsBlob {
    ELRSAxisCalibrationData elrsAxis[ELRS_GIMBAL_AXIS_COUNT];
};

static ELRSCrsfSettingsBlob defaultCrsfSettings()
{
    ELRSCrsfSettingsBlob settings = {};
    const ELRSInputAxisProfile defaultProfile = elrsDefaultInputAxisProfile();

    for(int i = 0; i < ELRS_GIMBAL_AXIS_COUNT; i++) {
        settings.axisProfile[i] = defaultProfile;
        settings.outputLimits[i] = elrsDefaultOutputLimits();
    }
    settings.gimbalRouting = elrsDefaultGimbalRouting();
    settings.adcHysteresis = ELRS_INPUT_TOLERANCE_DEFAULT;
    settings.throttleIdleDeadband = ELRS_INPUT_TOLERANCE_DEFAULT;
    settings.switchRouting = elrsDefaultSwitchRouting();
    settings.displayConfig = elrsDefaultDisplayConfig();
    settings.vehicleConfig = elrsDefaultVehicleConfig();

    return settings;
}

static ELRSAxisCalibrationData profileToCalibration(const ELRSInputAxisProfile &profile)
{
    ELRSAxisCalibrationData calibration;

    calibration.minimum = profile.minimum;
    calibration.center = profile.center;
    calibration.maximum = profile.maximum;

    return calibration;
}

static ELRSInputAxisProfile calibrationToProfile(const ELRSAxisCalibrationData &calibration)
{
    ELRSInputAxisProfile profile = elrsDefaultInputAxisProfile();

    profile.minimum = calibration.minimum;
    profile.center = calibration.center;
    profile.maximum = calibration.maximum;

    return profile;
}

static void sanitizeCrsfSettings(ELRSCrsfSettingsBlob &settings)
{
    if(!elrsIsValidDisplayConfig(settings.displayConfig)) settings.displayConfig = elrsDefaultDisplayConfig();
    if(!elrsIsValidVehicleConfig(settings.vehicleConfig)) settings.vehicleConfig = elrsDefaultVehicleConfig();
    for(int i = 0; i < ELRS_GIMBAL_AXIS_COUNT; i++) {
        settings.axisProfile[i] = elrsSanitizeInputAxisProfile(settings.axisProfile[i]);
        settings.outputLimits[i] = elrsSanitizeOutputLimits(settings.outputLimits[i]);
    }
    elrsSanitizeInputRouting(settings.gimbalRouting, settings.switchRouting);
    for(uint8_t &action : settings.localActions) {
        if(action > 1) action = 0;
    }
    if(settings.adcHysteresis > ELRS_INPUT_TOLERANCE_MAX) settings.adcHysteresis = ELRS_INPUT_TOLERANCE_MAX;
    if(settings.throttleIdleDeadband > ELRS_INPUT_TOLERANCE_MAX) settings.throttleIdleDeadband = ELRS_INPUT_TOLERANCE_MAX;
}

static int clampProfileCount(int count)
{
    if(count < 0) {
        return 0;
    }

    return min(count, ELRS_GIMBAL_AXIS_COUNT);
}

static ELRSCrsfSettingsBlob crsfSettings = defaultCrsfSettings();

static int      crsfSetValidBytes = 0;
static uint32_t crsfSettingsHash  = 0;
static bool     haveCRSFSettings  = false;

static const char *crsfCfgName  = "/crsfcfg";
static const char *crsfTmpName  = "/crsfcfg.tmp";
static const char *crsfBackupName = "/crsfcfg.bak";

static bool crsfReadSettingsFile(fs::FS &storage, const char *path, uint8_t *buf, int &validBytes)
{
    validBytes = 0;
    File file = storage.open(path, FILE_READ);
    if(!file) return false;
    const size_t size = file.size();
    // Existing binary framing: uint16_t payload length, payload, checksum.
    uint8_t *data = size >= 3 && size <= 65538 ? (uint8_t *)malloc(size) : NULL;
    bool ret = data && file.read(data, size) == size;
    file.close();
    if(ret) {
        const int payloadBytes = data[0] | (data[1] << 8);
        uint16_t checksum = 0;
        for(size_t i = 0; i < size - 1; i++) checksum += data[i];
        checksum = (checksum >> 8) + (checksum & 0xff);
        checksum += checksum >> 8;
        ret = payloadBytes == size - 3 && data[size - 1] == (uint8_t)~checksum;
        if(ret) {
            validBytes = payloadBytes;
            if(buf) memcpy(buf, data + 2, min((int)sizeof(ELRSCrsfSettingsBlob), validBytes));
        }
    }
    if(data) free(data);
    return ret;
}

static bool crsfLoadStoredSettings(uint8_t *buf, int &validBytes)
{
    validBytes = 0;
    const bool fromSD = haveSD && (settings.CfgOnSD[0] != '0' || FlashROMode);
    for(int medium = 0; medium < 2; medium++) {
        if(medium ? !haveFS : !fromSD) continue;
        fs::FS &storage = medium ? static_cast<fs::FS &>(LittleFS) : static_cast<fs::FS &>(SD);
        if(crsfReadSettingsFile(storage, crsfCfgName, buf, validBytes) ||
           crsfReadSettingsFile(storage, crsfBackupName, buf, validBytes)) return true;
    }
    return false;
}

static bool crsfSaveStoredSettings(uint8_t *buf, int len)
{
    // The shared writer's medium is private and may change during moveSettings.
    // An unused stage identifies its target without modifying the other medium.
    char temporary[32];
    snprintf(temporary, sizeof(temporary), "%s", crsfTmpName);
    for(unsigned int suffix = 0; (haveSD && SD.exists(temporary)) ||
                                (haveFS && LittleFS.exists(temporary)); suffix++) {
        snprintf(temporary, sizeof(temporary), "%s.%u", crsfTmpName, suffix);
    }
    // Reuse the existing binary writer, but its truncating FILE_WRITE targets only the stage.
    bool ret = saveConfigFile(temporary, buf, len, FlashROMode ? 1 : 0);
    const bool toSD = haveSD && SD.exists(temporary);
    fs::FS &storage = toSD ? static_cast<fs::FS &>(SD) : static_cast<fs::FS &>(LittleFS);
    // A buffered write can succeed even if close fails to flush the complete file.
    uint8_t verified[sizeof(ELRSCrsfSettingsBlob)];
    int validBytes;
    ret = ret && len >= 0 && len <= (int)sizeof(verified) &&
          crsfReadSettingsFile(storage, temporary, verified, validBytes) &&
          validBytes == len && !memcmp(verified, buf, len);
    if(ret) {
        bool hadOriginal = storage.exists(crsfCfgName);
        if(hadOriginal && storage.exists(crsfBackupName)) {
            int validBytes;
            if(crsfReadSettingsFile(storage, crsfCfgName, NULL, validBytes)) {
                ret = storage.remove(crsfBackupName);
            } else if(crsfReadSettingsFile(storage, crsfBackupName, NULL, validBytes)) {
                // A failed repair must preserve the valid backup, not replace it with corrupt data.
                ret = storage.remove(crsfCfgName);
                hadOriginal = false;
            } else {
                // Neither copy is valid; allow the verified stage to repair them.
                ret = storage.remove(crsfBackupName);
            }
        }
        // SD cannot rename over an existing file; retain a recovery copy across both renames.
        if(ret && hadOriginal) ret = storage.rename(crsfCfgName, crsfBackupName);
        if(ret) {
            ret = storage.rename(temporary, crsfCfgName);
            if(ret) storage.remove(crsfBackupName);
            else if(hadOriginal) storage.rename(crsfBackupName, crsfCfgName);
        }
    }
    if(toSD || (haveFS && !FlashROMode)) storage.remove(temporary);
    return ret;
}

static const uint16_t packetRates[5] = {
    ELRS_PACKET_RATE_50HZ,
    ELRS_PACKET_RATE_100HZ,
    ELRS_PACKET_RATE_150HZ,
    ELRS_PACKET_RATE_250HZ,
    ELRS_PACKET_RATE_500HZ
};

static const uint8_t speedUnits[2] = {
    ELRS_SPEED_UNITS_KMH,
    ELRS_SPEED_UNITS_MPH
};

static const uint8_t telemetryRatios[7] = {
    ELRS_TLM_RATIO_STD,
    ELRS_TLM_RATIO_1_2,
    ELRS_TLM_RATIO_1_4,
    ELRS_TLM_RATIO_1_8,
    ELRS_TLM_RATIO_1_16,
    ELRS_TLM_RATIO_1_32,
    ELRS_TLM_RATIO_OFF
};

static const uint8_t maxPowers[6] = {
    ELRS_MAX_POWER_10MW,
    ELRS_MAX_POWER_25MW,
    ELRS_MAX_POWER_100MW,
    ELRS_MAX_POWER_250MW,
    ELRS_MAX_POWER_500MW,
    ELRS_MAX_POWER_1000MW
};

static const uint8_t dynamicPowers[2] = {
    ELRS_DYNAMIC_POWER_OFF,
    ELRS_DYNAMIC_POWER_DYNAMIC
};

uint16_t crsf_getPacketRate(int idx)
{
    if(idx < 0 || idx > 4) idx = 3;
    return packetRates[idx];
}

uint8_t crsf_getSpeedUnits(int idx)
{
    if(idx < 0 || idx > 1) idx = 0;
    return speedUnits[idx];
}

uint8_t crsf_getTelemetryRatio(int idx)
{
    if(idx < 0 || idx > 6) idx = 0;
    return telemetryRatios[idx];
}

uint8_t crsf_getMaxPower(int idx)
{
    if(idx < 0 || idx > 5) idx = 3;
    return maxPowers[idx];
}

uint8_t crsf_getDynamicPower(int idx)
{
    if(idx < 0 || idx > 1) idx = 0;
    return dynamicPowers[idx];
}

void crsf_load_settings()
{
    uint8_t rawSettings[sizeof(crsfSettings)] = { 0 };

    crsfSettings = defaultCrsfSettings();
    if(crsfLoadStoredSettings(rawSettings, crsfSetValidBytes)) {
        if(crsfSetValidBytes <= (int)sizeof(ELRSCrsfLegacySettingsBlob)) {
            ELRSCrsfLegacySettingsBlob legacySettings = {};
            int legacyAxisCount;

            memcpy(&legacySettings, rawSettings, min(crsfSetValidBytes, (int)sizeof(legacySettings)));
            legacyAxisCount = clampProfileCount(crsfSetValidBytes / (int)sizeof(ELRSAxisCalibrationData));
            for(int i = 0; i < legacyAxisCount; i++) {
                crsfSettings.axisProfile[i] = calibrationToProfile(legacySettings.elrsAxis[i]);
            }
        } else {
            int bytes = min(crsfSetValidBytes, (int)sizeof(crsfSettings));
            const int toleranceOffset = offsetof(ELRSCrsfSettingsBlob, adcHysteresis);
            const int switchOffset = offsetof(ELRSCrsfSettingsBlob, switchRouting);
            const int limitsOffset = offsetof(ELRSCrsfSettingsBlob, outputLimits);
            const int localOffset = offsetof(ELRSCrsfSettingsBlob, localActions);
            const int displayOffset = offsetof(ELRSCrsfSettingsBlob, displayConfig);
            const int vehicleOffset = offsetof(ELRSCrsfSettingsBlob, vehicleConfig);
            if(bytes > vehicleOffset && bytes < (int)sizeof(crsfSettings)) bytes = vehicleOffset;
            if(bytes > displayOffset && bytes < vehicleOffset) bytes = displayOffset;
            // A partial permutation cannot safely replace the complete default map.
            if(bytes > switchOffset && bytes < limitsOffset) bytes = switchOffset;
            // A partial endpoint pair retains both default bounds for that axis.
            if(bytes > limitsOffset && bytes < localOffset) bytes = limitsOffset + ((bytes - limitsOffset) / (int)sizeof(ELRSOutputLimits)) * sizeof(ELRSOutputLimits);
            // Optional tolerances use defaults until a complete uint16_t is present.
            if(bytes > toleranceOffset && bytes < switchOffset) bytes = toleranceOffset + ((bytes - toleranceOffset) / 2) * 2;
            memcpy(&crsfSettings, rawSettings, bytes);
        }
        sanitizeCrsfSettings(crsfSettings);
        crsfSettingsHash = calcHash((uint8_t *)&crsfSettings, sizeof(crsfSettings));
        haveCRSFSettings = true;
    }
}

bool crsf_save_settings(bool useCache)
{
    uint32_t newHash = calcHash((uint8_t *)&crsfSettings, sizeof(crsfSettings));
    if(useCache && newHash == crsfSettingsHash) return true;
    if(!crsfSaveStoredSettings((uint8_t *)&crsfSettings, sizeof(crsfSettings))) return false;
    crsfSettingsHash = newHash;
    return true;
}

void loadELRSCalibration(ELRSAxisCalibrationData *cal, int count)
{
    if(!cal) {
        return;
    }

    count = clampProfileCount(count);
    for(int i = 0; i < count; i++) {
        cal[i] = profileToCalibration(crsfSettings.axisProfile[i]);
    }
}

bool saveELRSCalibration(const ELRSAxisCalibrationData *cal, int count)
{
    if(!cal) {
        return false;
    }

    ELRSCrsfSettingsBlob previous = crsfSettings;
    count = clampProfileCount(count);
    for(int i = 0; i < count; i++) {
        crsfSettings.axisProfile[i].minimum = cal[i].minimum;
        crsfSettings.axisProfile[i].center = cal[i].center;
        crsfSettings.axisProfile[i].maximum = cal[i].maximum;
    }
    sanitizeCrsfSettings(crsfSettings);
    if(crsf_save_settings(true)) return true;
    crsfSettings = previous;
    return false;
}

void loadELRSInputProfiles(ELRSInputAxisProfile *profiles, int count)
{
    if(!profiles) {
        return;
    }

    count = clampProfileCount(count);
    for(int i = 0; i < count; i++) {
        profiles[i] = crsfSettings.axisProfile[i];
    }
}

void saveELRSInputProfiles(const ELRSInputAxisProfile *profiles, int count)
{
    if(profiles) saveELRSInputConfig(profiles, count);
}

ELRSGimbalRouting loadELRSGimbalRouting()
{
    return crsfSettings.gimbalRouting;
}

ELRSDisplayConfig loadELRSDisplayConfig()
{
    return crsfSettings.displayConfig;
}

ELRSVehicleConfig loadELRSVehicleConfig()
{
    return crsfSettings.vehicleConfig;
}

void loadELRSInputConfig(ELRSInputAxisProfile *profiles, int count, ELRSGimbalRouting *routing,
                         uint16_t *adcHysteresis, uint16_t *throttleIdleDeadband,
                         ELRSSwitchRouting *switchRouting, ELRSOutputLimits *outputLimits, uint8_t *localActions)
{
    if(profiles) {
        loadELRSInputProfiles(profiles, count);
    }

    if(routing) {
        *routing = crsfSettings.gimbalRouting;
    }
    if(adcHysteresis) *adcHysteresis = crsfSettings.adcHysteresis;
    if(throttleIdleDeadband) *throttleIdleDeadband = crsfSettings.throttleIdleDeadband;
    if(switchRouting) *switchRouting = crsfSettings.switchRouting;
    if(outputLimits) memcpy(outputLimits, crsfSettings.outputLimits, sizeof(crsfSettings.outputLimits));
    if(localActions) memcpy(localActions, crsfSettings.localActions, sizeof(crsfSettings.localActions));
}

void saveELRSGimbalRouting(const ELRSGimbalRouting &routing)
{
    saveELRSInputConfig(nullptr, 0, &routing);
}

bool saveELRSInputConfig(const ELRSInputAxisProfile *profiles, int count, const ELRSGimbalRouting *routing,
                         const uint16_t *adcHysteresis, const uint16_t *throttleIdleDeadband,
                         const ELRSSwitchRouting *switchRouting, const ELRSOutputLimits *outputLimits, const uint8_t *localActions,
                         const ELRSDisplayConfig *displayConfig, const ELRSVehicleConfig *vehicleConfig)
{
    if(displayConfig && !elrsIsValidDisplayConfig(*displayConfig)) return false;
    if(vehicleConfig && !elrsIsValidVehicleConfig(*vehicleConfig)) return false;
    count = clampProfileCount(count);
    if((adcHysteresis && *adcHysteresis > ELRS_INPUT_TOLERANCE_MAX) ||
       (throttleIdleDeadband && *throttleIdleDeadband > ELRS_INPUT_TOLERANCE_MAX)) return false;
    if(profiles) {
        for(int i = 0; i < count; i++) {
            if(!elrsIsValidInputAxisProfile(profiles[i])) return false;
        }
    }
    if(!elrsIsValidInputRouting(routing ? *routing : crsfSettings.gimbalRouting,
                               switchRouting ? *switchRouting : crsfSettings.switchRouting)) return false;
    if(outputLimits) {
        for(int i = 0; i < ELRS_GIMBAL_AXIS_COUNT; i++) {
            if(!elrsIsValidOutputLimits(outputLimits[i])) return false;
        }
    }
    if(localActions) {
        for(int i = 0; i < ELRS_SWITCH_INPUT_COUNT; i++) {
            if(localActions[i] > 1) return false;
        }
    }
    const ELRSCrsfSettingsBlob previous = crsfSettings;
    const uint32_t previousHash = crsfSettingsHash;
    if(profiles) {
        for(int i = 0; i < count; i++) {
            crsfSettings.axisProfile[i] = profiles[i];
        }
    }

    if(routing) {
        crsfSettings.gimbalRouting = *routing;
    }
    if(adcHysteresis) crsfSettings.adcHysteresis = *adcHysteresis;
    if(throttleIdleDeadband) crsfSettings.throttleIdleDeadband = *throttleIdleDeadband;
    if(switchRouting) crsfSettings.switchRouting = *switchRouting;
    if(outputLimits) memcpy(crsfSettings.outputLimits, outputLimits, sizeof(crsfSettings.outputLimits));
    if(localActions) memcpy(crsfSettings.localActions, localActions, sizeof(crsfSettings.localActions));
    if(displayConfig) crsfSettings.displayConfig = *displayConfig;
    if(vehicleConfig) crsfSettings.vehicleConfig = *vehicleConfig;

    sanitizeCrsfSettings(crsfSettings);
    if(crsf_save_settings(true)) return true;
    crsfSettings = previous;
    crsfSettingsHash = previousHash;
    return false;
}

bool readELRSCurrentRawAxes(int16_t axes[ELRS_GIMBAL_AXIS_COUNT])
{
    return elrsMode.readCurrentRawAxes(axes);
}

void requestELRSModuleConfigUpdate(uint8_t telemetryRatio, uint8_t maxPower, uint8_t dynamicPower)
{
    elrsMode.requestModuleConfigUpdate(telemetryRatio, maxPower, dynamicPower);
}

bool crsf_begin(
            uint16_t packetRateHz,
            uint8_t speedDisplayUnits,
            uint8_t telemetryRatio,
            uint8_t maxPower,
            uint8_t dynamicPower,
            ButtonPack *buttonPack,
            bool haveButtonPack,
            remDisplay *remdisplay,
            remLED *pwrled,
            remLED *bLvLMeter,
            remLED *remledStop,
            bool usePowerLed,
            bool useLevelMeter,
            bool powerLedOnFakePower,
            bool levelMeterOnFakePower,
            void (*fpOnWifiHandler)(bool))
{
    ELRSInputAxisProfile axisProfiles[ELRS_GIMBAL_AXIS_COUNT];
    ELRSGimbalRouting inputRouting;
    ELRSSwitchRouting switchRouting;
    ELRSOutputLimits outputLimits[ELRS_GIMBAL_AXIS_COUNT];
    uint8_t localActions[ELRS_SWITCH_INPUT_COUNT];
    uint16_t adcHysteresis, throttleIdleDeadband;

    loadELRSInputConfig(axisProfiles, ELRS_GIMBAL_AXIS_COUNT, &inputRouting, &adcHysteresis, &throttleIdleDeadband, &switchRouting, outputLimits, localActions);
    const ELRSDisplayConfig displayConfig = loadELRSDisplayConfig();
    const ELRSVehicleConfig vehicleConfig = loadELRSVehicleConfig();

    return elrsMode.begin(
            packetRateHz,
            speedDisplayUnits,
            telemetryRatio,
            maxPower,
            dynamicPower,
            axisProfiles,
            inputRouting,
            buttonPack,
            haveButtonPack,
            remdisplay,
            pwrled,
            bLvLMeter,
            remledStop,
            usePowerLed,
            useLevelMeter,
            powerLedOnFakePower,
            levelMeterOnFakePower,
            fpOnWifiHandler,
            adcHysteresis,
            throttleIdleDeadband,
            &switchRouting,
            outputLimits,
            localActions,
            settings.opMode[0] == '2',
            &displayConfig,
            &vehicleConfig
        );
}

void crsf_loop(int battWarn)
{
    elrsMode.loop(battWarn);
}

void csrf_query_status(bool &FPBUnitIsOn)
{
    FPBUnitIsOn = elrsMode.fakePowerOn();
}

#endif
