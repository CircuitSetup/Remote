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
storage_fixture = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include "src/CRSF/crsf_settings.h"
using std::min;
using String = std::string;
constexpr int WM_CP_DESTROY = 0, WM_CP_LEN = 1;
size_t wmLenBuf;
std::vector<uint8_t> stored;
bool storageWriteOk = true;
bool loadConfigFile(const char *, uint8_t *data, size_t size, int &valid, int) {
    valid = min((int)size, (int)stored.size());
    if(!valid) return false;
    memcpy(data, stored.data(), valid);
    return true;
}
bool saveConfigFile(const char *, uint8_t *data, size_t size, int) {
    if(!storageWriteOk) return false;
    stored.assign(data, data + size);
    return true;
}
uint32_t calcHash(uint8_t *data, int size) {
    uint32_t hash = 2166136261U;
    while(size--) hash = (hash ^ *data++) * 16777619U;
    return hash;
}
struct {
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
    bool hasArg(const char *key) { return name == key; }
    String arg(const char *) { return value; }
} server;
struct { TestServer *server; } wm = {&server};
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
    assert(mapped.size() == previous.size() + 12);
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
        fprintf(preview, "<!doctype html><html><meta charset='utf-8'><title>ELRS switch mapping check</title><style>body{font-family:sans-serif;max-width:420px;margin:24px auto}label,select{display:block;width:100%%;margin:8px 0}select{padding:6px}</style><form>%s<button type='submit'>Save</button></form></html>", switchPage);
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
    assert(!strstr(switchPage, ">CH4</option>") && strstr(switchPage, "setCustomValidity"));
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
    assert(strstr(gimbalPage, "value='3' selected>CH4</option></select></div>"));
    assert(strlen(gimbalPage) >= 6 && !strcmp(gimbalPage + strlen(gimbalPage) - 6, "</div>"));
    assert(!strstr(gimbalPage, ">CH5"));
    assert(wmBuildCRSFGimbalChannelSelect(gimbalPage, WM_CP_DESTROY, "'>Rudder target channel", "cywch", gimbalChannel) == nullptr);
    puts("CRSF gimbal selector has four valid options and no trailing option fragment");
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
}
'''
compile_and_run(storage_fixture + stored_settings + axis_buffers + portal_callbacks + switch_post + switch_page + select_page + calibration_page + storage_cases, ['elrs_input_model.cpp'])

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
} settings;
unsigned int wifiLoopSaveAction = 0;
bool inputWriteOk = false;
int inputSaveAttempts = 0, inputReads = 0;
void getServerParam(const char *, char *, int, int, int, int) {}
void crsf_wifi_saveParamsCallback() { inputReads++; }
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
    saveParamsCallback(3);
    assert(inputReads == 1 && inputSaveAttempts == 1 && wifiLoopSaveAction == 0);
    inputWriteOk = true;
    saveParamsCallback(3);
    assert(inputReads == 2 && inputSaveAttempts == 2 && wifiLoopSaveAction == 32);
    wifiLoopSaveAction = 0;
    saveParamsCallback(1);
    assert(wifiLoopSaveAction == 8 && inputSaveAttempts == 2);
    manager._saveparamscallback = saveParamsCallback;
    wifiLoopSaveAction = 0;
    inputWriteOk = false;
    manager._handleParamSave(3, "ELRS");
    assert(httpServer.status == 400 && wifiLoopSaveAction == 0);
    inputWriteOk = true;
    manager._handleParamSave(3, "ELRS");
    assert(httpServer.status == 200 && wifiLoopSaveAction == 32);
    puts("CRSF HTTP save response and reboot scheduling check passed");
}
'''
compile_and_run(http_fixture + http_callbacks + http_cases, [])
