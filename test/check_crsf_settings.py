# Compile the actual portal/migration callbacks; stub only UI and storage dependencies.
from check_crsf_adc import ROOT, compile_and_run


def function(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature + '\n{')
    end = source.index('\n}', start) + 2
    return source[start:end] + '\n'


callbacks = function('src/src/CRSF/crsf_wifi.h', 'static bool crsf_wifi_loop_settings()')
callbacks += function('src/remote_settings.cpp', 'void moveSettings()')
callbacks += function('src/remote_settings.cpp', 'static void reInstallFlashFS()')

fixture = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
struct Settings {
    char crsfap[2] = "0", CfgOnSD[2] = "0";
    char elrsRollRev[2] = "0", elrsPitchRev[2] = "0", elrsThrRev[2] = "0", elrsYawRev[2] = "0";
    char elrsTlmRatio[2] = "0", elrsMaxPower[2] = "0", elrsDynPower[2] = "0";
} settings;
struct WiFiManagerParameter {
    char value[2] = "0";
    const char *getValue() { return value; }
};
WiFiManagerParameter custom_crsfap, custom_crsfrr, custom_crsfprv, custom_crsftrv, custom_crsfyrv;
bool opModeCRSF = false, haveNewBoard = true;
bool haveSD = true, haveFS = true, configOnSD = false, FlashROMode = false;
int mqttConfigHash = 0, mainConfigHash = 0, ipHash = 0;
const char *haCfgName = "/mqtt", *secCfgName = "/secondary";
struct { void println(const char *) {} } Serial;
struct { void remove(const char *) {} } MYNVS;
void requestELRSModuleConfigUpdate(uint8_t, uint8_t, uint8_t) {}
void flushDelayedSave() {}
void write_mqtt_settings() {}
void saveSecSettings(bool) {}
void deleteFileFromSD(const char *) {}
void saveId() {}
void write_settings() {}
void writeIpSettings() {}
int currentCrsf = 23, crsfFiles[2] = {23, -1};
bool writeOk = true;
bool crsf_save_settings(bool useCache) {
    if(!writeOk) return false;
    if(!useCache) crsfFiles[configOnSD] = currentCrsf;
    return true;
}
void formatFlashFS(bool) { crsfFiles[0] = -1; }
char savedReverse[4];
bool saveCRSFPortalInputSettings() {
    const char *values[] = {settings.elrsRollRev, settings.elrsPitchRev, settings.elrsThrRev, settings.elrsYawRev};
    for(int i = 0; i < 4; i++) savedReverse[i] = values[i][0];
    return true;
}
'''
# Use the actual checkbox-value conversion too.
fixture += function('src/remote_wifi.cpp', 'static void evalCB(char *sv, WiFiManagerParameter *el)')
fixture = '#include <stdint.h>\n#include <cstdlib>\n#include "remote_global.h"\n' + fixture
cases = r'''
int main() {
    {
        WiFiManagerParameter *boxes[] = {&custom_crsfrr, &custom_crsfprv, &custom_crsftrv, &custom_crsfyrv};
        for(int mask = 0; mask < 16; mask++) {
            for(int i = 0; i < 4; i++) boxes[i]->value[0] = (mask & (1 << i)) ? '1' : '0';
            assert(crsf_wifi_loop_settings());
            for(int i = 0; i < 4; i++) assert(savedReverse[i] == ((mask & (1 << i)) ? '1' : '0'));
        }
        puts("CRSF reverse checkbox check passed");
    }
    {
        for(int oldMedium = 0; oldMedium < 2; oldMedium++) {
            configOnSD = oldMedium;
            settings.CfgOnSD[0] = oldMedium ? '0' : '1';
            crsfFiles[oldMedium] = 23;
            crsfFiles[!oldMedium] = -1;
            moveSettings();
            assert(crsfFiles[!oldMedium] == 23);
            assert(configOnSD == (bool)oldMedium);
            writeOk = false;
            moveSettings();
            assert(configOnSD == (bool)oldMedium);
            assert(settings.CfgOnSD[0] == (oldMedium ? '1' : '0'));
            writeOk = true;
        }
        configOnSD = false;
        reInstallFlashFS();
        assert(crsfFiles[0] == 23);
        puts("CRSF storage migration/reinstall check passed");
    }
}
'''
compile_and_run(fixture + callbacks + cases, [])

# Keep binary migration and the rendered portal on the actual production code paths.
source = (ROOT / 'src/src/CRSF/crsf_kludge.cpp').read_text()
stored_settings = source[source.index('// CRSF settings\n'):source.index('bool readELRSCurrentRawAxes(')]
stored_settings_with_files = stored_settings
for signature in [
    'static bool crsfReadSettingsFile(fs::FS &storage, const char *path, uint8_t *buf, int &validBytes)',
    'static bool crsfLoadStoredSettings(uint8_t *buf, int &validBytes)',
    'static bool crsfSaveStoredSettings(uint8_t *buf, int len)',
]:
    stored_settings = stored_settings.replace(function('src/src/CRSF/crsf_kludge.cpp', signature), '')
portal = (ROOT / 'src/src/CRSF/crsf_wifi.h').read_text()
axis_buffers = portal[portal.index('struct CRSFAxisSettings {'):portal.index('static const char *wmBuildCRSFSelectField(')]
calibration_page = portal[portal.index('struct CRSFGimbalCalField {'):portal.index('static const char *wmBuildCRSFRC(const char *dest, int op)\n{')]
portal_callbacks = ''.join(function('src/src/CRSF/crsf_wifi.h', signature) for signature in [
    'static uint8_t crsfRoutingChannel(const ELRSGimbalRouting &routing, uint8_t axis)',
    'static void crsfSetRoutingChannel(ELRSGimbalRouting &routing, uint8_t axis, uint8_t channel)',
    'static void syncCRSFPortalBuffers()',
    'static bool saveCRSFPortalInputSettings()',
])
switch_page = function('src/src/CRSF/crsf_wifi.h', 'static const char *wmBuildCRSFSwitchMap(const char *dest, int op)')
limits_page = function('src/src/CRSF/crsf_wifi.h', 'static const char *wmBuildCRSFOutputLimits(const char *dest, int op)')
switch_post = function('src/src/CRSF/crsf_wifi.h', 'static void crsfReadSwitchParams()')
wifi_source = (ROOT / 'src/remote_wifi.cpp').read_text()
select_page = wifi_source[wifi_source.index('static const char custHTMLHdr1[]'):wifi_source.index('static const char custHTMLSelFmt[]')]
select_page += wifi_source[wifi_source.index('static const char custHTMLSelFmt[]'):wifi_source.index('\n', wifi_source.index('static const char custHTMLSelFmt[]'))] + '\n'
select_page += portal[portal.index('static const char *cChannelCustHTMLSrc['):portal.index('enum CRSFSelectFieldId')]
select_page += '#define STRLEN(s) (sizeof(s)-1)\n'
select_page += ''.join(function('src/remote_wifi.cpp', signature) for signature in [
    'static unsigned int calcSelectMenu(const char **theHTML, int cnt, char *setting, bool indent = false)',
    'static void buildSelectMenu(char *target, const char **theHTML, int cnt, char *setting, bool indent = false)',
    'static const char *wmBuildSelect(const char *dest, int op, const char **src, int count, char *setting, bool indent)',
])
select_page += function('src/src/CRSF/crsf_wifi.h', 'static const char *wmBuildSelectOneBased(const char *dest, int op, const char **src, int count, char *setting, bool indent = false)')
select_page += function('src/src/CRSF/crsf_wifi.h', 'static const char *wmBuildCRSFGimbalChannelSelect(const char *dest, int op, const char *label, const char *id, char *setting)')
post_parser = '\n'.join(line for line in (ROOT / 'src/remote_settings.h').read_text().splitlines() if line.startswith('#define DEF_ELRS')) + '\n'
post_parser += function('src/remote_wifi.cpp', 'static bool isNumString(char *s)')
post_parser += function('src/remote_wifi.cpp', 'static void getServerParam(const char *name, char *destBuf, size_t length, int minval, int maxval, int defaultVal)')
post_parser += function('src/src/CRSF/crsf_wifi.h', 'static void crsfReadInputParam(const char *name, char *destBuf, size_t length, int minval, int maxval, int offset)')
post_parser += function('src/src/CRSF/crsf_wifi.h', 'static void crsfReadOutputLimitParams()')
post_parser += function('src/src/CRSF/crsf_wifi.h', 'static void crsf_wifi_saveParamsCallback()')
portal_http = function('src/remote_wifi.cpp', 'static void evalCB(char *sv, WiFiManagerParameter *el)')
portal_http += function('src/src/CRSF/crsf_wifi.h', 'static bool crsf_wifi_loop_settings()')
portal_http += function('src/remote_wifi.cpp', 'static bool saveParamsCallback(int paramspage)')
portal_http += function('src/src/WiFiManager/WiFiManager.cpp', 'void WiFiManager::_handleParamSave(int aidx, const char *title)')
storage_moves = function('src/remote_settings.cpp', 'void moveSettings()')
storage_moves += function('src/remote_settings.cpp', 'static void reInstallFlashFS()')
style_source = (ROOT / 'src/src/WiFiManager/wm_strings_en.h').read_text()
portal_style = '#define HTTP_BLUE "#4f529d"\n#define HTTP_RED "#be5c9c"\n#define HTTP_BUTTON_TEXT "#fff"\n'
portal_style += style_source[style_source.index('static const char HTTP_STYLE[]'):style_source.index('\n#ifndef WM_50S_STYLE', style_source.index('static const char HTTP_STYLE[]'))].replace('PROGMEM', '')
storage_fixture = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include "src/CRSF/crsf_settings.h"
using std::min;
using String = std::string;
constexpr int WM_CP_DESTROY = 0, WM_CP_LEN = 1;
size_t wmLenBuf;
std::vector<uint8_t> stored;
std::vector<uint8_t> media[2];
bool configOnSD = false, haveSD = true, haveFS = true, FlashROMode = false;
bool storageWriteOk = true;
bool crsfLoadStoredSettings(uint8_t *data, int &valid) {
    valid = min(84, (int)stored.size());
    if(!valid) return false;
    memcpy(data, stored.data(), valid);
    return true;
}
bool crsfSaveStoredSettings(uint8_t *data, int size) {
    if(!storageWriteOk) return false;
    stored.assign(data, data + size);
    media[configOnSD] = stored;
    return true;
}
uint32_t calcHash(uint8_t *data, int size) {
    uint32_t hash = 2166136261U;
    while(size--) hash = (hash ^ *data++) * 16777619U;
    return hash;
}
struct {
    char opMode[2] = "1", elrsPktRate[2] = "3", elrsSpdUnit[2] = "0";
    char crsfap[2] = "0", CfgOnSD[2] = "0";
    char playTUT[2], musicFolder[2], refBut[2], oorst[2], ooTT[2], resAT[2];
    char elrsTlmRatio[2] = "0", elrsMaxPower[2] = "3", elrsDynPower[2] = "0";
    char elrsRollCh[3] = "1", elrsPitchCh[3] = "2", elrsThrCh[3] = "3", elrsYawCh[3] = "4";
    char elrsRollRev[2] = "0", elrsPitchRev[2] = "0", elrsThrRev[2] = "0", elrsYawRev[2] = "0";
    char elrsRollLow[6], elrsRollCtr[6], elrsRollHigh[6];
    char elrsPitchLow[6], elrsPitchCtr[6], elrsPitchHigh[6];
    char elrsThrLow[6], elrsThrCtr[6], elrsThrHigh[6];
    char elrsYawLow[6], elrsYawCtr[6], elrsYawHigh[6];
    char elrsAdcHysteresis[3] = "5", elrsThrIdleDeadband[3] = "5";
    char elrsSwitchCh[12][3] = {"5","6","7","8","9","10","11","12","13","14","15","16"};
} settings;
bool haveNewBoard = true, opModeCRSF = true;
struct TestServer {
    std::string name, value;
    std::map<std::string, std::string> args;
    int status = 0;
    String body;
    void send(int code, const char *, const char *content) { status = code; body = content; }
    bool hasArg(const char *key) { return name == key || args.count(key); }
    String arg(const char *key) {
        if(name == key) return value;
        auto found = args.find(key);
        return found == args.end() ? String() : found->second;
    }
} server;
#define WLA_SET1_B 3
#define DEF_TUT 0
#define DEF_REF_BUT 0
#define DEF_OORST 0
#define DEF_OO_TT 0
#define DEF_RES_AT 0
#define FPSTR(value) value
constexpr int WM_LP_PREHTTPSEND = 1, WM_LP_POSTHTTPSEND = 2;
const char HTTP_PARAMSAVED[] = "Settings saved.", HTTP_PARAMSAVED_END[] = " Rebooting.", HTTP_END[] = "</html>";
struct WiFiManager {
    TestServer *server = &::server;
    int _params[4] = {}, _paramsCount[4] = {};
    bool incGFXMSG = false;
    bool (*_saveparamscallback)(int) = nullptr;
    void (*_gpcallback)(int) = nullptr;
    void doParamSave(int, int) {}
    int getHTTPHeadLength(const char *, bool) { return 6; }
    void getHTTPHeadNew(String &page, const char *, bool) { page += "<html>"; }
    void HTTPSend(const String &page, bool) { server->send(200, "text/html", page.c_str()); }
    void _handleParamSave(int aidx, const char *title);
} wm;
struct WiFiManagerParameter { char *value; const char *getValue() { return value; } };
WiFiManagerParameter custom_crsfap = {settings.crsfap}, custom_crsfrr = {settings.elrsRollRev},
    custom_crsfprv = {settings.elrsPitchRev}, custom_crsftrv = {settings.elrsThrRev}, custom_crsfyrv = {settings.elrsYawRev};
unsigned int wifiLoopSaveAction = 0;
struct { void println(const char *) {} } Serial;
void requestELRSModuleConfigUpdate(uint8_t, uint8_t, uint8_t) {}
int mainConfigHash = 0, ipHash = 0;
const char *secCfgName = "/secondary";
struct { void remove(const char *) {} } MYNVS;
void flushDelayedSave() {}
void saveSecSettings(bool) {}
void deleteFileFromSD(const char *) {}
void saveId() {}
void write_settings() {}
void writeIpSettings() {}
void formatFlashFS(bool) { media[0].clear(); }
'''
storage_cases = r'''
int main() {
    struct OldBlob { ELRSInputAxisProfile axisProfile[4]; ELRSGimbalRouting gimbalRouting; } old = {};
    for(int i = 0; i < 4; i++) old.axisProfile[i] = elrsDefaultInputAxisProfile();
    old.axisProfile[0].minimum = 300;
    old.axisProfile[0].center = 900;
    old.axisProfile[0].maximum = 1500;
    old.axisProfile[0].reverse = 1;
    old.axisProfile[0].deadband = 12;
    old.gimbalRouting = {2, 1, 3, 4};
    stored.assign((uint8_t *)&old, (uint8_t *)&old + sizeof(old));
    crsf_load_settings();
    ELRSInputAxisProfile profiles[4];
    ELRSGimbalRouting routing;
    uint16_t hysteresis = 99, idle = 99;
    loadELRSInputConfig(profiles, 4, &routing, &hysteresis, &idle);
    assert(hysteresis == 5 && idle == 5);
    assert(profiles[0].minimum == 300 && profiles[0].reverse == 1 && profiles[0].deadband == 12);
    assert(routing.aileronChannel == 2 && routing.elevatorChannel == 1);
    hysteresis = 0; idle = 32;
    assert(saveELRSInputConfig(profiles, 4, &routing, &hysteresis, &idle));
    crsf_load_settings();
    loadELRSInputConfig(profiles, 4, &routing, &hysteresis, &idle);
    assert(hysteresis == 0 && idle == 32);
    const ELRSAxisCalibrationData calibration[4] = {{300,900,1500},{0,1024,2047},{0,1024,2047},{0,1024,2047}};
    saveELRSCalibration(calibration, 4);
    loadELRSInputConfig(profiles, 4, nullptr, &hysteresis, &idle);
    assert(hysteresis == 0 && idle == 32 && profiles[0].reverse == 1);
    auto complete = stored;
    for(int extra = 1; extra <= 3; extra++) {
        stored = complete;
        stored.resize(sizeof(old) + extra);
        crsf_load_settings();
        loadELRSInputConfig(nullptr, 0, nullptr, &hysteresis, &idle);
        assert(hysteresis == (extra < 2 ? 5 : 0) && idle == 5);
    }
    stored = complete;
    std::fill(stored.begin() + sizeof(old), stored.begin() + sizeof(old) + 4, 0xff);
    crsf_load_settings();
    loadELRSInputConfig(nullptr, 0, nullptr, &hysteresis, &idle);
    assert(hysteresis == 32 && idle == 32);
    hysteresis = 65535; idle = 65535;
    assert(saveELRSInputConfig(nullptr, 0, nullptr, &hysteresis, &idle));
    crsf_load_settings();
    loadELRSInputConfig(nullptr, 0, nullptr, &hysteresis, &idle);
    assert(hysteresis == 32 && idle == 32);
    syncCRSFPortalBuffers();
    assert(strcmp(settings.elrsAdcHysteresis, "32") == 0 && strcmp(settings.elrsThrIdleDeadband, "32") == 0);
    strcpy(settings.elrsAdcHysteresis, "0"); strcpy(settings.elrsThrIdleDeadband, "7");
    assert(saveCRSFPortalInputSettings());
    crsf_load_settings();
    loadELRSInputConfig(profiles, 4, &routing, &hysteresis, &idle);
    assert(hysteresis == 0 && idle == 7 && profiles[0].deadband == 12 && profiles[0].reverse == 1);
    syncCRSFPortalBuffers();
    const char *page = wmBuildCRSFCAL(nullptr, 2);
    assert(strstr(page, "name='chyst'") && strstr(page, "name='cthid'"));
    assert(strstr(page, "max='32'") && strstr(page, "Filtered ADC"));
    assert(strstr(page, "max='2047' required"));
    free((void *)page);
    stored.assign((const uint8_t *)calibration, (const uint8_t *)calibration + sizeof(calibration));
    crsf_load_settings();
    loadELRSInputConfig(profiles, 4, &routing, &hysteresis, &idle);
    assert(hysteresis == 5 && idle == 5 && profiles[0].minimum == 300 && profiles[0].center == 900);
    assert(routing.aileronChannel == 1 && profiles[0].reverse == 0);
    puts("CRSF tolerance persistence, legacy migration, and portal check passed");
    // A previous firmware's full blob has profiles, gimbal routing and tolerances, but no switch map.
    hysteresis = 8; idle = 7;
    assert(saveELRSInputConfig(profiles, 4, &routing, &hysteresis, &idle));
    stored.resize(sizeof(old) + 4);
    auto previous = stored;
    ELRSSwitchRouting switches;
    crsf_load_settings();
    loadELRSInputConfig(nullptr, 0, nullptr, &hysteresis, &idle, &switches);
    for(int i = 0; i < 12; i++) assert(switches.channels[i] == 5 + i);
    assert(hysteresis == 8 && idle == 7);
    for(int i = 0; i < 12; i++) switches.channels[i] = 16 - i;
    assert(saveELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, &switches));
    auto mapped = stored;
    assert(previous.size() == 56 && mapped.size() == 84);
    for(int partial = 0; partial < 12; partial++) {
        stored = mapped;
        stored.resize(previous.size() + partial);
        crsf_load_settings();
        loadELRSInputConfig(profiles, 4, &routing, &hysteresis, &idle, &switches);
        assert(hysteresis == 8 && idle == 7 && profiles[0].minimum == 300);
        for(int i = 0; i < 12; i++) assert(switches.channels[i] == 5 + i);
    }
    stored = mapped;
    crsf_load_settings();
    saveELRSCalibration(calibration, 4);
    crsf_load_settings();
    loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, &switches);
    for(int i = 0; i < 12; i++) assert(switches.channels[i] == 16 - i);
    auto validStored = stored;
    const uint8_t invalid[] = {0,4,17,255,15};
    for(uint8_t bad : invalid) {
        ELRSSwitchRouting invalidMap = switches;
        invalidMap.channels[0] = bad;
        assert(!saveELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, &invalidMap));
        assert(stored == validStored);
        ELRSSwitchRouting unchanged;
        loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, &unchanged);
        assert(memcmp(&switches, &unchanged, sizeof(switches)) == 0);
        stored = validStored;
        stored[previous.size()] = bad;
        crsf_load_settings();
        loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, &unchanged);
        for(int i = 0; i < 12; i++) assert(unchanged.channels[i] == 5 + i);
        stored = validStored;
        crsf_load_settings();
    }
    ELRSSwitchRouting changed = elrsDefaultSwitchRouting();
    storageWriteOk = false;
    assert(!saveELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, &changed));
    assert(stored == validStored);
    loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, &switches);
    assert(switches.channels[0] == 16);
    storageWriteOk = true;
    assert(saveELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, &changed));
    crsf_load_settings();
    loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, &switches);
    assert(switches.channels[0] == 5);
    puts("CRSF switch mapping migration, validation, calibration preservation and write retry check passed");
    for(int i = 0; i < 12; i++) switches.channels[i] = 16 - i;
    assert(saveELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, &switches));
    syncCRSFPortalBuffers();
    for(int i = 0; i < 12; i++) assert(atoi(settings.elrsSwitchCh[i]) == 16 - i);
    const char *switchPage = wmBuildCRSFSwitchMap(nullptr, 2);
    if(const char *previewPath = getenv("CRSF_SWITCH_PREVIEW")) {
        FILE *preview = fopen(previewPath, "w");
        assert(preview);
        fputs("<!doctype html><html><meta charset='utf-8'><title>ELRS channel mapping check</title><style>body{font-family:sans-serif;max-width:420px;margin:24px auto}label,select{display:block;width:100%;margin:8px 0}select{padding:6px}</style><h3>Channel Mappings</h3><form>", preview);
        const char *ids[] = {"crlch", "cptch", "cthch", "cywch"};
        const char *labels[] = {"'>Aileron target channel", "'>Elevator target channel", "'>Throttle target channel", "'>Rudder target channel"};
        for(int i = 0; i < 4; i++) {
            const char *gimbal = wmBuildCRSFGimbalChannelSelect(nullptr, 2, labels[i], ids[i], crsfAxisSettings[i].channel);
            fputs(gimbal, preview);
            wmBuildCRSFGimbalChannelSelect(gimbal, WM_CP_DESTROY, labels[i], ids[i], crsfAxisSettings[i].channel);
        }
        fprintf(preview, "%s<button type='submit'>Save</button></form></html>", switchPage);
        fclose(preview);
    }
    for(int i = 0; i < 12; i++) {
        char id[16]; snprintf(id, sizeof(id), "name='csw%d'", i);
        assert(strstr(switchPage, id));
        char selected[40]; snprintf(selected, sizeof(selected), "value='%d' selected", 16 - i);
        assert(strstr(switchPage, selected));
    }
    assert(strstr(switchPage, "Stop") && strstr(switchPage, "FakePower") && strstr(switchPage, "O.O"));
    assert(strstr(switchPage, "RESET") && strstr(switchPage, "ButtonPack 8"));
    assert(strstr(switchPage, ">CH5</option>") && strstr(switchPage, ">CH16</option>"));
    assert(strstr(switchPage, ">CH1</option>") && strstr(switchPage, ">CH4</option>"));
    assert(strstr(switchPage, "setCustomValidity"));
    size_t pageLength = strlen(switchPage) + 1;
    assert(*(const size_t *)wmBuildCRSFSwitchMap(nullptr, WM_CP_LEN) == pageLength);
    assert(wmBuildCRSFSwitchMap(switchPage, WM_CP_DESTROY) == nullptr);
    validStored = stored;
    const char *invalidText[] = {"15","4","17","5x","","256","-1"};
    server.name = "csw0";
    for(const char *bad : invalidText) {
        syncCRSFPortalBuffers();
        server.value = bad;
        crsfReadSwitchParams();
        strcpy(settings.elrsRollLow, "500"); // Rejection must also preserve calibration.
        assert(!saveCRSFPortalInputSettings());
        assert(stored == validStored);
        loadELRSInputConfig(profiles, 4, nullptr, nullptr, nullptr, &switches);
        assert(profiles[0].minimum == 300 && switches.channels[0] == 16);
    }
    syncCRSFPortalBuffers();
    strcpy(settings.elrsSwitchCh[0], "15"); strcpy(settings.elrsSwitchCh[1], "16");
    assert(saveCRSFPortalInputSettings());
    crsf_load_settings();
    loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, &switches);
    assert(switches.channels[0] == 15 && switches.channels[1] == 16 && switches.channels[2] == 14);
    puts("CRSF switch selector rendering, POST validation and portal save check passed");
    char gimbalChannel[] = "4";
    const char *gimbalPage = wmBuildCRSFGimbalChannelSelect(nullptr, 2, "'>Rudder target channel", "cywch", gimbalChannel);
    assert(strstr(gimbalPage, "value='3' selected>CH4</option>"));
    assert(strlen(gimbalPage) >= 6 && !strcmp(gimbalPage + strlen(gimbalPage) - 6, "</div>"));
    assert(strstr(gimbalPage, ">CH5</option>") && strstr(gimbalPage, ">CH16</option>"));
    assert(wmBuildCRSFGimbalChannelSelect(gimbalPage, WM_CP_DESTROY, "'>Rudder target channel", "cywch", gimbalChannel) == nullptr);
    char lastGimbalChannel[] = "16";
    gimbalPage = wmBuildCRSFGimbalChannelSelect(nullptr, 2, "'>Rudder target channel", "cywch", lastGimbalChannel);
    assert(strstr(gimbalPage, "value='15' selected>CH16</option></select></div>"));
    wmBuildCRSFGimbalChannelSelect(gimbalPage, WM_CP_DESTROY, "'>Rudder target channel", "cywch", lastGimbalChannel);
    puts("CRSF gimbal selector has sixteen valid options and no trailing option fragment");
    auto beforeCalibration = stored;
    ELRSAxisCalibrationData retryCalibration[4];
    memcpy(retryCalibration, calibration, sizeof(retryCalibration));
    retryCalibration[0].minimum = 350;
    storageWriteOk = false;
    assert(!saveELRSCalibration(retryCalibration, 4));
    assert(stored == beforeCalibration);
    loadELRSInputConfig(profiles, 4, nullptr, nullptr, nullptr, nullptr);
    assert(profiles[0].minimum == 300);
    storageWriteOk = true;
    assert(saveELRSCalibration(retryCalibration, 4));
    crsf_load_settings();
    loadELRSInputConfig(profiles, 4, nullptr, nullptr, nullptr, &switches);
    assert(profiles[0].minimum == 350);
    assert(switches.channels[0] == 15 && switches.channels[1] == 16);
    puts("CRSF calibration write failure and retry check passed");
    profiles[0].reverse = 1;
    assert(saveELRSInputConfig(profiles, 4));
    const auto validInputs = crsfSettings;
    const auto validHash = crsfSettingsHash;
    validStored = stored;
    for(int axis = 0; axis < 4; axis++) {
        ELRSInputAxisProfile invalidProfiles[4];
        memcpy(invalidProfiles, profiles, sizeof(profiles));
        invalidProfiles[axis].minimum = invalidProfiles[axis].center;
        assert(!saveELRSInputConfig(invalidProfiles, 4));
        saveELRSInputProfiles(invalidProfiles, 4);
        assert(stored == validStored && crsfSettingsHash == validHash);
        assert(memcmp(&crsfSettings, &validInputs, sizeof(validInputs)) == 0);
        syncCRSFPortalBuffers();
        strcpy(crsfAxisSettings[axis].low, crsfAxisSettings[axis].center);
        assert(!saveCRSFPortalInputSettings());
        assert(stored == validStored && crsfSettingsHash == validHash);
        assert(memcmp(&crsfSettings, &validInputs, sizeof(validInputs)) == 0);
    }
    ELRSGimbalRouting duplicateGimbals = routing;
    duplicateGimbals.aileronChannel = duplicateGimbals.elevatorChannel;
    assert(!saveELRSInputConfig(nullptr, 0, &duplicateGimbals));
    saveELRSGimbalRouting(duplicateGimbals);
    assert(stored == validStored && crsfSettingsHash == validHash);
    assert(memcmp(&crsfSettings, &validInputs, sizeof(validInputs)) == 0);
    profiles[0].minimum = 375;
    assert(saveELRSInputConfig(profiles, 4));
    crsf_load_settings();
    loadELRSInputConfig(profiles, 4);
    assert(profiles[0].minimum == 375 && profiles[0].reverse == 1);
    puts("CRSF invalid calibration/routing rejection and corrected retry check passed");
    loadELRSInputConfig(nullptr, 0, &routing, nullptr, nullptr, &switches);
    const auto blobSize = stored.size();
    const uint8_t oldStop = switches.channels[0];
    switches.channels[0] = routing.aileronChannel;
    routing.aileronChannel = oldStop;
    assert(saveELRSInputConfig(nullptr, 0, &routing, nullptr, nullptr, &switches));
    assert(stored.size() == blobSize);
    crsf_load_settings();
    loadELRSInputConfig(profiles, 4, &routing, nullptr, nullptr, &switches);
    assert(routing.aileronChannel == 15 && switches.channels[0] == 1);
    assert(profiles[0].minimum == 375 && profiles[0].reverse == 1);
    const auto mixedInputs = crsfSettings;
    const auto mixedStored = stored;
    const auto mixedHash = crsfSettingsHash;
    ELRSSwitchRouting collision = switches;
    collision.channels[0] = routing.aileronChannel;
    assert(!saveELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, &collision));
    saveELRSGimbalRouting(elrsDefaultGimbalRouting()); // CH1 is occupied by Stop.
    assert(stored == mixedStored && crsfSettingsHash == mixedHash);
    assert(memcmp(&crsfSettings, &mixedInputs, sizeof(mixedInputs)) == 0);
    syncCRSFPortalBuffers();
    assert(!strcmp(settings.elrsRollCh, "15") && !strcmp(settings.elrsSwitchCh[0], "1"));
    strcpy(settings.elrsRollLow, "500");
    strcpy(settings.elrsSwitchCh[0], "15");
    assert(!saveCRSFPortalInputSettings());
    assert(stored == mixedStored && crsfSettingsHash == mixedHash);
    assert(memcmp(&crsfSettings, &mixedInputs, sizeof(mixedInputs)) == 0);
    for(int corruptGimbal = 0; corruptGimbal < 2; corruptGimbal++) {
        stored = mixedStored;
        size_t offset = corruptGimbal ? sizeof(old.axisProfile) : sizeof(old) + 4;
        stored[offset] = corruptGimbal ? 0 : 2;
        crsf_load_settings();
        loadELRSInputConfig(profiles, 4, &routing, nullptr, nullptr, &switches);
        assert(routing.aileronChannel == 1 && routing.elevatorChannel == 2);
        assert(routing.throttleChannel == 3 && routing.rudderChannel == 4);
        for(int i = 0; i < 12; i++) assert(switches.channels[i] == 5 + i);
        assert(profiles[0].minimum == 375 && profiles[0].reverse == 1);
    }
    stored = mixedStored;
    crsf_load_settings();
    assert(saveELRSCalibration(retryCalibration, 4));
    crsf_load_settings();
    loadELRSInputConfig(profiles, 4, &routing, nullptr, nullptr, &switches);
    assert(routing.aileronChannel == 15 && switches.channels[0] == 1);
    assert(profiles[0].minimum == 350 && profiles[0].reverse == 1);
    syncCRSFPortalBuffers();
    strcpy(settings.elrsRollCh, "1"); strcpy(settings.elrsSwitchCh[0], "15");
    assert(saveCRSFPortalInputSettings());
    crsf_load_settings();
    loadELRSInputConfig(nullptr, 0, &routing, nullptr, nullptr, &switches);
    assert(routing.aileronChannel == 1 && switches.channels[0] == 15);
    puts("CRSF shared CH1-CH16 routing, collision rejection and corrupt-map recovery check passed");
    const char *channelNames[] = {"crlch", "cptch", "cthch", "cywch"};
    const char *pointNames[][3] = {{"crrlo","crrct","crrhi"}, {"cptlo","cptct","cpthi"},
                                 {"cthlo","cthct","cthhi"}, {"cywlo","cywct","cywhi"}};
    auto preparePost = [&]() {
        syncCRSFPortalBuffers();
        server.name.clear();
        server.args = {{"copm","1"}, {"cpktr","3"}, {"cspdu","0"}, {"ctlmr","0"}, {"cmpwr","3"}, {"cdynp","0"}};
        for(int axis = 0; axis < 4; axis++) {
            const auto &fields = crsfAxisSettings[axis];
            server.args[channelNames[axis]] = std::to_string(atoi(fields.channel) - 1);
            const char *points[] = {fields.low, fields.center, fields.high};
            for(int point = 0; point < 3; point++) server.args[pointNames[axis][point]] = points[point];
        }
        for(int i = 0; i < 12; i++) {
            char name[6]; snprintf(name, sizeof(name), "csw%d", i);
            server.args[name] = settings.elrsSwitchCh[i];
        }
    };
    const auto beforePost = stored;
    const auto beforePostInputs = crsfSettings;
    const auto beforePostHash = crsfSettingsHash;
    const std::string badPoints[] = {"", "x", "900x", "2048", "00900x", "-1", "99999999999999999999", std::string("900\0x",5)};
    for(int axis = 0; axis < 4; axis++) {
        for(int point = 0; point < 3; point++) {
            for(const auto &bad : badPoints) {
                preparePost();
                server.args[pointNames[axis][point]] = bad;
                crsf_wifi_saveParamsCallback();
                assert(!saveCRSFPortalInputSettings());
                assert(stored == beforePost && crsfSettingsHash == beforePostHash);
                assert(memcmp(&crsfSettings, &beforePostInputs, sizeof(crsfSettings)) == 0);
            }
        }
        for(const char *bad : {"", "16", "-1", "0x", "000x", "15x", "99999999999999999999"}) {
            preparePost();
            server.args[channelNames[axis]] = bad;
            crsf_wifi_saveParamsCallback();
            assert(!saveCRSFPortalInputSettings());
            assert(stored == beforePost && crsfSettingsHash == beforePostHash);
        }
    }
    preparePost();
    server.args["crlch"] = "15"; server.args["csw1"] = "1";
    crsf_wifi_saveParamsCallback();
    assert(saveCRSFPortalInputSettings());
    crsf_load_settings();
    loadELRSInputConfig(nullptr, 0, &routing, nullptr, nullptr, &switches);
    assert(routing.aileronChannel == 16 && switches.channels[1] == 1);
    puts("CRSF actual POST parser rejects blank/malformed calibration and channels, then accepts corrected CH16 swap");
    // The unchanged 68-byte prefix is followed by four atomic endpoint pairs.
    assert(sizeof(ELRSInputAxisProfile) == 12);
    assert(offsetof(ELRSCrsfSettingsBlob, outputLimits) == 68);
    assert(sizeof(ELRSCrsfSettingsBlob) == 84 && sizeof(ELRSOutputLimits) == 4);
    const ELRSOutputLimits limits[4] = {{1100,1700},{1200,1800},{1300,1900},{1400,1600}};
    ELRSOutputLimits loaded[4];
    assert(saveELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, limits));
    const auto limitedBlob = stored;
    for(int tail = 0; tail < 16; tail++) {
        stored = limitedBlob;
        stored.resize(68 + tail);
        crsf_load_settings();
        loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, loaded);
        assert(memcmp(&crsfSettings, limitedBlob.data(), 68) == 0);
        for(int axis = 0; axis < 4; axis++) {
            assert(loaded[axis].minimumUs == (tail >= (axis+1)*4 ? limits[axis].minimumUs : 1000));
            assert(loaded[axis].maximumUs == (tail >= (axis+1)*4 ? limits[axis].maximumUs : 2000));
        }
    }
    for(int version = 0; version < 4; version++) {
        if(version == 0) stored.assign((const uint8_t *)calibration, (const uint8_t *)calibration + sizeof(calibration));
        else {
            stored = limitedBlob;
            stored.resize(version == 1 ? 52 : version == 2 ? 56 : 68);
        }
        crsf_load_settings();
        loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, loaded);
        for(int axis = 0; axis < 4; axis++) assert(loaded[axis].minimumUs == 1000 && loaded[axis].maximumUs == 2000);
    }
    for(int corrupt = 0; corrupt < 4; corrupt++) {
        stored = limitedBlob;
        const uint16_t invalidMinimum = 1501;
        memcpy(stored.data() + 68 + corrupt*4, &invalidMinimum, 2);
        crsf_load_settings();
        loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, loaded);
        assert(memcmp(&crsfSettings, limitedBlob.data(), 68) == 0);
        for(int axis = 0; axis < 4; axis++) {
            assert(loaded[axis].minimumUs == (axis == corrupt ? 1000 : limits[axis].minimumUs));
            assert(loaded[axis].maximumUs == (axis == corrupt ? 2000 : limits[axis].maximumUs));
        }
    }
    stored = limitedBlob;
    crsf_load_settings();
    const auto limitedSettings = crsfSettings;
    const auto limitedHash = crsfSettingsHash;
    for(int axis = 0; axis < 4; axis++) {
        ELRSOutputLimits invalidLimits[4];
        memcpy(invalidLimits, limits, sizeof(limits));
        invalidLimits[axis] = {1501,1700};
        assert(!saveELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, invalidLimits));
        assert(stored == limitedBlob && crsfSettingsHash == limitedHash);
        assert(memcmp(&crsfSettings, &limitedSettings, sizeof(crsfSettings)) == 0);
    }
    storageWriteOk = false;
    const ELRSOutputLimits locked[4] = {{1500,1500},{1500,1500},{1500,1500},{1500,1500}};
    assert(!saveELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, locked));
    assert(stored == limitedBlob && crsfSettingsHash == limitedHash);
    assert(memcmp(&crsfSettings, &limitedSettings, sizeof(crsfSettings)) == 0);
    storageWriteOk = true;
    assert(saveELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, locked));
    crsf_load_settings();
    loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, loaded);
    assert(memcmp(loaded, locked, sizeof(locked)) == 0);
    stored = limitedBlob;
    crsf_load_settings();
    assert(saveELRSCalibration(calibration, 4));
    loadELRSInputConfig(profiles, 4);
    saveELRSInputProfiles(profiles, 4);
    loadELRSInputConfig(nullptr, 0, &routing);
    saveELRSGimbalRouting(routing);
    crsf_load_settings();
    loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, loaded);
    assert(memcmp(loaded, limits, sizeof(limits)) == 0);
    assert(memcmp(&crsfSettings.switchRouting, &limitedSettings.switchRouting, sizeof(switches)) == 0);
    assert(crsfSettings.adcHysteresis == limitedSettings.adcHysteresis && crsfSettings.throttleIdleDeadband == limitedSettings.throttleIdleDeadband);
    puts("CRSF output limits round trip, all legacy/partial tails, validation and calibration preservation passed");
    syncCRSFPortalBuffers();
    const char *limitsPage = wmBuildCRSFOutputLimits(nullptr, 2);
    assert(limitsPage && strstr(limitsPage, "Travel Limits"));
    const char *axisNames[] = {"Aileron", "Elevator", "Rudder", "Throttle"};
    for(int axis = 0; axis < 4; axis++) {
        char legend[40]; snprintf(legend, sizeof(legend), "<legend>%s</legend>", axisNames[axis]);
        const char *row = strstr(limitsPage, legend);
        assert(row);
        std::string block(row, strstr(row, "</fieldset>") - row);
        for(int side = 0; side < 2; side++) {
            char field[20], value[20];
            snprintf(field, sizeof(field), "name='cout%d%s'", axis, side ? "hi" : "lo");
            snprintf(value, sizeof(value), "value='%u'", side ? limits[axis].maximumUs : limits[axis].minimumUs);
            assert(block.find(field) != std::string::npos && block.find(value) != std::string::npos);
        }
        assert(block.find("Lower limit") != std::string::npos && block.find("Upper limit") != std::string::npos);
        assert(block.find("min='1000' max='1500'") != std::string::npos);
        assert(block.find("min='1500' max='2000'") != std::string::npos);
        assert(block.find("Center") != std::string::npos && block.find("value='1500' readonly") != std::string::npos);
        assert(block.find("required") != std::string::npos);
    }
    assert(*(const size_t *)wmBuildCRSFOutputLimits(nullptr, WM_CP_LEN) == strlen(limitsPage) + 1);
    if(const char *previewPath = getenv("CRSF_LIMITS_PREVIEW")) {
        FILE *preview = fopen(previewPath, "w");
        assert(preview);
        fprintf(preview, "<!doctype html><html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>ELRS travel limits</title><script>%s.cmp0{margin:0;padding:0}</style><div id='wrap'><form>%s<button type='submit'>Save</button></form></div></html>", HTTP_STYLE, limitsPage);
        fclose(preview);
    }
    assert(wmBuildCRSFOutputLimits(limitsPage, WM_CP_DESTROY) == nullptr);
    const auto beforeLimitsPost = stored;
    const auto beforeLimitsSettings = crsfSettings;
    const auto beforeLimitsHash = crsfSettingsHash;
    const std::string badLimits[] = {"", "+1200", "-1200", " 1200", "1200 ", "1200x", "1200.0", "1e03", "01200", "999", "9999", "99999999999999999999", std::string("1200\0x",6)};
    for(int axis = 0; axis < 4; axis++) {
        for(int side = 0; side < 2; side++) {
            char name[8]; snprintf(name, sizeof(name), "cout%d%s", axis, side ? "hi" : "lo");
            for(const auto &bad : badLimits) {
                preparePost();
                server.args[name] = bad;
                crsf_wifi_saveParamsCallback();
                assert(!saveCRSFPortalInputSettings());
                assert(stored == beforeLimitsPost && crsfSettingsHash == beforeLimitsHash);
                assert(memcmp(&crsfSettings, &beforeLimitsSettings, sizeof(crsfSettings)) == 0);
            }
            preparePost();
            server.args[name] = side ? "1499" : "1501";
            crsf_wifi_saveParamsCallback();
            assert(!saveCRSFPortalInputSettings());
            assert(stored == beforeLimitsPost && crsfSettingsHash == beforeLimitsHash);
        }
    }
    preparePost();
    memset(crsfOutputMin, 0, sizeof(crsfOutputMin));
    memset(crsfOutputMax, 0, sizeof(crsfOutputMax));
    crsf_wifi_saveParamsCallback(); // Absent fields must refresh saved limits, not stale buffers.
    assert(saveCRSFPortalInputSettings());
    loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, loaded);
    assert(memcmp(loaded, limits, sizeof(limits)) == 0);
    preparePost();
    server.args["cout2lo"] = "1500";
    server.args["cout2hi"] = "1500";
    crsf_wifi_saveParamsCallback();
    storageWriteOk = false;
    assert(!saveCRSFPortalInputSettings());
    assert(stored == beforeLimitsPost && crsfSettingsHash == beforeLimitsHash);
    storageWriteOk = true;
    assert(saveCRSFPortalInputSettings());
    crsf_load_settings();
    loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, loaded);
    assert(loaded[2].minimumUs == 1500 && loaded[2].maximumUs == 1500);
    for(int axis : {0,1,3}) assert(memcmp(&loaded[axis], &limits[axis], sizeof(ELRSOutputLimits)) == 0);
    puts("CRSF travel controls, strict POST parsing, missing fields and write retry passed");
    wm._saveparamscallback = saveParamsCallback;
    const auto beforeHttpBlob = stored;
    const auto beforeHttpInputs = crsfSettings;
    const auto beforeHttpHash = crsfSettingsHash;
    for(const auto &bad : badLimits) {
        preparePost();
        const auto beforeHttpSettings = settings;
        server.args["cout0lo"] = bad;
        wm._handleParamSave(3, "ELRS");
        assert(server.status == 400 && server.body.find("Settings saved.") == String::npos);
        assert(wifiLoopSaveAction == 0 && stored == beforeHttpBlob && crsfSettingsHash == beforeHttpHash);
        assert(memcmp(&settings, &beforeHttpSettings, sizeof(settings)) == 0);
        assert(memcmp(&crsfSettings, &beforeHttpInputs, sizeof(crsfSettings)) == 0);
    }
    preparePost();
    const auto beforeHttpSettings = settings;
    server.args["cout0lo"] = "1250";
    storageWriteOk = false;
    wm._handleParamSave(3, "ELRS");
    assert(server.status == 400 && wifiLoopSaveAction == 0 && stored == beforeHttpBlob);
    assert(memcmp(&settings, &beforeHttpSettings, sizeof(settings)) == 0);
    assert(memcmp(&crsfSettings, &beforeHttpInputs, sizeof(crsfSettings)) == 0 && crsfSettingsHash == beforeHttpHash);
    storageWriteOk = true;
    wm._handleParamSave(3, "ELRS");
    assert(server.status == 200 && server.body.find("Settings saved.") != String::npos && wifiLoopSaveAction == 32);
    crsf_load_settings();
    loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, loaded);
    assert(loaded[0].minimumUs == 1250 && loaded[0].maximumUs == 1700 && loaded[2].minimumUs == 1500);
    puts("CRSF actual travel-limit POST through HTTP response, rollback and reboot scheduling passed");
    const auto beforeMove = stored;
    for(int from = 0; from < 2; from++) {
        configOnSD = from;
        settings.CfgOnSD[0] = from ? '0' : '1';
        media[from] = beforeMove;
        media[!from].clear();
        moveSettings();
        assert(media[!from] == beforeMove && media[from] == beforeMove && configOnSD == (bool)from);
        storageWriteOk = false;
        media[!from].clear();
        moveSettings();
        assert(media[!from].empty() && media[from] == beforeMove && configOnSD == (bool)from);
        storageWriteOk = true;
        moveSettings();
        assert(media[!from] == beforeMove);
    }
    configOnSD = false;
    reInstallFlashFS();
    assert(media[0] == beforeMove && stored == beforeMove);
    puts("CRSF real 84-byte settings preserve limits, calibration, tolerances and mappings across Flash/SD migration and reinstall");
}
'''
compile_and_run(storage_fixture + stored_settings + axis_buffers + portal_callbacks + switch_post + switch_page + limits_page + select_page + calibration_page + post_parser + portal_http + storage_moves + portal_style + storage_cases, ['elrs_input_model.cpp'])

# Exercise the actual HTTP handler and the application's reboot scheduling callback.
save_signature = 'static bool saveParamsCallback(int paramspage)'
http_fixture = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#define WLA_SET1_B 3
#define DEF_TUT 0
#define DEF_REF_BUT 0
#define DEF_OORST 0
#define DEF_OO_TT 0
#define DEF_RES_AT 0
#define STRLEN strlen
#define FPSTR(value) value
using String = std::string;
constexpr int WM_LP_PREHTTPSEND = 1, WM_LP_POSTHTTPSEND = 2;
const char *HTTP_PARAMSAVED = "Settings saved.", *HTTP_PARAMSAVED_END = " Rebooting.", *HTTP_END = "</html>";
struct TestServer {
    int status = 0;
    String body;
    void send(int code, const char *, const char *content) { status = code; body = content; }
} httpServer;
class WiFiManager {
public:
    TestServer *server = &httpServer;
    int _params[4] = {}, _paramsCount[4] = {};
    bool incGFXMSG = false;
    bool (*_saveparamscallback)(int) = nullptr;
    void (*_gpcallback)(int) = nullptr;
    void doParamSave(int, int) {}
    int getHTTPHeadLength(const char *, bool) { return 6; }
    void getHTTPHeadNew(String &page, const char *, bool) { page += "<html>"; }
    void HTTPSend(const String &page, bool) { server->send(200, "text/html", page.c_str()); }
    void _handleParamSave(int aidx, const char *title);
};
struct {
    char playTUT[2], musicFolder[2], refBut[2], oorst[2], ooTT[2], resAT[2];
    char opMode[2] = "1", crsfap[2] = "0", elrsPktRate[2] = "3", elrsSpdUnit[2] = "0";
    char elrsTlmRatio[2] = "0", elrsMaxPower[2] = "0", elrsDynPower[2] = "0";
} settings;
unsigned int wifiLoopSaveAction = 0;
bool inputWriteOk = false;
int inputSaveAttempts = 0, inputReads = 0;
void getServerParam(const char *, char *, int, int, int, int) {}
void crsf_wifi_saveParamsCallback() {
    inputReads++;
    strcpy(settings.opMode, "0"); strcpy(settings.crsfap, "1"); strcpy(settings.elrsPktRate, "4");
    strcpy(settings.elrsSpdUnit, "1"); strcpy(settings.elrsTlmRatio, "2");
    strcpy(settings.elrsMaxPower, "5"); strcpy(settings.elrsDynPower, "1");
}
bool crsf_wifi_loop_settings() { inputSaveAttempts++; return inputWriteOk; }
'''
http_callbacks = function('src/remote_wifi.cpp', save_signature)
http_callbacks += function('src/src/WiFiManager/WiFiManager.cpp', 'void WiFiManager::_handleParamSave(int aidx, const char *title)')
http_cases = r'''
bool inputSaveResult(int) { return inputWriteOk; }
int main() {
    WiFiManager manager;
    manager._saveparamscallback = inputSaveResult;
    manager._handleParamSave(3, "ELRS");
    assert(httpServer.status == 400);
    assert(httpServer.body.find("Settings saved.") == String::npos);
    inputWriteOk = true;
    manager._handleParamSave(3, "ELRS");
    assert(httpServer.status == 200 && httpServer.body.find("Settings saved.") != String::npos);
    inputWriteOk = false;
    const auto beforeRejected = settings;
    saveParamsCallback(3);
    assert(inputReads == 1 && inputSaveAttempts == 1 && wifiLoopSaveAction == 0);
    assert(memcmp(&settings, &beforeRejected, sizeof(settings)) == 0);
    inputWriteOk = true;
    saveParamsCallback(3);
    assert(inputReads == 2 && inputSaveAttempts == 2 && wifiLoopSaveAction == 32);
    wifiLoopSaveAction = 0;
    saveParamsCallback(1);
    assert(wifiLoopSaveAction == 8 && inputSaveAttempts == 2);
    manager._saveparamscallback = saveParamsCallback;
    wifiLoopSaveAction = 0;
    inputWriteOk = false;
    settings = beforeRejected;
    manager._handleParamSave(3, "ELRS");
    assert(httpServer.status == 400 && wifiLoopSaveAction == 0);
    assert(memcmp(&settings, &beforeRejected, sizeof(settings)) == 0);
    saveParamsCallback(1); // An unrelated later save must see only the original values.
    assert(memcmp(&settings, &beforeRejected, sizeof(settings)) == 0);
    wifiLoopSaveAction = 0;
    inputWriteOk = true;
    manager._handleParamSave(3, "ELRS");
    assert(httpServer.status == 200 && wifiLoopSaveAction == 32);
    assert(!strcmp(settings.opMode, "0") && !strcmp(settings.elrsPktRate, "4"));
    puts("CRSF HTTP save response and reboot scheduling check passed");
}
'''
compile_and_run(http_fixture + http_callbacks + http_cases, [])

# Exercise the real framed-file writer too: FILE_WRITE truncates its target.
file_fixture = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include "remote_global.h"
#include "src/CRSF/crsf_settings.h"
using std::min;
using String = std::string;
bool haveSD = true, haveFS = true, configOnSD = false, FlashROMode = false;
struct { char CfgOnSD[2] = "0"; } settings;
struct File {
    std::vector<uint8_t> *bytes = nullptr;
    size_t allowedWrite = 999;
    explicit operator bool() const { return bytes != nullptr; }
    size_t size() { return bytes->size(); }
    size_t read(uint8_t *data, int length) {
        size_t count = min((size_t)length, bytes->size());
        memcpy(data, bytes->data(), count);
        return count;
    }
    size_t write(uint8_t *data, int length) {
        size_t count = min((size_t)length, allowedWrite);
        bytes->assign(data, data + count);
        return count;
    }
    void close() {}
};
namespace fs {
struct FS {
    std::map<std::string, std::vector<uint8_t>> files;
    size_t allowedWrite = 999;
    bool failOpen = false, failBackupRemove = false;
    int renameCalls = 0;
    std::vector<int> failedRenames;
    bool exists(const char *name) { return files.count(name); }
    File open(const char *name, const char *mode) {
        File result;
        if(mode[0] == 'w') {
            if(failOpen) return result;
            files[name].clear(); // Arduino FILE_WRITE is "w", not an append.
        } else if(!exists(name)) return result;
        result.bytes = &files[name];
        result.allowedWrite = allowedWrite;
        return result;
    }
    bool remove(const char *name) {
        if(failBackupRemove && std::string(name) == "/crsfcfg.bak") return false;
        return files.erase(name);
    }
    bool rename(const char *from, const char *to) {
        renameCalls++;
        if(std::find(failedRenames.begin(), failedRenames.end(), renameCalls) != failedRenames.end()) return false;
        // FatFS cannot rename over an existing destination.
        if(!exists(from) || exists(to)) return false;
        files[to] = std::move(files[from]);
        files.erase(from);
        return true;
    }
};
}
fs::FS SD, MYNVS;
#define LittleFS MYNVS
#define FILE_READ "r"
#define FILE_WRITE "w"
'''
file_functions = ''.join(function('src/remote_settings.cpp', signature) for signature in [
    'static bool writeFile(File& myFile, uint8_t *buf, int len)',
    'bool writeFileToSD(const char *fn, uint8_t *buf, int len)',
    'static bool writeFileToFS(const char *fn, uint8_t *buf, int len)',
    'static uint8_t cfChkSum(const uint8_t *buf, int len)',
    'bool saveConfigFile(const char *fn, uint8_t *buf, int len, int forcefs = 0)',
    'uint32_t calcHash(uint8_t *buf, int len)',
])
file_cases = r'''
int main() {
    const char *name = "/crsfcfg";
    uint8_t payload[84] = {}, loaded[84];
    // A valid checksum does not make a mismatched length header safe to read.
    assert(saveConfigFile(name, payload, sizeof(payload)));
    auto malformed = MYNVS.files[name];
    malformed[0] = malformed[1] = 0;
    malformed.back() = cfChkSum(malformed.data(), malformed.size() - 1);
    MYNVS.files[name] = malformed;
    memset(loaded, 0xa5, sizeof(loaded));
    int valid = 777;
    assert(!crsfLoadStoredSettings(loaded, valid));
    assert(valid == 0 && loaded[0] == 0xa5);
    MYNVS.files[name] = {0, 0};
    assert(!crsfLoadStoredSettings(loaded, valid));
    for(int length : {24, 52, 56, 68, 69, 70, 71, 72, 83, 84}) {
        assert(saveConfigFile(name, payload, length));
        assert(crsfLoadStoredSettings(loaded, valid));
        assert(valid == length && !memcmp(payload, loaded, length));
    }
    puts("Binary settings reject checksum-valid mismatched length headers");

    const ELRSOutputLimits original[4] = {{1200,1800},{1200,1800},{1200,1800},{1200,1800}};
    const ELRSOutputLimits changed[4] = {{1300,1700},{1300,1700},{1300,1700},{1300,1700}};
    auto checkLimits = [](uint16_t low, uint16_t high) {
        ELRSOutputLimits live[4];
        loadELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, live);
        for(const auto &limits : live) assert(limits.minimumUs == low && limits.maximumUs == high);
    };
    for(int medium = 0; medium < 2; medium++) {
        SD = fs::FS(); MYNVS = fs::FS();
        configOnSD = medium;
        settings.CfgOnSD[0] = medium ? '1' : '0';
        fs::FS &storage = medium ? SD : MYNVS;
        crsfSettings = defaultCrsfSettings(); crsfSettingsHash = 0;
        assert(saveELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, original));
        const auto before = storage.files[name];
        const auto originalHash = crsfSettingsHash;
        assert(before.size() == 87);
        for(int count : {0, 3, 86}) {
            storage.allowedWrite = count;
            assert(!saveELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, changed));
            assert(storage.files[name] == before && !storage.exists("/crsfcfg.tmp"));
            assert(crsfSettingsHash == originalHash);
            checkLimits(1200, 1800);
            storage.allowedWrite = 999;
            // Unchanged values can hit the cache only because the old file survived.
            assert(saveELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, original));
            assert(storage.files[name] == before);
            crsf_load_settings();
            checkLimits(1200, 1800);
        }
        storage.failOpen = true;
        assert(!saveELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, changed));
        assert(storage.files[name] == before);
        storage.failOpen = false;
        for(const auto &failures : {std::vector<int>{1}, std::vector<int>{2}, std::vector<int>{2,3}}) {
            storage.renameCalls = 0; storage.failedRenames = failures;
            assert(!saveELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, changed));
            assert(crsfSettingsHash == originalHash && !storage.exists("/crsfcfg.tmp"));
            assert((storage.exists(name) && storage.files[name] == before) ||
                   (storage.exists("/crsfcfg.bak") && storage.files["/crsfcfg.bak"] == before));
            crsf_load_settings();
            checkLimits(1200, 1800);
            storage.failedRenames.clear();
        }
        // Recover a backup even when rollback rename failed, then commit a healthy retry.
        assert(saveELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, changed));
        assert(storage.exists(name) && !storage.exists("/crsfcfg.bak"));
        crsf_load_settings(); checkLimits(1300, 1700);
        // A failed cleanup after a successful commit leaves a usable primary and backup.
        storage.failBackupRemove = true;
        assert(saveELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, original));
        const auto committed = storage.files[name];
        assert(storage.exists("/crsfcfg.bak"));
        assert(!saveELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, changed));
        assert(storage.files[name] == committed);
        crsf_load_settings(); checkLimits(1200, 1800);
        storage.failBackupRemove = false;
        assert(saveELRSInputConfig(nullptr, 0, nullptr, nullptr, nullptr, nullptr, changed));
        crsf_load_settings(); checkLimits(1300, 1700);
    }
    puts("Real Flash/SD short writes, open/rename failures, reloads and retries preserve gimbal limits");

    // A migration can temporarily select a different medium from the portal preference.
    const auto sourceCopy = SD.files[name];
    configOnSD = false; settings.CfgOnSD[0] = '1';
    assert(crsf_save_settings(false));
    assert(MYNVS.files[name] == sourceCopy && SD.files[name] == sourceCopy);

    // Keep the shared writer's existing medium selection, including Flash read-only mode.
    for(int cfg = 0; cfg < 2; cfg++)
    for(int ro = 0; ro < 2; ro++) for(int sd = 0; sd < 2; sd++) for(int flash = 0; flash < 2; flash++) {
        SD = fs::FS(); MYNVS = fs::FS();
        configOnSD = cfg; FlashROMode = ro; haveSD = sd; haveFS = flash;
        settings.CfgOnSD[0] = cfg ? '1' : '0';
        const bool toSD = cfg || ro;
        const bool available = toSD ? sd : flash;
        assert(crsfSaveStoredSettings(payload, sizeof(payload)) == available);
        assert(SD.exists(name) == (available && toSD));
        assert(MYNVS.exists(name) == (available && !toSD));
        assert(crsfLoadStoredSettings(loaded, valid) == available);
        if(available) assert(valid == 84 && !memcmp(payload, loaded, sizeof(payload)));
    }
    haveSD = haveFS = true; configOnSD = true; FlashROMode = false;
    settings.CfgOnSD[0] = '1';
    SD = fs::FS(); MYNVS = fs::FS();
    memset(payload, 0x11, sizeof(payload)); assert(saveConfigFile(name, payload, sizeof(payload), 1));
    SD.files["/crsfcfg.bak"] = SD.files[name]; SD.files.erase(name);
    memset(payload, 0x22, sizeof(payload)); assert(saveConfigFile(name, payload, sizeof(payload), -1));
    assert(crsfLoadStoredSettings(loaded, valid));
    assert(loaded[0] == 0x11); // Selected SD backup wins over an older Flash primary.
    SD.files[name] = malformed;
    assert(crsfLoadStoredSettings(loaded, valid));
    assert(loaded[0] == 0x11); // Invalid primary cannot hide a valid recovery copy.
    SD.renameCalls = 0; SD.failedRenames = {1};
    assert(!crsfSaveStoredSettings(payload, sizeof(payload)));
    assert(crsfLoadStoredSettings(loaded, valid));
    assert(loaded[0] == 0x11); // Failed repair must also preserve the valid backup.
    SD.failedRenames.clear();
    SD.files.erase("/crsfcfg.bak");
    assert(!crsfReadSettingsFile(SD, name, loaded, valid));
    assert(crsfLoadStoredSettings(loaded, valid));
    assert(loaded[0] == 0x22);
    assert(crsfSaveStoredSettings(payload, sizeof(payload)));
    assert(crsfLoadStoredSettings(loaded, valid) && loaded[0] == 0x22);
    puts("CRSF file recovery retains selected-medium priority and survives a failed repair");
}
'''
compile_and_run(file_fixture + file_functions + stored_settings_with_files + file_cases, ['elrs_input_model.cpp'])
