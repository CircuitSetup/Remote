"""Compare CRSF paired-save encoding with the unchanged core writer and native file contracts."""
from check_crsf_adc import ROOT, HEADERS, compile_and_run

HEADERS['FS.h'] = '#pragma once\nclass String {}; class File {};\n'
source = (ROOT / 'src/remote_settings.cpp').read_text()
start = source.index('bool write_settings()\n{')
writer = source[start:source.index('\n}', start)+2]+'\n'
cases = r'''
#include <cassert>
#include <cstring>
#include "remote_settings_transfer.h"
Settings settings;
ELRSRadioPreferences remoteRadioPreferences;
bool haveFS=true,haveNewBoard=true,FlashROMode=false;
const char *cfgName="/remoteconfig.json",*fsNoAvail="unavailable";
uint32_t mainConfigHash=0;
DynamicJsonDocument written(8192);
struct {void printf(const char*,...){}} Serial;
#define DECLARE_D_JSON(size,name) DynamicJsonDocument name(8192)
bool writeJSONCfgFile(const JsonDocument&doc,const char*,bool,uint32_t,uint32_t*) {
    written.set(doc);return true;
}
WRITER
int main() {
    settings.ssid[1] = 'X';
    strcpy(settings.pass, "placeholder-secret");
    ELRSRadioPreferences &radio=remoteRadioPreferences;
    radio.modelId = 12;
    strcpy(radio.maxPower.label, "25/1000");
    strcpy(radio.telemetry, "1:8");
    strcpy(radio.dynamic, "AUX9");
    DynamicJsonDocument stored(8192), transfer(8192), oldBoard(8192), mqtt(8192);
    assert(remoteEncodeSettings(stored.to<JsonObject>(), settings, radio, false,
                               RemoteSettingsEncoding::StoredFile, true));
    assert(!stored.containsKey("ssid") && !stored.containsKey("pass") && !stored.containsKey("bssid"));
    assert(remoteEncodeSettings(transfer.to<JsonObject>(), settings, radio, false));
    assert(transfer.containsKey("ssid") && transfer.containsKey("pass") && transfer.containsKey("bssid"));
    assert(remoteEncodeSettings(oldBoard.to<JsonObject>(), settings, radio, false,
                               RemoteSettingsEncoding::StoredFile, false));
    assert(!oldBoard.containsKey("eRadio") && !oldBoard.containsKey("opMode"));
#ifdef HAVE_CRSF
    assert(stored["eRadio"]["modelId"].as<int>() == 12);
    assert(!strcmp(stored["eRadio"]["maxPower"]["label"], "25/1000"));
    assert(!strcmp(stored["eRadio"]["telemetry"], "1:8"));
    assert(!strcmp(stored["eRadio"]["dynamic"], "AUX9"));
    assert(transfer.containsKey("eRadio") && transfer.containsKey("opMode"));
#else
    assert(!stored.containsKey("eRadio") && !transfer.containsKey("opMode"));
#endif
    settings.ssid[1] = 0;
    stored.clear();
    assert(remoteEncodeSettings(stored.to<JsonObject>(), settings, radio, false,
                               RemoteSettingsEncoding::StoredFile, true));
    assert(stored.containsKey("ssid") && stored.containsKey("pass") && stored.containsKey("bssid"));
    assert(remoteEncodeSettings(mqtt.to<JsonObject>(), settings, radio, true,
                               RemoteSettingsEncoding::StoredFile, true));
#ifdef HAVE_MQTT
    assert(mqtt.containsKey("useMQTT") && mqtt.containsKey("mqttb8f"));
#ifdef HAVE_MQTT_MP
    assert(mqtt.containsKey("pMP"));
#else
    assert(!mqtt.containsKey("pMP"));
#endif
#endif
    assert(!mqtt.containsKey("ssid") && !mqtt.containsKey("eRadio"));
    for(bool oldBoard:{false,true})for(bool placeholder:{false,true}) {
        haveNewBoard=!oldBoard;settings.ssid[0]=0;settings.ssid[1]=placeholder?'X':0;
        strcpy(settings.hostName,"keep-existing");strcpy(settings.cm_pass,"retain-credentials");
        strcpy(settings.autoThrottle,"1");strcpy(settings.CfgOnSD,"1");
        written.clear();assert(write_settings());stored.clear();
        assert(remoteEncodeSettings(stored.to<JsonObject>(),settings,radio,false,RemoteSettingsEncoding::StoredFile,haveNewBoard));
        assert(written.as<JsonObjectConst>().size()==stored.as<JsonObjectConst>().size());
        for(JsonPairConst field:written.as<JsonObjectConst>())assert(field.value()==stored[field.key()]);
    }
    assert(!stored.overflowed() && !transfer.overflowed() && !mqtt.overflowed());
    puts("CRSF paired encoding matches unchanged core writer; native transfer and feature policies pass");
}
'''
preferences = (ROOT / 'src/src/CRSF/elrs_device_config.cpp').read_text().split('bool elrsMatchSelection', 1)[0]
preferences = preferences.replace('#include "elrs_device_config.h"', '#include "src/CRSF/elrs_device_config.h"')
for flags in [[], ['-UHAVE_CRSF'], ['-DHAVE_MQTT_MP']]:
    compile_and_run(preferences + cases.replace("WRITER",writer), ['elrs_input_model.cpp'],
                    ['-I' + str(ROOT / '.pio/libdeps/esp32dev/ArduinoJson/src'),
                     str(ROOT / 'src/remote_settings_transfer.cpp'), '-lbcrypt', *flags])
