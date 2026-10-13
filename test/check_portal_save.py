"""Exercise the production CRSF save acknowledgment; fake UI and the checked storage boundary."""
from check_crsf_adc import ROOT, HEADERS, compile_and_run
HEADERS['FS.h']='#pragma once\n#include <string>\nusing String=std::string;class File {};\n'

def function(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature + '\n{')
    return source[start:source.index('\n}', start) + 2] + '\n'

save = function('src/remote_wifi.cpp', 'static bool saveCRSFPortalSettings(RemoteTransferError &error)')
callback = function('src/remote_wifi.cpp', 'static bool saveParamsCallback(int paramspage)')
delayed = function('src/remote_main.cpp', 'void flushDelayedSave()')
publication = function('src/remote_settings.cpp', 'uint32_t calcHash(uint8_t *buf, int len)')
source = (ROOT / 'src/src/CRSF/crsf_kludge.cpp').read_text()
start = source.index('void installCommittedELRSSettings(')
publication += source[start:source.index('\n}', start)+2]+'\n'
fixture = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include "remote_settings_transfer.h"
Settings settings;
ELRSRadioPreferences remoteRadioPreferences;
bool remoteRadioVersioned=true,havePwrMon=false;
bool busy=false,radioBusy=false,review=false,parserValid=true,builderValid=true,commitPass=true;
bool remotePortalUploading=false;
unsigned captureCalls=0,parseCalls=0,commitCalls=0,updates=0,remotePortalPreferenceFailed=0;
uint32_t wifiLoopSaveAction=0,brichgnow=0,volchgnow=0,vischgnow=0,currentGeneration=1;
unsigned brightnessSaves=0,volumeSaves=0,visualSaves=0;
void saveBrightness(){assert(!busy);brightnessSaves++;currentGeneration++;}
void saveCurVolume(){assert(!busy);volumeSaves++;currentGeneration++;}
void saveVis(){assert(!busy);visualSaves++;currentGeneration++;}
RemoteSettingsSnapshot persisted;
ELRSCrsfSettingsBlob crsfSettings={};
uint32_t crsfSettingsHash=123;
struct {int error=0;void setParamSaveError(int e){error=e;}} wm;
bool remoteSettingsBusy(){return busy;}bool remotePortalRadioBusy(){return radioBusy;}
bool remoteRadioReviewPending(){return review;}
void remotePortalApplyPreferences(){}
void getServerParam(const char*,char*,int,int,int,int){}
void handleMQTTTopMsg(int){}
bool crsf_wifi_saveParamsCallback(){
    parseCalls++;if(!parserValid)return false;
    strcpy(settings.opMode,"2");return true;
}
bool buildCRSFPortalInputSettings(ELRSCrsfSettingsBlob&b,RemoteTransferError&){
    strcpy(settings.crsfap,"1");b.logicVersion=2;b.axisProfile[0].expo=42;return builderValid;
}
void syncCRSFPortalBuffers(){updates++;}
bool captureRemoteSettings(RemoteSettingsSnapshot&s,RemoteTransferError&){
    captureCalls++;s={};s.settings=settings;s.radio=remoteRadioPreferences;
    // Production capture normalizes live runtime fields; ELRS Save retains raw unrelated settings.
    strcpy(s.settings.autoThrottle,"1");
    s.newBoard=true;s.generation=currentGeneration;s.stores[0].state=s.stores[6].state=RemoteStoreState::Present;
    return true;
}
bool remoteCommitELRSSettings(const RemoteSettingsSnapshot&before,const RemoteSettingsSnapshot&candidate,RemoteTransferError&e){
    commitCalls++;assert(!memcmp(&settings,&before.settings,sizeof(Settings)));
    if(!commitPass){e.code=RemoteTransferCode::WriteFailed;return false;}
    persisted=candidate;busy=true;return true;
}
bool remoteTransferFail(RemoteTransferError&e,RemoteTransferCode c,const char*p,const char*m){e.code=c;snprintf(e.path,sizeof(e.path),"%s",p);snprintf(e.message,sizeof(e.message),"%s",m);return false;}
constexpr unsigned WLA_SET1_B=3;
'''
tests = r'''
int main(){
    strcpy(settings.hostName,"unchanged");strcpy(settings.ssid,"old-wifi");strcpy(settings.pass,"secret");
    strcpy(settings.autoThrottle,"0");remoteRadioPreferences.modelId=12;strcpy(remoteRadioPreferences.maxPower.label,"25/1000");
    strcpy(remoteRadioPreferences.telemetry,"1:8");strcpy(remoteRadioPreferences.dynamic,"AUX9");
    const auto initial=settings;const auto initialRadio=remoteRadioPreferences;
    RemoteTransferError e;
    for(bool *owned:{&busy,&radioBusy,&remotePortalUploading,&review}){
        *owned=true;assert(!saveParamsCallback(3));assert(wm.error==409);
        assert(!captureCalls && !parseCalls && !commitCalls && !wifiLoopSaveAction);*owned=false;
    }
    parserValid=false;assert(!saveParamsCallback(3));assert(wm.error==409 && !commitCalls);parserValid=true;
    builderValid=false;assert(!saveParamsCallback(3));assert(wm.error==400 && !commitCalls);builderValid=true;
    assert(!memcmp(&settings,&initial,sizeof(Settings)) && crsfSettingsHash==123);
    commitPass=false;assert(!saveParamsCallback(3));assert(wm.error==500 && !wifiLoopSaveAction);
    assert(!memcmp(&settings,&initial,sizeof(Settings)) && crsfSettingsHash==123);
    assert(!memcmp(&remoteRadioPreferences,&initialRadio,sizeof(initialRadio)));
    brichgnow=volchgnow=vischgnow=1;
    commitPass=true;remotePortalPreferenceFailed=45;assert(saveParamsCallback(3));
    flushDelayedSave(); // Same call made by prepareReboot, now blocked by transaction ownership.
    assert(brightnessSaves==1 && volumeSaves==1 && visualSaves==1);
    assert(!brichgnow && !volchgnow && !vischgnow && busy);
    assert(persisted.generation==currentGeneration && currentGeneration==4);
    assert(wifiLoopSaveAction==32 && !remotePortalPreferenceFailed && crsfSettings.axisProfile[0].expo==42);
    assert(crsfSettingsHash==calcHash((uint8_t*)&crsfSettings,sizeof(crsfSettings)));
    assert(!memcmp(&persisted.radio,&initialRadio,sizeof(initialRadio)));
    assert(!strcmp(persisted.settings.hostName,"unchanged") && !strcmp(persisted.settings.ssid,"old-wifi"));
    assert(!strcmp(persisted.settings.pass,"secret") && !strcmp(persisted.settings.autoThrottle,"0"));
    busy=false;wifiLoopSaveAction=0;remoteRadioVersioned=false;settings=initial;
    strcpy(settings.elrsTlmRatio,"2");strcpy(settings.elrsMaxPower,"3");strcpy(settings.elrsDynPower,"1");
    assert(saveParamsCallback(3));assert(persisted.radio.modelId==12 && remoteRadioVersioned);
    const unsigned elrsCommits=commitCalls;
    wifiLoopSaveAction=0;assert(saveParamsCallback(1));assert(wifiLoopSaveAction==8 && commitCalls==elrsCommits);
    wifiLoopSaveAction=0;assert(saveParamsCallback(2));assert(wifiLoopSaveAction==16 && commitCalls==elrsCommits);
    puts("CRSF Save waits for commit, preserves unrelated settings and semantic radio choices, flushes pending adjustments before ownership, and leaves other pages on their original path");
}
'''
preferences=(ROOT/'src/src/CRSF/elrs_device_config.cpp').read_text().split('bool elrsMatchSelection',1)[0]
preferences=preferences.replace('#include "elrs_device_config.h"','#include "src/CRSF/elrs_device_config.h"')
compile_and_run(fixture+preferences+publication+delayed+save+callback+tests, [], ['-I'+str(ROOT/'.pio/libdeps/esp32dev/ArduinoJson/src')])
