
#ifdef HAVE_CRSF

#include "elrs_crsf.h"

static const char *wmBuildCRSFStatus(const char *dest, int op);
static const char *wmBuildCRSFOM(const char *dest, int op);
static const char *wmBuildCRSFPR(const char *dest, int op);
static const char *wmBuildCRSFSU(const char *dest, int op);
static const char *wmBuildCRSFTR(const char *dest, int op);
static const char *wmBuildCRSFMP(const char *dest, int op);
static const char *wmBuildCRSFDP(const char *dest, int op);
static const char *wmBuildCRSFRC(const char *dest, int op);
static const char *wmBuildCRSFPC(const char *dest, int op);
static const char *wmBuildCRSFTC(const char *dest, int op);
static const char *wmBuildCRSFYC(const char *dest, int op);
static const char *wmBuildCRSFCAL(const char *dest, int op);
static const char *wmBuildCRSFOutputLimits(const char *dest, int op);
static void crsfReadOutputLimitParams();
static const char *wmBuildCRSFSwitchMap(const char *dest, int op);
static void crsfReadSwitchParams();
static void crsfReadExpoParams();

static void syncCRSFPortalBuffers();
static bool saveCRSFPortalInputSettings();
static void crsfReadInputParam(const char *name, char *destBuf, size_t length, int minval, int maxval, int offset);
static uint8_t crsfRoutingChannel(const ELRSGimbalRouting &routing, uint8_t axis);
static void crsfSetRoutingChannel(ELRSGimbalRouting &routing, uint8_t axis, uint8_t channel);

static const char *cOpModeCustHTMLSrc[4] = {
    "'>Operation mode",
    "copm",
    ">Legacy%s1'",
    ">ELRS/CRSF%s"
};
static const char *cPktRateCustHTMLSrc[7] = {
    "'>ELRS Packet rate",
    "cpktr",
    ">50 Hz%s1'",
    ">100 Hz%s2'",
    ">150 Hz%s3'",
    ">250 Hz%s4'",
    ">500 Hz%s"
};
static const char *cSpdUnitCustHTMLSrc[4] = {
    "'>Speed units",
    "cspdu",
    ">km/h%s1'",
    ">mph%s"
};
static const char *cTlmRatioCustHTMLSrc[9] = {
    "'>Telemetry Ratio",
    "ctlmr",
    ">Std%s1'",
    ">1:2%s2'",
    ">1:4%s3'",
    ">1:8%s4'",
    ">1:16%s5'",
    ">1:32%s6'",
    ">Off%s"
};
static const char *cMaxPowerCustHTMLSrc[8] = {
    "'>Max Power",
    "cmpwr",
    ">10 mW%s1'",
    ">25 mW%s2'",
    ">100 mW%s3'",
    ">250 mW%s4'",
    ">500 mW%s5'",
    ">1000 mW%s"
};
static const char *cDynPowerCustHTMLSrc[4] = {
    "'>Dynamic Power",
    "cdynp",
    ">Off%s1'",
    ">Dyn%s"
};
static const char *cChannelCustHTMLSrc[16] = {
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
    CRSF_SELECT_TLMRATIO,
    CRSF_SELECT_MAXPOWER,
    CRSF_SELECT_DYNPOWER,
    CRSF_SELECT_COUNT
};

struct CRSFSelectField {
    const char **html;
    int count;
    char *setting;
};

static CRSFSelectField crsfSelectFields[CRSF_SELECT_COUNT] = {
    { cOpModeCustHTMLSrc, 4, settings.opMode },
    { cPktRateCustHTMLSrc, 7, settings.elrsPktRate },
    { cSpdUnitCustHTMLSrc, 4, settings.elrsSpdUnit },
    { cTlmRatioCustHTMLSrc, 9, settings.elrsTlmRatio },
    { cMaxPowerCustHTMLSrc, 8, settings.elrsMaxPower },
    { cDynPowerCustHTMLSrc, 4, settings.elrsDynPower }
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

static CRSFAxisSettings crsfAxisSettings[] = {
    { ELRS_GIMBAL_INPUT_AILERON, settings.elrsRollCh, settings.elrsRollRev, settings.elrsRollLow, settings.elrsRollCtr, settings.elrsRollHigh, "crlexp", settings.elrsAxisExpo[ELRS_GIMBAL_INPUT_AILERON] },
    { ELRS_GIMBAL_INPUT_ELEVATOR, settings.elrsPitchCh, settings.elrsPitchRev, settings.elrsPitchLow, settings.elrsPitchCtr, settings.elrsPitchHigh, "cptexp", settings.elrsAxisExpo[ELRS_GIMBAL_INPUT_ELEVATOR] },
    { ELRS_GIMBAL_INPUT_THROTTLE, settings.elrsThrCh, settings.elrsThrRev, settings.elrsThrLow, settings.elrsThrCtr, settings.elrsThrHigh, "cthexp", settings.elrsAxisExpo[ELRS_GIMBAL_INPUT_THROTTLE] },
    { ELRS_GIMBAL_INPUT_RUDDER, settings.elrsYawCh, settings.elrsYawRev, settings.elrsYawLow, settings.elrsYawCtr, settings.elrsYawHigh, "cywexp", settings.elrsAxisExpo[ELRS_GIMBAL_INPUT_RUDDER] }
};

static char crsfOutputMin[ELRS_GIMBAL_AXIS_COUNT][5] = {"1000", "1000", "1000", "1000"};
static char crsfOutputMax[ELRS_GIMBAL_AXIS_COUNT][5] = {"2000", "2000", "2000", "2000"};

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
CRSF_SELECT_BUILDER(wmBuildCRSFTR, CRSF_SELECT_TLMRATIO)
CRSF_SELECT_BUILDER(wmBuildCRSFMP, CRSF_SELECT_MAXPOWER)
CRSF_SELECT_BUILDER(wmBuildCRSFDP, CRSF_SELECT_DYNPOWER)

WiFiManagerParameter custom_crsfom(wmBuildCRSFOM, WFM_SECTS_HEAD);
WiFiManagerParameter custom_ss_crsf("<h3>ELRS/CRSF Settings</h3>", WFM_SECTS|WFM_HL);
WiFiManagerParameter custom_crsfstatus(wmBuildCRSFStatus);
WiFiManagerParameter custom_crsfap("Connect to WiFi in ELRS/CRSF mode<br><span>If unchecked, device will remain in AP mode.</span>", settings.crsfap, "", WFM_LABEL_AFTER|WFM_IS_CHKBOX);
WiFiManagerParameter custom_crsfpr(wmBuildCRSFPR);
WiFiManagerParameter custom_crsfsu(wmBuildCRSFSU);
WiFiManagerParameter custom_crsftr(wmBuildCRSFTR);
WiFiManagerParameter custom_crsfmp(wmBuildCRSFMP);
WiFiManagerParameter custom_crsfdp(wmBuildCRSFDP);
WiFiManagerParameter custom_ss_crsfmap("<h3>Channel Mappings</h3><p style='margin:0 0 10px'><small>Changes apply after saving and restarting.</small></p>", WFM_SECTS|WFM_HL);
WiFiManagerParameter custom_crsfrc(wmBuildCRSFRC);
WiFiManagerParameter custom_crsfrr("Reverse Aileron", settings.elrsRollRev, "class='mt5 ml20'", WFM_LABEL_AFTER|WFM_IS_CHKBOX);
WiFiManagerParameter custom_crsfpc(wmBuildCRSFPC);
WiFiManagerParameter custom_crsfprv("Reverse Elevator", settings.elrsPitchRev, "class='mt5 ml20'", WFM_LABEL_AFTER|WFM_IS_CHKBOX);
WiFiManagerParameter custom_crsftc(wmBuildCRSFTC);
WiFiManagerParameter custom_crsftrv("Reverse Throttle", settings.elrsThrRev, "class='mt5 ml20'", WFM_LABEL_AFTER|WFM_IS_CHKBOX);
WiFiManagerParameter custom_crsfyc(wmBuildCRSFYC);
WiFiManagerParameter custom_crsfyrv("Reverse Rudder", settings.elrsYawRev, "class='mt5 ml20'", WFM_LABEL_AFTER|WFM_IS_CHKBOX);
WiFiManagerParameter custom_crsfswmap(wmBuildCRSFSwitchMap);
WiFiManagerParameter custom_crsflimits(wmBuildCRSFOutputLimits);
WiFiManagerParameter custom_ss_crsfcal("<h3>Gimbal Calibration</h3>", WFM_SECTS|WFM_HL);
WiFiManagerParameter custom_crsfcal(wmBuildCRSFCAL, WFM_FOOT);

WiFiManagerParameter *crsfParmArray[] = {
      &custom_crsfom,
      &custom_ss_crsf,
      &custom_crsfstatus,
      &custom_crsfap,
      &custom_crsfpr,
      &custom_crsfsu,
      &custom_crsftr,
      &custom_crsfmp,
      &custom_crsfdp,
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
      &custom_crsflimits,
      &custom_ss_crsfcal,
      &custom_crsfcal,
      NULL
};


/*
 * Save input settings before the portal reports success.
 *
 */
static bool crsf_wifi_loop_settings()
{
    evalCB(settings.crsfap, &custom_crsfap);
    evalCB(settings.elrsRollRev, &custom_crsfrr);
    evalCB(settings.elrsPitchRev, &custom_crsfprv);
    evalCB(settings.elrsThrRev, &custom_crsftrv);
    evalCB(settings.elrsYawRev, &custom_crsfyrv);
    if(saveCRSFPortalInputSettings()) {
        if(opModeCRSF) {
            requestELRSModuleConfigUpdate((uint8_t)atoi(settings.elrsTlmRatio),
                                          (uint8_t)atoi(settings.elrsMaxPower),
                                          (uint8_t)atoi(settings.elrsDynPower));
        }
        return true;
    }
    Serial.println("Config Portal: Failed to save CRSF input settings");
    return false;
}

/*
 * Callback from saveParamsCallback()
 *
 */
static void crsf_wifi_saveParamsCallback()
{
    getServerParam("copm", settings.opMode, 1, 0, 1, 0);
    getServerParam("cpktr", settings.elrsPktRate, 1, 0, 4, DEF_ELRSPKTRATE);
    getServerParam("cspdu", settings.elrsSpdUnit, 1, 0, 1, DEF_ELRSSPDUNIT);
    getServerParam("ctlmr", settings.elrsTlmRatio, 1, 0, 6, DEF_ELRSTLMRATIO);
    getServerParam("cmpwr", settings.elrsMaxPower, 1, 0, 5, DEF_ELRSMAXPOWER);
    getServerParam("cdynp", settings.elrsDynPower, 1, 0, 1, DEF_ELRSDYNPWR);
    crsfReadInputParam("crlch", settings.elrsRollCh, 2, 1, 16, 1);
    crsfReadInputParam("cptch", settings.elrsPitchCh, 2, 1, 16, 1);
    crsfReadInputParam("cthch", settings.elrsThrCh, 2, 1, 16, 1);
    crsfReadInputParam("cywch", settings.elrsYawCh, 2, 1, 16, 1);
    crsfReadSwitchParams();
    crsfReadOutputLimitParams();
    crsfReadExpoParams();
    if(opModeCRSF) {
        getServerParam("chyst", settings.elrsAdcHysteresis, 2, 0, ELRS_INPUT_TOLERANCE_MAX, ELRS_INPUT_TOLERANCE_DEFAULT);
        getServerParam("cthid", settings.elrsThrIdleDeadband, 2, 0, ELRS_INPUT_TOLERANCE_MAX, ELRS_INPUT_TOLERANCE_DEFAULT);
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

    loadELRSInputConfig(profiles, ELRS_GIMBAL_AXIS_COUNT, &routing, &adcHysteresis, &throttleIdleDeadband, &switches, limits);
    for(int i = 0; i < ELRS_GIMBAL_AXIS_COUNT; i++) {
        snprintf(crsfOutputMin[i], sizeof(crsfOutputMin[i]), "%u", (unsigned)limits[i].minimumUs);
        snprintf(crsfOutputMax[i], sizeof(crsfOutputMax[i]), "%u", (unsigned)limits[i].maximumUs);
    }
    snprintf(settings.elrsAdcHysteresis, sizeof(settings.elrsAdcHysteresis), "%u", (unsigned)adcHysteresis);
    snprintf(settings.elrsThrIdleDeadband, sizeof(settings.elrsThrIdleDeadband), "%u", (unsigned)throttleIdleDeadband);
    for(int i = 0; i < ELRS_SWITCH_INPUT_COUNT; i++) {
        snprintf(settings.elrsSwitchCh[i], sizeof(settings.elrsSwitchCh[i]), "%u", (unsigned)switches.channels[i]);
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

static bool saveCRSFPortalInputSettings()
{
    ELRSInputAxisProfile profiles[ELRS_GIMBAL_AXIS_COUNT];
    ELRSOutputLimits limits[ELRS_GIMBAL_AXIS_COUNT];
    ELRSGimbalRouting routing;
    ELRSSwitchRouting switches;

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
               bounds[side] < ELRS_INPUT_US_MIN || bounds[side] > ELRS_INPUT_US_MAX) return false;
        }
        limits[i] = {(uint16_t)bounds[0], (uint16_t)bounds[1]};
        if(!elrsIsValidOutputLimits(limits[i])) return false;
    }

    for(int i = 0; i < ELRS_SWITCH_INPUT_COUNT; i++) {
        const char *value = settings.elrsSwitchCh[i];
        char *end;
        long channel = strtol(value, &end, 10);
        if(!*value || *end || channel < 1 || channel > 16) return false;
        switches.channels[i] = (uint8_t)channel;
    }
    if(!elrsIsValidSwitchRouting(switches)) return false;

    for(const CRSFAxisSettings &axis : crsfAxisSettings) {
        const char *value = axis.expo;
        size_t length = strlen(value);
        if(length < 1 || length > 3) return false;
        for(size_t i = 0; i < length; i++) if(value[i] < '0' || value[i] > '9') return false;
        if(atoi(value) > 100) return false;
    }

    loadELRSInputConfig(profiles, ELRS_GIMBAL_AXIS_COUNT, &routing);

    for(size_t i = 0; i < sizeof(crsfAxisSettings) / sizeof(crsfAxisSettings[0]); i++) {
        const CRSFAxisSettings &axis = crsfAxisSettings[i];
        ELRSInputAxisProfile &profile = profiles[axis.axis];

        crsfSetRoutingChannel(routing, axis.axis, (uint8_t)atoi(axis.channel));
        profile.reverse = (axis.reverse[0] != '0');
        profile.minimum = (int16_t)atoi(axis.low);
        profile.center = (int16_t)atoi(axis.center);
        profile.maximum = (int16_t)atoi(axis.high);
        profile.expo = (uint8_t)atoi(axis.expo);
    }

    const uint16_t adcHysteresis = (uint16_t)atoi(settings.elrsAdcHysteresis);
    const uint16_t throttleIdleDeadband = (uint16_t)atoi(settings.elrsThrIdleDeadband);
    return saveELRSInputConfig(profiles, ELRS_GIMBAL_AXIS_COUNT, &routing, &adcHysteresis, &throttleIdleDeadband, &switches, limits);
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

static const char *wmBuildSelectOneBased(const char *dest, int op, const char **src, int count, char *setting, bool indent = false)
{
    char tempSetting[3];
    int selectValue = atoi(setting);

    if(selectValue < 1) {
        selectValue = 1;
    } else if(selectValue > (count - 2)) {
        selectValue = count - 2;
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
    html.reserve(10000);
    html += "<div class='cmp0'><h4 style='margin:12px 0 5px'>Switch Channels</h4><p style='margin:0 0 10px;white-space:normal'><small>All inputs can use CH1-CH16. Choose a different channel for each gimbal and switch. ELRS RF mode determines receiver channel availability and resolution. Keep steering and throttle on CH1-CH4 in Hybrid/Wide; use Full Resolution 16ch for all 16 input channels.</small></p>";
    for(int i = 0; i < ELRS_SWITCH_INPUT_COUNT; i++) {
        char name[6];
        snprintf(name, sizeof(name), "csw%d", i);
        html += "<div class='cmp0'><label class='mp0' for='"; html += name; html += "'>"; html += labels[i]; html += " target channel</label><select class='sel0' data-elrs-switch id='";
        html += name; html += "' name='"; html += name; html += "'>";
        for(int channel = 1; channel <= 16; channel++) {
            char value[3];
            snprintf(value, sizeof(value), "%d", channel);
            html += "<option value='"; html += value; html += "'";
            if(channel == atoi(settings.elrsSwitchCh[i])) html += " selected";
            html += ">CH"; html += value; html += "</option>";
        }
        html += "</select></div>";
    }
    html += "</div><script>(function(){var s=document.querySelectorAll('#crlch,#cptch,#cthch,#cywch,[data-elrs-switch]');function c(e){return Number(e.value)+(e.hasAttribute('data-elrs-switch')?0:1);}function v(){var n={};for(var i=0;i<s.length;i++)n[c(s[i])]=(n[c(s[i])]||0)+1;for(var i=0;i<s.length;i++)s[i].setCustomValidity(n[c(s[i])]>1?'Choose a different channel for each input.':'');}for(var i=0;i<s.length;i++)s[i].addEventListener('change',v);v();})();</script>";
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
    const char *html[18];

    html[0] = label;
    html[1] = id;
    for(int i = 0; i < 16; i++) {
        html[i + 2] = cChannelCustHTMLSrc[i];
    }

    return wmBuildSelectOneBased(dest, op, html, 18, setting, false);
}

static const char *wmBuildCRSFOutputLimits(const char *dest, int op)
{
    if(op == WM_CP_DESTROY) {
        if(dest) free((void *)dest);
        return NULL;
    }
    static const char *axes[] = {"Aileron", "Elevator", "Rudder", "Throttle"};
    static const char *labels[] = {"Lower limit", "Center", "Upper limit"};
    static const char *suffixes[] = {"lo", "ct", "hi"};
    String html;
    html.reserve(3200);
    html += "<div class='cmp0 elrsout' style='white-space:normal'><h3>Travel Limits</h3>"
            "<p><small>RC output in microseconds equivalent. Full stick travel scales to these limits; center stays at 1500. Changes apply after saving and restarting.</small></p>"
            "<style>.elrsout label{display:block;font-size:.8em}.elrsout input{box-sizing:border-box;width:100%;min-width:0;max-width:100%}.elrsout .elrscenter{display:block;padding:5px;margin:5px 0}</style>";
    for(int i = 0; i < ELRS_GIMBAL_AXIS_COUNT; i++) {
        const char *values[] = {crsfOutputMin[i], "1500", crsfOutputMax[i]};
        html += "<fieldset style='margin:10px 0;padding:8px;min-width:0'><legend>";
        html += axes[i];
        html += "</legend><div style='display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px'>";
        for(int field = 0; field < 3; field++) {
            if(field == 1) {
                html += "<div><span style='display:block;font-size:.8em'>Center</span><span class='elrscenter'>1500</span></div>";
                continue;
            }
            char name[8];
            snprintf(name, sizeof(name), "cout%d%s", i, suffixes[field]);
            html += "<div><label for='"; html += name; html += "'>"; html += labels[field];
            html += "</label><input id='"; html += name; html += "' type='number' value='";
            html += values[field]; html += "'";
            html += " name='"; html += name; html += "' maxlength='4'";
            html += field == 0 ? " min='1000' max='1500' required" : " min='1500' max='2000' required";
            html += "></div>";
        }
        html += "</div></fieldset>";
    }
    html += "</div>";
    if(op == WM_CP_LEN) {
        wmLenBuf = html.length() + 1;
        return (const char *)&wmLenBuf;
    }
    char *result = (char *)malloc(html.length() + 1);
    if(!result) return NULL;
    strcpy(result, html.c_str());
    return result;
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
    html += "<div class='elrscal-axis'><div class='elrscal-head'><span class='elrscal-name'>";
    html += axis.name;
    html += "</span><span class='elrscal-live'>Filtered ADC <span id='";
    html += axis.liveId;
    html += "'>--</span></span></div>";
    for(int i = 0; i < 3; i++) {
        wmAppendCRSFCALPoint(html, axis.fields[i].label, axis.fields[i].inputId, axis.liveId, axis.fields[i].value);
    }
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
    html += "</div>";
}

static const char crsfCalIntro[] =
    "<div class='cmp0 elrscal-wrap'><p style='font-size:0.85em;line-height:1.35em;margin:0 0 10px 0'>"
    "Capture each gimbal's filtered ADC low, center, and high points here, then save this page. "
    "Calibration maps to 1000/1500/2000 us before curves and Travel Limits. "
    "Aileron, Elevator and Rudder preserve neutral at 1500 us. "
    "Throttle center maps to 1500 us before its idle-based curve. Each curve follows its gimbal channel assignment. "
    "Curve edits preview locally; save and restart to apply."
    "</p>"
    "<div id='elrscalstat' style='font-size:0.8em;color:#444;margin:0 0 10px 0'>Filtered ADC: waiting for samples...</div>";

static const char crsfCalStyle[] =
    "<style>"
    ".elrscal-wrap{box-sizing:border-box;width:100%;max-width:100%;padding:0;margin:0;white-space:normal;overflow-wrap:anywhere;overflow:hidden}"
    ".elrscal-wrap p,.elrscal-wrap #elrscalstat{white-space:normal;overflow-wrap:anywhere;max-width:100%}"
    ".elrscal-wrap p{font-size:.85em;line-height:1.35em}"
    ".elrscal-axis{box-sizing:border-box;width:100%;max-width:100%;margin:12px 0 0 0;padding:10px 0 0 0;border-top:1px solid #ddd;overflow:hidden;white-space:normal}"
    ".elrscal-head{display:block;line-height:1.3em;max-width:100%;overflow-wrap:anywhere;white-space:normal}"
    ".elrscal-name{font-weight:bold}"
    ".elrscal-live{display:block;font-size:.85em;color:#333}"
    ".elrscal-row{box-sizing:border-box;width:100%;max-width:100%;margin:8px 0;overflow:hidden;padding:0}"
    ".elrscal-row label{display:block;font-size:.82em;margin:0 0 2px 0}"
    ".elrscal-ctl{box-sizing:border-box;display:grid;grid-template-columns:minmax(0,5.8em) max-content;gap:6px;width:100%;max-width:100%;padding:0;margin:0;align-items:stretch}"
    ".elrscal-row input{box-sizing:border-box;width:100%;max-width:100%;min-width:0}"
    ".elrscal-row button{box-sizing:border-box;width:auto;max-width:100%;min-width:0;margin:0;padding:0 6px;font-size:.95em;line-height:2rem}"
    "</style>";

static const char crsfExpoScript[] =
    "<script>(function(){var controls=document.querySelectorAll('[data-elrs-expo]');"
    "function draw(c){var strength=Number(c.value),centered=c.dataset.centered==='1',points=[];"
    "document.getElementById(c.id+'_value').textContent=strength+'%';"
    "for(var i=0;i<=100;i++){var x=i*10,d=centered?x-500:x,m=Math.abs(d),span=centered?500:1000;"
    "var magnitude=Math.round(((100-strength)*m*span*span+strength*m*m*m)/(100*span*span));"
    "var y=centered?500+(d<0?-magnitude:magnitude):magnitude;points.push(x+','+(1000-y));}"
    "document.getElementById(c.id+'_curve').setAttribute('points',points.join(' '));}"
    "for(var i=0;i<controls.length;i++){draw(controls[i]);controls[i].addEventListener('input',function(){draw(this);});}"
    "})();</script>";

static const char crsfCalScript[] =
    "<script>(function(){if(window.__elrsCalInit)return;window.__elrsCalInit=true;"
    "function ge(id){return document.getElementById(id);}function setLive(id,val){var el=ge(id);if(el)el.textContent=val;}"
    "function setStatus(msg){var el=ge('elrscalstat');if(el)el.textContent=msg;}"
    "window.elrsCapture=function(liveId,targetId){var live=ge(liveId),target=ge(targetId);if(!live||!target)return;"
    "if(live.textContent==='--')return;target.value=live.textContent;};"
    "function fail(){setStatus('Filtered ADC unavailable. Make sure the board is powered and the ADS1015 is reachable.');"
    "setLive('elrs_roll_live','--');setLive('elrs_pitch_live','--');setLive('elrs_throttle_live','--');setLive('elrs_yaw_live','--');}"
    "function poll(){fetch('/elrsraw',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){"
    "if(!d.ok){fail();return;}setLive('elrs_roll_live',d.roll);setLive('elrs_pitch_live',d.pitch);"
    "setLive('elrs_throttle_live',d.throttle);setLive('elrs_yaw_live',d.yaw);"
    "setStatus('Filtered ADC connected.');"
    "}).catch(fail);}poll();setInterval(poll,500);})();</script></div>";

static const char *wmBuildCRSFCAL(const char *dest, int op)
{
    if(op == WM_CP_DESTROY) {
        if(dest) free((void *)dest);
        return NULL;
    }

    if(!opModeCRSF)
        return NULL;

    String html;

    html.reserve(sizeof(crsfCalIntro) + sizeof(crsfCalStyle) + sizeof(crsfCalScript) + sizeof(crsfExpoScript) + 4000);
    html += crsfCalIntro;
    html += crsfCalStyle;

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
    html += "<p>Both settings default to 5 counts; 0 disables the setting. Higher jitter tolerance ignores more small movements. "
            "Live readings and captures include filtering and jitter tolerance, before throttle idle and calibration mapping.</p>";

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
    // Keep invalid submissions invalid until the atomic input validator rejects them.
    snprintf(destBuf, length + 1, "%ld", valid ? parsed + offset : -1L);
}

#endif
