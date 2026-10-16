"""Exercise the CRSF form save callback and its checked settings-file boundary."""
import re
from pathlib import Path

from check_crsf_adc import HEADERS, ROOT, compile_and_run


def function(path, signature):
    source = (ROOT / path).read_text(encoding="utf-8")
    match = re.search(signature, source)
    if not match:
        raise AssertionError(f"missing function matching {signature}")
    opening = source.index("{", match.start())
    depth = 0
    for offset in range(opening, len(source)):
        if source[offset] == "{":
            depth += 1
        elif source[offset] == "}":
            depth -= 1
            if not depth:
                return source[match.start():offset + 1] + "\n"
    raise AssertionError(f"unterminated function matching {signature}")


loop_save = function("src/src/CRSF/crsf_wifi.h", r"static bool crsf_wifi_loop_settings\(\)")
callback = function("src/remote_wifi.cpp", r"static bool saveParamsCallback\(int paramspage\)")
HEADERS["FS.h"] = "#pragma once\n"

fixture = r'''
#include <cassert>
#include <cstdint>
struct ELRSCrsfSettingsBlob { int value=0; };
struct RemoteTransferError {};
struct ELRSPortalPreferences { unsigned modelId=0; unsigned marker=0; };
bool deviceBusy=false;
struct TestDeviceConfig { bool busy() const { return deviceBusy; } };
struct TestMode { TestDeviceConfig &deviceConfig(){return config;} TestDeviceConfig config; } elrsMode;
struct TestWM { int error=0; void setParamSaveError(int value){error=value;} } wm;
bool opModeCRSF=true,settingsBusy=false,reviewPending=false;
bool buildPass=true,persistPass=true;
unsigned buildCalls=0,persistCalls=0,syncCalls=0,flushCalls=0;
ELRSPortalPreferences savedPreferences;
bool remoteSettingsBusy(){return settingsBusy;}
bool remoteRadioReviewPending(){return reviewPending;}
bool buildCRSFPortalInputSettings(ELRSCrsfSettingsBlob &candidate,RemoteTransferError&){buildCalls++;candidate.value=55;return buildPass;}
ELRSPortalPreferences snapshotELRSPortalPreferences(){return savedPreferences;}
void flushDelayedSave(){flushCalls++;}
bool saveELRSSettings(const ELRSCrsfSettingsBlob &model,const ELRSPortalPreferences &prefs){persistCalls++;assert(model.value==55);assert(prefs.modelId==12&&prefs.marker==34);return persistPass;}
void syncCRSFPortalBuffers(){syncCalls++;}
'''

tests = r'''
int main(){
    savedPreferences.modelId=12;savedPreferences.marker=34;
    assert(crsf_wifi_loop_settings());
    assert(buildCalls==1&&flushCalls==1&&persistCalls==1&&syncCalls==1&&wm.error==0);
    buildCalls=persistCalls=syncCalls=flushCalls=0;settingsBusy=true;
    assert(!crsf_wifi_loop_settings()&&wm.error==409);
    assert(buildCalls==0&&persistCalls==0&&syncCalls==0);settingsBusy=false;
    deviceBusy=true;assert(!crsf_wifi_loop_settings()&&wm.error==409);
    assert(buildCalls==0&&persistCalls==0);deviceBusy=false;
    reviewPending=true;assert(!crsf_wifi_loop_settings()&&wm.error==409);
    assert(buildCalls==0&&persistCalls==0);reviewPending=false;
    buildPass=false;assert(!crsf_wifi_loop_settings()&&wm.error==400);
    assert(buildCalls==1&&persistCalls==0&&syncCalls==1);buildPass=true;
    persistPass=false;assert(!crsf_wifi_loop_settings()&&wm.error==500);
    assert(persistCalls==1&&syncCalls==2);
}
'''
compile_and_run(fixture + loop_save + tests, [])

if "case 3:" not in callback or "crsf_wifi_saveParamsCallback() || !crsf_wifi_loop_settings()" not in callback:
    raise AssertionError("CRSF Settings page must run both its parser and checked save callback")
if "settings = previousSettings" not in callback or "return false" not in callback:
    raise AssertionError("failed CRSF saves must restore form buffers and reject the HTTP save")
if "case 1:" not in callback or "case 2:" not in callback:
    raise AssertionError("general settings and MQTT pages must retain their existing save callbacks")
print("CRSF settings save handles busy, invalid form, failed file persistence, HTTP callback rollback, and saved model ID")
