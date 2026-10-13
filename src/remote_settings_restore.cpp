#include "remote_settings_transfer.h"
#ifndef REMOTE_TRANSFER_TEST
#include <LittleFS.h>
#include "src/SD/SD.h"
#include "remote_main.h"
#include "src/CRSF/crsf_kludge.h"
#ifdef HAVE_CRSF
#include "src/CRSF/elrs_crsf.h"
extern ELRSCrsfMode elrsMode;
#endif
#include <mbedtls/sha256.h>
#else
#include "remote_transfer_test_io.h"
#endif
#include <cstring>
#include <memory>
static bool restoreBusy=false, radioReview=false;
static uint32_t maintenanceExpires=0;
bool remoteSettingsBusy() {return restoreBusy || remoteSettingsRecoveryBlocked;}
bool remoteRadioReviewPending() {return radioReview;}
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
    if(opModeCRSF) {elrsMode.setDeviceConfigAccess(remoteMaintenanceActive(),remoteSettingsBusy(),radioReview);serviceCRSF(false);}
#endif
#else
    testTransferService();
#endif
}
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
static bool settingsHash(const RemoteSettingsSnapshot &s,char hash[65],RemoteTransferError &e) {
    std::unique_ptr<uint8_t[]> bytes(new(std::nothrow) uint8_t[16384]);if(!bytes) return remoteTransferFail(e,RemoteTransferCode::TooLarge,"settings","No snapshot memory");
    char hashes[7*68+8]={};
    for(uint8_t i=0;i<7;i++) {size_t n=0;if(!remoteSerializeStore(s,i,bytes.get(),16384,n,e)) return false;remoteSha256(bytes.get(),n,hashes+i*68);hashes[i*68+65]=(char)s.stores[i].state;hashes[i*68+66]=(char)s.stores[i].medium;}
    hashes[7*68]=s.flashRO;hashes[7*68+1]=s.newBoard;remoteSha256((const uint8_t*)hashes,sizeof(hashes),hash);return true;
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
static bool decodeFile(File &file,RemoteTransferKind kind,const RemoteSettingsSnapshot &target,RemoteSettingsSnapshot &candidate,RemoteTransferError &e,bool clone) {
    if(!file || !file.size() || file.size()>(kind==RemoteTransferKind::DeviceBackup?REMOTE_BACKUP_MAX_BYTES:REMOTE_MODEL_MAX_BYTES)) return remoteTransferFail(e,RemoteTransferCode::TooLarge,"file","Native file exceeds its size bound");
    if(!file.seek(0) || !uniqueJsonKeys(file)) return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"file","Duplicate/escaped keys, excessive depth or malformed structure");
    DynamicJsonDocument doc(REMOTE_TRANSFER_JSON_CAPACITY);TransferReader reader(file);
    auto result=deserializeJson(doc,reader,DeserializationOption::NestingLimit(8));
    if(result) return remoteTransferFail(e,result==DeserializationError::NoMemory?RemoteTransferCode::TooLarge:RemoteTransferCode::InvalidValue,"file","Native JSON could not be parsed within its bound");
    return remoteDecodeTransfer(doc.as<JsonVariantConst>(),kind,target,candidate,e,clone);
}
bool validateRemoteTransfer(File &file,RemoteTransferKind kind,const RemoteSettingsSnapshot &target,RemoteRestorePreview &preview,RemoteTransferError &e) {
    preview.valid=false;
    std::unique_ptr<RemoteSettingsSnapshot> proposed(new(std::nothrow) RemoteSettingsSnapshot);
    if(!proposed) return remoteTransferFail(e,RemoteTransferCode::TooLarge,"file","No candidate memory");
    if(!fileHash(file,preview.fileHash) || !decodeFile(file,kind,target,*proposed,e,preview.cloneIdentity) || !settingsHash(target,preview.settingsHash,e)) return false;
    preview.kind=kind;preview.settingsGeneration=target.generation;preview.deviceGeneration=target.deviceGeneration;preview.capabilityFingerprint=target.capabilityFingerprint;
    preview.expiresAt=millis()+300000U;preview.proposedIdentity=proposed->identity;
    preview.currentCrsf=target.crsf;preview.proposedCrsf=proposed->crsf;preview.currentRadio=target.radio;preview.proposedRadio=proposed->radio;
#ifdef HAVE_CRSF
    const uint16_t rates[]={50,100,150,250,500};preview.currentCadence=rates[atoi(target.settings.elrsPktRate)];preview.proposedCadence=rates[atoi(proposed->settings.elrsPktRate)];
#endif
    preview.changedStores=0;
    std::unique_ptr<uint8_t[]> bytes(new(std::nothrow) uint8_t[16384]);if(!bytes)return remoteTransferFail(e,RemoteTransferCode::TooLarge,"preview","No diff memory");
    for(uint8_t i=0;i<7;i++) {size_t n;char before[65];if(!remoteSerializeStore(target,i,bytes.get(),16384,n,e))return false;remoteSha256(bytes.get(),n,before);if(!remoteSerializeStore(*proposed,i,bytes.get(),16384,n,e))return false;remoteSha256(bytes.get(),n,proposed->stores[i].sha256);proposed->stores[i].byteLength=n;
        if(kind==RemoteTransferKind::DeviceBackup)proposed->stores[i].medium=(i==5 || (i!=0&&i!=2&&i!=3&&proposed->settings.CfgOnSD[0]!='0'))?RemoteStoreMedium::SD:RemoteStoreMedium::Flash;
        if(strcmp(before,proposed->stores[i].sha256) || target.stores[i].state!=proposed->stores[i].state || target.stores[i].medium!=proposed->stores[i].medium)preview.changedStores|=1<<i;
    }
    snprintf(preview.hostname,sizeof(preview.hostname),"%s",proposed->settings.hostName);snprintf(preview.ip,sizeof(preview.ip),"%s",proposed->ip.ip);
    memcpy(preview.stores,proposed->stores,sizeof(preview.stores));preview.valid=true;return true;
}
static fs::FS &storeFS(RemoteStoreMedium medium) {return medium==RemoteStoreMedium::SD?static_cast<fs::FS&>(SD):static_cast<fs::FS&>(LittleFS);}
static bool mediumAvailable(RemoteStoreMedium medium) {return medium==RemoteStoreMedium::SD?haveSD:haveFS && !FlashROMode;}
static void stagePath(uint8_t id,char path[32]) {snprintf(path,32,"/.remote-new-%u",id);}
static void backupPath(uint8_t id,char path[32],bool ordinary=false) {
    if(ordinary)snprintf(path,32,"%s.write-old",remoteStorePath(id));
    else snprintf(path,32,"/.remote-old-%u",id);
}
static bool verifyPath(fs::FS &fs,const char *path,size_t length,const char *hash) {
    File file=fs.open(path,"r");char actual[65];bool ok=file && file.size()==length && fileHash(file,actual) && !strcmp(actual,hash);file.close();return ok;
}
static bool writeVerified(fs::FS &fs,const char *path,const uint8_t *bytes,size_t len) {
    File f=fs.open(path,"w");if(!f) return false;
    bool ok=true;for(size_t at=0;at<len && ok;at+=256) {size_t n=std::min((size_t)256,len-at);ok=f.write(bytes+at,n)==n;transferService();}
    f.flush();f.close();char hash[65];remoteSha256(bytes,len,hash);return ok && verifyPath(fs,path,len,hash);
}
bool remoteCheckedSettingsWrite(const char *path,const uint8_t *bytes,size_t len,bool useSD) {
    if(remoteSettingsBusy() || (useSD?!haveSD:!haveFS || FlashROMode)) return false;
    fs::FS &fs=storeFS(useSD?RemoteStoreMedium::SD:RemoteStoreMedium::Flash);
    char stage[64],old[64];if(strlen(path)>44) return false;
    snprintf(stage,sizeof(stage),"%s.write-new",path);snprintf(old,sizeof(old),"%s.write-old",path);
    if(fs.exists(old) && !fs.exists(path) && !fs.rename(old,path)) return false;
    if(!writeVerified(fs,stage,bytes,len)) return false;
    bool existed=fs.exists(path);
    if(fs.exists(old) && !fs.remove(old)) return false;
    if(existed && !fs.rename(path,old)) return false;
    if(!fs.rename(stage,path)) {if(existed && !fs.rename(old,path)) remoteSettingsRecoveryBlocked=true;return false;}
    char hash[65];remoteSha256(bytes,len,hash);
    if(!verifyPath(fs,path,len,hash)) {
        if(!fs.remove(path) || (existed && !fs.rename(old,path))) remoteSettingsRecoveryBlocked=true;
        return false;
    }
    if(existed) fs.remove(old);
    remoteSettingsChanged();return true;
}
static bool stageCandidate(const RemoteSettingsSnapshot &s,RemoteTransferError &e,uint8_t mask=127,RemoteSettingsEncoding encoding=RemoteSettingsEncoding::Transfer) {
    const bool durable=haveFS && !FlashROMode;
    if(!durable && encoding==RemoteSettingsEncoding::Transfer) return remoteTransferFail(e,RemoteTransferCode::RecoveryAnchorUnavailable,"storage","Multi-store import requires writable flash for recovery");
    std::unique_ptr<uint8_t[]> bytes(new(std::nothrow) uint8_t[16384]);if(!bytes) return remoteTransferFail(e,RemoteTransferCode::TooLarge,"stores","No stage memory");
    uint64_t needed[2]={durable?4096U:0U,0};
    for(uint8_t i=0;i<7;i++) if((mask&(1<<i)) && s.stores[i].state==RemoteStoreState::Present) {
        if(!mediumAvailable(s.stores[i].medium)) return remoteTransferFail(e,RemoteTransferCode::MediaUnavailable,remoteStoreName(i),"Destination medium unavailable");
        size_t len;if(!remoteSerializeStore(s,i,bytes.get(),16384,len,e,encoding)) return false;needed[(uint8_t)s.stores[i].medium]+=len;
    }
    if((needed[0] && needed[0]>LittleFS.totalBytes()-LittleFS.usedBytes()) || (needed[1] && needed[1]>SD.totalBytes()-SD.usedBytes())) return remoteTransferFail(e,RemoteTransferCode::WriteFailed,"storage","Insufficient staging and recovery space");
    for(uint8_t i=0;i<7;i++) if((mask&(1<<i)) && s.stores[i].state==RemoteStoreState::Present) {
        size_t len;char path[32];stagePath(i,path);
        if(!remoteSerializeStore(s,i,bytes.get(),16384,len,e,encoding) || !writeVerified(storeFS(s.stores[i].medium),path,bytes.get(),len)) return remoteTransferFail(e,RemoteTransferCode::WriteFailed,remoteStoreName(i),"Stage write/readback failed; originals retained");
    }
    return true;
}
namespace {
constexpr const char *journalPath="/remote-restore.journal",*journalNew="/remote-restore.journal.new",*journalOld="/remote-restore.journal.bak";
struct [[gnu::packed]] JournalEntry {
    uint8_t active,id,medium,oldPresent,newPresent;
    uint16_t oldLength,newLength;
    char oldHash[65],newHash[65];
};
struct [[gnu::packed]] RestoreJournal {
    char magic[4];uint8_t version,state,review;uint32_t generation;
    JournalEntry entries[14];char checksum[65];
};
bool validJournal(const RestoreJournal &j) {
    if(memcmp(j.magic,"RRS1",4) || j.version!=1 || j.state<1 || j.state>3 || j.review>1) return false;
    char hash[65];remoteSha256((const uint8_t*)&j,offsetof(RestoreJournal,checksum),hash);if(memcmp(hash,j.checksum,65)) return false;
    uint16_t seen=0;
    for(const auto &e:j.entries) if(e.active) {
        if(e.active!=1 || e.id>6 || e.medium>1 || e.oldPresent>1 || e.newPresent>1 || e.oldLength>16384 || e.newLength>16384 || e.oldHash[64] || e.newHash[64]) return false;
        const uint16_t bit=1<<(e.id*2+e.medium);if(seen&bit) return false;seen|=bit;
        if((e.oldPresent && !e.oldLength) || (e.newPresent && !e.newLength)) return false;
    }
    return true;
}
bool readJournalAt(const char *path,RestoreJournal &j) {
    File f=LittleFS.open(path,"r");bool ok=f && f.size()==sizeof(j) && f.read((uint8_t*)&j,sizeof(j))==sizeof(j);f.close();return ok && validJournal(j);
}
bool readJournal(RestoreJournal &j,bool &present) {
    present=LittleFS.exists(journalPath)||LittleFS.exists(journalOld);
    if(readJournalAt(journalPath,j) || readJournalAt(journalOld,j)) return true;
    if(present) return false;
    // A stage cannot activate files until PREPARED is authoritative.
    if(readJournalAt(journalNew,j) && j.state==1) {present=true;return true;}
    return true;
}
bool persistJournal(RestoreJournal &j) {
    remoteSha256((const uint8_t*)&j,offsetof(RestoreJournal,checksum),j.checksum);
    if(!writeVerified(LittleFS,journalNew,(const uint8_t*)&j,sizeof(j))) return false;
    if(LittleFS.exists(journalOld) && !LittleFS.remove(journalOld)) return false;
    const bool existed=LittleFS.exists(journalPath);
    if(existed && !LittleFS.rename(journalPath,journalOld)) return false;
    if(!LittleFS.rename(journalNew,journalPath)) {if(existed) LittleFS.rename(journalOld,journalPath);return false;}
    RestoreJournal check;return readJournalAt(journalPath,check) && !memcmp(&check,&j,sizeof(j));
}
bool allFiles(const RestoreJournal &j,bool proposed) {
    for(const auto &e:j.entries) if(e.active) {
        auto medium=(RemoteStoreMedium)e.medium;if(!mediumAvailable(medium)) return false;
        fs::FS &fs=storeFS(medium);bool present=proposed?e.newPresent:e.oldPresent;
        if(present) {if(!verifyPath(fs,remoteStorePath(e.id),proposed?e.newLength:e.oldLength,proposed?e.newHash:e.oldHash)) return false;}
        else if(fs.exists(remoteStorePath(e.id))) return false;
    }
    return true;
}
bool cleanJournal() {
    for(const char *p:{journalNew,journalOld,journalPath}) if(LittleFS.exists(p) && !LittleFS.remove(p)) return false;
    return true;
}
bool cleanStages(const RestoreJournal &j,bool ordinary=false) {
    for(const auto &e:j.entries) if(e.active) {auto medium=(RemoteStoreMedium)e.medium;if(!mediumAvailable(medium)) return false;fs::FS &fs=storeFS(medium);char path[32];
        stagePath(e.id,path);if(fs.exists(path) && !fs.remove(path)) return false;
        backupPath(e.id,path,ordinary);if(fs.exists(path) && !fs.remove(path)) return false;
    }
    return true;
}
bool rollback(const RestoreJournal &j,bool durable=true) {
    for(const auto &e:j.entries) if(e.active) {
        auto medium=(RemoteStoreMedium)e.medium;if(!mediumAvailable(medium)) return false;
        fs::FS &fs=storeFS(medium);const char *path=remoteStorePath(e.id);char old[32];backupPath(e.id,old,!durable);
        if(e.oldPresent) {
            if(verifyPath(fs,old,e.oldLength,e.oldHash)) {
                if(fs.exists(path) && !fs.remove(path)) return false;
                if(!fs.rename(old,path)) return false;
            } else if(!verifyPath(fs,path,e.oldLength,e.oldHash)) return false;
        } else if(fs.exists(path) && !fs.remove(path)) return false;
    }
    return allFiles(j,false) && cleanStages(j,!durable) && (!durable || cleanJournal());
}
bool prepareEntry(JournalEntry &entry,uint8_t id,RemoteStoreMedium medium,const RemoteSettingsSnapshot &proposed,bool newPresent) {
    entry.active=1;entry.id=id;entry.medium=(uint8_t)medium;entry.newPresent=newPresent;
    fs::FS &fs=storeFS(medium);entry.oldPresent=fs.exists(remoteStorePath(id));
    if(entry.oldPresent) {File f=fs.open(remoteStorePath(id),"r");entry.oldLength=f.size();bool ok=entry.oldLength && fileHash(f,entry.oldHash);f.close();if(!ok) return false;}
    else remoteSha256(nullptr,0,entry.oldHash);
    if(newPresent) {char path[32];stagePath(id,path);File f=fs.open(path,"r");entry.newLength=f.size();bool ok=entry.newLength && fileHash(f,entry.newHash);f.close();if(!ok) return false;}
    else remoteSha256(nullptr,0,entry.newHash);
    return true;
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
static bool commitSettings(const RemoteSettingsSnapshot &before,const RemoteSettingsSnapshot &proposed,uint8_t mask,bool review,RemoteTransferError &e) {
    if(remoteSettingsBusy() || radioReview || (haveFS && (LittleFS.exists(journalPath) || LittleFS.exists(journalOld))))
        return remoteTransferFail(e,RemoteTransferCode::Busy,"storage","Finish the owned save, recovery or radio review first");
    if(!mask || (mask&128))return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"stores","Invalid local store selection");
    const bool durable=haveFS && !FlashROMode;
    // shortcut: FlashRO local saves recover per file; a writable flash anchor is required for batch power-loss atomicity.
    if(review && !durable)return remoteTransferFail(e,RemoteTransferCode::RecoveryAnchorUnavailable,"storage","Writable flash recovery anchor required");
    std::unique_ptr<RestoreJournal> journal(new(std::nothrow) RestoreJournal());
    if(!journal)return remoteTransferFail(e,RemoteTransferCode::TooLarge,"storage","No transaction memory");
    restoreBusy=true;
    auto fail=[&](const char *path){restoreBusy=false;return remoteTransferFail(e,RemoteTransferCode::WriteFailed,path,"Settings commit stopped; originals retained");};
    if(!stageCandidate(proposed,e,mask,review?RemoteSettingsEncoding::Transfer:RemoteSettingsEncoding::StoredFile)) {restoreBusy=false;return false;}
    memcpy(journal->magic,"RRS1",4);journal->version=1;journal->state=1;journal->review=review;journal->generation=before.generation+1;
    for(uint8_t i=0;i<7;i++) if((mask&(1<<i)) && proposed.stores[i].state!=RemoteStoreState::Disabled) {
        const auto medium=proposed.stores[i].medium;
        if(!mediumAvailable(medium) && proposed.stores[i].state==RemoteStoreState::Absent && before.stores[i].state==RemoteStoreState::Absent) continue;
        if(!mediumAvailable(medium) || !prepareEntry(journal->entries[i*2],i,medium,proposed,proposed.stores[i].state==RemoteStoreState::Present)) return fail(remoteStoreName(i));
        if(before.stores[i].medium!=medium && before.stores[i].state==RemoteStoreState::Present) {
            if(!mediumAvailable(before.stores[i].medium) || !prepareEntry(journal->entries[i*2+1],i,before.stores[i].medium,proposed,false)) return fail(remoteStoreName(i));
        }
    }
    if(durable && !persistJournal(*journal)) return fail("journal");
    bool ok=true;
    for(const auto &entry:journal->entries) if(ok && entry.active) {
        fs::FS &fs=storeFS((RemoteStoreMedium)entry.medium);const char *path=remoteStorePath(entry.id);char old[32],stage[32];backupPath(entry.id,old,!durable);stagePath(entry.id,stage);
        if(fs.exists(old)) {ok=false;break;}
        if(entry.oldPresent) ok=fs.rename(path,old);
        if(ok && entry.newPresent) ok=fs.rename(stage,path);
        transferService();
    }
    ok=ok && allFiles(*journal,true) && (!review || (refreshRadioProof(false) && applySafe(e)));
    if(ok && durable) {
        journal->state=2;ok=persistJournal(*journal);
        if(!ok) {RestoreJournal check;bool present=false;ok=readJournal(check,present) && present && check.state==2 && check.generation==journal->generation;}
    }
    if(!ok) {
        journal->state=1;
        if(!rollback(*journal,durable)) {remoteSettingsRecoveryBlocked=true;restoreBusy=false;return remoteTransferFail(e,RemoteTransferCode::RollbackRequired,"journal","Recovery must restore the complete previous generation");}
        return fail("stores");
    }
    radioReview=review;remoteSettingsChanged();return true;
}
bool remoteCommitELRSSettings(const RemoteSettingsSnapshot &before,const RemoteSettingsSnapshot &candidate,RemoteTransferError &e) {
#ifndef HAVE_CRSF
    return remoteTransferFail(e,RemoteTransferCode::UnsupportedBehavior,"crsf","CRSF is not compiled");
#else
    if(!candidate.newBoard || candidate.stores[6].state!=RemoteStoreState::Present || !elrsIsValidCrsfSettingsBlob(candidate.crsf))
        return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"stores.crsf","Invalid local model settings");
    return commitSettings(before,candidate,65,false,e);
#endif
}
bool applyRemoteTransfer(const RemoteRestorePreview &p,RemoteTransferError &e) {
    if(!haveFS || FlashROMode) return remoteTransferFail(e,RemoteTransferCode::RecoveryAnchorUnavailable,"storage","Writable flash recovery anchor required");
    if(remoteSettingsBusy() || radioReview || LittleFS.exists(journalPath) || LittleFS.exists(journalOld)) return remoteTransferFail(e,RemoteTransferCode::Busy,"storage","Finish recovery or radio review first");
    if(!p.valid || (int32_t)(millis()-p.expiresAt)>=0) return remoteTransferFail(e,RemoteTransferCode::StalePreview,"preview","Preview expired");
    if(!applySafe(e)) return false;
    std::unique_ptr<RemoteSettingsSnapshot> current(new(std::nothrow) RemoteSettingsSnapshot), proposed(new(std::nothrow) RemoteSettingsSnapshot);
    if(!current || !proposed) return remoteTransferFail(e,RemoteTransferCode::TooLarge,"preview","No transaction memory");
    if(p.deviceGeneration) {
        restoreBusy=true;bool refreshed=refreshRadioProof(true);restoreBusy=false;
        if(!refreshed)return remoteTransferFail(e,RemoteTransferCode::StalePreview,"radio","Live radio proof could not be refreshed");
    }
    char hash[65];File file=LittleFS.open("/remote-transfer.upload","r");
    bool valid=captureRemoteSettings(*current,e) && current->generation==p.settingsGeneration && current->deviceGeneration==p.deviceGeneration && current->capabilityFingerprint==p.capabilityFingerprint && settingsHash(*current,hash,e) && !strcmp(hash,p.settingsHash) && fileHash(file,hash) && !strcmp(hash,p.fileHash);
    if(!valid) {file.close();return remoteTransferFail(e,RemoteTransferCode::StalePreview,"preview","File, settings or capabilities changed");}
    valid=decodeFile(file,p.kind,*current,*proposed,e,p.cloneIdentity);file.close();if(!valid) return false;
    const uint8_t mask=p.kind==RemoteTransferKind::ModelProfile?65:127;
    for(uint8_t i=0;i<7;i++) if(mask&(1<<i)) {
        if(p.kind==RemoteTransferKind::DeviceBackup) proposed->stores[i].medium=(i==5 || (i!=0 && i!=2 && i!=3 && proposed->settings.CfgOnSD[0]!='0'))?RemoteStoreMedium::SD:RemoteStoreMedium::Flash;
    }
    return commitSettings(*current,*proposed,mask,true,e);
}

bool recoverRemoteSettingsRestore(RemoteTransferError &e) {
    RestoreJournal j={};bool present=false;
    if(haveFS && !readJournal(j,present)) {remoteSettingsRecoveryBlocked=true;return remoteTransferFail(e,RemoteTransferCode::RollbackRequired,"journal","No valid recovery journal; evidence retained");}
    if(present) {
        if(FlashROMode) {remoteSettingsRecoveryBlocked=true;return remoteTransferFail(e,RemoteTransferCode::RecoveryAnchorUnavailable,"journal","Restore requires its original writable-flash policy");}
        if(j.state==1) {
            if(!rollback(j)) {remoteSettingsRecoveryBlocked=true;return remoteTransferFail(e,RemoteTransferCode::RollbackRequired,"journal","Previous generation or required medium unavailable");}
        } else {
            bool available=true;for(const auto &entry:j.entries) if(entry.active) available&=mediumAvailable((RemoteStoreMedium)entry.medium);
            if(!available || (j.state==2 && !allFiles(j,true))) {remoteSettingsRecoveryBlocked=true;return remoteTransferFail(e,RemoteTransferCode::RollbackRequired,"journal","Committed generation or required medium unavailable");}
            radioReview=j.review;
        }
    }
    // Finish interrupted ordinary single-store saves before any reader/default writer.
    for(uint8_t i=0;i<7;i++) for(uint8_t m=0;m<2;m++) if(mediumAvailable((RemoteStoreMedium)m)) {
        if(!haveFS && i!=0 && i!=6)continue;
        auto &fs=storeFS((RemoteStoreMedium)m);char old[64];snprintf(old,sizeof(old),"%s.write-old",remoteStorePath(i));
        if(fs.exists(old) && !fs.exists(remoteStorePath(i)) && !fs.rename(old,remoteStorePath(i))) {remoteSettingsRecoveryBlocked=true;return remoteTransferFail(e,RemoteTransferCode::RollbackRequired,remoteStoreName(i),"Single-store recovery failed");}
    }
    restoreBusy=false;return true;
}
bool remoteSettingsRestoreLoaded(RemoteTransferError &e) {
    RestoreJournal j={};bool present=false;if(haveFS && !readJournal(j,present)) return remoteTransferFail(e,RemoteTransferCode::RollbackRequired,"journal","Journal unreadable after loading");
    if(!present) {
        for(uint8_t i:{0,6})for(uint8_t m=0;m<2;m++)if(mediumAvailable((RemoteStoreMedium)m)) {
            auto &fs=storeFS((RemoteStoreMedium)m);char old[32];backupPath(i,old,true);
            if(fs.exists(old) && fs.exists(remoteStorePath(i)) && !fs.remove(old))return remoteTransferFail(e,RemoteTransferCode::RollbackRequired,remoteStoreName(i),"Single-store cleanup failed");
        }
        restoreBusy=false;return true;
    }
    if(j.state==3) return (cleanStages(j) && (j.review || cleanJournal())) || remoteTransferFail(e,RemoteTransferCode::RollbackRequired,"journal","Loaded generation cleanup failed");
    if(j.state!=2 || !allFiles(j,true)) return remoteTransferFail(e,RemoteTransferCode::RollbackRequired,"journal","Loaded generation failed verification");
    j.state=3;if(!persistJournal(j) || !cleanStages(j) || (!j.review && !cleanJournal())) return remoteTransferFail(e,RemoteTransferCode::RollbackRequired,"journal","Cannot finish loaded generation; backups retained");
    restoreBusy=false;return true;
}
bool remoteCompleteRadioReview() {
    if(remoteSettingsBusy()) return false;
#ifndef REMOTE_TRANSFER_TEST
    RemoteTransferError e;if(!applySafe(e))return false;
#ifdef HAVE_CRSF
    if(opModeCRSF){auto &c=elrsMode.deviceConfig();if(!c.modelIdKnown()||c.modelId()!=remoteRadioPreferences.modelId)return false;
        const char *labels[]={remoteRadioPreferences.maxPower.label,remoteRadioPreferences.telemetry,remoteRadioPreferences.dynamic};const char *units[]={remoteRadioPreferences.maxPower.unit,"",""};
        RemoteSettingsSnapshot s;remoteCaptureRfChoices(s);
        for(unsigned i=0;i<3;i++){const auto &r=rfObservations[i+5];if(r.generation!=s.deviceGeneration || (uint32_t)(millis()-r.at)>1000 || strcmp(r.choice.label,labels[i]) || strcmp(r.choice.unit,units[i]))return false;}
    }
#endif
#endif
    RestoreJournal j={};bool present=false;if(!readJournal(j,present) || !present || j.state!=3) return false;
    if(!cleanStages(j) || !cleanJournal()) return false;
    radioReview=false;return true;
}
