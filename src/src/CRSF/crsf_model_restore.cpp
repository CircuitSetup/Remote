#include "crsf_model_files.h"
#ifdef HAVE_CRSF
#include "../../remote_global.h"
#include "../../remote_main.h"
#include "../../remote_settings.h"
#include "../../remote_wifi.h"
#include "crsf_kludge.h"
#include "crsf_settings.h"
#include "elrs_crsf.h"
#include <LittleFS.h>
#include <SD.h>
#include <memory>
#include <cstring>
#include <cstdio>
#ifndef _WIN32
#include <mbedtls/sha256.h>
#else
#include <vector>
#endif
extern ELRSCrsfMode elrsMode;
static uint32_t settingsGeneration=1;
void remoteSettingsChanged(){++settingsGeneration;}
static uint32_t maintenanceExpires=0;
bool remoteSettingsBusy() {return crsfSettingsSaving || crsfSettingsRecoveryBlocked;}
bool remoteRadioReviewPending() {return snapshotELRSPortalPreferences().radioReview!=0;}
bool remoteMaintenanceActive() {return maintenanceExpires && (int32_t)(millis()-maintenanceExpires)<0;}
void remoteSetMaintenance(bool enabled) {maintenanceExpires=enabled?millis()+300000U:0;}
struct RfObservation {RemoteRfChoice choice;uint32_t generation=0,at=0,operationId=0;uint8_t field=0;};
static RfObservation rfObservations[8];
static uint32_t rfProofOperationId=0x60000000;
static const char *const rfRoles[]={"rfBand","rfRate","antenna","switchMode","modelMatch","power","telemetry","dynamic"};
const char *remoteParameterRole(const ELRSParameterView &p) {
    if(p.device!=0xee || p.type!=9 || p.hidden())return "";
    if(!strncmp(p.name(),"Model Match (ID:",16)){const char *at=p.name()+16;unsigned id=0,n=0;
        while(*at>='0'&&*at<='9'&&n<3){id=id*10+*at++-'0';n++;}
        return n&&n<=2&&id<=63&&!strcmp(at,")")?"modelMatch":"";}
    char name[64]={};unsigned n=0;for(const char *s=p.name();*s&&n<63;s++)if(*s>='A'&&*s<='Z')name[n++]=*s+32;else if((*s>='a'&&*s<='z')||(*s>='0'&&*s<='9'))name[n++]=*s;
    const char *tokens[]={"rfband","packetrate","antennamode","switchmode","modelmatch","maxpower","telemratio","dynamic"};
    for(unsigned i=0;i<8;i++)if(!strcmp(name,tokens[i]))return rfRoles[i];return "";
}
void remoteObserveRfChoice(const ELRSParameterView &p,uint32_t operationId) {
    const char *role=remoteParameterRole(p);if(!*role)return;unsigned slot=0;while(slot<8&&strcmp(role,rfRoles[slot]))slot++;
    const char *label;size_t length;if(slot==8 || !p.optionAt(p.value.raw,label,length) || !length || length>=64 || strlen(p.unit())>=24)return;
    auto &record=rfObservations[slot];if(record.operationId==operationId && record.generation==p.generation)return;
    record={};snprintf(record.choice.role,24,"%s",role);memcpy(record.choice.label,label,length);snprintf(record.choice.unit,24,"%s",p.unit());record.choice.descriptorHash=p.value.descriptorHash;
    record.generation=p.generation;record.at=millis();record.operationId=operationId;record.field=p.field;
}
void remoteCaptureRfChoices(RemoteSettingsSnapshot &s) {
    s.rfChoiceCount=0;s.capabilityFingerprint=0;s.deviceGeneration=0;
#ifndef REMOTE_TRANSFER_TEST
#ifdef HAVE_CRSF
    if(opModeCRSF)for(unsigned i=0;i<elrsMode.deviceConfig().deviceCount();i++){auto *d=elrsMode.deviceConfig().deviceAt(i);if(d->address==0xee && d->serial==0x454c5253)s.deviceGeneration=d->generation;}
#endif
#endif
    uint32_t hash=2166136261U;
    for(const auto &r:rfObservations)if(r.field && r.generation==s.deviceGeneration && (uint32_t)(millis()-r.at)<=300000){s.rfChoices[s.rfChoiceCount++]=r.choice;for(const char *text:{r.choice.role,r.choice.label,r.choice.unit})while(*text)hash=(hash^(uint8_t)*text++)*16777619U;hash=(hash^r.choice.descriptorHash)*16777619U;}
    if(s.rfChoiceCount)s.capabilityFingerprint=hash;
}

static void transferService() {
#ifndef REMOTE_TRANSFER_TEST
#ifdef HAVE_CRSF
    if(opModeCRSF) {elrsMode.setDeviceConfigAccess(remoteMaintenanceActive(),remoteSettingsBusy(),remoteRadioReviewPending());serviceCRSF(false);}
#endif
#else
    testTransferService();
#endif
}
namespace {
class TransferReader {
public:
    explicit TransferReader(File &f):file(f) {}
    int read() {if(!(++count&255)) transferService();return file.read();}
    size_t readBytes(char *b,size_t n) {size_t read=0;while(read<n) {int c=this->read();if(c<0) break;b[read++]=c;}return read;}
private:File &file;unsigned count=0;
};
static bool fileHash(File &file,char hash[65]) {
    if(!file || !file.seek(0)) return false;
#ifndef REMOTE_TRANSFER_TEST
    mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);mbedtls_sha256_starts_ret(&ctx,0);
    uint8_t block[256],digest[32];size_t total=0,n;
    while((n=file.read(block,sizeof(block)))) {total+=n;mbedtls_sha256_update_ret(&ctx,block,n);transferService();}
    mbedtls_sha256_finish_ret(&ctx,digest);mbedtls_sha256_free(&ctx);
    for(unsigned i=0;i<32;i++) snprintf(hash+i*2,3,"%02x",digest[i]);
#else
    std::vector<uint8_t> b(file.size());size_t total=file.read(b.data(),b.size());remoteSha256(b.data(),total,hash);
#endif
    return total==file.size() && file.seek(0);
}
static bool settingsHash(const RemoteSettingsSnapshot &s,char hash[65],RemoteTransferError &) {
    uint8_t bytes[sizeof(s.crsf)+sizeof(s.radio)+sizeof(s.settings)];size_t n=0;
    memcpy(bytes+n,&s.crsf,sizeof(s.crsf));n+=sizeof(s.crsf);memcpy(bytes+n,&s.radio,sizeof(s.radio));n+=sizeof(s.radio);memcpy(bytes+n,&s.settings,sizeof(s.settings));
    remoteSha256(bytes,sizeof(bytes),hash);return true;
}
static bool uniqueJsonKeys(File &file) {
    struct Level {uint32_t keys[96];uint8_t count;bool object,expectKey;};
    std::unique_ptr<Level[]> levels(new(std::nothrow) Level[8]());if(!levels) return false;
    int depth=0,c;bool inString=false,key=false,escaped=false,rootSeen=false;uint32_t hash=2166136261U;
    TransferReader reader(file);
    while((c=reader.read())>=0) {
        if(!depth && !inString && c!=' ' && c!='\r' && c!='\n' && c!='\t' && (rootSeen || c!='{')) return false;
        if(inString) {
            if(escaped) {escaped=false;continue;}
            if(c=='\\') {if(key) return false;escaped=true;continue;}
            if(c=='"') {
                inString=false;
                if(key) {auto &l=levels[depth-1];if(l.count>=96) return false;for(unsigned i=0;i<l.count;i++) if(l.keys[i]==hash) return false;l.keys[l.count++]=hash;l.expectKey=false;}
            } else if(key) hash=(hash^(uint8_t)c)*16777619U;
            continue;
        }
        if(c=='"') {inString=true;key=depth && levels[depth-1].object && levels[depth-1].expectKey;hash=2166136261U;}
        else if(c=='{' || c=='[') {if(depth==8) return false;rootSeen=true;levels[depth++]={{},0,c=='{',c=='{'};}
        else if(c=='}' || c==']') {if(!depth) return false;--depth;}
        else if(c==',' && depth && levels[depth-1].object) levels[depth-1].expectKey=true;
    }
    return rootSeen && !inString && !depth && file.seek(0);
}
static bool decodeFile(File &file,RemoteTransferKind kind,const RemoteSettingsSnapshot &target,RemoteSettingsSnapshot &candidate,RemoteTransferError &e) {
    if(!file || !file.size() || file.size()>(kind==RemoteTransferKind::DeviceBackup?REMOTE_BACKUP_MAX_BYTES:REMOTE_MODEL_MAX_BYTES)) return remoteTransferFail(e,RemoteTransferCode::TooLarge,"file","Native file exceeds its size bound");
    if(!file.seek(0) || !uniqueJsonKeys(file)) return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"file","Duplicate/escaped keys, excessive depth or malformed structure");
    DynamicJsonDocument doc(REMOTE_TRANSFER_JSON_CAPACITY);TransferReader reader(file);
    auto result=deserializeJson(doc,reader,DeserializationOption::NestingLimit(8));
    if(result) return remoteTransferFail(e,result==DeserializationError::NoMemory?RemoteTransferCode::TooLarge:RemoteTransferCode::InvalidValue,"file","Native JSON could not be parsed within its bound");
    return remoteDecodeTransfer(doc.as<JsonVariantConst>(),kind,target,candidate,e);
}
bool applySafe(RemoteTransferError &e) {
#ifndef REMOTE_TRANSFER_TEST
#ifdef HAVE_CRSF
    if(opModeCRSF) {
        auto s=elrsMode.deviceConfig().radioStatus(millis());auto local=elrsMode.getStatus();
        if(!remoteMaintenanceActive() || !s.rfConnectionKnown || s.rfConnected || !s.armedKnown || s.armed || s.statusAgeMs>1000 || elrsMode.finalChannel5()>992 || local.fakePowerOn || local.calibrating || local.selfTestActive || elrsMode.deviceConfig().busy())
            return remoteTransferFail(e,RemoteTransferCode::Busy,"maintenance","Fresh disconnected and disarmed maintenance is required");
    } else
#endif
    if(!remoteMaintenanceActive()) return remoteTransferFail(e,RemoteTransferCode::Busy,"maintenance","Enter maintenance before activating settings");
#endif
    return true;
}
bool refreshRadioProof(bool descriptors) {
#if !defined(REMOTE_TRANSFER_TEST) && defined(HAVE_CRSF)
    if(opModeCRSF) {
        auto &config=elrsMode.deviceConfig();uint32_t generation=0;
        for(unsigned i=0;i<config.deviceCount();i++){auto *d=config.deviceAt(i);if(d->address==0xee)generation=d->generation;}
        const auto awaitRead=[&](uint32_t id){const uint32_t started=millis();while(config.busy() && (uint32_t)(millis()-started)<3000){transferService();delay(1);}return config.status().id==id && config.status().state==ELRSOperationState::Complete;};
        if(descriptors)for(unsigned i=0;i<8;i++){auto previous=rfObservations[i];if(!previous.field||previous.generation!=generation)continue;const uint32_t id=++rfProofOperationId;
            if(!config.read(0xee,previous.field,generation,id)||!awaitRead(id))return false;
            const auto *p=config.parameter();if(!p)return false;remoteObserveRfChoice(*p,id);
        }
        const uint32_t id=++rfProofOperationId;if(!config.readRadioStatus(id)||!awaitRead(id))return false;
    }
#endif
    return true;
}
}
bool remoteUploadAvailable(){return haveNewBoard && ((haveFS && !FlashROMode) || haveSD);}
fs::FS &remoteUploadStorage(){return haveFS && !FlashROMode?static_cast<fs::FS &>(LittleFS):static_cast<fs::FS &>(SD);}
const char *remoteUploadPath(){return "/crsf-transfer.upload";}
bool remoteFileActivationSafe(RemoteTransferError &e){return (refreshRadioProof(false) || remoteTransferFail(e,RemoteTransferCode::Busy,"radio","Fresh radio status unavailable")) && applySafe(e);}
bool captureRemoteSettings(RemoteSettingsSnapshot &s,RemoteTransferError &e) {
    if(remoteSettingsBusy())return remoteTransferFail(e,RemoteTransferCode::Busy,"crsf","Finish the CRSF save or recovery first");
    s={};s.crsf=snapshotELRSSettings();const auto p=snapshotELRSPortalPreferences();s.radio=p.radio;
    s.settings.opMode[0]='0'+p.opMode;s.settings.crsfap[0]=p.connectWiFi?'1':'0';s.settings.elrsPktRate[0]='0'+p.cadence;s.settings.elrsSpdUnit[0]='0'+p.speedUnits;
    s.identity=myRemID;s.newBoard=haveNewBoard;s.generation=settingsGeneration;remoteCaptureRfChoices(s);return true;
}
bool validateRemoteTransfer(File &file,RemoteTransferKind kind,const RemoteSettingsSnapshot &target,RemoteRestorePreview &preview,RemoteTransferError &e) {
    preview.valid=false;std::unique_ptr<RemoteSettingsSnapshot> proposed(new(std::nothrow) RemoteSettingsSnapshot);
    if(!proposed)return remoteTransferFail(e,RemoteTransferCode::TooLarge,"file","No CRSF preview memory");
    if(!fileHash(file,preview.fileHash))return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"file","File could not be read completely");
    if(!decodeFile(file,kind,target,*proposed,e) || !settingsHash(target,preview.settingsHash,e))return false;
    preview.kind=kind;preview.settingsGeneration=target.generation;preview.deviceGeneration=target.deviceGeneration;preview.capabilityFingerprint=target.capabilityFingerprint;preview.expiresAt=millis()+300000U;
    preview.currentSettings=target.settings;preview.proposedSettings=proposed->settings;
    preview.currentCrsf=target.crsf;preview.proposedCrsf=proposed->crsf;preview.currentRadio=target.radio;preview.proposedRadio=proposed->radio;
    const uint16_t rates[]={50,100,150,250,500};preview.currentCadence=rates[atoi(target.settings.elrsPktRate)];preview.proposedCadence=rates[atoi(proposed->settings.elrsPktRate)];preview.valid=true;return true;
}
bool applyRemoteTransfer(const RemoteRestorePreview &preview,RemoteTransferError &e) {
    if(remoteSettingsBusy() || remoteRadioReviewPending())return remoteTransferFail(e,RemoteTransferCode::Busy,"crsf","Finish CRSF recovery or radio review first");
    if(!preview.valid || (int32_t)(millis()-preview.expiresAt)>=0)return remoteTransferFail(e,RemoteTransferCode::StalePreview,"preview","Preview expired");
    if(!refreshRadioProof(true))return remoteTransferFail(e,RemoteTransferCode::Busy,"radio","Fresh radio proof unavailable");
    if(!applySafe(e))return false;
    RemoteSettingsSnapshot before,candidate;char hash[65];File file=remoteUploadStorage().open(remoteUploadPath(),"r");
    bool valid=captureRemoteSettings(before,e) && before.generation==preview.settingsGeneration && before.deviceGeneration==preview.deviceGeneration && before.capabilityFingerprint==preview.capabilityFingerprint && settingsHash(before,hash,e) && !strcmp(hash,preview.settingsHash) && fileHash(file,hash) && !strcmp(hash,preview.fileHash);
    if(!valid){file.close();return remoteTransferFail(e,RemoteTransferCode::StalePreview,"preview","File, CRSF settings or capabilities changed");}
    valid=decodeFile(file,preview.kind,before,candidate,e);file.close();if(!valid)return false;
    ELRSPortalPreferences p=snapshotELRSPortalPreferences();p.radio=candidate.radio;p.opMode=atoi(candidate.settings.opMode);p.connectWiFi=candidate.settings.crsfap[0]!='0';p.cadence=atoi(candidate.settings.elrsPktRate);p.speedUnits=atoi(candidate.settings.elrsSpdUnit);p.radioReview=1;
    if(!saveELRSSettings(candidate.crsf,p,true))return remoteTransferFail(e,RemoteTransferCode::WriteFailed,"crsf","CRSF save stopped; check storage and fresh radio status");return true;
}
bool remoteCompleteRadioReview(){
    if(!opModeCRSF || remoteSettingsBusy())return false;RemoteTransferError e;if(!remoteFileActivationSafe(e))return false;
    if(opModeCRSF){auto &c=elrsMode.deviceConfig();if(!c.modelIdKnown() || c.modelId()!=remoteRadioPreferences.modelId)return false;
        const char *labels[]={remoteRadioPreferences.maxPower.label,remoteRadioPreferences.telemetry,remoteRadioPreferences.dynamic};const char *units[]={remoteRadioPreferences.maxPower.unit,"",""};RemoteSettingsSnapshot s;remoteCaptureRfChoices(s);
        for(unsigned i=0;i<3;i++){const auto &r=rfObservations[i+5];if(r.generation!=s.deviceGeneration || (uint32_t)(millis()-r.at)>1000 || strcmp(r.choice.label,labels[i]) || strcmp(r.choice.unit,units[i]))return false;}}
    auto p=snapshotELRSPortalPreferences();p.radioReview=0;return saveELRSSettings(snapshotELRSSettings(),p);
}
#endif
