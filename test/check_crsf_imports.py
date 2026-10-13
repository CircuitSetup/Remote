"""Exercise the production transfer parser, never a second firmware validator."""
from check_crsf_adc import ROOT, HEADERS, compile_and_run
import json
import tempfile
import sys
from pathlib import Path

HEADERS['FS.h'] = '#pragma once\nclass String {}; class File {};\n'
cases = r'''
#include <cassert>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include "remote_settings_transfer.h"
int main(int argc,char **argv) {
    RemoteSettingsSnapshot target;
    target.crsf = remoteDefaultCrsfSettings();
    target.crsf.axisProfile[0].minimum = 11;
    target.crsf.axisProfile[0].maximum = 1900;
    strcpy(target.settings.ssid, "target wifi");
    target.identity = 42;
    target.newBoard = true;
    target.stores[0].state = RemoteStoreState::Present;
    target.stores[6].state = RemoteStoreState::Present;
    target.crsf.logic.rules[0] = {1,1,0,30,0};
    target.crsf.logic.rules[8] = {3,17,5,0,0};
    target.crsf.logic.rules[15] = {4,26,6,0,0};
    target.crsf.mixes.lines[0] = {1,25,33,55,-9,2};
    assert(elrsIsValidModelReferences(target.crsf.logic, target.crsf.mixes));
    target.crsf.mixes.lines[0].switchSource = 128 | 18;
    assert(!elrsIsValidModelReferences(target.crsf.logic, target.crsf.mixes));
    target.crsf.mixes.lines[0].switchSource = 33;
    for(int i=1;i<argc;i++) {
        std::ifstream f(argv[i]);std::stringstream buffer;buffer<<f.rdbuf();std::string file=buffer.str();
        assert(!file.empty() && file.size()<REMOTE_MODEL_MAX_BYTES);
        DynamicJsonDocument imported(16384);assert(!deserializeJson(imported,file));RemoteSettingsSnapshot proposed;RemoteTransferError err;
        if(!remoteDecodeTransfer(imported.as<JsonVariantConst>(),RemoteTransferKind::ModelProfile,target,proposed,err)) {fprintf(stderr,"Converted fixture rejected %s: %s %s\n",argv[i],err.path,err.message);return 1;}
        assert(proposed.identity==42 && proposed.crsf.axisProfile[0].minimum==11);
        ELRSParameterView power;power.type=9;power.minimum.raw=0;power.maximum.raw=3;power.optionsOffset=2;
        const char options[]="25;50;100;250";memcpy(power.data+2,options,sizeof(options));power.unitOffset=2+sizeof(options);strcpy((char*)power.data+power.unitOffset,"mW");
        uint8_t selected=0;assert(elrsMatchSelection(power,proposed.radio.maxPower,selected)&&selected==3);
        ELRSLogicInputs inputs={};inputs.axesValid=true;inputs.validSwitches=0xfff;
        for(int16_t value:{1000,1250,1500,1750,2000}) {for(auto &axis:inputs.axesUs)axis=value;auto logical=elrsEvaluateLogic(proposed.crsf.logic,inputs);int16_t base[16];for(auto &b:base)b=1000;auto result=elrsEvaluateMixes(proposed.crsf.mixes,inputs,logical,base,0);assert(!result.invalidChannels);}
        if(std::string(argv[i]).find("switch_mix")!=std::string::npos){
            for(unsigned state:{0,1}){inputs.switchStates=state;int16_t base[16];for(auto &b:base)b=1500;
                for(unsigned sw=0;sw<12;sw++)if(proposed.crsf.switchRouting.channels[sw])base[proposed.crsf.switchRouting.channels[sw]-1]=(state&(1<<sw))?2000:1000;
                auto result=elrsEvaluateMixes(proposed.crsf.mixes,inputs,elrsEvaluateLogic(proposed.crsf.logic,inputs),base,0);assert(result.valuesUs[4]==1750);
            }
        }
    }
    DynamicJsonDocument doc(16384);
    RemoteTransferError error;
    assert(remoteEncodeModel(doc.to<JsonObject>(), target, error));
    std::string file;
    serializeJson(doc, file);
    assert(!deserializeJson(doc,file));
    RemoteSettingsSnapshot candidate;
    assert(remoteDecodeTransfer(doc.as<JsonVariantConst>(), RemoteTransferKind::ModelProfile, target, candidate, error));
    assert(candidate.identity == 42 && !strcmp(candidate.settings.ssid,"target wifi"));
    assert(!memcmp(&target.crsf, &candidate.crsf, sizeof(target.crsf)));
    assert(candidate.crsf.logic.rules[1].function == 0);
    assert(candidate.crsf.mixes.lines[0].source == 25);
    for(const char *invalid:{"","reverse|deadband"}) {
        doc["inputs"][0][invalid]=0;
        assert(!remoteDecodeTransfer(doc.as<JsonVariantConst>(),RemoteTransferKind::ModelProfile,target,candidate,error));
        doc["inputs"][0].remove(invalid);
    }
    {
        auto floats=target;floats.crsf.displayConfig.offset=-0.0f;
        DynamicJsonDocument encoded(16384);assert(remoteEncodeModel(encoded.to<JsonObject>(),floats,error));std::string text;serializeJson(encoded,text);assert(!deserializeJson(encoded,text));
        assert(remoteDecodeTransfer(encoded.as<JsonVariantConst>(),RemoteTransferKind::ModelProfile,target,candidate,error));
        assert(!memcmp(&floats.crsf.displayConfig.offset,&candidate.crsf.displayConfig.offset,4));
    }
    doc["radio"].createNestedArray("requirements").createNestedObject()["role"]="antenna";
    doc["radio"]["requirements"][0]["label"]="Gemini";
    doc["radio"]["requirements"][0]["unit"]="";
    assert(!remoteDecodeTransfer(doc.as<JsonVariantConst>(),RemoteTransferKind::ModelProfile,target,candidate,error));
    target.rfChoiceCount=1;target.deviceGeneration=2;target.capabilityFingerprint=99;
    strcpy(target.rfChoices[0].role,"antenna");strcpy(target.rfChoices[0].label,"Gemini");target.rfChoices[0].descriptorHash=99;
    assert(remoteDecodeTransfer(doc.as<JsonVariantConst>(),RemoteTransferKind::ModelProfile,target,candidate,error));
    {
        auto observed=target;observed.rfChoiceCount=4;
        const char *roles[]={"antenna","power","telemetry","dynamic"};
        for(int i=0;i<4;i++){strcpy(observed.rfChoices[i].role,roles[i]);strcpy(observed.rfChoices[i].label,"current");}
        DynamicJsonDocument exported(16384);assert(remoteEncodeModel(exported.to<JsonObject>(),observed,error));
        assert(exported["radio"]["requirements"].size()==1);
    }
    doc["radio"].remove("requirements");
    doc["mixes"][0][2] = 18;
    assert(!remoteDecodeTransfer(doc.as<JsonVariantConst>(),RemoteTransferKind::ModelProfile,target,candidate,error));
    doc["mixes"][0][2] = 33;
    doc["inputs"][0]["minimum"] = 123;
    assert(!remoteDecodeTransfer(doc.as<JsonVariantConst>(),RemoteTransferKind::ModelProfile,target,candidate,error));
    doc["inputs"][0].remove("minimum");
    doc["radio"]["maxPower"]["label"] = "25/1000";
    assert(remoteDecodeTransfer(doc.as<JsonVariantConst>(),RemoteTransferKind::ModelProfile,target,candidate,error));
    assert(!strcmp(candidate.radio.maxPower.label,"25/1000"));
    doc["radio"]["modelId"] = 64;
    assert(!remoteDecodeTransfer(doc.as<JsonVariantConst>(),RemoteTransferKind::ModelProfile,target,candidate,error));
    doc["radio"]["modelId"] = 0;
    doc.remove("localActions");
    assert(!remoteDecodeTransfer(doc.as<JsonVariantConst>(),RemoteTransferKind::ModelProfile,target,candidate,error));
    assert(target.identity == 42 && target.crsf.axisProfile[0].minimum == 11);
    for(int i=0;i<7;i++) target.stores[i].state=RemoteStoreState::Present;
    target.sdAvailable=true;target.secondary.brightness=15;target.secondary.curVolume=20;
    for(int i=0;i<8;i++) {memset(target.settings.mqttbt[i],'"',127);memset(target.settings.mqttbo[i],'\\',63);memset(target.settings.mqttbf[i],'\n',63);}
    doc.clear();assert(remoteEncodeBackup(doc.to<JsonObject>(),target,error));
    assert(!doc.overflowed()); file.clear();serializeJson(doc,file);
    assert(file.size()<REMOTE_BACKUP_MAX_BYTES);
    assert(!deserializeJson(doc,file,DeserializationOption::NestingLimit(8)));
    printf("maximum escaped backup bytes=%u, native document=%u\n",(unsigned)file.size(),(unsigned)doc.memoryUsage());
    assert(remoteDecodeTransfer(doc.as<JsonVariantConst>(),RemoteTransferKind::DeviceBackup,target,candidate,error));
    assert(!memcmp(&candidate.crsf,&target.crsf,sizeof(target.crsf)));
    assert(!strcmp(candidate.settings.mqttbt[7],target.settings.mqttbt[7]));
    {
        JsonObject main=doc["stores"][0]["data"];const std::string original=main["wifiConRetries"].as<const char*>();
        for(const char *invalid:{"+1","01"," 1"}) {main["wifiConRetries"]=invalid;assert(!remoteDecodeSettings(main,candidate.settings,candidate.radio,false,error));}
        main["wifiConRetries"]=original;
    }
    doc["stores"][0]["data"]["pass"]="tampered";
    assert(!remoteDecodeTransfer(doc.as<JsonVariantConst>(),RemoteTransferKind::DeviceBackup,target,candidate,error));
    for(unsigned length : {24,48,52,54,56,68,72,76,80,84,96,106,119,120,209,297}) {
        uint8_t bytes[300] = {};
        auto original = remoteDefaultCrsfSettings();
        if(length == 24) {
            for(int i=0;i<4;i++) { ELRSAxisCalibrationData cal={0,1024,2047}; memcpy(bytes+2+i*6,&cal,6); }
        } else {
            memcpy(bytes+2,&original,length);
            if(length==209) bytes[122]=1;
        }
        bytes[0]=length&255; bytes[1]=length>>8; bytes[length+2]=remoteFrameChecksum(bytes,length+2);
        ELRSCrsfSettingsBlob decoded;
        // Only explicitly documented complete layouts are admitted.
        assert(remoteDecodeCrsf(bytes,length+3,decoded,error));
    }
    uint8_t invalid[301]={};
    for(unsigned length : {1,23,25,53,57,69,85,97,107,121,208,210,296,298}) {
        invalid[0]=length&255; invalid[1]=length>>8;
        invalid[length+2]=remoteFrameChecksum(invalid,length+2);
        ELRSCrsfSettingsBlob decoded;
        assert(!remoteDecodeCrsf(invalid,length+3,decoded,error));
    }
    char hash[65]; remoteSha256((const uint8_t*)"abc",3,hash);
    assert(!strcmp(hash,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    puts("native model admission, retained calibration, stable sources, migration and SHA256 pass");
}
'''
with tempfile.TemporaryDirectory(prefix='remote-converted-') as directory:
    generated=[]
    if (ROOT/'tools/model_import.py').exists():
        sys.path.insert(0,str(ROOT/'tools'))
        from model_import import convert_model
        for fixture in sorted((ROOT/'test/fixtures/model_import/edgetx').glob('*/*.yml')):
            mapping={'axes':{'Ail':1,'Ele':2,'Rud':3,'Thr':4},'gimbals':[1,2,3,4],'switches':[5]+[0]*11,
                     'switchMap':{('SF' if fixture.parent.name=='2.12.4' else 'SA'):{'input':0,'reverse':True,'positions':2}}}
            result=convert_model(fixture,'TX16S' if fixture.parent.name=='2.12.4' else 'Boxer',fixture.parent.name,mapping)
            if result.profile is not None:
                output=Path(directory)/(fixture.parent.name+'-'+fixture.stem+'.json')
                output.write_text(json.dumps(result.profile,ensure_ascii=False),encoding='utf-8');generated.append(output)
        assert len(generated)==5, 'Every admitted versioned fixture must reach production validation'
    compile_and_run(cases,['elrs_input_model.cpp','elrs_crsf_core.cpp','elrs_crsf_transport.cpp','../../remote_settings_transfer.cpp'],
                    ['-I'+str(ROOT/'.pio/libdeps/esp32dev/ArduinoJson/src'),'-lbcrypt'],generated)
    print('production firmware admitted/evaluated',len(generated),'converted fixtures')
