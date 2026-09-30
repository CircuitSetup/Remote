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
