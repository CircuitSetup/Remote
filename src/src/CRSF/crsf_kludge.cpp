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
#include "crsf_model_files.h"

// External
extern bool     saveConfigFile(const char *fn, uint8_t *buf, int len, int forcefs = 0);
extern uint32_t calcHash(uint8_t *buf, int len);

extern void     wifiOnFakePowerOn(bool showWait);

// CRSF settings
// Stored as one complete model and portal-preferences record in /crsfcfg.

ELRSRadioPreferences remoteRadioPreferences;
bool crsfSettingsSaving=false,crsfSettingsRecoveryBlocked=false;
static ELRSPortalPreferences crsfPortalPreferences;
struct ELRSStoredSettings {ELRSCrsfSettingsBlob model;ELRSPortalPreferences portal;};
static_assert(sizeof(ELRSStoredSettings)==sizeof(ELRSCrsfSettingsBlob)+sizeof(ELRSPortalPreferences),"No padding in CRSF settings storage");
static bool validPortalPreferences(const ELRSPortalPreferences &p) {
    return p.version==1 && p.opMode<=2 && p.connectWiFi<=1 && p.cadence<=4 && p.speedUnits<=1 && p.radioReview<=1 && elrsIsValidRadioPreferences(p.radio);
}
ELRSPortalPreferences snapshotELRSPortalPreferences() {
    ELRSPortalPreferences p;p.version=1;p.opMode=atoi(settings.opMode);p.connectWiFi=settings.crsfap[0]!='0';
    p.cadence=atoi(settings.elrsPktRate);p.speedUnits=atoi(settings.elrsSpdUnit);p.radioReview=crsfPortalPreferences.radioReview;
    p.radio=remoteRadioPreferences;return p;
}
static void installPortalPreferences(const ELRSPortalPreferences &p) {
    crsfPortalPreferences=p;remoteRadioPreferences=p.radio;
    settings.opMode[0]='0'+p.opMode;settings.opMode[1]=0;settings.crsfap[0]=p.connectWiFi?'1':'0';settings.crsfap[1]=0;
    settings.elrsPktRate[0]='0'+p.cadence;settings.elrsPktRate[1]=0;settings.elrsSpdUnit[0]='0'+p.speedUnits;settings.elrsSpdUnit[1]=0;
}

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
    settings.logicVersion = 2;

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

static void sanitizeCrsfSettings(ELRSCrsfSettingsBlob &settings)
{
    if(settings.logicVersion != 2 || !elrsIsValidLogicConfig(settings.logic) || !elrsIsValidMixConfig(settings.mixes)) {
        settings.logic = {};
        settings.mixes = {};
    }
    settings.logicVersion = 2;
    if(!elrsIsValidDisplayConfig(settings.displayConfig)) settings.displayConfig = elrsDefaultDisplayConfig();
    if(!elrsIsValidVehicleConfig(settings.vehicleConfig)) settings.vehicleConfig = elrsDefaultVehicleConfig();
    for(int i = 0; i < ELRS_GIMBAL_AXIS_COUNT; i++) {
        settings.axisProfile[i] = elrsSanitizeInputAxisProfile(settings.axisProfile[i]);
        settings.outputLimits[i] = elrsSanitizeOutputLimits(settings.outputLimits[i]);
    }
    elrsSanitizeInputRouting(settings.gimbalRouting, settings.switchRouting);
    if(!elrsIsValidTimeTravelConfig(settings.timeTravel, settings.gimbalRouting, settings.mixes)) settings.timeTravel = {};
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
    const size_t oldSize = sizeof(ELRSStoredSettings) - sizeof(ELRSTimeTravelConfig);
    uint8_t *data = (size == sizeof(ELRSStoredSettings)+3 || size == oldSize+3) ? (uint8_t *)malloc(size) : NULL;
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
            if(buf) {
                if(size == oldSize + 3) {
                    const size_t oldModelSize = sizeof(ELRSCrsfSettingsBlob) - sizeof(ELRSTimeTravelConfig);
                    memcpy(buf, data + 2, oldModelSize);
                    memset(buf + oldModelSize, 0, sizeof(ELRSTimeTravelConfig));
                    memcpy(buf + sizeof(ELRSCrsfSettingsBlob), data + 2 + oldModelSize, sizeof(ELRSPortalPreferences));
                } else memcpy(buf, data + 2, validBytes);
            }
            validBytes = sizeof(ELRSStoredSettings);
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

static bool crsfSaveStoredSettings(uint8_t *buf, int len, bool requireSafe=false)
{
    if(crsfSettingsSaving || crsfSettingsRecoveryBlocked)return false;
    crsfSettingsSaving=true;
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
    uint8_t verified[sizeof(ELRSStoredSettings)];
    int validBytes;
    ret = ret && len >= 0 && len <= (int)sizeof(verified) &&
          crsfReadSettingsFile(storage, temporary, verified, validBytes) &&
          validBytes == len && !memcmp(verified, buf, len);
    if(ret && requireSafe) {RemoteTransferError e;ret=remoteFileActivationSafe(e);}
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
            else if(hadOriginal && !storage.rename(crsfBackupName, crsfCfgName)) crsfSettingsRecoveryBlocked=true;
        }
    }
    if(toSD || (haveFS && !FlashROMode)) storage.remove(temporary);
    crsfSettingsSaving=false;return ret;
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

void crsf_load_settings()
{
    uint8_t rawSettings[sizeof(ELRSStoredSettings)] = { 0 };
    crsfSettingsRecoveryBlocked=false;
    installPortalPreferences(ELRSPortalPreferences{});
    crsfSettings = defaultCrsfSettings();
    crsfSettingsHash = 0;
    haveCRSFSettings = false;
    if(!crsfLoadStoredSettings(rawSettings, crsfSetValidBytes) || crsfSetValidBytes!=(int)sizeof(ELRSStoredSettings))return;
    ELRSStoredSettings loaded;memcpy(&loaded,rawSettings,sizeof(loaded));
    if(!validPortalPreferences(loaded.portal) || loaded.model.logicVersion!=2){crsfSettingsRecoveryBlocked=true;return;}
    crsfSettings=loaded.model;
    sanitizeCrsfSettings(crsfSettings);
    installPortalPreferences(loaded.portal);
    ELRSStoredSettings stored={crsfSettings,crsfPortalPreferences};
    crsfSettingsHash = calcHash((uint8_t *)&stored, sizeof(stored));
    haveCRSFSettings = true;
}

ELRSCrsfSettingsBlob snapshotELRSSettings() { return crsfSettings; }
bool saveELRSSettings(const ELRSCrsfSettingsBlob &model,const ELRSPortalPreferences &portal,bool requireSafe) {
    if(!elrsIsValidCrsfSettingsBlob(model) || !validPortalPreferences(portal))return false;
    ELRSStoredSettings next={model,portal};
    if(!crsfSaveStoredSettings((uint8_t *)&next,sizeof(next),requireSafe))return false;
    crsfSettings=model;installPortalPreferences(portal);crsfSettingsHash=calcHash((uint8_t *)&next,sizeof(next));remoteSettingsChanged();return true;
}
bool crsf_save_settings(bool useCache)
{
    ELRSStoredSettings next={crsfSettings,snapshotELRSPortalPreferences()};
    const uint32_t hash=calcHash((uint8_t *)&next,sizeof(next));if(useCache && hash==crsfSettingsHash)return true;
    if(!crsfSaveStoredSettings((uint8_t *)&next,sizeof(next)))return false;
    installPortalPreferences(next.portal);crsfSettingsHash=hash;remoteSettingsChanged();return true;
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
    if(!cal || remoteSettingsBusy()) {
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
                         ELRSSwitchRouting *switchRouting, ELRSOutputLimits *outputLimits, uint8_t *localActions,
                         ELRSLogicConfig *logicConfig, ELRSMixConfig *mixConfig)
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
    if(logicConfig) *logicConfig = crsfSettings.logic;
    if(mixConfig) *mixConfig = crsfSettings.mixes;
}

void saveELRSGimbalRouting(const ELRSGimbalRouting &routing)
{
    saveELRSInputConfig(nullptr, 0, &routing);
}

bool saveELRSInputConfig(const ELRSInputAxisProfile *profiles, int count, const ELRSGimbalRouting *routing,
                         const uint16_t *adcHysteresis, const uint16_t *throttleIdleDeadband,
                         const ELRSSwitchRouting *switchRouting, const ELRSOutputLimits *outputLimits, const uint8_t *localActions,
                         const ELRSDisplayConfig *displayConfig, const ELRSVehicleConfig *vehicleConfig,
                         const ELRSLogicConfig *logicConfig, const ELRSMixConfig *mixConfig)
{
    if(remoteSettingsBusy())return false;
    if(logicConfig && !elrsIsValidLogicConfig(*logicConfig)) return false;
    if(mixConfig && !elrsIsValidMixConfig(*mixConfig)) return false;
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
    if(logicConfig) crsfSettings.logic = *logicConfig;
    if(mixConfig) crsfSettings.mixes = *mixConfig;

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

bool crsf_begin(
            uint16_t packetRateHz,
            uint8_t speedDisplayUnits,
            const ELRSRadioPreferences &radio,
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
    if(crsfSettingsRecoveryBlocked)return false;
    ELRSInputAxisProfile axisProfiles[ELRS_GIMBAL_AXIS_COUNT];
    ELRSGimbalRouting inputRouting;
    ELRSSwitchRouting switchRouting;
    ELRSOutputLimits outputLimits[ELRS_GIMBAL_AXIS_COUNT];
    uint8_t localActions[ELRS_SWITCH_INPUT_COUNT];
    uint16_t adcHysteresis, throttleIdleDeadband;
    ELRSLogicConfig logic;
    ELRSMixConfig mixes;

    loadELRSInputConfig(axisProfiles, ELRS_GIMBAL_AXIS_COUNT, &inputRouting, &adcHysteresis, &throttleIdleDeadband, &switchRouting, outputLimits, localActions, &logic, &mixes);
    const ELRSDisplayConfig displayConfig = loadELRSDisplayConfig();
    const ELRSVehicleConfig vehicleConfig = loadELRSVehicleConfig();

    bool started=elrsMode.begin(
            packetRateHz,
            speedDisplayUnits,
            radio,
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
            &vehicleConfig,
            &logic,
            &mixes,
            &crsfSettings.timeTravel
        );
    if(started) elrsMode.setRadioPreferences(remoteRadioPreferences,remoteRadioReviewPending());
    if(started) elrsMode.setDeviceConfigAccess(false,remoteSettingsBusy(),remoteRadioReviewPending());
    return started;
}

void crsf_loop(int battWarn)
{
    elrsMode.setDeviceConfigAccess(remoteMaintenanceActive(),remoteSettingsBusy(),remoteRadioReviewPending());
    elrsMode.loop(battWarn);
}

void csrf_query_status(bool &FPBUnitIsOn)
{
    FPBUnitIsOn = elrsMode.fakePowerOn();
}

#endif
