#pragma once
#include "crsf_model_files.h"
#include "crsf_config_ui.h"
#include <memory>
#include <cstring>
#include <cstdio>

static char remotePortalNonce[33]={};
static uint32_t remotePortalExpires=0,remotePortalOperationId=1;
static uint32_t remotePortalPreferenceId=0,remotePortalPreferenceHandled=0,remotePortalPreferenceFailed=0;
static bool remotePortalPreferenceModel=false;
static IPAddress remotePortalOwner;
static RemoteRestorePreview remotePortalPreview;
static File remotePortalUploadFile;
static size_t remotePortalUploadBytes=0;
static bool remotePortalUploading=false,remotePortalUploadOk=false;
static uint8_t remotePortalUploadFiles=0;
static RemoteTransferKind remotePortalUploadKind;
static RemoteTransferError remotePortalUploadError;

static void remotePortalService() {
#ifdef HAVE_CRSF
    if(opModeCRSF) serviceCRSF(false);
#endif
}
class RemotePortalWriter {
public:
    size_t write(uint8_t b) {data[used++]=(char)b;if(used==sizeof(data))flush();return 1;}
    size_t write(const uint8_t *b,size_t n) {for(size_t i=0;i<n;i++)write(b[i]);return n;}
    void flush() {if(used){wm.server->sendContent(data,used);used=0;remotePortalService();}}
private:char data[256];size_t used=0;
};
static void remotePortalJson(JsonDocument &doc,int status=200) {
    wm.server->sendHeader("Cache-Control","no-store");wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);wm.server->send(status,"application/json","");
    RemotePortalWriter writer;serializeJson(doc,writer);writer.flush();wm.server->sendContent("",0);
}
static void remotePortalError(int status,const char *code,const char *path,const char *message) {
    StaticJsonDocument<512> doc;doc["code"]=code;doc["path"]=path;doc["message"]=message;remotePortalJson(doc,status);
}
static void remotePortalError(const RemoteTransferError &e,int status=400) {remotePortalError(status,remoteTransferCodeName(e.code),e.path,e.message);}
static bool remotePortalProtectedAP() {
    if(strlen(settings.appw)<8) return false;
    const IPAddress local=wm.server->client().localIP(),peer=wm.server->client().remoteIP(),ap=WiFi.softAPIP();
    return local==ap && peer[0]==ap[0] && peer[1]==ap[1] && peer[2]==ap[2];
}
static bool remotePortalOrigin() {
    const String origin=wm.server->header("Origin");String host=wm.server->hostHeader();
    if(host.endsWith(":80"))host.remove(host.length()-3);
    if(host!=WiFi.softAPIP().toString() && host!=settings.hostName && host!=String(settings.hostName)+".local")return false;
    const String expected=String("http://")+host;
    if(!origin.isEmpty()) return origin==expected || origin==expected+":80";
    const String referer=wm.server->header("Referer");return wm.server->method()==HTTP_GET && (referer.startsWith(expected+"/") || referer.startsWith(expected+":80/"));
}
static bool remotePortalToken(bool expiry=true) {
    const String nonce=wm.server->header("X-Remote-Nonce");
    return remotePortalNonce[0] && nonce.length()==32 && !strcmp(nonce.c_str(),remotePortalNonce) && wm.server->client().remoteIP()==remotePortalOwner && (!expiry || (int32_t)(millis()-remotePortalExpires)<0);
}
static bool remotePortalGate() {
    if(!remotePortalProtectedAP() || !remotePortalOrigin() || !remotePortalToken()) {remotePortalError(403,"ACCESS_DENIED","session","Use a current session on the password-protected Remote AP");return false;}
    return true;
}
static bool remotePortalRadioBusy() {
#ifdef HAVE_CRSF
    return opModeCRSF && elrsMode.deviceConfig().busy();
#else
    return false;
#endif
}
static bool remotePortalUnsigned(const char *name,uint32_t max,uint32_t &value) {
    String text=wm.server->arg(name);if(!text.length() || text.length()>10) return false;uint64_t n=0;
    for(unsigned i=0;i<text.length();i++){char c=text[i];if(c<'0'||c>'9')return false;n=n*10+c-'0';if(n>max)return false;}value=n;return true;
}
static bool remotePortalSigned(const char *name,int32_t &value) {
    String text=wm.server->arg(name);if(!text.length() || text.length()>11) return false;bool minus=text[0]=='-';uint64_t n=0;
    if(minus && text.length()==1) return false;for(unsigned i=minus;i<text.length();i++){char c=text[i];if(c<'0'||c>'9')return false;n=n*10+c-'0';if(n>(minus?2147483648ULL:2147483647ULL))return false;}
    value=minus?-(int64_t)n:n;return true;
}
static void remotePortalSession() {
    if(!remotePortalProtectedAP() || !remotePortalOrigin()) {remotePortalError(403,"ACCESS_DENIED","session","Secure the Remote AP and connect through it");return;}
    if(remoteSettingsBusy() || remotePortalUploading || (remotePortalRadioBusy() && !remotePortalToken(false))) {remotePortalError(409,"BUSY","session","An operation owns the session");return;}
    for(unsigned i=0;i<4;i++) snprintf(remotePortalNonce+i*8,9,"%08x",esp_random());
    remotePortalOwner=wm.server->client().remoteIP();remotePortalExpires=millis()+300000U;remotePortalPreview.valid=false;
    remotePortalUploadFiles=0;
    StaticJsonDocument<128> doc;doc["nonce"]=remotePortalNonce;remotePortalJson(doc);
}
static void remotePortalDevices() {
    StaticJsonDocument<1536> doc;auto devices=doc.createNestedArray("devices");doc["protectedAP"]=remotePortalProtectedAP();doc["maintenance"]=remoteMaintenanceActive();doc["radioReviewPending"]=remoteRadioReviewPending();doc["importAvailable"]=remoteUploadAvailable()&&!crsfSettingsRecoveryBlocked;
#ifdef HAVE_CRSF
    doc["crsfActive"]=opModeCRSF;
    if(opModeCRSF) {
        auto &config=elrsMode.deviceConfig();for(unsigned i=0;i<config.deviceCount();i++){const auto *d=config.deviceAt(i);auto x=devices.createNestedObject();x["address"]=d->address;x["name"]=d->name;x["fieldCount"]=d->fieldCount;x["generation"]=d->generation;x["serial"]=d->serial;x["hardwareVersion"]=d->hardwareVersion;x["softwareVersion"]=d->softwareVersion;x["parameterVersion"]=d->parameterVersion;}
        auto s=config.radioStatus(millis());doc["rfKnown"]=s.rfConnectionKnown;doc["connected"]=s.rfConnected;doc["modelMatched"]=s.modelMatched;doc["armedKnown"]=s.armedKnown;doc["armed"]=s.armed;doc["statusAgeMs"]=s.statusAgeMs;doc["ch5"]=elrsMode.finalChannel5();if(config.modelIdKnown())doc["modelId"]=config.modelId();
    }
#endif
    if(!doc.containsKey("crsfActive"))doc["crsfActive"]=false;
    remotePortalJson(doc);
}
static void remotePortalApplyPreferences() {
#ifdef HAVE_CRSF
    if(!opModeCRSF || !remotePortalPreferenceId || remotePortalPreferenceHandled==remotePortalPreferenceId)return;
    auto &config=elrsMode.deviceConfig();const auto s=config.status();const auto *p=config.parameter();
    if(s.id!=remotePortalPreferenceId || s.state!=ELRSOperationState::Complete || !p || s.device!=p->device || s.field!=p->field)return;
    remotePortalPreferenceHandled=s.id;auto next=remoteRadioPreferences;bool changed=false;
    if(remotePortalPreferenceModel && config.modelIdKnown()){next.modelId=config.modelId();changed=true;}
    else if(p->device==0xee && p->type==9){const char *role=remoteParameterRole(*p),*label;size_t length;
        char *dest=!strcmp(role,"power")?next.maxPower.label:!strcmp(role,"telemetry")?next.telemetry:!strcmp(role,"dynamic")?next.dynamic:nullptr;
        const size_t cap=!strcmp(role,"power")?sizeof(next.maxPower.label):!strcmp(role,"telemetry")?sizeof(next.telemetry):sizeof(next.dynamic);
        if(dest){if(!p->optionAt(p->value.raw,label,length) || length>=cap || strlen(p->unit())>=sizeof(next.maxPower.unit)){remotePortalPreferenceFailed=s.id;return;}
            memcpy(dest,label,length);dest[length]=0;if(!strcmp(role,"power"))strcpy(next.maxPower.unit,p->unit());changed=true;}
    }
    if(!changed)return;
    // Update runtime before file I/O so servicing CRSF cannot reapply the old preference.
    if(!elrsMode.setRadioPreferences(next,remoteRadioReviewPending(),false)){remotePortalPreferenceFailed=s.id;return;}
    auto preferences=snapshotELRSPortalPreferences();preferences.radio=next;
    if(!saveELRSSettings(snapshotELRSSettings(),preferences))remotePortalPreferenceFailed=s.id;
#endif
}
static void remotePortalOperation() {
    if(!remotePortalGate())return;
    remotePortalApplyPreferences();
    StaticJsonDocument<2048> doc;
#ifdef HAVE_CRSF
    if(opModeCRSF) {
        auto &config=elrsMode.deviceConfig();const auto &s=config.status();
        const char *states[]={"idle","queued","waiting","confirmation","running","complete","cancelled","failed","unknown"};
        const char *errors[]={"none","busy","stale","malformed","tooLarge","unsupported","timeout","unsafe","rejected","notConnected"};
        doc["id"]=s.id;doc["generation"]=s.generation;doc["state"]=states[(uint8_t)s.state];doc["error"]=errors[(uint8_t)s.error];doc["device"]=s.device;doc["field"]=s.field;doc["message"]=s.message;
        const auto *p=config.parameter();if(p && s.state==ELRSOperationState::Complete && s.device==p->device && s.field==p->field){remoteObserveRfChoice(*p,s.id);auto x=doc.createNestedObject("parameter");x["device"]=p->device;x["field"]=p->field;x["parent"]=p->parent;x["generation"]=p->generation;x["type"]=p->type;x["name"]=p->name();x["value"]=p->value.raw;x["minimum"]=p->minimum.raw;x["maximum"]=p->maximum.raw;x["options"]=p->options();x["unit"]=p->unit();x["info"]=p->info();x["precision"]=p->precision;x["hash"]=p->value.descriptorHash;x["hidden"]=p->hidden();x["writable"]=p->writable&&config.mutationSupported();x["commandAllowed"]=p->type==13&&config.mutationSupported();x["role"]=remoteParameterRole(*p);}
        if(remotePortalPreferenceFailed && s.id==remotePortalPreferenceFailed){doc["state"]="unknown";doc["error"]="writeFailed";doc["message"]="Radio changed; saving Remote preferences failed";}
    }else
#endif
    doc["state"]="idle";
    remotePortalJson(doc);
}
static void remotePortalAction() {
    if(!remotePortalGate())return;
    remotePortalApplyPreferences();
    if(remoteSettingsBusy() || remotePortalUploading){remotePortalError(409,"BUSY","action","Storage operation is active");return;}
    const String action=wm.server->arg("action");uint32_t device=0,field=0,generation=0,id=0,code=0,type=0,hash=0;int32_t expected=0,value=0;
    if(action=="maintenance") {if(!remotePortalUnsigned("enabled",1,code)){remotePortalError(400,"INVALID_VALUE","enabled","Expected 0 or 1");return;}remoteSetMaintenance(code);StaticJsonDocument<64> doc;doc["ok"]=true;remotePortalJson(doc);return;}
#ifdef HAVE_CRSF
    if(!opModeCRSF){remotePortalError(409,"UNSUPPORTED_BEHAVIOR","radio","CRSF mode must be active");return;}
    auto &config=elrsMode.deviceConfig();bool ok=false;
    if(action!="discover" && action!="status" && !remotePortalUnsigned("generation",UINT32_MAX,generation))goto invalid;
    if(action=="read" || action=="write" || action=="command" || action=="assign")if(!remotePortalUnsigned("device",255,device))goto invalid;
    if(action=="read" || action=="write" || action=="command")if(!remotePortalUnsigned("field",255,field) || !field)goto invalid;
    id=++remotePortalOperationId;if(!id||id==UINT32_MAX)id=remotePortalOperationId=1;
    if(action=="discover")ok=config.discover(id);
    else if(action=="status")ok=config.readRadioStatus(id);
    else if(action=="read")ok=config.read(device,field,generation,id);
    else if(action=="write"){
        if(!remotePortalUnsigned("confirmed",1,code)||code!=1 || !remotePortalUnsigned("type",9,type) || !remotePortalUnsigned("hash",UINT32_MAX,hash) || !remotePortalSigned("expected",expected) || !remotePortalSigned("value",value))goto invalid;
        ELRSParameterValue before(type,expected),after(type,value);before.descriptorHash=hash;ok=config.write(device,field,generation,before,after,id);
    } else if(action=="command") {
        if(!remotePortalUnsigned("status",5,code)||(code!=1&&code!=4&&code!=5))goto invalid;
        if(code==1){uint32_t confirmed;if(!remotePortalUnsigned("confirmed",1,confirmed)||confirmed!=1)goto invalid;}
        else {if(!remotePortalUnsigned("operationId",UINT32_MAX,id)||id!=config.status().id){remotePortalError(409,"STALE_PREVIEW","operationId","Command ownership changed");return;}}
        ok=config.command(device,field,generation,code,id);
    } else if(action=="model" || action=="assign") {if(!remotePortalUnsigned("modelId",63,code))goto invalid;ok=action=="model"?config.selectModel(code,generation,id):config.assignReceiver(device,code,generation,id);}
    else goto invalid;
    if(!ok){remotePortalError(409,"OPERATION_REJECTED","radio","Busy, stale, unsupported or unsafe operation");return;}
    if(action=="write" || action=="model" || action=="assign" || action=="command")elrsMode.pauseModulePreferenceApply();
    if(action=="write" || action=="model" || action=="assign"){remotePortalPreferenceId=id;remotePortalPreferenceModel=action!="write";}
    {StaticJsonDocument<64> doc;doc["id"]=id;doc["state"]="queued";remotePortalJson(doc,202);return;}
invalid:
#endif
    remotePortalError(400,"INVALID_VALUE","action","Unknown action or invalid bounded argument");
}
static void remotePortalUpload() {
    auto &upload=wm.server->upload();
    if(upload.status==UPLOAD_FILE_START){remotePortalUploadOk=false;remotePortalUploadBytes=0;remotePortalUploadError={};
        if(!remotePortalGate())return;
        if(++remotePortalUploadFiles!=1){remoteTransferFail(remotePortalUploadError,RemoteTransferCode::InvalidValue,"file","Upload exactly one native file");return;}
        if(remoteSettingsBusy()||remotePortalRadioBusy()||remotePortalUploading || !remoteUploadAvailable()){remoteTransferFail(remotePortalUploadError,RemoteTransferCode::Busy,"upload","Storage/radio busy or writable storage unavailable");return;}
        const String kind=wm.server->arg("kind");if(kind!="model"&&kind!="backup"){remoteTransferFail(remotePortalUploadError,RemoteTransferCode::InvalidValue,"kind","Choose a native file kind");return;}
        remotePortalUploadKind=kind=="backup"?RemoteTransferKind::DeviceBackup:RemoteTransferKind::ModelProfile;
        remotePortalPreview.valid=false;remotePortalUploading=true;remotePortalUploadFile=remoteUploadStorage().open(remoteUploadPath(),"w");remotePortalUploadOk=(bool)remotePortalUploadFile;
    }else if(upload.status==UPLOAD_FILE_WRITE && remotePortalUploading){
        const size_t cap=remotePortalUploadKind==RemoteTransferKind::DeviceBackup?REMOTE_BACKUP_MAX_BYTES:REMOTE_MODEL_MAX_BYTES;
        if(!remotePortalToken() || upload.currentSize>cap-std::min(cap,remotePortalUploadBytes)){remotePortalUploadOk=false;remoteTransferFail(remotePortalUploadError,RemoteTransferCode::TooLarge,"file","Upload exceeds its bound or session expired");}
        if(remotePortalUploadOk)for(size_t at=0;at<upload.currentSize;at+=256){size_t n=std::min((size_t)256,upload.currentSize-at);if(remotePortalUploadFile.write(upload.buf+at,n)!=n)remotePortalUploadOk=false;remotePortalService();}
        remotePortalUploadBytes+=upload.currentSize;
    }else if(upload.status==UPLOAD_FILE_END || upload.status==UPLOAD_FILE_ABORTED){
        if(remotePortalUploadFile){remotePortalUploadFile.flush();remotePortalUploadOk=remotePortalUploadOk && remotePortalUploadFile.size()==remotePortalUploadBytes;remotePortalUploadFile.close();}
        if(upload.status==UPLOAD_FILE_ABORTED)remotePortalUploadOk=false;remotePortalUploading=false;
    }
}
static void remotePortalUploadDone() {
    remotePortalUploadFiles=0;
    if(!remotePortalGate())return;
    if(!remotePortalUploadOk){if(remotePortalUploadError.code==RemoteTransferCode::None)remoteTransferFail(remotePortalUploadError,RemoteTransferCode::WriteFailed,"file","Upload incomplete or failed");remotePortalError(remotePortalUploadError);return;}
    std::unique_ptr<RemoteSettingsSnapshot> current(new(std::nothrow) RemoteSettingsSnapshot);RemoteTransferError e;
    File file=remoteUploadStorage().open(remoteUploadPath(),"r");
    bool ok=current && captureRemoteSettings(*current,e) && validateRemoteTransfer(file,remotePortalUploadKind,*current,remotePortalPreview,e);file.close();remotePortalUploadOk=false;
    if(!ok){remotePortalError(e);return;}
    DynamicJsonDocument doc(REMOTE_TRANSFER_JSON_CAPACITY);doc["fileHash"]=remotePortalPreview.fileHash;doc["expiresInSeconds"]=300;
    doc["calibration"]=remotePortalUploadKind==RemoteTransferKind::DeviceBackup?"Restore the file's CRSF calibration":"Keep this Remote's calibration";
    doc["restart"]=true;doc["radio"]="Imported radio preferences require separate readback and review";
    if(current->newBoard) {
        const ELRSCrsfSettingsBlob *models[]={&remotePortalPreview.currentCrsf,&remotePortalPreview.proposedCrsf};
        const ELRSRadioPreferences *radios[]={&remotePortalPreview.currentRadio,&remotePortalPreview.proposedRadio};
        for(unsigned side=0;side<2;side++){auto x=doc.createNestedObject(side?"proposed":"current");const auto &m=*models[side];auto inputs=x.createNestedArray("inputs");for(unsigned i=0;i<4;i++){auto a=inputs.createNestedArray();a.add((bool)m.axisProfile[i].reverse);a.add(m.axisProfile[i].deadband);a.add(m.axisProfile[i].expo);a.add(m.outputLimits[i].minimumUs);a.add(m.outputLimits[i].maximumUs);}
            auto logic=x.createNestedArray("logical");for(const auto &r:m.logic.rules){auto a=logic.createNestedArray();a.add(r.function);a.add(r.sourceA);a.add(r.sourceB);a.add(r.threshold);a.add(r.andSwitch);}
            auto mixes=x.createNestedArray("mixes");for(const auto &r:m.mixes.lines){auto a=mixes.createNestedArray();a.add(r.channel);a.add(r.source);a.add(r.switchSource);a.add(r.weight);a.add(r.offset);a.add(r.multiplex);}
            const auto &prefs=side?remotePortalPreview.proposedSettings:remotePortalPreview.currentSettings;
            auto calibration=x.createNestedArray("calibration");for(const auto &p:m.axisProfile){auto a=calibration.createNestedArray();a.add(p.minimum);a.add(p.center);a.add(p.maximum);}
            x["inputFiltering"]=m.adcHysteresis;x["mode"]=atoi(prefs.opMode);x["connectWiFi"]=prefs.crsfap[0]!='0';x["speedUnits"]=prefs.elrsSpdUnit[0]=='0'?"kmh":"mph";
            x["CRSF channel cadence"]=side?remotePortalPreview.proposedCadence:remotePortalPreview.currentCadence;x["profileId"]=radios[side]->modelId;x["telemetry"]=radios[side]->telemetry;x["power"]=radios[side]->maxPower.label;x["powerUnit"]=radios[side]->maxPower.unit;x["dynamic"]=radios[side]->dynamic;
        }
    }
    if(doc.overflowed()){remotePortalPreview.valid=false;remotePortalError(413,"TOO_LARGE","preview","Preview document exceeds its bound");return;}
    remotePortalJson(doc);
}
static void remotePortalExport() {
    if(!remotePortalGate())return;
    if(remoteSettingsBusy()||remotePortalUploading){remotePortalError(409,"BUSY","export","Storage operation active");return;}
    const String kind=wm.server->arg("kind");if(kind!="model"&&kind!="backup"){remotePortalError(400,"INVALID_VALUE","kind","Choose model or backup");return;}
    std::unique_ptr<RemoteSettingsSnapshot> snapshot(new(std::nothrow) RemoteSettingsSnapshot);RemoteTransferError e;DynamicJsonDocument doc(REMOTE_TRANSFER_JSON_CAPACITY);
    bool ok=snapshot && captureRemoteSettings(*snapshot,e) && (kind=="backup"?remoteEncodeBackup(doc.to<JsonObject>(),*snapshot,e):remoteEncodeModel(doc.to<JsonObject>(),*snapshot,e));
    if(!ok || doc.overflowed()){if(e.code==RemoteTransferCode::None)remoteTransferFail(e,RemoteTransferCode::TooLarge,"export","Insufficient snapshot/document memory");remotePortalError(e);return;}
    wm.server->sendHeader("Content-Disposition",kind=="backup"?"attachment; filename=remote.crsf-backup.json":"attachment; filename=remote.remote-model.json");remotePortalJson(doc);
}
static void remotePortalApply() {
    if(!remotePortalGate())return;
    uint32_t confirmed;if(!remotePortalUnsigned("confirmed",1,confirmed)||confirmed!=1){remotePortalError(400,"INVALID_VALUE","confirmed","Confirm this preview before activation");return;}
    const String hash=wm.server->arg("fileHash");if(!remotePortalPreview.valid || hash.length()!=64 || strcmp(hash.c_str(),remotePortalPreview.fileHash)){remotePortalError(409,"STALE_PREVIEW","fileHash","Use this session's exact validated preview");return;}
    RemoteTransferError e;if(!applyRemoteTransfer(remotePortalPreview,e)){remotePortalPreview.valid=false;remotePortalError(e,409);return;}
    remotePortalPreview.valid=false;StaticJsonDocument<96> doc;doc["ok"]=true;doc["restart"]=true;remotePortalJson(doc);remotePortalService();prepareReboot();esp_restart();
}
static void remotePortalReview() {
    if(!remotePortalGate())return;uint32_t confirmed;
    if(!remotePortalUnsigned("confirmed",1,confirmed)||confirmed!=1 || !remoteCompleteRadioReview()){remotePortalError(409,"RADIO_REVIEW_REQUIRED","radio","Verify the imported radio values before finishing review");return;}
    StaticJsonDocument<64> doc;doc["ok"]=true;remotePortalJson(doc);
}
static void remoteSetupPortal() {
    const char *headers[]={"Origin","Referer","X-Remote-Nonce"};wm.server->collectHeaders(headers,3);
    wm.server->on("/remote-control",HTTP_GET,[](){
        const char *page="/param3#remote-controls";
        wm.server->sendHeader("Location",page);wm.server->sendHeader("Cache-Control","no-store");wm.server->send(302,"text/plain","");
    });
    wm.server->on("/remote/ui.js",HTTP_GET,[](){wm.server->sendHeader("Content-Encoding","gzip");wm.server->sendHeader("Cache-Control","no-store");wm.server->send_P(200,"application/javascript",(const char*)remoteConfigUiGzip,remoteConfigUiGzipLength);});
    wm.server->on("/remote/session",HTTP_POST,remotePortalSession);wm.server->on("/remote/devices",HTTP_GET,remotePortalDevices);wm.server->on("/remote/operation",HTTP_GET,remotePortalOperation);
    wm.server->on("/remote/action",HTTP_POST,remotePortalAction);wm.server->on("/remote/upload",HTTP_POST,remotePortalUploadDone,remotePortalUpload);wm.server->on("/remote/export",HTTP_GET,remotePortalExport);
    wm.server->on("/remote/apply",HTTP_POST,remotePortalApply);wm.server->on("/remote/review",HTTP_POST,remotePortalReview);
}
