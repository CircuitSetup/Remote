
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

static void syncCRSFPortalBuffers();
static bool saveCRSFPortalInputSettings();
static void getServerParamOneBased(const char *name, char *destBuf, size_t length, int minval, int maxval, int defaultVal);
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
};

static CRSFAxisSettings crsfAxisSettings[] = {
    { ELRS_GIMBAL_INPUT_AILERON, settings.elrsRollCh, settings.elrsRollRev, settings.elrsRollLow, settings.elrsRollCtr, settings.elrsRollHigh },
    { ELRS_GIMBAL_INPUT_ELEVATOR, settings.elrsPitchCh, settings.elrsPitchRev, settings.elrsPitchLow, settings.elrsPitchCtr, settings.elrsPitchHigh },
    { ELRS_GIMBAL_INPUT_THROTTLE, settings.elrsThrCh, settings.elrsThrRev, settings.elrsThrLow, settings.elrsThrCtr, settings.elrsThrHigh },
    { ELRS_GIMBAL_INPUT_RUDDER, settings.elrsYawCh, settings.elrsYawRev, settings.elrsYawLow, settings.elrsYawCtr, settings.elrsYawHigh }
};

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
WiFiManagerParameter custom_ss_crsf("ELRS/CRSF Settings", WFM_SECTS|WFM_HL);
WiFiManagerParameter custom_crsfstatus(wmBuildCRSFStatus);
WiFiManagerParameter custom_crsfap("cAP", "Connect to WiFi in ELRS/CRSF mode<br><span>If unchecked, device will remain in AP mode.</span>", settings.crsfap, "", WFM_LABEL_AFTER|WFM_IS_CHKBOX);
WiFiManagerParameter custom_crsfpr(wmBuildCRSFPR);
WiFiManagerParameter custom_crsfsu(wmBuildCRSFSU);
WiFiManagerParameter custom_crsftr(wmBuildCRSFTR);
WiFiManagerParameter custom_crsfmp(wmBuildCRSFMP);
WiFiManagerParameter custom_crsfdp(wmBuildCRSFDP);
WiFiManagerParameter custom_crsfrc(wmBuildCRSFRC);
WiFiManagerParameter custom_crsfrr("crrv", "Reverse Aileron", settings.elrsRollRev, "class='mt5 ml20'", WFM_LABEL_AFTER|WFM_IS_CHKBOX);
WiFiManagerParameter custom_crsfpc(wmBuildCRSFPC);
WiFiManagerParameter custom_crsfprv("cprv", "Reverse Elevator", settings.elrsPitchRev, "class='mt5 ml20'", WFM_LABEL_AFTER|WFM_IS_CHKBOX);
WiFiManagerParameter custom_crsftc(wmBuildCRSFTC);
WiFiManagerParameter custom_crsftrv("ctrv", "Reverse Throttle", settings.elrsThrRev, "class='mt5 ml20'", WFM_LABEL_AFTER|WFM_IS_CHKBOX);
WiFiManagerParameter custom_crsfyc(wmBuildCRSFYC);
WiFiManagerParameter custom_crsfyrv("cyrv", "Reverse Rudder", settings.elrsYawRev, "class='mt5 ml20'", WFM_LABEL_AFTER|WFM_IS_CHKBOX);
WiFiManagerParameter custom_ss_crsfcal("ELRS/CRSF Gimbal Calibration", WFM_SECTS|WFM_HL);
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
      &custom_crsfrc,
      &custom_crsfrr,
      &custom_crsfpc,
      &custom_crsfprv,
      &custom_crsftc,
      &custom_crsftrv,
      &custom_crsfyc,
      &custom_crsfyrv,
      &custom_ss_crsfcal,
      &custom_crsfcal,
      NULL
};


/*
 * Callback from wifi_loop() for saving settings
 *
 */
static bool crsf_wifi_loop_settings()
{
    evalCB(settings.crsfap, &custom_crsfap);
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
    getServerParamOneBased("crlch", settings.elrsRollCh, 2, 1, 16, DEF_ELRSROLLCH);
    getServerParamOneBased("cptch", settings.elrsPitchCh, 2, 1, 16, DEF_ELRSPITCHCH);
    getServerParamOneBased("cthch", settings.elrsThrCh, 2, 1, 16, DEF_ELRSTHRCH);
    getServerParamOneBased("cywch", settings.elrsYawCh, 2, 1, 16, DEF_ELRSYAWCH);
    getServerParam("crrv", settings.elrsRollRev, 1, 0, 1, 0);
    getServerParam("cprv", settings.elrsPitchRev, 1, 0, 1, 0);
    getServerParam("ctrv", settings.elrsThrRev, 1, 0, 1, 0);
    getServerParam("cyrv", settings.elrsYawRev, 1, 0, 1, 0);
    if(opModeCRSF) {
        getServerParam("crrlo", settings.elrsRollLow, 5, 0, 2047, 0);
        getServerParam("crrct", settings.elrsRollCtr, 5, 0, 2047, 1024);
        getServerParam("crrhi", settings.elrsRollHigh, 5, 0, 2047, 2047);
        getServerParam("cptlo", settings.elrsPitchLow, 5, 0, 2047, 0);
        getServerParam("cptct", settings.elrsPitchCtr, 5, 0, 2047, 1024);
        getServerParam("cpthi", settings.elrsPitchHigh, 5, 0, 2047, 2047);
        getServerParam("cthlo", settings.elrsThrLow, 5, 0, 2047, 0);
        getServerParam("cthct", settings.elrsThrCtr, 5, 0, 2047, 1024);
        getServerParam("cthhi", settings.elrsThrHigh, 5, 0, 2047, 2047);
        getServerParam("cywlo", settings.elrsYawLow, 5, 0, 2047, 0);
        getServerParam("cywct", settings.elrsYawCtr, 5, 0, 2047, 1024);
        getServerParam("cywhi", settings.elrsYawHigh, 5, 0, 2047, 2047);
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
    ELRSGimbalRouting routing;

    if(!haveNewBoard) {
        return;
    }

    loadELRSInputConfig(profiles, ELRS_GIMBAL_AXIS_COUNT, &routing);

    for(size_t i = 0; i < sizeof(crsfAxisSettings) / sizeof(crsfAxisSettings[0]); i++) {
        const CRSFAxisSettings &axis = crsfAxisSettings[i];
        ELRSInputAxisProfile &profile = profiles[axis.axis];

        snprintf(axis.channel, sizeof(settings.elrsRollCh), "%u", (unsigned)crsfRoutingChannel(routing, axis.axis));
        axis.reverse[0] = profile.reverse ? '1' : '0';
        axis.reverse[1] = 0;
        snprintf(axis.low, sizeof(settings.elrsRollLow), "%d", profile.minimum);
        snprintf(axis.center, sizeof(settings.elrsRollCtr), "%d", profile.center);
        snprintf(axis.high, sizeof(settings.elrsRollHigh), "%d", profile.maximum);
    }
}

static bool saveCRSFPortalInputSettings()
{
    ELRSInputAxisProfile profiles[ELRS_GIMBAL_AXIS_COUNT];
    ELRSGimbalRouting routing;

    if(!haveNewBoard) {
        return false;
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
    }

    return saveELRSInputConfig(profiles, ELRS_GIMBAL_AXIS_COUNT, &routing);
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

static const char *wmBuildCRSFChannelSelect(const char *dest, int op, const char *label, const char *id, char *setting)
{
    const char *html[18];

    html[0] = label;
    html[1] = id;
    for(int i = 0; i < 16; i++) {
        html[i + 2] = cChannelCustHTMLSrc[i];
    }

    return wmBuildSelectOneBased(dest, op, html, 18, setting, false);
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
    html += "' type='number' min='0' max='2047'><button type='button' onclick=\"elrsCapture('";
    html += liveId;
    html += "','";
    html += inputId;
    html += "')\">Capture</button></div></div>";
}

static void wmAppendCRSFCALAxis(String &html, const CRSFGimbalCalAxis &axis)
{
    html += "<div class='elrscal-axis'><div class='elrscal-head'><span class='elrscal-name'>";
    html += axis.name;
    html += "</span><span class='elrscal-live'>Live <span id='";
    html += axis.liveId;
    html += "'>--</span></span></div>";
    for(int i = 0; i < 3; i++) {
        wmAppendCRSFCALPoint(html, axis.fields[i].label, axis.fields[i].inputId, axis.liveId, axis.fields[i].value);
    }
    html += "</div>";
}

static const char crsfCalIntro[] =
    "<div class='cmp0 elrscal-wrap'><p style='font-size:0.85em;line-height:1.35em;margin:0 0 10px 0'>"
    "Capture each gimbal's raw ADC low, center, and high points here, then save this page. "
    "Those saved points become the real CRSF gimbal output mapping: low=1000, center=1500, high=2000."
    "</p>"
    "<div id='elrscalstat' style='font-size:0.8em;color:#444;margin:0 0 10px 0'>Live ADC: waiting for samples...</div>";

static const char crsfCalStyle[] =
    "<style>"
    ".elrscal-wrap{box-sizing:border-box;width:100%;max-width:100%;padding:0;margin:0;white-space:normal;overflow-wrap:anywhere;overflow:hidden}"
    ".elrscal-wrap p,.elrscal-wrap #elrscalstat{white-space:normal;overflow-wrap:anywhere;max-width:100%}"
    ".elrscal-axis{box-sizing:border-box;width:100%;max-width:100%;margin:12px 0 0 0;padding:10px 0 0 0;border-top:1px solid #ddd;overflow:hidden;white-space:normal}"
    ".elrscal-head{display:block;line-height:1.3em;max-width:100%;overflow-wrap:anywhere;white-space:normal}"
    ".elrscal-name{font-weight:bold}"
    ".elrscal-live{display:block;font-size:.85em;color:#333}"
    ".elrscal-row{box-sizing:border-box;width:100%;max-width:100%;margin:8px 0;overflow:hidden;padding:0}"
    ".elrscal-row label{display:block;font-size:.82em;margin:0 0 2px 0}"
    ".elrscal-ctl{box-sizing:border-box;display:grid;grid-template-columns:minmax(0,5.8em) minmax(4.8em,1fr);gap:6px;width:100%;max-width:100%;padding:0;margin:0;align-items:stretch}"
    ".elrscal-row input{box-sizing:border-box;width:100%;max-width:100%;min-width:0}"
    ".elrscal-row button{box-sizing:border-box;width:100%;max-width:100%;min-width:0;margin:0;font-size:.95em;line-height:2rem}"
    "</style>";

static const char crsfCalScript[] =
    "<script>(function(){if(window.__elrsCalInit)return;window.__elrsCalInit=true;"
    "function ge(id){return document.getElementById(id);}function setLive(id,val){var el=ge(id);if(el)el.textContent=val;}"
    "function setStatus(msg){var el=ge('elrscalstat');if(el)el.textContent=msg;}"
    "window.elrsCapture=function(liveId,targetId){var live=ge(liveId),target=ge(targetId);if(!live||!target)return;"
    "if(live.textContent==='--')return;target.value=live.textContent;};"
    "function fail(){setStatus('Live ADC unavailable. Make sure the board is powered and the ADS1015 is reachable.');"
    "setLive('elrs_roll_live','--');setLive('elrs_pitch_live','--');setLive('elrs_throttle_live','--');setLive('elrs_yaw_live','--');}"
    "function poll(){fetch('/elrsraw',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){"
    "if(!d.ok){fail();return;}setLive('elrs_roll_live',d.roll);setLive('elrs_pitch_live',d.pitch);"
    "setLive('elrs_throttle_live',d.throttle);setLive('elrs_yaw_live',d.yaw);"
    "setStatus('Live ADC connected.');"
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

    html.reserve(sizeof(crsfCalIntro) + sizeof(crsfCalStyle) + sizeof(crsfCalScript) + 1600);
    html += crsfCalIntro;
    html += crsfCalStyle;

    CRSFGimbalCalAxis axes[] = {
        { "Rudder", "elrs_yaw_live", {
            { "Left", "cywlo", settings.elrsYawLow },
            { "Center", "cywct", settings.elrsYawCtr },
            { "Right", "cywhi", settings.elrsYawHigh }
        } },
        { "Throttle", "elrs_throttle_live", {
            { "Up", "cthhi", settings.elrsThrHigh },
            { "Center", "cthct", settings.elrsThrCtr },
            { "Down", "cthlo", settings.elrsThrLow }
        } },
        { "Aileron", "elrs_roll_live", {
            { "Left", "crrlo", settings.elrsRollLow },
            { "Center", "crrct", settings.elrsRollCtr },
            { "Right", "crrhi", settings.elrsRollHigh }
        } },
        { "Elevator", "elrs_pitch_live", {
            { "Up", "cpthi", settings.elrsPitchHigh },
            { "Center", "cptct", settings.elrsPitchCtr },
            { "Down", "cptlo", settings.elrsPitchLow }
        } }
    };

    for(size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); i++) {
        wmAppendCRSFCALAxis(html, axes[i]);
    }

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
    return wmBuildCRSFChannelSelect(dest, op, "'>Aileron target channel", "crlch", settings.elrsRollCh);
}
static const char *wmBuildCRSFPC(const char *dest, int op)
{
    return wmBuildCRSFChannelSelect(dest, op, "'>Elevator target channel", "cptch", settings.elrsPitchCh);
}
static const char *wmBuildCRSFTC(const char *dest, int op)
{
    return wmBuildCRSFChannelSelect(dest, op, "'>Throttle target channel", "cthch", settings.elrsThrCh);
}
static const char *wmBuildCRSFYC(const char *dest, int op)
{
    return wmBuildCRSFChannelSelect(dest, op, "'>Rudder target channel", "cywch", settings.elrsYawCh);
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

static void getServerParamOneBased(const char *name, char *destBuf, size_t length, int minval, int maxval, int defaultVal)
{
    char tempBuf[4];

    getServerParam(name, tempBuf, sizeof(tempBuf) - 1, minval - 1, maxval - 1, defaultVal - 1);
    snprintf(destBuf, length + 1, "%d", atoi(tempBuf) + 1);
}

#endif
