
#ifdef HAVE_CRSF

#include "crsf_model_files.h"
#include <ArduinoJson.h>
#include "elrs_crsf.h"

static const char *wmBuildCRSFStatus(const char *dest, int op);
static const char *wmBuildCRSFOM(const char *dest, int op);
static const char *wmBuildCRSFPR(const char *dest, int op);
static const char *wmBuildCRSFSU(const char *dest, int op);
static const char *wmBuildCRSFRC(const char *dest, int op);
static const char *wmBuildCRSFPC(const char *dest, int op);
static const char *wmBuildCRSFTC(const char *dest, int op);
static const char *wmBuildCRSFYC(const char *dest, int op);
static const char *wmBuildCRSFCAL(const char *dest, int op);
static void crsfReadOutputLimitParams();
static const char *wmBuildCRSFSwitchMap(const char *dest, int op);
static void crsfReadSwitchParams();
static void crsfReadDisplayParams();
static const char *wmBuildCRSFDisplay(const char *dest, int op);
static void crsfReadExpoParams();
static void crsfReadLogicParams();
static void crsfReadMixParams();
static const char *wmBuildCRSFLogicMix(const char *dest, int op);

static void syncCRSFPortalBuffers();
static bool buildCRSFPortalInputSettings(ELRSCrsfSettingsBlob &candidate, RemoteTransferError &error);
static void crsfReadInputParam(const char *name, char *destBuf, size_t length, int minval, int maxval, int offset);
static void crsfReadGimbalChannelParam(const char *name, char *destBuf);
static uint8_t crsfRoutingChannel(const ELRSGimbalRouting &routing, uint8_t axis);
static void crsfSetRoutingChannel(ELRSGimbalRouting &routing, uint8_t axis, uint8_t channel);

static const char * const cOpModeCustHTMLSrc[5] = {
    "'>Operation mode",
    "copm",
    ">Legacy%s1'",
    ">ELRS/CRSF%s2'",
    ">Prop controls + ELRS/CRSF%s"
};
static const char * const cPktRateCustHTMLSrc[7] = {
    "'>CRSF channel cadence (restart required)",
    "cpktr",
    ">50 Hz%s1'",
    ">100 Hz%s2'",
    ">150 Hz%s3'",
    ">250 Hz%s4'",
    ">500 Hz%s"
};
static const char * const cSpdUnitCustHTMLSrc[4] = {
    "'>Speed units",
    "cspdu",
    ">km/h%s1'",
    ">mph%s"
};
static const char * const cChannelCustHTMLSrc[16] = {
    ">CH1%s1'",
    ">CH2%s2'",
    ">CH3%s3'",
    ">CH4%s4'",
    ">CH5%s5'",
    ">CH6%s6'",
    ">CH7%s7'",
    ">CH8%s8'",
    ">CH9%s9'",
    ">CH10%s10'",
    ">CH11%s11'",
    ">CH12%s12'",
    ">CH13%s13'",
    ">CH14%s14'",
    ">CH15%s15'",
    ">CH16%s"
};

enum CRSFSelectFieldId : uint8_t {
    CRSF_SELECT_OPMODE,
    CRSF_SELECT_PKTRATE,
    CRSF_SELECT_SPDUNIT,
    CRSF_SELECT_COUNT
};

struct CRSFSelectField {
    const char * const *html;
    int count;
    char *setting;
};

static const CRSFSelectField crsfSelectFields[CRSF_SELECT_COUNT] = {
    { cOpModeCustHTMLSrc, 5, settings.opMode },
    { cPktRateCustHTMLSrc, 7, settings.elrsPktRate },
    { cSpdUnitCustHTMLSrc, 4, settings.elrsSpdUnit },
};

struct CRSFAxisSettings {
    uint8_t axis;
    char *channel;
    char *reverse;
    char *low;
    char *center;
    char *high;
    const char *expoId;
    char *expo;
};

static const CRSFAxisSettings crsfAxisSettings[] = {
    { ELRS_GIMBAL_INPUT_AILERON, settings.elrsRollCh, settings.elrsRollRev, settings.elrsRollLow, settings.elrsRollCtr, settings.elrsRollHigh, "crlexp", settings.elrsAxisExpo[ELRS_GIMBAL_INPUT_AILERON] },
    { ELRS_GIMBAL_INPUT_ELEVATOR, settings.elrsPitchCh, settings.elrsPitchRev, settings.elrsPitchLow, settings.elrsPitchCtr, settings.elrsPitchHigh, "cptexp", settings.elrsAxisExpo[ELRS_GIMBAL_INPUT_ELEVATOR] },
    { ELRS_GIMBAL_INPUT_THROTTLE, settings.elrsThrCh, settings.elrsThrRev, settings.elrsThrLow, settings.elrsThrCtr, settings.elrsThrHigh, "cthexp", settings.elrsAxisExpo[ELRS_GIMBAL_INPUT_THROTTLE] },
    { ELRS_GIMBAL_INPUT_RUDDER, settings.elrsYawCh, settings.elrsYawRev, settings.elrsYawLow, settings.elrsYawCtr, settings.elrsYawHigh, "cywexp", settings.elrsAxisExpo[ELRS_GIMBAL_INPUT_RUDDER] }
};

static char crsfOutputMin[ELRS_GIMBAL_AXIS_COUNT][5] = {"1000", "1000", "1000", "1000"};
static char crsfOutputMax[ELRS_GIMBAL_AXIS_COUNT][5] = {"2000", "2000", "2000", "2000"};
static char crsfLocalActions[ELRS_SWITCH_INPUT_COUNT][2] = {};
static ELRSTimeTravelConfig crsfTimeTravelConfig = {};
static ELRSDisplayConfig crsfDisplayConfig = elrsDefaultDisplayConfig();
static ELRSVehicleConfig crsfVehicleConfig = elrsDefaultVehicleConfig();
static bool crsfDisplayParamsValid = true;
static ELRSLogicConfig crsfLogicConfig = {};
static ELRSMixConfig crsfMixConfig = {};
static bool crsfLogicParamsValid = true, crsfMixParamsValid = true;

static const char *wmBuildCRSFSelectField(const char *dest, int op, uint8_t fieldId)
{
    if(fieldId >= CRSF_SELECT_COUNT) {
        return NULL;
    }

    const CRSFSelectField &field = crsfSelectFields[fieldId];
    return wmBuildSelect(dest, op, field.html, field.count, field.setting, false);
}

#define CRSF_SELECT_BUILDER(fn, fieldId) \
static const char *fn(const char *dest, int op) \
{ \
    return wmBuildCRSFSelectField(dest, op, fieldId); \
}

CRSF_SELECT_BUILDER(wmBuildCRSFOM, CRSF_SELECT_OPMODE)
CRSF_SELECT_BUILDER(wmBuildCRSFPR, CRSF_SELECT_PKTRATE)
CRSF_SELECT_BUILDER(wmBuildCRSFSU, CRSF_SELECT_SPDUNIT)

WiFiManagerParameter custom_crsfom(wmBuildCRSFOM, WFM_SECTS_HEAD);
static const char crsfModeHelpSource[] =
    "<p><small>Legacy runs normal prop controls. ELRS/CRSF shows radio telemetry; switch checkboxes also enable their Settings actions. In telemetry mode, TCD power/speed control (including O.O/RESET power-master actions) requires the FakePower checkbox; TCD braking also requires Stop. Other enabled buttons use state-free BTTFN keepalives and independent Time Travel/refill requests. Prop controls + ELRS/CRSF runs all normal prop controls, display/speedometer, MQTT and BTTFN while transmitting all mapped gimbals and switches. The AIN3 Throttle gimbal also controls prop speed; calibrate prop speed with the normal Calibration button and radio output with Gimbal Calibration below. O.O uses its normal Settings behavior, and Time Travel retains its normal throttle condition. Save and restart to apply.</small></p>"
;
WiFiManagerParameter custom_crsfmodehelp(crsfModeHelpSource);
WiFiManagerParameter custom_ss_crsf("<h3>ELRS/CRSF Settings</h3>", WFM_SECTS|WFM_HL);
WiFiManagerParameter custom_crsfstatus(wmBuildCRSFStatus);
WiFiManagerParameter custom_crsfap("Connect to WiFi in ELRS/CRSF mode<br><span>If unchecked, device will remain in AP mode.</span>", settings.crsfap, "", WFM_LABEL_AFTER|WFM_IS_CHKBOX);
WiFiManagerParameter custom_crsfpr(wmBuildCRSFPR);
WiFiManagerParameter custom_crsfsu(wmBuildCRSFSU);
WiFiManagerParameter custom_crsfdisplay(wmBuildCRSFDisplay);
WiFiManagerParameter custom_crsflogicmix(wmBuildCRSFLogicMix);
WiFiManagerParameter custom_ss_crsfmap("<div hidden>");
WiFiManagerParameter custom_crsfrc(wmBuildCRSFRC);
WiFiManagerParameter custom_crsfrr("Reverse Aileron", settings.elrsRollRev, "data-elrs-reverse='crlch'", WFM_LABEL_AFTER|WFM_IS_CHKBOX);
WiFiManagerParameter custom_crsfpc(wmBuildCRSFPC);
WiFiManagerParameter custom_crsfprv("Reverse Elevator", settings.elrsPitchRev, "data-elrs-reverse='cptch'", WFM_LABEL_AFTER|WFM_IS_CHKBOX);
WiFiManagerParameter custom_crsftc(wmBuildCRSFTC);
WiFiManagerParameter custom_crsftrv("Reverse Throttle", settings.elrsThrRev, "data-elrs-reverse='cthch'", WFM_LABEL_AFTER|WFM_IS_CHKBOX);
WiFiManagerParameter custom_crsfyc(wmBuildCRSFYC);
WiFiManagerParameter custom_crsfyrv("Reverse Rudder", settings.elrsYawRev, "data-elrs-reverse='cywch'", WFM_LABEL_AFTER|WFM_IS_CHKBOX);
WiFiManagerParameter custom_crsfswmap(wmBuildCRSFSwitchMap);
WiFiManagerParameter custom_ss_crsfcal("<h3>Gimbal Settings</h3>", WFM_SECTS|WFM_HL);
WiFiManagerParameter custom_crsfcal(wmBuildCRSFCAL, WFM_FOOT);

WiFiManagerParameter custom_remote_controls("<div id='remote-controls'></div><script defer src='/remote/ui.js'></script>");

WiFiManagerParameter * const crsfParmArray[] = {
      &custom_crsfom,
      &custom_crsfmodehelp,
      &custom_ss_crsf,
      &custom_crsfstatus,
      &custom_crsfap,
      &custom_crsfpr,
      &custom_crsfsu,
      &custom_crsfdisplay,
      &custom_crsflogicmix,
      &custom_ss_crsfmap,
      &custom_crsfrc,
      &custom_crsfrr,
      &custom_crsfpc,
      &custom_crsfprv,
      &custom_crsftc,
      &custom_crsftrv,
      &custom_crsfyc,
      &custom_crsfyrv,
      &custom_crsfswmap,
      &custom_ss_crsfcal,
      &custom_crsfcal,
      &custom_remote_controls,
      NULL
};


/*
 * Save input settings before the portal reports success.
 *
 */

/*
 * Callback from saveParamsCallback()
 *
 */
static bool crsf_wifi_loop_settings()
{
    if(remoteSettingsBusy() || remoteRadioReviewPending() || (opModeCRSF && elrsMode.deviceConfig().busy())){wm.setParamSaveError(409);return false;}
    ELRSCrsfSettingsBlob candidate;RemoteTransferError error;
    if(!buildCRSFPortalInputSettings(candidate,error)){wm.setParamSaveError(400);syncCRSFPortalBuffers();return false;}
    const auto preferences=snapshotELRSPortalPreferences();flushDelayedSave();
    if(!saveELRSSettings(candidate,preferences)){wm.setParamSaveError(500);syncCRSFPortalBuffers();return false;}
    syncCRSFPortalBuffers();return true;
}

static bool crsf_wifi_saveParamsCallback()
{
    getServerParam("copm", settings.opMode, 1, 0, 2, 0);
    getServerParam("cpktr", settings.elrsPktRate, 1, 0, 4, DEF_ELRSPKTRATE);
    getServerParam("cspdu", settings.elrsSpdUnit, 1, 0, 1, DEF_ELRSSPDUNIT);

    crsfReadGimbalChannelParam("crlch", settings.elrsRollCh);
    crsfReadGimbalChannelParam("cptch", settings.elrsPitchCh);
    crsfReadGimbalChannelParam("cthch", settings.elrsThrCh);
    crsfReadGimbalChannelParam("cywch", settings.elrsYawCh);
    crsfReadSwitchParams();
    crsfReadOutputLimitParams();
    crsfReadExpoParams();
    crsfReadDisplayParams();
    crsfReadLogicParams();
    crsfReadMixParams();
    if(opModeCRSF) {
        crsfReadInputParam("chyst", settings.elrsAdcHysteresis, 2, 0, ELRS_INPUT_TOLERANCE_MAX, 0);
        crsfReadInputParam("cthid", settings.elrsThrIdleDeadband, 2, 0, ELRS_INPUT_TOLERANCE_MAX, 0);
        crsfReadInputParam("crrlo", settings.elrsRollLow, 5, 0, 2047, 0);
        crsfReadInputParam("crrct", settings.elrsRollCtr, 5, 0, 2047, 0);
        crsfReadInputParam("crrhi", settings.elrsRollHigh, 5, 0, 2047, 0);
        crsfReadInputParam("cptlo", settings.elrsPitchLow, 5, 0, 2047, 0);
        crsfReadInputParam("cptct", settings.elrsPitchCtr, 5, 0, 2047, 0);
        crsfReadInputParam("cpthi", settings.elrsPitchHigh, 5, 0, 2047, 0);
        crsfReadInputParam("cthlo", settings.elrsThrLow, 5, 0, 2047, 0);
        crsfReadInputParam("cthct", settings.elrsThrCtr, 5, 0, 2047, 0);
        crsfReadInputParam("cthhi", settings.elrsThrHigh, 5, 0, 2047, 0);
        crsfReadInputParam("cywlo", settings.elrsYawLow, 5, 0, 2047, 0);
        crsfReadInputParam("cywct", settings.elrsYawCtr, 5, 0, 2047, 0);
        crsfReadInputParam("cywhi", settings.elrsYawHigh, 5, 0, 2047, 0);
    }
    return true;
}

static uint8_t crsfRoutingChannel(const ELRSGimbalRouting &routing, uint8_t axis)
{
    switch(axis) {
    case ELRS_GIMBAL_INPUT_AILERON:
        return routing.aileronChannel;
    case ELRS_GIMBAL_INPUT_ELEVATOR:
        return routing.elevatorChannel;
    case ELRS_GIMBAL_INPUT_THROTTLE:
        return routing.throttleChannel;
    case ELRS_GIMBAL_INPUT_RUDDER:
        return routing.rudderChannel;
    default:
        return 1;
    }
}

static void crsfSetRoutingChannel(ELRSGimbalRouting &routing, uint8_t axis, uint8_t channel)
{
    switch(axis) {
    case ELRS_GIMBAL_INPUT_AILERON:
        routing.aileronChannel = channel;
        break;
    case ELRS_GIMBAL_INPUT_ELEVATOR:
        routing.elevatorChannel = channel;
        break;
    case ELRS_GIMBAL_INPUT_THROTTLE:
        routing.throttleChannel = channel;
        break;
    case ELRS_GIMBAL_INPUT_RUDDER:
        routing.rudderChannel = channel;
        break;
    default:
        break;
    }
}

static void syncCRSFPortalBuffers()
{
    ELRSInputAxisProfile profiles[ELRS_GIMBAL_AXIS_COUNT];
    ELRSOutputLimits limits[ELRS_GIMBAL_AXIS_COUNT];
    ELRSGimbalRouting routing;
    ELRSSwitchRouting switches;
    uint16_t adcHysteresis, throttleIdleDeadband;

    if(!haveNewBoard) {
        return;
    }
    crsfDisplayConfig = loadELRSDisplayConfig();
    crsfVehicleConfig = loadELRSVehicleConfig();
    crsfTimeTravelConfig = snapshotELRSSettings().timeTravel;
    crsfDisplayParamsValid = true;
    loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &crsfLogicConfig, &crsfMixConfig);
    crsfLogicParamsValid = crsfMixParamsValid = true;

    uint8_t localActions[ELRS_SWITCH_INPUT_COUNT];
    loadELRSInputConfig(profiles, ELRS_GIMBAL_AXIS_COUNT, &routing, &adcHysteresis, &throttleIdleDeadband, &switches, limits, localActions);
    for(int i = 0; i < ELRS_GIMBAL_AXIS_COUNT; i++) {
        snprintf(crsfOutputMin[i], sizeof(crsfOutputMin[i]), "%u", (unsigned)limits[i].minimumUs);
        snprintf(crsfOutputMax[i], sizeof(crsfOutputMax[i]), "%u", (unsigned)limits[i].maximumUs);
    }
    snprintf(settings.elrsAdcHysteresis, sizeof(settings.elrsAdcHysteresis), "%u", (unsigned)adcHysteresis);
    snprintf(settings.elrsThrIdleDeadband, sizeof(settings.elrsThrIdleDeadband), "%u", (unsigned)throttleIdleDeadband);
    for(int i = 0; i < ELRS_SWITCH_INPUT_COUNT; i++) {
        snprintf(settings.elrsSwitchCh[i], sizeof(settings.elrsSwitchCh[i]), "%u", (unsigned)switches.channels[i]);
        snprintf(crsfLocalActions[i], sizeof(crsfLocalActions[i]), "%u", (unsigned)localActions[i]);
    }

    for(size_t i = 0; i < sizeof(crsfAxisSettings) / sizeof(crsfAxisSettings[0]); i++) {
        const CRSFAxisSettings &axis = crsfAxisSettings[i];
        ELRSInputAxisProfile &profile = profiles[axis.axis];

        snprintf(axis.channel, sizeof(settings.elrsRollCh), "%u", (unsigned)crsfRoutingChannel(routing, axis.axis));
        axis.reverse[0] = profile.reverse ? '1' : '0';
        axis.reverse[1] = 0;
        snprintf(axis.low, sizeof(settings.elrsRollLow), "%d", profile.minimum);
        snprintf(axis.center, sizeof(settings.elrsRollCtr), "%d", profile.center);
        snprintf(axis.high, sizeof(settings.elrsRollHigh), "%d", profile.maximum);
        snprintf(axis.expo, sizeof(settings.elrsAxisExpo[0]), "%u", (unsigned)profile.expo);
    }
}

static bool buildCRSFPortalInputSettings(ELRSCrsfSettingsBlob &candidate, RemoteTransferError &error)
{
    evalCB(settings.crsfap, &custom_crsfap);
    evalCB(settings.elrsRollRev, &custom_crsfrr);evalCB(settings.elrsPitchRev, &custom_crsfprv);
    evalCB(settings.elrsThrRev, &custom_crsftrv);evalCB(settings.elrsYawRev, &custom_crsfyrv);
    if(!crsfLogicParamsValid || !crsfMixParamsValid) return false;
    if(!crsfDisplayParamsValid || !elrsIsValidDisplayConfig(crsfDisplayConfig) || !elrsIsValidVehicleConfig(crsfVehicleConfig)) return false;
    ELRSInputAxisProfile profiles[ELRS_GIMBAL_AXIS_COUNT];
    ELRSOutputLimits limits[ELRS_GIMBAL_AXIS_COUNT];
    ELRSGimbalRouting routing;
    ELRSSwitchRouting switches;

    uint8_t localActions[ELRS_SWITCH_INPUT_COUNT];
    for(int i = 0; i < ELRS_SWITCH_INPUT_COUNT; i++) {
        const char *value = crsfLocalActions[i];
        const size_t length = strlen(value);
        if(length != 1 || (value[0] != '0' && value[0] != '1')) return false;
        localActions[i] = (uint8_t)atoi(value);
    }

    if(!haveNewBoard) {
        return false;
    }

    for(int i = 0; i < ELRS_GIMBAL_AXIS_COUNT; i++) {
        const char *values[] = {crsfOutputMin[i], crsfOutputMax[i]};
        long bounds[2];
        for(int side = 0; side < 2; side++) {
            char *end;
            bounds[side] = strtol(values[side], &end, 10);
            if(strlen(values[side]) != 4 || strspn(values[side], "0123456789") != 4 || *end ||
               bounds[side] < (side ? ELRS_INPUT_US_MID : ELRS_INPUT_US_MIN) ||
               bounds[side] > (side ? ELRS_INPUT_US_MAX : ELRS_INPUT_US_MID)) {
                bounds[side] = side ? ELRS_INPUT_US_MAX : ELRS_INPUT_US_MIN;
            }
        }
        limits[i] = {(uint16_t)bounds[0], (uint16_t)bounds[1]};
    }

    for(int i = 0; i < ELRS_SWITCH_INPUT_COUNT; i++) {
        const char *value = settings.elrsSwitchCh[i];
        char *end;
        long channel = strtol(value, &end, 10);
        // Zero is explicit None; invalid channels must still reset the map.
        if(!*value || strspn(value, "0123456789") != strlen(value) || *end || channel < 0 || channel > 16) channel = 17;
        switches.channels[i] = (uint8_t)channel;
    }

    loadELRSInputConfig(profiles, ELRS_GIMBAL_AXIS_COUNT, &routing);

    for(size_t i = 0; i < sizeof(crsfAxisSettings) / sizeof(crsfAxisSettings[0]); i++) {
        const CRSFAxisSettings &axis = crsfAxisSettings[i];
        ELRSInputAxisProfile &profile = profiles[axis.axis];

        char *end;
        long channel = strtol(axis.channel, &end, 10);
        if(!*axis.channel || strspn(axis.channel, "0123456789") != strlen(axis.channel) || *end || channel < 0 || channel > 16) channel = 17;
        crsfSetRoutingChannel(routing, axis.axis, (uint8_t)channel);
        profile.reverse = (axis.reverse[0] != '0');
        profile.minimum = (int16_t)atoi(axis.low);
        profile.center = (int16_t)atoi(axis.center);
        profile.maximum = (int16_t)atoi(axis.high);
        if(!elrsIsValidInputAxisProfile(profile)) {
            const ELRSInputAxisProfile defaults = elrsDefaultInputAxisProfile();
            profile.minimum = defaults.minimum;
            profile.center = defaults.center;
            profile.maximum = defaults.maximum;
        }
        const size_t length = strlen(axis.expo);
        profile.expo = length >= 1 && length <= 3 && strspn(axis.expo, "0123456789") == length &&
            atoi(axis.expo) <= 100 ? (uint8_t)atoi(axis.expo) : 0;
    }

    elrsSanitizeInputRouting(routing, switches);
    uint16_t adcHysteresis = (uint16_t)atoi(settings.elrsAdcHysteresis);
    uint16_t throttleIdleDeadband = (uint16_t)atoi(settings.elrsThrIdleDeadband);
    if(adcHysteresis > ELRS_INPUT_TOLERANCE_MAX) adcHysteresis = ELRS_INPUT_TOLERANCE_DEFAULT;
    if(throttleIdleDeadband > ELRS_INPUT_TOLERANCE_MAX) throttleIdleDeadband = ELRS_INPUT_TOLERANCE_DEFAULT;
    candidate=snapshotELRSSettings();
    memcpy(candidate.axisProfile,profiles,sizeof(profiles));candidate.gimbalRouting=routing;candidate.switchRouting=switches;
    candidate.adcHysteresis=adcHysteresis;candidate.throttleIdleDeadband=throttleIdleDeadband;
    memcpy(candidate.outputLimits,limits,sizeof(limits));memcpy(candidate.localActions,localActions,sizeof(localActions));
    candidate.displayConfig=crsfDisplayConfig;candidate.vehicleConfig=crsfVehicleConfig;
    candidate.timeTravel=crsfTimeTravelConfig;
    candidate.logic=crsfLogicConfig;candidate.mixes=crsfMixConfig;
    if(!elrsIsValidTimeTravelConfig(candidate.timeTravel,candidate.gimbalRouting,candidate.mixes))
        return remoteTransferFail(error,RemoteTransferCode::InvalidValue,"timeTravel.channel","Select a channel without a gimbal or mixer");
    if(!elrsIsValidCrsfSettingsBlob(candidate))return remoteTransferFail(error,RemoteTransferCode::InvalidValue,"crsf","Invalid local input settings");
    return true;
}

static void crsfReadOutputLimitParams()
{
    ELRSOutputLimits limits[ELRS_GIMBAL_AXIS_COUNT];
    loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, limits);
    for(int i = 0; i < ELRS_GIMBAL_AXIS_COUNT; i++) {
        snprintf(crsfOutputMin[i], sizeof(crsfOutputMin[i]), "%u", (unsigned)limits[i].minimumUs);
        snprintf(crsfOutputMax[i], sizeof(crsfOutputMax[i]), "%u", (unsigned)limits[i].maximumUs);
        for(int side = 0; side < 2; side++) {
            char name[8];
            snprintf(name, sizeof(name), "cout%d%s", i, side ? "hi" : "lo");
            if(!wm.server->hasArg(name)) continue;
            char *buffer = side ? crsfOutputMax[i] : crsfOutputMin[i];
            String value = wm.server->arg(name);
            buffer[0] = 0;
            if(value.length() == 4 && strspn(value.c_str(), "0123456789") == 4) {
                crsfReadInputParam(name, buffer, 4, side ? ELRS_INPUT_US_MID : ELRS_INPUT_US_MIN,
                                   side ? ELRS_INPUT_US_MAX : ELRS_INPUT_US_MID, 0);
            }
        }
    }
}

static bool crsfParseDisplayNumber(const String &value, float &number)
{
    const char *text = value.c_str(), *p = text;
    const size_t length = value.length();
    if(!length || length > 24 || strlen(text) != length) return false;
    if(*p == '+' || *p == '-') ++p;
    const char *mantissa = p;
    size_t digits = strspn(p, "0123456789"); p += digits;
    if(*p == '.') { ++p; size_t fraction = strspn(p, "0123456789"); p += fraction; digits += fraction; }
    if(!digits) return false;
    const bool zero = strspn(mantissa, "0.") == size_t(p - mantissa);
    if(*p == 'e' || *p == 'E') {
        ++p; if(*p == '+' || *p == '-') ++p;
        size_t exponent = strspn(p, "0123456789"); if(!exponent) return false; p += exponent;
    }
    if(*p) return false;
    if(zero) { number = *text == '-' ? -0.0f : 0.0f; return true; }
    // ponytail: JSON conversion can round rare decimal boundaries differently; use strtof if exact rounding is required.
    StaticJsonDocument<0> document;
    document.set(text);
    number = document.as<float>();
    if(number == 0 && *text == '-') number = -0.0f;
    return std::isfinite(number);
}

static bool crsfParseDisplayParams(ELRSDisplayConfig &config)
{
    const char *names[] = {"cdsrc", "cddec"};
    for(int i = 0; i < 2; i++) {
        if(!wm.server->hasArg(names[i])) continue;
        String value = wm.server->arg(names[i]);
        const size_t length = value.length();
        if(!length || length > 3 || strlen(value.c_str()) != length || strspn(value.c_str(), "0123456789") != length) return false;
        const int number = atoi(value.c_str());
        if(i == 0) { if(number > ELRS_DISPLAY_RPM_SCALED_MPH) return false; config.source = number; }
        else { if(number > 2 && number != 255) return false; config.decimalPlaces = number; }
    }
    float multiplier = config.multiplier, offset = config.offset;
    if(wm.server->hasArg("cdmul") && !crsfParseDisplayNumber(wm.server->arg("cdmul"), multiplier)) return false;
    if(wm.server->hasArg("cdoff") && !crsfParseDisplayNumber(wm.server->arg("cdoff"), offset)) return false;
    config.multiplier = multiplier;
    config.offset = offset;
    return elrsIsValidDisplayConfig(config);
}

static bool crsfParseVehicleParams(ELRSVehicleConfig &config)
{
    const char *names[] = {"crpoles", "crgear", "crdiam", "crscale", "crrpmtype"};
    float values[] = {(float)config.motorPoles, config.gearRatio, config.tireDiameterMm, config.scaleFactor, (float)config.rpmType};
    for(int i = 0; i < 5; i++) {
        if(wm.server->hasArg(names[i]) && !crsfParseDisplayNumber(wm.server->arg(names[i]), values[i])) return false;
    }
    if(values[0] < 2 || values[0] > 64 || std::fmod(values[0], 2.0f) != 0) return false;
    if(values[4] != ELRS_RPM_ELECTRICAL && values[4] != ELRS_RPM_SHAFT) return false;
    config = {(uint8_t)values[0], values[1], values[2], values[3], (uint8_t)values[4]};
    return elrsIsValidVehicleConfig(config);
}

static void crsfReadDisplayParams()
{
    crsfDisplayConfig = loadELRSDisplayConfig();
    crsfVehicleConfig = loadELRSVehicleConfig();
    crsfDisplayParamsValid = crsfParseDisplayParams(crsfDisplayConfig) && crsfParseVehicleParams(crsfVehicleConfig);
    crsfTimeTravelConfig = snapshotELRSSettings().timeTravel;
    if(wm.server->hasArg("cttform")) {
        const String channel = wm.server->arg("cttch");
        const bool enabled = wm.server->hasArg("cttenabled");
        if(wm.server->arg("cttform") != "1" || (enabled && wm.server->arg("cttenabled") != "1") ||
           channel.length() < 1 || channel.length() > 2 ||
           strspn(channel.c_str(), "0123456789") != channel.length() || atoi(channel.c_str()) > 16) {
            crsfDisplayParamsValid = false;
        } else crsfTimeTravelConfig = {(uint8_t)enabled,(uint8_t)atoi(channel.c_str())};
    }
}

static void crsfReadExpoParams()
{
    ELRSInputAxisProfile profiles[ELRS_GIMBAL_AXIS_COUNT];
    loadELRSInputProfiles(profiles, ELRS_GIMBAL_AXIS_COUNT);
    for(const CRSFAxisSettings &axis : crsfAxisSettings) {
        snprintf(axis.expo, sizeof(settings.elrsAxisExpo[0]), "%u", (unsigned)profiles[axis.axis].expo);
        if(wm.server->hasArg(axis.expoId)) {
            String value = wm.server->arg(axis.expoId);
            axis.expo[0] = 0;
            if(value.length() < sizeof(settings.elrsAxisExpo[0]) && value.length() == strlen(value.c_str())) {
                strcpy(axis.expo, value.c_str());
            }
        }
    }
}

static void crsfReadSwitchParams()
{
    uint8_t localActions[ELRS_SWITCH_INPUT_COUNT];
    loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr, localActions);
    const bool localForm = wm.server->hasArg("cslocal");
    String localMarker = wm.server->arg("cslocal");
    const bool validLocalForm = !localForm || (localMarker.length() == 1 && localMarker == "1");
    for(int i = 0; i < ELRS_SWITCH_INPUT_COUNT; i++) {
        char name[6];
        snprintf(name, sizeof(name), "csw%d", i);
        if(wm.server->hasArg(name)) {
            String value = wm.server->arg(name);
            settings.elrsSwitchCh[i][0] = 0;
            if(value.length() < sizeof(settings.elrsSwitchCh[i]) && value.length() == strlen(value.c_str())) {
                strcpy(settings.elrsSwitchCh[i], value.c_str());
            }
        }
        snprintf(crsfLocalActions[i], sizeof(crsfLocalActions[i]), "%u", (unsigned)localActions[i]);
        snprintf(name, sizeof(name), "csa%d", i);
        if(!validLocalForm) {
            crsfLocalActions[i][0] = 0;
        } else if(localForm) {
            strcpy(crsfLocalActions[i], "0");
            if(wm.server->hasArg(name)) {
                String value = wm.server->arg(name);
                crsfLocalActions[i][0] = 0;
                if(value.length() == 1 && value == "1") strcpy(crsfLocalActions[i], "1");
            }
        }
    }
}

/*
 * Callback from saveParamsCallback()
 *
 */
static void crsf_wifi_updateConfigPortalValues()
{
    syncCRSFPortalBuffers();
    setCBVal(&custom_crsfap, settings.crsfap);
    setCBVal(&custom_crsfrr, settings.elrsRollRev);
    setCBVal(&custom_crsfprv, settings.elrsPitchRev);
    setCBVal(&custom_crsftrv, settings.elrsThrRev);
    setCBVal(&custom_crsfyrv, settings.elrsYawRev);
    // all others done on-the-fly
}

static const char *wmBuildSelectOneBased(const char *dest, int op, const char * const *src, int count, char *setting, bool indent = false)
{
    char tempSetting[3];
    int selectValue = atoi(setting);

    if(selectValue == 0) {
        selectValue = count - 2;
    } else if(selectValue < 1 || selectValue > count - 3) {
        selectValue = 1;
    }

    snprintf(tempSetting, sizeof(tempSetting), "%d", selectValue - 1);

    return wmBuildSelect(dest, op, src, count, tempSetting, indent);
}

static void wmAppendEscaped(String &html, const char *text)
{
    if(!text) {
        return;
    }

    while(*text) {
        switch(*text) {
        case '&': html += "&amp;"; break;
        case '<': html += "&lt;"; break;
        case '>': html += "&gt;"; break;
        case '"': html += "&quot;"; break;
        case '\'': html += "&#39;"; break;
        default: html += *text; break;
        }
        text++;
    }
}

static const char *wmBuildCRSFStatus(const char *dest, int op)
{
    if(op == WM_CP_DESTROY) {
        if(dest) free((void *)dest);
        return NULL;
    }

    ELRSCrsfStatus status = elrsMode.getStatus();
    String html;

    html.reserve(180);
    html += "<div class='cmp0' style='font-size:0.85em;line-height:1.3em;margin:0 0 10px 0;color:";
    if(!opModeCRSF) {
        html += "#777'>ELRS/CRSF mode inactive";
    } else if(status.replyActive) {
        html += "#176d2f'>Module communicating";
        if(status.moduleName[0]) {
            html += ": ";
            wmAppendEscaped(html, status.moduleName);
        }
    } else if(status.everReplied) {
        html += "#8a6d1d'>Module response lost";
        if(status.moduleName[0]) {
            html += ": ";
            wmAppendEscaped(html, status.moduleName);
        }
    } else {
        html += "#a22'>No module response yet";
    }
    html += "</div>";

    if(op == WM_CP_LEN) {
        wmLenBuf = html.length() + 1;
        return (const char *)&wmLenBuf;
    }

    char *str = (char *)malloc(html.length() + 1);
    if(!str) {
        return NULL;
    }
    strcpy(str, html.c_str());
    return str;
}

static const char *wmBuildCRSFSwitchMap(const char *dest, int op)
{
    if(op == WM_CP_DESTROY) {
        if(dest) free((void *)dest);
        return NULL;
    }
    static const char *labels[ELRS_SWITCH_INPUT_COUNT] = {
        "Stop", "FakePower", "O.O", "RESET", "ButtonPack 1", "ButtonPack 2", "ButtonPack 3", "ButtonPack 4",
        "ButtonPack 5", "ButtonPack 6", "ButtonPack 7", "ButtonPack 8"
    };
    String html;
    html.reserve(13000);
    html += "</div><div class='cmp0'><p style='margin:0 0 10px;white-space:normal'><small>All inputs can use CH1-CH16 or None. None disables that input's radio output; multiple inputs can use None. Assigned channels must be unique. Unclaimed channels transmit the minimum value. ELRS RF mode determines receiver channel availability and resolution. Keep steering and throttle on CH1-CH4 in Hybrid/Wide; use Full Resolution 16ch for all 16 input channels.</small></p>";
    html += "<input type='hidden' name='cslocal' value='1'><p style='margin:0 0 10px;white-space:normal'><small>In ELRS/CRSF mode, check a switch to also run its behavior configured on the Settings page. In Prop controls + ELRS/CRSF, all prop controls run regardless of these checkboxes. Unchecked by default. Save and restart to apply.</small></p><div hidden>";
    for(int i = 0; i < ELRS_SWITCH_INPUT_COUNT; i++) {
        char name[6];
        snprintf(name, sizeof(name), "csw%d", i);
        html += "<div class='cmp0'><label class='mp0' for='"; html += name; html += "'>"; html += labels[i]; html += " target channel</label><select class='sel0' data-elrs-switch id='";
        html += name; html += "' name='"; html += name; html += "'>";
        for(int channel = 0; channel <= 16; channel++) {
            char value[3];
            snprintf(value, sizeof(value), "%d", channel);
            html += "<option value='"; html += value; html += "'";
            if(channel == atoi(settings.elrsSwitchCh[i])) html += " selected";
            if(channel) { html += ">CH"; html += value; }
            else html += ">None";
            html += "</option>";
        }
        html += "</select>";
        snprintf(name, sizeof(name), "csa%d", i);
        html += "<label class='ml20' for='"; html += name; html += "'><input type='checkbox' id='";
        html += name; html += "' name='"; html += name; html += "' value='1'";
        if(crsfLocalActions[i][0] == '1') html += " checked";
        html += "> Also trigger Settings action</label></div>";
    }
    html += "</div></div><script>(function(){var s=document.querySelectorAll('#crlch,#cptch,#cthch,#cywch,[data-elrs-switch]');function c(e){var v=Number(e.value);return e.hasAttribute('data-elrs-switch')?v:(v===16?0:v+1);}function v(){var n={};for(var i=0;i<s.length;i++)if(c(s[i]))n[c(s[i])]=(n[c(s[i])]||0)+1;for(var i=0;i<s.length;i++)s[i].setCustomValidity(n[c(s[i])]>1?'Choose a different channel for each input.':'');}for(var i=0;i<s.length;i++)s[i].addEventListener('change',v);v();})();</script>";
    if(op == WM_CP_LEN) {
        wmLenBuf = html.length() + 1;
        return (const char *)&wmLenBuf;
    }
    char *result = (char *)malloc(html.length() + 1);
    if(!result) return NULL;
    strcpy(result, html.c_str());
    return result;
}

static const char *wmBuildCRSFGimbalChannelSelect(const char *dest, int op, const char *label, const char *id, char *setting)
{
    const char *html[19];

    html[0] = label;
    html[1] = id;
    for(int i = 0; i < 16; i++) {
        html[i + 2] = cChannelCustHTMLSrc[i];
    }

    html[17] = ">CH16%s16'";
    html[18] = ">None%s";
    return wmBuildSelectOneBased(dest, op, html, 19, setting, false);
}

static void wmAppendCRSFOutputLimits(String &html, uint8_t axis)
{
    static const char *labels[] = {"Lower limit", "Center", "Upper limit"};
    static const char *suffixes[] = {"lo", "ct", "hi"};
    const char *values[] = {crsfOutputMin[axis], "1500", crsfOutputMax[axis]};
    html += "<h4>Travel Limits</h4><div class='elrscal-points elrsout'>";
    for(int field = 0; field < 3; field++) {
        if(field == 1) {
            html += "<div class='elrscal-row'><span class='elrscal-label'>Center</span><div class='elrscal-ctl'><span class='elrscenter'>1500</span></div></div>";
            continue;
        }
        char name[8];
        snprintf(name, sizeof(name), "cout%d%s", axis, suffixes[field]);
        html += "<div class='elrscal-row'><label for='"; html += name; html += "'>"; html += labels[field];
        html += "</label><div class='elrscal-ctl'><input id='"; html += name; html += "' type='number' value='";
        html += values[field]; html += "'";
        html += " name='"; html += name; html += "' maxlength='4'";
        html += field == 0 ? " min='1000' max='1500' required" : " min='1500' max='2000' required";
        html += "></div></div>";
    }
    html += "</div>";
}

struct CRSFGimbalCalField {
    const char *label;
    const char *inputId;
    char *value;
};

struct CRSFGimbalCalAxis {
    const char *name;
    const char *liveId;
    CRSFGimbalCalField fields[3];
    uint8_t axis;
};

static void wmAppendCRSFCALPoint(String &html,
                                 const char *label,
                                 const char *inputId,
                                 const char *liveId,
                                 const char *value)
{
    html += "<div class='elrscal-row'><label for='";
    html += inputId;
    html += "'>";
    html += label;
    html += "</label><div class='elrscal-ctl'><input id='";
    html += inputId;
    html += "' name='";
    html += inputId;
    html += "' maxlength='4' value='";
    html += value;
    html += "' type='number' min='0' max='2047' required><button type='button' onclick=\"elrsCapture('";
    html += liveId;
    html += "','";
    html += inputId;
    html += "')\">Capture</button></div></div>";
}

static void wmAppendCRSFCALAxis(String &html, const CRSFGimbalCalAxis &axis)
{
    html += "<fieldset class='elrscal-axis'><legend class='elrscal-name'>";
    html += axis.name;
    html += "</legend><h4>Calibration (ADC counts)</h4><div class='elrscal-head'><span class='elrscal-live'>Filtered ADC <span id='";
    html += axis.liveId;
    html += "'>--</span></span></div>";
    html += "<div class='elrscal-points'>";
    for(int i = 0; i < 3; i++) {
        wmAppendCRSFCALPoint(html, axis.fields[i].label, axis.fields[i].inputId, axis.liveId, axis.fields[i].value);
    }
    html += "</div><h4>Response Curve</h4>";
    for(const CRSFAxisSettings &binding : crsfAxisSettings) {
        if(binding.axis != axis.axis) continue;
        html += "<div class='elrscal-row'><label for='"; html += binding.expoId;
        html += "'>Curve strength (%)</label><input id='"; html += binding.expoId;
        html += "' name='"; html += binding.expoId;
        html += "' type='range' min='0' max='100' step='1' value='"; html += binding.expo;
        html += "' data-elrs-expo data-centered='"; html += axis.axis == ELRS_GIMBAL_INPUT_THROTTLE ? "0" : "1";
        html += "'><output id='"; html += binding.expoId; html += "_value' for='"; html += binding.expoId;
        html += "'>"; html += binding.expo; html += "%</output></div><svg viewBox='0 0 1000 1000' role='img' aria-label='";
        html += axis.name; html += " input to output curve, 1000 to 2000 microseconds' style='display:block;width:100%;max-width:100%;height:160px'>"
                "<line x1='0' y1='1000' x2='1000' y2='0' stroke='#999' stroke-width='8'/>"
                "<polyline id='";
        html += binding.expoId; html += "_curve' fill='none' stroke='#176d2f' stroke-width='12'/></svg>";
        break;
    }
    wmAppendCRSFOutputLimits(html, axis.axis);
    html += "</fieldset>";
}

static const char crsfCalIntro[] =

    "<div class='cmp0 elrscal-wrap'><p style='font-size:0.85em;line-height:1.35em;margin:0 0 10px 0'>"
    "Calibrate and tune each gimbal below. Changes apply after saving and restarting."
    "</p><p>Calibration: move the stick to each labeled position and click Capture to record its filtered ADC reading. "
    "The low, center, and high points map to 1000, 1500, and 2000 microseconds before curves and travel limits."
    "</p><p>Response curves: 0% is linear; higher strengths soften response around center for Aileron, Elevator and Rudder, "
    "and near idle for Throttle. The previews show response before travel limits."
    "</p><p>Travel Limits set the final output range in microseconds. Lower limits allow 1000-1500; upper limits allow 1500-2000. "
    "Defaults of 1000 and 2000 use full travel. The fixed 1500 center is the reference for scaling the curved output."
    "</p>"
    "<div id='elrscalstat' style='font-size:0.8em;color:#444;margin:0 0 10px 0'>Filtered ADC: waiting for samples...</div>"
;

static const char crsfCalStyle[] =

    "<style>"
    ".elrscal-wrap{box-sizing:border-box;width:100%;max-width:100%;padding:0;margin:0;white-space:normal;overflow-wrap:anywhere;overflow:hidden}"
    ".elrscal-wrap p,.elrscal-wrap #elrscalstat{white-space:normal;overflow-wrap:anywhere;max-width:100%}"
    ".elrscal-wrap p{font-size:.85em;line-height:1.35em}"
    ".elrscal-axis{box-sizing:border-box;width:100%;max-width:100%;min-width:0;margin:10px 0;padding:8px;white-space:normal}"
    ".elrscal-head{display:block;line-height:1.3em;max-width:100%;overflow-wrap:anywhere;white-space:normal}"
    ".elrscal-name{font-weight:bold}"
    ".elrscal-live{display:block;font-size:.85em;color:#333}"
    ".elrscal-row{box-sizing:border-box;width:100%;max-width:100%;margin:8px 0;overflow:hidden;padding:0}"
    ".elrscal-row label,.elrscal-label{display:block;font-size:.82em;margin:0 0 2px 0}"
    ".elrscal-ctl{box-sizing:border-box;display:grid;grid-template-columns:minmax(0,5.8em) max-content;gap:6px;width:100%;max-width:100%;padding:0;margin:0;align-items:stretch}"
    ".elrscal-row input{box-sizing:border-box;width:100%;max-width:100%;min-width:0}"
    ".elrscal-row button{box-sizing:border-box;width:auto;max-width:100%;min-width:0;margin:0;padding:0 6px;font-size:.95em;line-height:2rem}"
    ".elrscal-points{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px;width:100%;padding:0;margin:8px 0}"
    ".elrscal-points .elrscal-row{margin:0}"
    ".elrscal-points .elrscal-ctl{grid-template-columns:minmax(0,1fr);gap:4px}"
    ".elrscal-points input{max-width:5em}"
    ".elrscal-points button{width:100%;padding:0 4px;font-size:.85em}"
    ".elrscenter{display:block;padding:5px;margin:5px 0}"
    ".elrscal-axis h4{margin:12px 0 5px}"
    "</style>"
;

static const char crsfExpoScript[] =

    "<script>(function(){var controls=document.querySelectorAll('[data-elrs-expo]');"
    "function draw(c){var strength=Number(c.value),centered=c.dataset.centered==='1',points=[];"
    "document.getElementById(c.id+'_value').textContent=strength+'%';"
    "for(var i=0;i<=100;i++){var x=i*10,d=centered?x-500:x,m=Math.abs(d),span=centered?500:1000;"
    "var magnitude=Math.round(((100-strength)*m*span*span+strength*m*m*m)/(100*span*span));"
    "var y=centered?500+(d<0?-magnitude:magnitude):magnitude;points.push(x+','+(1000-y));}"
    "document.getElementById(c.id+'_curve').setAttribute('points',points.join(' '));}"
    "for(var i=0;i<controls.length;i++){draw(controls[i]);controls[i].addEventListener('input',function(){draw(this);});}"
    "})();</script>"
;

static const char crsfCalScript[] =

    "<script>(function(){if(window.__elrsCalInit)return;window.__elrsCalInit=true;"
    "function ge(id){return document.getElementById(id);}function setLive(id,val){var el=ge(id);if(el)el.textContent=val;}"
    "function setStatus(msg){var el=ge('elrscalstat');if(el)el.textContent=msg;}"
    "window.elrsCapture=function(liveId,targetId){var live=ge(liveId),target=ge(targetId);if(!live||!target)return;"
    "if(live.textContent==='--')return;target.value=live.textContent;};"
    "function fail(){setStatus('Filtered ADC unavailable. Make sure the board is powered and the ADS1015 is reachable.');"
    "setLive('elrs_roll_live','--');setLive('elrs_pitch_live','--');setLive('elrs_throttle_live','--');setLive('elrs_yaw_live','--');}"
    "function poll(){if(ge('remote-controls')?.dataset.busy==='1'){setTimeout(poll,500);return;}fetch('/elrsraw',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){"
    "if(!d.ok){fail();return;}setLive('elrs_roll_live',d.roll);setLive('elrs_pitch_live',d.pitch);"
    "setLive('elrs_throttle_live',d.throttle);setLive('elrs_yaw_live',d.yaw);"
    "setStatus('Filtered ADC connected.');"
    "}).catch(fail).then(function(){setTimeout(poll,500);});}poll();})();</script></div>"
;

static const char *wmBuildCRSFCAL(const char *dest, int op)
{
    if(op == WM_CP_DESTROY) {
        if(dest) free((void *)dest);
        return NULL;
    }

    if(!opModeCRSF)
        return NULL;

    String html;

    html.reserve(sizeof(crsfCalIntro) + sizeof(crsfCalStyle) + sizeof(crsfCalScript) + sizeof(crsfExpoScript) + 6500);
    html += crsfCalIntro;
    html += crsfCalStyle;

    html += "<fieldset class='elrscal-axis'><legend class='elrscal-name'>Input Filtering</legend>";
    CRSFGimbalCalField tolerances[] = {
        { "Jitter tolerance (ADC counts)", "chyst", settings.elrsAdcHysteresis },
        { "Throttle idle deadband (ADC counts)", "cthid", settings.elrsThrIdleDeadband }
    };
    for(const CRSFGimbalCalField &field : tolerances) {
        html += "<div class='elrscal-row'><label for='";
        html += field.inputId;
        html += "'>";
        html += field.label;
        html += "</label><div class='elrscal-ctl'><input id='";
        html += field.inputId;
        html += "' name='";
        html += field.inputId;
        html += "' maxlength='2' type='number' min='0' max='32' value='";
        html += field.value;
        html += "'></div></div>";
    }
    html += "<p>Jitter tolerance ignores small changes in ADC readings. Throttle idle deadband holds throttle at its lower output "
            "near the idle endpoint. Both default to 5 ADC counts; 0 disables the setting. "
            "Live readings and captures include jitter filtering, before throttle idle adjustment, calibration, curves, and travel limits.</p></fieldset>";

    CRSFGimbalCalAxis axes[] = {
        { "Rudder", "elrs_yaw_live", {
            { "Left", "cywlo", settings.elrsYawLow },
            { "Center", "cywct", settings.elrsYawCtr },
            { "Right", "cywhi", settings.elrsYawHigh }
        }, ELRS_GIMBAL_INPUT_RUDDER },
        { "Throttle", "elrs_throttle_live", {
            { "Up", "cthhi", settings.elrsThrHigh },
            { "Center", "cthct", settings.elrsThrCtr },
            { "Down", "cthlo", settings.elrsThrLow }
        }, ELRS_GIMBAL_INPUT_THROTTLE },
        { "Aileron", "elrs_roll_live", {
            { "Left", "crrlo", settings.elrsRollLow },
            { "Center", "crrct", settings.elrsRollCtr },
            { "Right", "crrhi", settings.elrsRollHigh }
        }, ELRS_GIMBAL_INPUT_AILERON },
        { "Elevator", "elrs_pitch_live", {
            { "Up", "cpthi", settings.elrsPitchHigh },
            { "Center", "cptct", settings.elrsPitchCtr },
            { "Down", "cptlo", settings.elrsPitchLow }
        }, ELRS_GIMBAL_INPUT_ELEVATOR }
    };

    for(size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); i++) {
        wmAppendCRSFCALAxis(html, axes[i]);
    }

    html += crsfExpoScript;
    html += crsfCalScript;

    if(op == WM_CP_LEN) {
        wmLenBuf = html.length() + 1;
        return (const char *)&wmLenBuf;
    }

    char *str = (char *)malloc(html.length() + 1);
    if(!str) {
        return NULL;
    }
    strcpy(str, html.c_str());
    return str;
}

static const char *wmBuildCRSFRC(const char *dest, int op)
{
    return wmBuildCRSFGimbalChannelSelect(dest, op, "'>Aileron target channel", "crlch", settings.elrsRollCh);
}
static const char *wmBuildCRSFPC(const char *dest, int op)
{
    return wmBuildCRSFGimbalChannelSelect(dest, op, "'>Elevator target channel", "cptch", settings.elrsPitchCh);
}
static const char *wmBuildCRSFTC(const char *dest, int op)
{
    return wmBuildCRSFGimbalChannelSelect(dest, op, "'>Throttle target channel", "cthch", settings.elrsThrCh);
}
static const char *wmBuildCRSFYC(const char *dest, int op)
{
    return wmBuildCRSFGimbalChannelSelect(dest, op, "'>Rudder target channel", "cywch", settings.elrsYawCh);
}

static const char crsfDisplayScript[] =
 R"JS(<script>(function(){
var byId=function(id){return document.getElementById(id);},source=byId('cdsrc'),hidden=byId('cdsrcvalue'),preview=byId('cdpreview'),status=byId('cdstatus'),busy=false;
function query(){var unit=byId('cspdu');return 'cdsrc='+encodeURIComponent(hidden.value)+'&cdmul='+encodeURIComponent(byId('cdmul').value)+'&cdoff='+encodeURIComponent(byId('cdoff').value)+'&cddec='+encodeURIComponent(byId('cddec').value)+(unit?'&cspdu='+encodeURIComponent(unit.value):'')+['crpoles','crgear','crdiam','crscale','crrpmtype'].map(function(id){return '&'+id+'='+encodeURIComponent(byId(id).value);}).join('');}
function scaling(){var disabled=hidden.value==='0'||hidden.value==='13'||hidden.value==='14';['cdmul','cdoff','cddec'].forEach(function(id){byId(id).disabled=disabled;});byId('rpmspeed').hidden=hidden.value!=='15'&&hidden.value!=='16';byId('rpmscale').hidden=hidden.value!=='16';byId('rpmpoles').hidden=byId('crrpmtype').value==='1';}
function options(rows){var wanted=hidden.value,label=source.selectedOptions.length?source.selectedOptions[0].textContent:'Saved source';source.textContent='';
function add(id,text,disabled){var option=document.createElement('option');option.value=String(id);option.textContent=text;option.disabled=!!disabled;source.appendChild(option);}
add(14,'None (normal display)');add(0,'Auto');add(13,'Off');rows.forEach(function(row){add(row.id,row.label+' ('+row.unit+')');});
if(!Array.from(source.options).some(function(option){return option.value===wanted;}))add(wanted,label.replace(/ \(unavailable\)$/,'')+' (unavailable)',true);source.value=wanted;}
function fail(message){options([]);preview.textContent='---';status.textContent=message;}
function poll(){if(busy||source.dataset.live!=='1')return;if(byId('remote-controls')?.dataset.busy==='1')return;busy=true;var pending=query();fetch('/elrstelemetry?'+pending,{cache:'no-store'}).then(function(response){if(!response.ok)throw Error(response.status===400?'Invalid display settings':'Telemetry unavailable');return response.json();}).then(function(data){options(data.sources);if(pending===query()){preview.textContent=data.preview.text||'Blank';status.textContent=data.preview.source===14?'Normal gimbal-controlled speed/speedo':data.preview.source===13?'Normal readout off':data.preview.label+(data.preview.available?': '+data.preview.value+(data.preview.unit?' '+data.preview.unit:''):' — unavailable');}}).catch(function(error){if(pending===query())fail(error.message);}).then(function(){busy=false;});}
source.addEventListener('change',function(){hidden.value=source.value;scaling();preview.textContent='…';poll();});
['cdmul','cdoff','cddec','cspdu','crpoles','crgear','crdiam','crscale','crrpmtype'].forEach(function(id){var input=byId(id);if(input)input.addEventListener('input',function(){scaling();preview.textContent='…';poll();});});
scaling();poll();setInterval(poll,500);
})();</script>)JS"
;

static const char *wmBuildCRSFDisplay(const char *dest, int op)
{
    if(op == WM_CP_DESTROY) { if(dest) free((void *)dest); return NULL; }
    if(!haveNewBoard) return NULL;
    String html;
    char buf[220];
    html.reserve(sizeof(crsfDisplayScript) + 1700);
    html += "<fieldset style='min-width:0;white-space:normal'><legend>3 digit display</legend><label for='cdsrc'>Display source</label>";
    snprintf(buf, sizeof(buf), "<select id='cdsrc' data-live='%u'>", opModeCRSF ? 1 : 0);
    html += buf;
    const uint8_t choices[] = {ELRS_DISPLAY_NONE, ELRS_DISPLAY_AUTO, ELRS_DISPLAY_OFF, crsfDisplayConfig.source};
    for(int i = 0; i < 4; i++) {
        const uint8_t id = choices[i];
        if(i == 3 && (id == ELRS_DISPLAY_AUTO || id == ELRS_DISPLAY_OFF || id == ELRS_DISPLAY_NONE)) continue;
        snprintf(buf, sizeof(buf), "<option value='%u'%s%s>%s%s</option>", id,
                 id == crsfDisplayConfig.source ? " selected" : "", i == 3 ? " disabled" : "",
                 elrsTelemetrySourceLabel(id), i == 3 ? " (unavailable)" : "");
        html += buf;
    }
    snprintf(buf, sizeof(buf), "</select><input type='hidden' id='cdsrcvalue' name='cdsrc' value='%u'>", crsfDisplayConfig.source);
    html += buf;
    snprintf(buf, sizeof(buf), "<label for='cdmul'>Multiplier</label><input type='number' id='cdmul' name='cdmul' min='-1000' max='1000' step='any' value='%.9g'>", crsfDisplayConfig.multiplier);
    html += buf;
    snprintf(buf, sizeof(buf), "<label for='cdoff'>Offset</label><input type='number' id='cdoff' name='cdoff' min='-999' max='999' step='any' value='%.9g'>", crsfDisplayConfig.offset);
    html += buf;
    html += "<label for='cddec'>Decimal places</label><select id='cddec' name='cddec'>";
    const uint8_t precision[] = {255,0,1,2};
    const char *labels[] = {"Source default","0","1","2"};
    for(int i = 0; i < 4; i++) {
        snprintf(buf, sizeof(buf), "<option value='%u'%s>%s</option>", precision[i], precision[i] == crsfDisplayConfig.decimalPlaces ? " selected" : "", labels[i]);
        html += buf;
    }
    html += "</select><p>Preview: <output id='cdpreview' style='font-family:monospace;font-size:1.5em' aria-live='polite'>---</output><br><small id='cdstatus'>Waiting for received telemetry</small></p>";
    html += "<fieldset id='rpmspeed' hidden><legend>Motor RPM speed</legend>";
    html += "<label for='crrpmtype'>RPM input</label><select id='crrpmtype' name='crrpmtype'>";
    const char *rpmLabels[] = {"Electrical RPM", "Shaft RPM (already corrected)"};
    for(int i = 0; i < 2; i++) {
        snprintf(buf, sizeof(buf), "<option value='%u'%s>%s</option>", (unsigned)i, i == crsfVehicleConfig.rpmType ? " selected" : "", rpmLabels[i]);
        html += buf;
    }
    html += "</select>";
    const char *ids[] = {"crpoles", "crgear", "crdiam", "crscale"};
    const char *vehicleLabels[] = {"Motor poles", "Final drive ratio", "Tire outside diameter (mm)", "Scale factor"};
    const float values[] = {(float)crsfVehicleConfig.motorPoles, crsfVehicleConfig.gearRatio, crsfVehicleConfig.tireDiameterMm, crsfVehicleConfig.scaleFactor};
    for(int i = 0; i < 4; i++) {
        if(i == 0) html += "<div id='rpmpoles'>";
        if(i == 3) html += "<div id='rpmscale' hidden>";
        snprintf(buf, sizeof(buf), "<label for='%s'>%s</label><input type='number' id='%s' name='%s' min='%.9g' max='%u' step='%s' value='%.9g'%s>",
                 ids[i], vehicleLabels[i], ids[i], ids[i], i == 0 ? 2.0f : (i == 2 ? 1.0f : 0.01f),
                 i == 0 ? 64U : 1000U, i == 0 ? "2" : "any", values[i], i == 1 ? " list='rpmgears'" : "");
        html += buf;
        if(i == 0 || i == 3) html += "</div>";
    }
    html += "<datalist id='rpmgears'><option value='6.55'>TT-02R 68T/27T</option><option value='6.16'>TT-02R 64T/27T</option></datalist><p><small>Electrical RPM is divided by motor pole pairs; select Shaft RPM if the sensor already reports mechanical RPM. Actual mph uses total motor-to-wheel reduction and tire diameter. Scaled mph also applies the scale factor (10 for a 1:10 model). Use Multiplier for calibration (default 1) and Offset 0. Both RPM sources use mph. TT-02R/Firma defaults: Electrical RPM, 4 poles, 6.55:1, 64 mm tires.</small></p></fieldset>";
    html += "<p><small>Output = value × multiplier + offset. Speed units apply before scaling. Only fresh received sources are offered. HI/LO means the scaled value exceeds three digits; --- means unavailable. Auto uses GPS speed, airspeed, then uplink LQ with default scaling. Off blanks the normal readout. None keeps the gimbal speed/speedometer in Prop controls + ELRS/CRSF; in telemetry mode it uses Auto. Temporary display messages retain priority. Save and restart to apply; preview does not change the display.</small></p></fieldset>";
    html += "<fieldset><legend>88 speed time travel</legend><input type='hidden' name='cttform' value='1'>";
    snprintf(buf, sizeof(buf), "<label for='cttenabled'><input type='checkbox' id='cttenabled' name='cttenabled' value='1'%s> Lights and time travel sound at 88</label>", crsfTimeTravelConfig.enabled ? " checked" : "");
    html += buf;
    html += "<label for='cttch'>Receiver lights channel</label><select id='cttch' name='cttch'><option value='0'>None</option>";
    for(unsigned channel = 1; channel <= 16; ++channel) {
        snprintf(buf, sizeof(buf), "<option value='%u'%s>CH%u</option>", channel, channel == crsfTimeTravelConfig.channel ? " selected" : "", channel);
        html += buf;
    }
    html += "</select><p><small>Lights follow timetravel.mp3 at displayed speed 88. Rearms below 88. Takes over this switch channel.</small></p></fieldset>";
    html += crsfDisplayScript;
    if(op == WM_CP_LEN) {
        wmLenBuf = html.length() + 1;
        return (const char *)&wmLenBuf;
    }
    char *result = (char *)malloc(html.length() + 1);
    if(result) strcpy(result, html.c_str());
    return result;
}

static void handleELRSTelemetryRead()
{
    ELRSDisplayConfig config = loadELRSDisplayConfig();
    ELRSVehicleConfig vehicle = loadELRSVehicleConfig();
    uint8_t units = elrsMode.speedDisplayUnits();
    if(!crsfParseDisplayParams(config) || !crsfParseVehicleParams(vehicle)) {
        wm.server->send(400, "application/json", "{\"ok\":false}"); return;
    }
    if(wm.server->hasArg("cspdu")) {
        String value = wm.server->arg("cspdu");
        if(value.length() != 1 || (value[0] != '0' && value[0] != '1')) {
            wm.server->send(400, "application/json", "{\"ok\":false}"); return;
        }
        units = value[0] - '0';
    }
    const uint32_t now = millis();
    String json = "{\"ok\":true,\"sources\":[";
    json.reserve(1024);
    char buf[280], value[32];
    bool first = true;
    for(uint8_t source = ELRS_DISPLAY_GPS_SPEED; source <= ELRS_DISPLAY_RPM_SCALED_MPH; source++) {
        const ELRSTelemetrySample sample = elrsMode.telemetrySample(source, now, &vehicle);
        if(!sample.available) continue;
        snprintf(buf, sizeof(buf), "%s{\"id\":%u,\"label\":\"%s\",\"unit\":\"%s\"}",
                 first ? "" : ",", source, elrsTelemetrySourceLabel(source), elrsTelemetrySourceUnit(source, ELRS_SPEED_UNITS_KMH));
        json += buf;
        first = false;
    }
    const ELRSDisplayConfig effective = elrsEffectiveDisplayConfig(config, opModePropCRSF);
    const ELRSTelemetrySample sample = elrsMode.telemetrySample(effective.source, now, &vehicle);
    if(sample.available) snprintf(value, sizeof(value), "%.9g", sample.source <= ELRS_DISPLAY_AIRSPEED && units == ELRS_SPEED_UNITS_MPH ? sample.value * 0.621371192f : sample.value);
    else strcpy(value, "null");
    char text[8];
    elrsFormatTelemetry(effective, sample, units, text);
    snprintf(buf, sizeof(buf), "],\"preview\":{\"source\":%u,\"label\":\"%s\",\"unit\":\"%s\",\"value\":%s,\"available\":%s,\"text\":\"%s\"}}",
             sample.available ? sample.source : effective.source, elrsTelemetrySourceLabel(sample.available ? sample.source : effective.source),
             elrsTelemetrySourceUnit(sample.available ? sample.source : effective.source, units), value, sample.available ? "true" : "false", text);
    json += buf;
    wm.server->send(200, "application/json", json);
}

static void handleELRSRawRead()
{
    int16_t axes[ELRS_GIMBAL_AXIS_COUNT];
    char buf[128];

    if(!readELRSCurrentRawAxes(axes)) {
        wm.server->send(503, "application/json", "{\"ok\":false}");
        return;
    }

    snprintf(buf, sizeof(buf),
             "{\"ok\":true,\"roll\":%d,\"pitch\":%d,\"throttle\":%d,\"yaw\":%d}",
             axes[ELRS_GIMBAL_INPUT_AILERON],
             axes[ELRS_GIMBAL_INPUT_ELEVATOR],
             axes[ELRS_GIMBAL_INPUT_THROTTLE],
             axes[ELRS_GIMBAL_INPUT_RUDDER]);
    wm.server->send(200, "application/json", buf);
}

static void crsfReadInputParam(const char *name, char *destBuf, size_t length, int minval, int maxval, int offset)
{
    String value = wm.server->arg(name);
    const char *text = value.c_str();
    char *end;
    long parsed = strtol(text, &end, 10);
    bool valid = *text && !*end && value.length() == strlen(text) &&
        parsed >= minval - offset && parsed <= maxval - offset;
    // Mark invalid input for replacement with defaults at save time.
    snprintf(destBuf, length + 1, "%ld", valid ? parsed + offset : -1L);
}

static void crsfReadGimbalChannelParam(const char *name, char *destBuf)
{
    String value = wm.server->arg(name);
    const char *text = value.c_str();
    long channel = strtol(text, NULL, 10);
    bool valid = value.length() >= 1 && value.length() <= 2 && strspn(text, "0123456789") == value.length() && channel <= 16;
    // Retain HTTP0..15 for CH1..16; the appended option16 means no channel.
    snprintf(destBuf, sizeof(settings.elrsRollCh), "%ld", valid ? (channel == 16 ? 0 : channel + 1) : -1L);
}


static bool crsfParseLogicNumber(const String &value, int minimum, int maximum, long &number)
{
    const size_t length = value.length();
    if(!length || length > 4 || length != strlen(value.c_str())) return false;
    const size_t start = value[0] == '-' && minimum < 0 ? 1 : 0;
    if(start == length) return false;
    for(size_t i = start; i < length; i++) if(value[i] < '0' || value[i] > '9') return false;
    number = strtol(value.c_str(), nullptr, 10);
    return number >= minimum && number <= maximum;
}

static bool crsfReadLogicMixForm(bool mixes)
{
    const char *marker = mixes ? "cmform" : "clform";
    if(!wm.server->hasArg(marker)) return true;
    const String value = wm.server->arg(marker);
    if(value.length() != 1 || value[0] != '1') return false;
    const char * const fields[2][6] = {{"clfn","cla","clb","clx","cland",nullptr},
                                     {"cmch","cmsrc","cmsw","cmwt","cmoff","cmmux"}};
    const uint8_t count = mixes ? sizeof(ELRSMixLine) : sizeof(ELRSLogicRule);
    uint8_t *rows = mixes ? (uint8_t *)&crsfMixConfig : (uint8_t *)&crsfLogicConfig;
    for(uint8_t i = 0; i < (mixes ? ELRS_MIX_COUNT : ELRS_LOGIC_COUNT); i++) for(uint8_t j = 0; j < count; j++) {
        char name[10];
        snprintf(name, sizeof(name), "%s%u", fields[mixes][j], i);
        if(!wm.server->hasArg(name)) return false;
        const bool signedField = j == 3 || (mixes && j == 4);
        const int maximum = signedField ? 100 : j == 0 ? (mixes ? 16 : 5) : mixes && j == 5 ? 2 : 255;
        long number;
        if(!crsfParseLogicNumber(wm.server->arg(name), signedField ? -100 : 0, maximum, number)) return false;
        rows[i * count + j] = (uint8_t)number;
    }
    return true;
}

static void crsfReadLogicParams()
{
    loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &crsfLogicConfig);
    crsfLogicParamsValid = crsfReadLogicMixForm(false) && elrsIsValidLogicConfig(crsfLogicConfig);
}

static void crsfReadMixParams()
{
    loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &crsfMixConfig);
    crsfMixParamsValid = crsfReadLogicMixForm(true) && elrsIsValidMixConfig(crsfMixConfig);
}

static const char crsfLogicMixScript[] =
 R"JS(function init(){const g=id=>document.getElementById(id),ln=i=>'L'+String(i+1).padStart(2,'0'),names=['---','Aileron','Elevator','Rudder','Throttle','Stop','FakePower','O.O','RESET',...Array.from({length:8},(_,i)=>'ButtonPack '+(i+1)),...Array.from({length:16},(_,i)=>ln(i)),'MAX'],fn=['Choose Func','a>x','a<x','AND','OR','XOR'],mux=['Add','Multiply','Replace'],lf=['clfn','cla','clb','clx','cland'],mf=['cmch','cmsrc','cmsw','cmwt','cmoff','cmmux'];names.splice(25,0,names.pop());let t=0,k=-1,ch=1,v=[],c=[];const d=g('lme'),f=g('lmfields'),base=[];for(let[id,src]of[['crlch',1],['cptch',2],['cywch',3],['cthch',4]]){let e=g(id);if(e)base.push([src,e,document.querySelectorAll('[data-elrs-reverse="'+id+'"]')[0],null])}for(let i=0;i<12;i++){let e=g('csw'+i);if(e)base.push([i+5,e,null,g('csa'+i)])}for(let b of base){b[1].setAttribute('aria-label','Channel for '+names[b[0]]);let box=b[2]||b[3];if(box)box.setAttribute('aria-label',(b[2]?'Reverse ':'Trigger Settings action for ')+names[b[0]])}function channel(b){let n=Number(b[1].value);return b[1].hasAttribute('data-elrs-switch')?n:(n===16?0:n+1)}function el(tag,text){let e=document.createElement(tag);if(text!==undefined)e.textContent=text;return e}function btn(text,cb){let e=el('button',text);e.type='button';e.onclick=cb;return e}function label(id){return(id&128?'!':'')+names[id&127]}function bools(max){let a=[[0,'---']];for(let i=5;i<=max;i++)if(i!==25&&(i<17||L[i-17-(i>25)][0]))a.push([i,names[i]],[i|128,'!'+names[i]]);return a}function used(i){let id=17+i+(i>=8);return[[L,[1,2,4]],[M,[1,2]]].some(([a,b])=>a.some(r=>r[0]&&b.some(j=>(r[j]&127)===id)))}function field(j,text,opts,required){let l=el('label',text+' '),e=el(opts?'select':'input');if(opts)for(let [id,name]of opts){let o=el('option',name);o.value=id;e.appendChild(o)}else{e.type='number';e.min=-100;e.max=100;e.step=1;e.required=true}e.value=v[j];if(opts&&!opts.some(x=>x[0]===v[j])){let o=el('option','Missing '+label(v[j]));o.value=v[j];o.disabled=true;e.appendChild(o);e.value=v[j]}e.opts=opts;e.req=required;l.appendChild(e);f.appendChild(l);c[j]=e;return e}function draw(){f.replaceChildren();c=[];if(!t){let e=field(0,'Func',fn.map((x,i)=>[i,x]),true);e.onchange=()=>{v=[Number(e.value),0,0,0,0];draw()};if(v[0]){field(1,'V1',v[0]<=2?[[0,'Choose source'],...names.slice(1,5).map((x,i)=>[i+1,x])]:bools(16+k+(k>8)),true);field(v[0]<=2?3:2,'V2'+(v[0]<=2?' (%)':''),v[0]<=2?null:bools(16+k+(k>8)),v[0]>2);field(4,'AND switch',bools(16+k+(k>8)))}}else{field(0,'Channel',Array.from({length:16},(_,i)=>[i+1,'CH'+(i+1)]));field(1,'Source',[[0,'Choose source'],...names.slice(1,5).map((x,i)=>[i+1,x]),...bools(33).slice(1),[25,'MAX']],true);field(3,'Weight (%)');field(4,'Offset (%)');field(2,'Switch',bools(33));field(5,'Multiplex',mux.map((x,i)=>[i,x]))}}function edit(type,i,fresh){if(!fresh&&!(type?M:L)[i][0])return;t=type;k=i;v=fresh||((t?M:L)[i].slice());g('lmtitle').textContent=t?'Mix':ln(i);if(t)ch=v[0];draw();d.showModal();refresh()}function hidden(name,value){let e=el('input');e.type='hidden';e.name=name;e.value=value;g('lmvalues').appendChild(e)}function row(table,values,actions,selected){let r=el('tr');if(selected)r.className='lmselected';for(let value of values){let cell=el('td');if(typeof value==='string')cell.textContent=value;else cell.appendChild(value);r.appendChild(cell)}let a=el('td');for(let b of actions)a.appendChild(b);r.appendChild(a);table.appendChild(r)}function refresh(){let active=document.activeElement;g('lmvalues').replaceChildren();hidden('clform',1);hidden('cmform',1);for(let [a,keys]of[[L,lf],[M,mf]])a.forEach((r,i)=>r.forEach((x,j)=>hidden(keys[j]+i,x)));let l=g('lml'),m=g('lmm');l.replaceChildren();m.replaceChildren();L.forEach((r,i)=>{if(!r[0])return;let del=btn('Delete',()=>{if(used(i))return;L[i]=[0,0,0,0,0];refresh()});del.disabled=used(i);del.title=del.disabled?'Change references first':'';row(l,[ln(i),fn[r[0]],label(r[1]),r[0]>2?label(r[2]):r[3]+'%',label(r[4])],[btn('Edit',()=>edit(0,i)),del],!t&&k===i)});for(let n=0;n<=16;n++){for(let b of base.filter(b=>channel(b)===n))row(m,[b[1],'Base '+names[b[0]],'100%','---','Replace',b[2]||'---',b[3]||'---'],[]);M.forEach((r,i)=>{if(!r[0]||r[0]!==n)return;function move(dir){let j=i+dir;while(j>=0&&j<M.length&&M[j][0]!==n)j+=dir;if(j>=0&&j<M.length){[M[i],M[j]]=[M[j],M[i]];t=1;ch=n;k=j;refresh()}}row(m,['CH'+n,label(r[1]),r[3]+'%',label(r[2]),mux[r[5]],'---','---'],[btn('Edit',()=>edit(1,i)),btn('Up',()=>move(-1)),btn('Down',()=>move(1)),btn('Delete',()=>{M[i]=[0,0,0,0,0,0];refresh()})],t&&k===i)})}for(let[a,id,text]of[[L,'ll','rows'],[M,'lm','additional lines']]){let n=a.filter(r=>r[0]).length;g(id+'count').textContent=n+'/'+a.length+' '+text;g(id+'add').disabled=n===a.length}if(active&&base.some(b=>b.includes(active)))active.focus()}g('lmapply').onclick=()=>{for(let e of c.filter(Boolean)){let n=Number(e.value);e.setCustomValidity(e.req&&!n||e.opts&&!e.opts.some(x=>x[0]===n)?'Choose an available value':'');if(!e.reportValidity()||!Number.isInteger(n))return}for(let j=0;j<c.length;j++)if(c[j])v[j]=Number(c[j].value);if(!t&&!v[0])v=[0,0,0,0,0];if(t)ch=v[0];(t?M:L)[k]=v;d.oncancel();refresh()};d.oncancel=g('lmcancel').onclick=()=>{f.replaceChildren();d.close()};d.addEventListener('keydown',e=>{if(e.key==='Enter'&&e.target.tagName!=='BUTTON'){e.preventDefault();g('lmapply').click()}});for(let[a,id,type]of[[L,'ll',0],[M,'lm',1]])g(id+'add').onclick=()=>{let i=a.findIndex(r=>!r[0]);if(i>=0)edit(type,i,type?[ch,0,0,100,0,0]:[0,0,0,0,0])};for(let e of document.querySelectorAll('#crlch,#cptch,#cthch,#cywch,[data-elrs-switch]'))e.addEventListener('change',refresh);refresh()}if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',init);else init();})();</script>)JS"
;

static const char crsfLogicMixIntro[] =
 "<style>.lms{overflow-x:auto}.lmt{width:100%;font-size:.8em;border-collapse:collapse}.lmt td,.lmt th{padding:3px;text-align:left;border-bottom:1px solid #ddd}.lmselected{background:#e8eef5}.lmt button{width:auto;padding:3px;margin:1px}#lmm select{width:100%;min-width:65px;padding:3px;margin:0}#lmm input[type=checkbox]{margin:0}#lme{width:85%;max-width:400px;max-height:80vh;overflow:auto}#lmfields label{display:block;margin:10px 0}#lmfields label>*{width:100%}</style><h3>Logical Switches</h3><small>! = Not.</small><button type='button' id='lladd'>Add logical switch</button> <small id='llcount'></small><div class='lms'><table class='lmt'><tr><th>Switch<th>Func<th>V1<th>V2<th>AND switch</tr><tbody id='lml'></tbody></table></div><h3>Mixes</h3><small>Base assignments and additional mixes. Source=value; Switch=gate. Save and restart to apply.</small><button type='button' id='lmadd'>Add mix</button> <small id='lmcount'></small><div class='lms'><table class='lmt'><tr><th>Channel<th>Source<th>Weight<th>Switch<th>Multiplex<th>Reverse<th>Settings action<th></tr><tbody id='lmm'></tbody></table></div><div id='lmvalues'></div><dialog id='lme' aria-labelledby='lmtitle'><h4 id='lmtitle'></h4><div id='lmfields'></div><button type='button' id='lmapply'>Apply</button> <button type='button' id='lmcancel'>Cancel</button></dialog><noscript>Enable JavaScript.</noscript><script>(function(){const L=["
;

static const char *wmBuildCRSFLogicMix(const char *dest, int op)
{
    if(op == WM_CP_DESTROY) { if(dest) free((void *)dest); return nullptr; }
    String html;
    html.reserve(sizeof(crsfLogicMixIntro) + sizeof(crsfLogicMixScript) + 1200);
    html += crsfLogicMixIntro;
    char buf[56];
    for(int i = 0; i < ELRS_LOGIC_COUNT; i++) {
        const auto &r = crsfLogicConfig.rules[i];
        snprintf(buf, sizeof(buf), "%s[%u,%u,%u,%d,%u]", i ? "," : "", r.function, r.sourceA, r.sourceB, r.threshold, r.andSwitch);
        html += buf;
    }
    html += "],M=[";
    for(int i = 0; i < ELRS_MIX_COUNT; i++) {
        const auto &r = crsfMixConfig.lines[i];
        snprintf(buf, sizeof(buf), "%s[%u,%u,%u,%d,%d,%u]", i ? "," : "", r.channel, r.source, r.switchSource, r.weight, r.offset, r.multiplex);
        html += buf;
    }
    html += "];";
    html += crsfLogicMixScript;
    if(op == WM_CP_LEN) {
        wmLenBuf = html.length() + 1;
        return (const char *)&wmLenBuf;
    }
    char *result = (char *)malloc(html.length() + 1);
    if(result) strcpy(result, html.c_str());
    return result;
}

#include "crsf_config_portal.h"

#endif
