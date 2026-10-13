"""Fault injection through the production staging/journal/recovery implementation."""
from check_crsf_adc import ROOT, HEADERS, compile_and_run

HEADERS['FS.h'] = r'''
#pragma once
#include <vector>
#include <map>
#include <string>
#include <cstring>
class String {};
extern unsigned testStep, testFailAt, testCrashAt, testSDUnavailableAt;
struct TestPowerLoss {};
inline bool testIOPass();
class File {
    std::vector<uint8_t>*data=nullptr; size_t pos=0; bool writing=false;
public:
    File(){} File(std::vector<uint8_t>*d,bool w=false):data(d),writing(w){}
    explicit operator bool()const{return data!=nullptr;}
    size_t size()const{return data?data->size():0;}
    bool seek(size_t p){if(!data || !testIOPass())return false;pos=p;return true;}
    int read(){return data && pos<data->size()?(*data)[pos++]:-1;}
    size_t read(uint8_t*b,size_t n){if(!data || !testIOPass())return 0;size_t len=std::min(n,data->size()-std::min(pos,data->size()));memcpy(b,data->data()+pos,len);pos+=len;return len;}
    size_t write(const uint8_t*b,size_t n){if(!data)return 0;if(!testIOPass() && n)n--;data->insert(data->end(),b,b+n);return n;}
    void flush(){if(writing && !testIOPass() && data && !data->empty())data->pop_back();}
    void close(){data=nullptr;}
};
namespace fs {
struct FS {
    std::map<std::string,std::vector<uint8_t>> files;
    bool available=true; uint64_t capacity=1000000;
    bool exists(const char*p){return available && files.count(p);}
    File open(const char*p,const char*mode="r"){if(!available || !testIOPass())return {};if(mode[0]=='w'){files[p].clear();return File(&files[p],true);}return files.count(p)?File(&files[p]):File();}
    bool remove(const char*p){if(!available || !testIOPass())return false;files.erase(p);return true;}
    bool rename(const char*a,const char*b){if(!available || !testIOPass() || !files.count(a) || files.count(b))return false;files[b]=files[a];files.erase(a);return true;}
    uint64_t totalBytes(){return capacity;} uint64_t usedBytes(){uint64_t n=0;for(auto &p:files)n+=p.second.size();return n;}
};
}
extern fs::FS LittleFS,SD;
inline bool testIOPass() {++testStep;if(testStep==testSDUnavailableAt)SD.available=false;if(testStep==testCrashAt)throw TestPowerLoss{};return testStep!=testFailAt;}
'''
HEADERS['remote_transfer_test_io.h'] = r'''
#pragma once
#include "Arduino.h"
void testTransferService();
'''
cases = r'''
#define REMOTE_TRANSFER_TEST
#include <cassert>
#include "remote_settings_restore.cpp"
fs::FS LittleFS,SD;
unsigned testStep=0,testFailAt=0,testCrashAt=0,testSDUnavailableAt=0;
unsigned long testNow=100;
bool haveFS=true,haveSD=true,FlashROMode=false,remoteSettingsRecoveryBlocked=false;
ELRSRadioPreferences remoteRadioPreferences;bool remoteRadioVersioned=false;
RemoteSettingsSnapshot current;
void testTransferService(){}
bool captureRemoteSettings(RemoteSettingsSnapshot&s,RemoteTransferError&){s=current;return true;}
void remoteSettingsChanged(){current.generation++;}
RemoteSecondarySettings secSettings={};RemoteTertiarySettings terSettings={};
uint32_t secSettingsHash=111,terSettingsHash=222;
const char *secCfgName="/secondary",*terCfgName="/tertiary";
bool cachedWritePass=false;
bool saveConfigFile(const char*,uint8_t*,int,int){return cachedWritePass;}
bool writeFileToSD(const char*,uint8_t*,int){return cachedWritePass;}
bool writeFileToFS(const char*,uint8_t*,int){return cachedWritePass;}
int main(){
    ELRSParameterView role;role.device=0xee;role.type=9;
    for(const char *unknown:{"Telemetry","Packet Rate Backpack","Antenna Future"}) {strcpy((char*)role.data+2,unknown);assert(!*remoteParameterRole(role));}
    const char *names[]={"RF Band","Packet Rate","Antenna Mode","Switch Mode","Model Match","Max Power","Telem Ratio","Dynamic"};
    for(unsigned i=0;i<8;i++){strcpy((char*)role.data+2,names[i]);assert(!strcmp(remoteParameterRole(role),rfRoles[i]));}
    for(const char *name:{"Model Match (ID:0)","Model Match (ID:63)"}){strcpy((char*)role.data+2,name);assert(!strcmp(remoteParameterRole(role),"modelMatch"));}
    for(const char *name:{"Model Match (ID:64)","Model Match (ID:)","Model Match (ID:7) future"}){strcpy((char*)role.data+2,name);assert(!*remoteParameterRole(role));}
    assert(!saveSecSettings(false));assert(secSettingsHash==111);
    assert(!saveTerSettings(false));assert(terSettingsHash==222);
    DynamicJsonDocument failed(128);failed["hostName"]="test";uint32_t hash=333;
    testFailAt=testStep+1;
    assert(!writeJSONCfgFile(failed,"/main",false,0,&hash));assert(hash==333);
    testFailAt=0;
    current.newBoard=true;current.sdAvailable=true;current.identity=42;current.crsf=remoteDefaultCrsfSettings();current.generation=1;
    for(int i=0;i<7;i++){current.stores[i].state=RemoteStoreState::Present;current.stores[i].medium=i==5?RemoteStoreMedium::SD:RemoteStoreMedium::Flash;}
    RemoteTransferError error;
    DynamicJsonDocument doc(16384);assert(remoteEncodeModel(doc.to<JsonObject>(),current,error));std::string input;serializeJson(doc,input);
    LittleFS.files["/remote-transfer.upload"]={input.begin(),input.end()};
    File f=LittleFS.open("/remote-transfer.upload");RemoteRestorePreview p;
    assert(validateRemoteTransfer(f,RemoteTransferKind::ModelProfile,current,p,error));assert(p.valid);f.close();
    std::string duplicated=input;duplicated.insert(duplicated.size()-1,",\"schemaVersion\":1");
    LittleFS.files["/remote-transfer.upload"]={duplicated.begin(),duplicated.end()};f=LittleFS.open("/remote-transfer.upload");
    assert(!validateRemoteTransfer(f,RemoteTransferKind::ModelProfile,current,p,error));f.close();
    for(uint8_t i=0;i<7;i++) {uint8_t b[16384];size_t len;assert(remoteSerializeStore(current,i,b,sizeof(b),len,error));storeFS(current.stores[i].medium).open(remoteStorePath(i),"w").write(b,len);}
    auto beforeFlash=LittleFS.files,beforeSD=SD.files;
    testStep=0;
    assert(stageCandidate(current,error));unsigned stages=testStep;
    for(unsigned fault=1;fault<=stages;fault++) {
        LittleFS.files=beforeFlash;SD.files=beforeSD;testStep=0;testFailAt=fault;
        bool ok=stageCandidate(current,error);testFailAt=0;
        for(auto &old:beforeFlash) assert(LittleFS.files[old.first]==old.second);
        for(auto &old:beforeSD) assert(SD.files[old.first]==old.second);
        assert(current.generation==1);
        (void)ok;
    }
    LittleFS.files=beforeFlash;SD.files=beforeSD;testStep=0;
    uint8_t changed[]={1,2,3};assert(remoteCheckedSettingsWrite("/ordinary",changed,3,false));unsigned writes=testStep;
    for(unsigned fault=1;fault<=writes;fault++) {
        LittleFS.files=beforeFlash;LittleFS.files["/ordinary"]={9,8,7};testStep=0;testFailAt=fault;
        bool ok=remoteCheckedSettingsWrite("/ordinary",changed,3,false);testFailAt=0;
        assert(ok || LittleFS.files["/ordinary"]==std::vector<uint8_t>({9,8,7}));
        remoteSettingsRecoveryBlocked=false;
    }
    puts("production upload binding, duplicate-key rejection and all-store staging faults pass");
    testStep=0;testFailAt=0;restoreBusy=false;radioReview=false;current.generation=1;
    strcpy(current.settings.CfgOnSD,"0");
    RemoteSettingsSnapshot changedSettings=current;
    strcpy(changedSettings.settings.hostName,"restored");strcpy(changedSettings.settings.mqttUser,"new:secret");
    changedSettings.secondary.brightness=12;changedSettings.tertiary.musFolderNum=9;changedSettings.crsf.axisProfile[0].expo=37;
    doc.clear();assert(remoteEncodeBackup(doc.to<JsonObject>(),changedSettings,error));input.clear();serializeJson(doc,input);
    LittleFS.files=beforeFlash;SD.files=beforeSD;
    for(uint8_t i=0;i<7;i++){uint8_t b[16384];size_t n;assert(remoteSerializeStore(current,i,b,sizeof(b),n,error));storeFS(current.stores[i].medium).files[remoteStorePath(i)]={b,b+n};}
    LittleFS.files["/remote-transfer.upload"]={input.begin(),input.end()};
    f=LittleFS.open("/remote-transfer.upload");assert(validateRemoteTransfer(f,RemoteTransferKind::DeviceBackup,current,p,error));f.close();
    auto originalsFlash=LittleFS.files,originalsSD=SD.files;
    testStep=0;assert(applyRemoteTransfer(p,error));unsigned transactionSteps=testStep;
    assert(remoteRadioReviewPending());assert(recoverRemoteSettingsRestore(error));
    auto newFlash=LittleFS.files,newSD=SD.files;
    for(unsigned mode=0;mode<2;mode++) for(unsigned fault=1;fault<=transactionSteps;fault++) {
        LittleFS.files=originalsFlash;SD.files=originalsSD;current.generation=1;restoreBusy=false;radioReview=false;remoteSettingsRecoveryBlocked=false;
        testStep=0;testFailAt=mode?0:fault;testCrashAt=mode?fault:0;
        try{applyRemoteTransfer(p,error);}catch(const TestPowerLoss&){}
        testFailAt=testCrashAt=0;restoreBusy=false;radioReview=false;remoteSettingsRecoveryBlocked=false;
        assert(recoverRemoteSettingsRestore(error));
        bool allOld=true,allNew=true;
        for(uint8_t i=0;i<7;i++){auto &fs=storeFS(current.stores[i].medium);const char *path=remoteStorePath(i);allOld&=fs.files[path]==(i==5?originalsSD:originalsFlash)[path];allNew&=fs.files[path]==(i==5?newSD:newFlash)[path];}
        assert(allOld || allNew);
    }
    LittleFS.files=originalsFlash;SD.files=originalsSD;current.generation=1;restoreBusy=false;radioReview=false;
    FlashROMode=true;assert(!applyRemoteTransfer(p,error));assert(LittleFS.files==originalsFlash);FlashROMode=false;
    testNow=p.expiresAt;assert(!applyRemoteTransfer(p,error));assert(LittleFS.files==originalsFlash);testNow=100;
    assert(applyRemoteTransfer(p,error));haveSD=false;SD.available=false;restoreBusy=false;
    assert(!recoverRemoteSettingsRestore(error));assert(remoteSettingsRecoveryBlocked);
    haveSD=true;SD.available=true;remoteSettingsRecoveryBlocked=false;
    assert(recoverRemoteSettingsRestore(error));
    assert(remoteSettingsRestoreLoaded(error));assert(remoteCompleteRadioReview());
    LittleFS.files=originalsFlash;SD.files=originalsSD;current.generation=1;restoreBusy=false;radioReview=false;
    strcpy(changedSettings.settings.CfgOnSD,"1");doc.clear();assert(remoteEncodeBackup(doc.to<JsonObject>(),changedSettings,error));input.clear();serializeJson(doc,input);
    LittleFS.files["/remote-transfer.upload"]={input.begin(),input.end()};f=LittleFS.open("/remote-transfer.upload");assert(validateRemoteTransfer(f,RemoteTransferKind::DeviceBackup,current,p,error));f.close();
    assert(applyRemoteTransfer(p,error));
    assert(!LittleFS.exists("/crsfcfg") && SD.exists("/crsfcfg"));
    assert(!LittleFS.exists("/remhacfg.json") && SD.exists("/remhacfg.json"));
    assert(recoverRemoteSettingsRestore(error));assert(remoteSettingsRestoreLoaded(error));
    printf("%u transaction boundaries: write failures and power loss recover one complete generation; missing SD blocks recovery\n",transactionSteps);
    current.generation=1;restoreBusy=false;radioReview=false;
    RemoteSettingsSnapshot localBefore=current,localCandidate=current;
    localBefore.stores[0].medium=RemoteStoreMedium::Flash;
    localBefore.stores[6].medium=RemoteStoreMedium::SD;
    localCandidate=localBefore;localCandidate.radio.modelId=12;
    strcpy(localCandidate.settings.hostName,"local-save");localCandidate.crsf.axisProfile[0].expo=42;
    LittleFS.files.clear();SD.files.clear();
    for(uint8_t id:{0,6}) {uint8_t b[16384];size_t n;assert(remoteSerializeStore(localBefore,id,b,sizeof(b),n,error,RemoteSettingsEncoding::StoredFile));storeFS(localBefore.stores[id].medium).files[remoteStorePath(id)]={b,b+n};}
    auto localOldFlash=LittleFS.files,localOldSD=SD.files;
    testStep=0;assert(remoteCommitELRSSettings(localBefore,localCandidate,error));unsigned localSteps=testStep;
    assert(remoteSettingsBusy() && !remoteRadioReviewPending());
    auto localNewFlash=LittleFS.files,localNewSD=SD.files;
    assert(recoverRemoteSettingsRestore(error) && remoteSettingsRestoreLoaded(error));
    assert(!LittleFS.exists(journalPath) && !remoteRadioReviewPending());
    for(unsigned mode=0;mode<2;mode++)for(unsigned fault=1;fault<=localSteps;fault++) {
        LittleFS.files=localOldFlash;SD.files=localOldSD;current.generation=1;
        restoreBusy=radioReview=remoteSettingsRecoveryBlocked=false;testStep=0;
        testFailAt=mode?0:fault;testCrashAt=mode?fault:0;
        try{remoteCommitELRSSettings(localBefore,localCandidate,error);}catch(const TestPowerLoss&){}
        testFailAt=testCrashAt=0;restoreBusy=radioReview=remoteSettingsRecoveryBlocked=false;
        assert(recoverRemoteSettingsRestore(error));assert(!remoteRadioReviewPending());
        bool allOld=LittleFS.files[remoteStorePath(0)]==localOldFlash[remoteStorePath(0)] && SD.files[remoteStorePath(6)]==localOldSD[remoteStorePath(6)];
        bool allNew=LittleFS.files[remoteStorePath(0)]==localNewFlash[remoteStorePath(0)] && SD.files[remoteStorePath(6)]==localNewSD[remoteStorePath(6)];
        assert(allOld || allNew);
    }
    FlashROMode=true;localBefore.flashRO=localCandidate.flashRO=true;
    localBefore.stores[0].medium=localCandidate.stores[0].medium=RemoteStoreMedium::SD;
    SD.files.clear();LittleFS.files.clear();LittleFS.files["/readonly"]={7};
    for(uint8_t id:{0,6}) {uint8_t b[16384];size_t n;assert(remoteSerializeStore(localBefore,id,b,sizeof(b),n,error,RemoteSettingsEncoding::StoredFile));SD.files[remoteStorePath(id)]={b,b+n};}
    auto sdOld=SD.files;restoreBusy=radioReview=remoteSettingsRecoveryBlocked=false;testStep=0;
    assert(remoteCommitELRSSettings(localBefore,localCandidate,error));unsigned sdSteps=testStep;
    assert(LittleFS.files.size()==1 && !remoteRadioReviewPending());
    auto sdNew=SD.files;
    for(unsigned fault=1;fault<=sdSteps;fault++) {
        SD.files=sdOld;restoreBusy=radioReview=remoteSettingsRecoveryBlocked=false;testStep=0;testFailAt=fault;
        bool ok=remoteCommitELRSSettings(localBefore,localCandidate,error);testFailAt=0;
        if(!ok)for(uint8_t id:{0,6})assert(SD.files[remoteStorePath(id)]==sdOld[remoteStorePath(id)]);
        assert(LittleFS.files.size()==1 && !remoteRadioReviewPending());
    }
    unsigned sdCrashes=0;
    for(unsigned fault=1;fault<=sdSteps;fault++) {
        SD.available=true;SD.files=sdOld;LittleFS.files.clear();LittleFS.files["/readonly"]={7};
        restoreBusy=radioReview=remoteSettingsRecoveryBlocked=false;testStep=0;testCrashAt=fault;
        try{remoteCommitELRSSettings(localBefore,localCandidate,error);}catch(const TestPowerLoss&){}
        testCrashAt=0;testFailAt=testSDUnavailableAt=0;
        assert(recoverRemoteSettingsRestore(error));assert(remoteSettingsRestoreLoaded(error));
        for(uint8_t id:{0,6}) {
            const auto &actual=SD.files[remoteStorePath(id)];
            assert(actual==sdOld[remoteStorePath(id)] || actual==sdNew[remoteStorePath(id)]);
        }
        ++sdCrashes;
    }
    bool missingSdRollbackReported=false;
    for(unsigned fault=1;fault<=sdSteps && !missingSdRollbackReported;fault++) {
        SD.available=true;SD.files=sdOld;LittleFS.files.clear();LittleFS.files["/readonly"]={7};
        restoreBusy=radioReview=remoteSettingsRecoveryBlocked=false;testStep=0;
        testFailAt=fault;testSDUnavailableAt=fault;
        const bool ok=remoteCommitELRSSettings(localBefore,localCandidate,error);
        testFailAt=testSDUnavailableAt=0;
        if(!ok && error.code==RemoteTransferCode::RollbackRequired) {
            assert(remoteSettingsRecoveryBlocked);
            assert(!SD.available);
            missingSdRollbackReported=true;
        }
    }
    assert(missingSdRollbackReported);
    SD.available=true;
    FlashROMode=false;restoreBusy=false;
    printf("%u local journal, %u FlashRO checked-save, %u FlashRO per-file crash, and missing-SD rollback boundaries pass\n",localSteps,sdSteps,sdCrashes);
}
'''
def function(signature):
    source=(ROOT/'src/remote_settings.cpp').read_text()
    start=source.index(signature+'\n{')
    return source[start:source.index('\n}',start)+2]+'\n'

functions=''.join(function(s) for s in ['uint32_t calcHash(uint8_t *buf, int len)',
    'static bool saveSecSettings(bool useCache)','static bool saveTerSettings(bool useCache)',
    'static bool writeJSONCfgFile(const JsonDocument& json, const char *fn, bool useSD, uint32_t oldHash = 0, uint32_t *newHash = NULL)'])
cases=cases.replace('int main(){',functions+'int main(){')
compile_and_run(cases,['elrs_input_model.cpp','elrs_crsf_core.cpp','elrs_crsf_transport.cpp','../../remote_settings_transfer.cpp'],
                ['-I'+str(ROOT/'.pio/libdeps/esp32dev/ArduinoJson/src'),'-lbcrypt'])
