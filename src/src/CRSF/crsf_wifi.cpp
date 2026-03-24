#include "../../remote_global.h"

#ifdef HAVE_CRSF

#include <Arduino.h>
#include <stdlib.h>

#include "../../remote_settings.h"
#include "../WiFiManager/WiFiManager.h"
#include "crsf_wifi.h"

namespace {

#define STRLEN(x) (sizeof(x) - 1)

static const char *crsfModeCustHTMLSrc[4] = {
    "'>Control mode",
    "opMode",
    ">Legacy%s1'",
    ">ELRS/CRSF%s"
};
static const char *crsfPktRateCustHTMLSrc[6] = {
    "'>Packet rate",
    "ePRIdx",
    ">50 Hz%s1'",
    ">100 Hz%s2'",
    ">150 Hz%s3'",
    ">250 Hz%s"
};

static const char custHTMLHdr1[] = "<div class='cmp0";
static const char custHTMLHdrI[] = " ml20";
static const char custHTMLHdr2[] = "'><label class='mp0' for='";
static const char custHTMLSHdr[] = "</label><select class='sel0' value='";
static const char osde[] = "</option></select></div>";
static const char ooe[]  = "</option><option value='";
static const char custHTMLSel[] = " selected";
static const char custHTMLSelFmt[] = "' name='%s' id='%s' autocomplete='off'><option value='0'";

static unsigned int wmLenBuf = 0;
static char crsfPktRateIdx[2] = "3";

static int elrsPacketRateIndex()
{
    switch(elrsPacketRateOrDefault((uint16_t)atoi(settings.elrsPktRate))) {
    case ELRS_PACKET_RATE_50HZ:
        return 0;
    case ELRS_PACKET_RATE_100HZ:
        return 1;
    case ELRS_PACKET_RATE_150HZ:
        return 2;
    default:
        return 3;
    }
}

static void updateCRSFPacketRateIndex()
{
    snprintf(crsfPktRateIdx, sizeof(crsfPktRateIdx), "%d", elrsPacketRateIndex());
}

static void setELRSPacketRateFromIndex(const char *idxText)
{
    uint16_t rate = ELRS_PACKET_RATE_DEFAULT;

    switch(atoi(idxText)) {
    case 0:
        rate = ELRS_PACKET_RATE_50HZ;
        break;
    case 1:
        rate = ELRS_PACKET_RATE_100HZ;
        break;
    case 2:
        rate = ELRS_PACKET_RATE_150HZ;
        break;
    default:
        rate = ELRS_PACKET_RATE_250HZ;
        break;
    }

    snprintf(settings.elrsPktRate, sizeof(settings.elrsPktRate), "%u", (unsigned)rate);
    updateCRSFPacketRateIndex();
}

static void getServerParam(WiFiManager &wm, const char *name, char *destBuf, size_t length, int defaultVal)
{
    memset(destBuf, 0, length + 1);
    if(wm.server->hasArg(name)) {
        strncpy(destBuf, wm.server->arg(name).c_str(), length);
    }
    if(!*destBuf) {
        snprintf(destBuf, length + 1, "%d", defaultVal);
    }
}

static unsigned int calcSelectMenu(const char **theHTML, int cnt, char *setting, bool indent = false)
{
    int sr = atoi(setting);
    unsigned int l = 0;

    l += STRLEN(custHTMLHdr1);
    if(indent) {
        l += STRLEN(custHTMLHdrI);
    }
    l += STRLEN(custHTMLHdr2);
    l += strlen(theHTML[0]);
    l += STRLEN(custHTMLSHdr);
    l += strlen(setting);
    l += (STRLEN(custHTMLSelFmt) - (2 * 2));
    l += (3 * strlen(theHTML[1]));
    for(int i = 0; i < cnt - 2; i++) {
        if(sr == i) {
            l += STRLEN(custHTMLSel);
        }
        l += (strlen(theHTML[i + 2]) - 2);
        l += strlen((i == cnt - 3) ? osde : ooe);
    }

    return l + 8;
}

static void buildSelectMenu(char *target, const char **theHTML, int cnt, char *setting, bool indent = false)
{
    int sr = atoi(setting);

    strcpy(target, custHTMLHdr1);
    if(indent) {
        strcat(target, custHTMLHdrI);
    }
    strcat(target, custHTMLHdr2);
    strcat(target, theHTML[1]);
    strcat(target, theHTML[0]);
    strcat(target, custHTMLSHdr);
    strcat(target, setting);
    sprintf(target + strlen(target), custHTMLSelFmt, theHTML[1], theHTML[1]);
    for(int i = 0; i < cnt - 2; i++) {
        if(sr == i) {
            strcat(target, custHTMLSel);
        }
        sprintf(target + strlen(target), theHTML[i + 2], (i == cnt - 3) ? osde : ooe);
    }
}

static const char *wmBuildCRSFMode(const char *dest, int op)
{
    if(op == WM_CP_DESTROY) {
        if(dest) {
            free((void *)dest);
        }
        return NULL;
    }

    unsigned int l = calcSelectMenu(crsfModeCustHTMLSrc, 4, settings.opMode);

    if(op == WM_CP_LEN) {
        wmLenBuf = l;
        return (const char *)&wmLenBuf;
    }

    char *str = (char *)malloc(l);
    buildSelectMenu(str, crsfModeCustHTMLSrc, 4, settings.opMode);
    return str;
}

static const char *wmBuildCRSFPacketRate(const char *dest, int op)
{
    if(op == WM_CP_DESTROY) {
        if(dest) {
            free((void *)dest);
        }
        return NULL;
    }

    updateCRSFPacketRateIndex();
    unsigned int l = calcSelectMenu(crsfPktRateCustHTMLSrc, 6, crsfPktRateIdx);

    if(op == WM_CP_LEN) {
        wmLenBuf = l;
        return (const char *)&wmLenBuf;
    }

    char *str = (char *)malloc(l);
    buildSelectMenu(str, crsfPktRateCustHTMLSrc, 6, crsfPktRateIdx);
    return str;
}

WiFiManagerParameter custom_crsfMode(wmBuildCRSFMode);
WiFiManagerParameter custom_crsfPktRate(wmBuildCRSFPacketRate);

}

void crsf_wifi_register_page(WiFiManager &wm)
{
    WiFiManagerParameter *parm3Array[] = {
        &custom_crsfMode,
        &custom_crsfPktRate,
        NULL
    };

    int temp = 0;

    wm.allocParms(WM_PARM_SETTINGS3, (sizeof(parm3Array) / sizeof(WiFiManagerParameter *)) - 1);
    while(parm3Array[temp]) {
        wm.addParameter(WM_PARM_SETTINGS3, parm3Array[temp]);
        temp++;
    }
}

void crsf_wifi_save_params(WiFiManager &wm)
{
    char packetRateIdx[2];
    int mode = DEF_OPMODE;

    getServerParam(wm, "opMode", settings.opMode, 1, DEF_OPMODE);
    mode = atoi(settings.opMode);
    if(mode < 0 || mode > 1) {
        mode = DEF_OPMODE;
    }
    snprintf(settings.opMode, sizeof(settings.opMode), "%d", mode);

    getServerParam(wm, "ePRIdx", packetRateIdx, 1, elrsPacketRateIndex());
    setELRSPacketRateFromIndex(packetRateIdx);
}

void crsf_wifi_update_values()
{
    updateCRSFPacketRateIndex();
}

#endif
