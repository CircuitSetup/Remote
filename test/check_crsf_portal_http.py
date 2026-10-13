"""Exercise the real HTTP portal callbacks with fake network and storage services."""
from check_crsf_adc import ROOT, HEADERS, compile_and_run


HEADERS['FS.h'] = r'''
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
class File {
    std::vector<uint8_t> *bytes = nullptr;
    size_t pos = 0;
    bool writing = false;
public:
    File() = default;
    File(std::vector<uint8_t> *b, bool w = false) : bytes(b), writing(w) {}
    explicit operator bool() const { return bytes != nullptr; }
    size_t size() const { return bytes ? bytes->size() : 0; }
    size_t write(const uint8_t *data, size_t len) {
        if (!writing || !bytes || failWrite) return 0;
        if (len > maxWrite) maxWrite = len;
        bytes->insert(bytes->end(), data, data + len);
        return shortWrite && len ? len - 1 : len;
    }
    void flush() { if (shortRead && bytes && !bytes->empty()) bytes->pop_back(); }
    void close() { bytes = nullptr; }
    static bool failWrite, shortWrite, shortRead;
    static size_t maxWrite;
};
namespace fs {
struct FS {
    std::map<std::string, std::vector<uint8_t>> files;
    File open(const char *path, const char *mode = "r") {
        if (mode[0] == 'w') { files[path].clear(); return File(&files[path], true); }
        auto it = files.find(path);
        return it == files.end() ? File() : File(&it->second);
    }
};
}
extern fs::FS LittleFS;
'''

HEADERS['WebServer.h'] = r'''
#pragma once
#include <map>
#include <string>
#include <vector>
#include "FS.h"
class String {
    std::string value;
public:
    String() = default;
    String(const char *s) : value(s ? s : "") {}
    String(std::string s) : value(std::move(s)) {}
    size_t length() const { return value.size(); }
    bool isEmpty() const { return value.empty(); }
    const char *c_str() const { return value.c_str(); }
    char operator[](size_t i) const { return value[i]; }
    bool startsWith(const String &s) const { return value.rfind(s.value, 0) == 0; }
    bool endsWith(const char *s) const { return value.size() >= strlen(s) && value.compare(value.size()-strlen(s),strlen(s),s)==0; }
    void remove(size_t i) { if(i<value.size()) value.erase(i); }
    friend String operator+(const String &a, const String &b) { return a.value + b.value; }
    friend bool operator==(const String &a, const String &b) { return a.value == b.value; }
    friend bool operator!=(const String &a, const String &b) { return !(a == b); }
    friend bool operator==(const String &a, const char *b) { return a.value == b; }
    friend bool operator==(const char *a, const String &b) { return b == a; }
    friend bool operator!=(const String &a, const char *b) { return !(a == b); }
};
class IPAddress {
    uint8_t octets[4] = {};
public:
    IPAddress() = default;
    IPAddress(int a, int b, int c, int d) : octets{(uint8_t)a,(uint8_t)b,(uint8_t)c,(uint8_t)d} {}
    uint8_t operator[](int i) const { return octets[i]; }
    String toString() const { return std::to_string(octets[0])+"."+std::to_string(octets[1])+"."+std::to_string(octets[2])+"."+std::to_string(octets[3]); }
    friend bool operator==(const IPAddress &a, const IPAddress &b) { return !memcmp(a.octets,b.octets,4); }
    friend bool operator!=(const IPAddress &a, const IPAddress &b) { return !(a == b); }
};
struct TestClient { IPAddress local, peer; IPAddress localIP() const{return local;} IPAddress remoteIP()const{return peer;} };
struct HTTPUpload { int status; uint8_t *buf; size_t currentSize; HTTPUpload(int s=0,uint8_t*b=nullptr,size_t n=0):status(s),buf(b),currentSize(n){} };
constexpr int HTTP_GET=0, HTTP_POST=1, UPLOAD_FILE_START=0, UPLOAD_FILE_WRITE=1, UPLOAD_FILE_END=2, UPLOAD_FILE_ABORTED=3;
constexpr size_t CONTENT_LENGTH_UNKNOWN=SIZE_MAX;
class WebServer {
public:
    struct Route { std::string path; int method; void (*handler)(); void (*upload)(); };
    std::vector<Route> routes;
    std::map<std::string,std::string> args, headersOut, requestHeaders;
    TestClient socket{IPAddress(192,168,4,1), IPAddress(192,168,4,22)};
    HTTPUpload currentUpload;
    int response=0; std::string body; size_t responseBytes=0;
    int requestMethod=HTTP_POST;
    void (*on(const char *path,int method,void (*handler)()))() { routes.push_back({path,method,handler,nullptr}); return handler; }
    void on(const char *path,int method,void (*handler)(),void (*upload)()) { routes.push_back({path,method,handler,upload}); }
    void collectHeaders(const char **,size_t) {}
    TestClient client() { return socket; }
    String header(const char *name) { auto it=requestHeaders.find(name);return it==requestHeaders.end()?String():String(it->second); }
    String hostHeader() { return "192.168.4.1"; }
    String arg(const char *name) { auto it=args.find(name);return it==args.end()?String():String(it->second); }
    bool hasArg(const char *name) { return args.count(name); }
    HTTPUpload &upload() { return currentUpload; }
    int method() const { return requestMethod; }
    void sendHeader(const char *name,const char *value) { headersOut[name]=value; }
    void setContentLength(size_t) {}
    void send(int code,const char *,const char *data) { response=code;body=data?data:"";responseBytes=body.size(); }
    void sendContent(const char *data,size_t n) { if(data)body.append(data,n);responseBytes+=n; }
    void send_P(int code,const char *,const char *,size_t n) { response=code;responseBytes=n; }
};
'''

fixture = r'''
#ifndef HAVE_CRSF
#define PORTAL_RESTORE_NO_CRSF
#define HAVE_CRSF
#endif
#include "src/CRSF/crsf_settings.h"
#ifdef PORTAL_RESTORE_NO_CRSF
#undef HAVE_CRSF
#endif
#define REMOTE_SETTINGS_TRANSFER_H
#define PROGMEM
#include <ArduinoJson.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include "WebServer.h"
using namespace ArduinoJson;
using std::min;
static unsigned long nowMs=100;
bool restarted=false;
bool File::failWrite=false,File::shortWrite=false,File::shortRead=false;size_t File::maxWrite=0;
unsigned long millis(){return nowMs;}
uint32_t esp_random(){static uint32_t v=1;return ++v;}
void esp_restart(){restarted=true;}
struct Settings { char appw[64]="portalpass"; char hostName[32]="remote"; } settings;
struct WiFiClass { IPAddress softAPIP()const{return {192,168,4,1};} } WiFi;
struct TestManager { WebServer *server; };
WebServer server; TestManager wm{&server};
fs::FS LittleFS;
bool haveFS=true,FlashROMode=false,remoteSettingsRecoveryBlocked=false;
bool remoteSettingsBusyFlag=false,radioBusy=false,maintenance=false,reviewPending=false,reviewComplete=false;
bool remoteSettingsBusy(){return remoteSettingsBusyFlag;}
bool remoteMaintenanceActive(){return maintenance;}
void remoteSetMaintenance(bool b){maintenance=b;}
bool remoteRadioReviewPending(){return reviewPending;}
bool remoteCompleteRadioReview(){if(!reviewComplete)return false;reviewPending=false;return true;}

enum class RemoteTransferKind:uint8_t{DeviceBackup,ModelProfile};
enum class RemoteTransferCode:uint8_t{None,TooLarge,InvalidValue,Busy,WriteFailed,StalePreview};
enum class RemoteStoreMedium:uint8_t{Flash,SD};
struct RemoteTransferError{RemoteTransferCode code=RemoteTransferCode::None;char path[96]={},message[128]={};};
struct RemoteStoreRecord{RemoteStoreMedium medium=RemoteStoreMedium::Flash;uint16_t byteLength=0;};
struct RemoteRestorePreview { RemoteTransferKind kind=RemoteTransferKind::ModelProfile;char fileHash[65]={},settingsHash[65]={};uint32_t expiresAt=0,settingsGeneration=0,deviceGeneration=0,capabilityFingerprint=0;bool valid=false,cloneIdentity=false,restart=true;RemoteStoreRecord stores[7];uint32_t proposedIdentity=0;char hostname[32]={},ip[20]={};ELRSCrsfSettingsBlob currentCrsf={},proposedCrsf={};ELRSRadioPreferences currentRadio,proposedRadio;uint16_t currentCadence=250,proposedCadence=250;uint8_t changedStores=0;};
struct RemoteSettingsSnapshot{Settings settings;bool newBoard=true;};
constexpr size_t REMOTE_BACKUP_MAX_BYTES=32768,REMOTE_MODEL_MAX_BYTES=16384;size_t REMOTE_TRANSFER_JSON_CAPACITY=16384;
const char *remoteTransferCodeName(RemoteTransferCode c){return c==RemoteTransferCode::TooLarge?"TOO_LARGE":c==RemoteTransferCode::InvalidValue?"INVALID_VALUE":c==RemoteTransferCode::Busy?"BUSY":c==RemoteTransferCode::WriteFailed?"WRITE_FAILED":"NONE";}
bool remoteTransferFail(RemoteTransferError &e,RemoteTransferCode c,const char *p,const char *m){e.code=c;strncpy(e.path,p,sizeof(e.path)-1);strncpy(e.message,m,sizeof(e.message)-1);return false;}
uint32_t currentGeneration=1;
bool captureRemoteSettings(RemoteSettingsSnapshot &s,RemoteTransferError &){strcpy(s.settings.appw,settings.appw);return true;}
const char *remoteStoreName(uint8_t i){static const char *names[]={"main","ip","secondary","tertiary","crsf","audio","radio"};return names[i];}
bool validateRemoteTransfer(File &f,RemoteTransferKind,RemoteSettingsSnapshot &,RemoteRestorePreview &p,RemoteTransferError &){if(!f)return false;p.valid=true;p.expiresAt=nowMs+300000;p.settingsGeneration=currentGeneration;strcpy(p.fileHash,"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");strcpy(p.hostname,"remote");strcpy(p.ip,"192.168.4.1");p.proposedIdentity=42;p.changedStores=0x05;for(unsigned i=0;i<7;i++){p.stores[i].medium=i==5?RemoteStoreMedium::SD:RemoteStoreMedium::Flash;p.stores[i].byteLength=100+i;}p.currentCrsf.axisProfile[0].deadband=6;p.proposedCrsf.axisProfile[0].deadband=12;p.currentCrsf.logic.rules[0].function=1;p.proposedCrsf.logic.rules[0].function=2;p.currentCrsf.mixes.lines[0].weight=50;p.proposedCrsf.mixes.lines[0].weight=75;strcpy(p.currentRadio.maxPower.label,"25");strcpy(p.currentRadio.maxPower.unit,"mW");strcpy(p.proposedRadio.maxPower.label,"100");strcpy(p.proposedRadio.maxPower.unit,"mW");return true;}
bool applyRemoteTransfer(const RemoteRestorePreview &p,RemoteTransferError &e){if(remoteSettingsBusyFlag||!maintenance)return remoteTransferFail(e,RemoteTransferCode::Busy,"maintenance","Maintenance and idle storage required");if(!p.valid||p.expiresAt<=nowMs||p.settingsGeneration!=currentGeneration)return remoteTransferFail(e,RemoteTransferCode::StalePreview,"preview","Preview expired or device changed");return true;}
bool remoteEncodeBackup(JsonObject,RemoteSettingsSnapshot &,RemoteTransferError &){return true;}
bool remoteEncodeModel(JsonObject,RemoteSettingsSnapshot &,RemoteTransferError &){return true;}

enum class ELRSOperationState:uint8_t{Idle,Queued,Waiting,ConfirmationRequired,Running,Complete,Cancelled,Failed,OutcomeUnknown};
enum class ELRSDeviceError:uint8_t{None,Busy,Stale,Malformed,TooLarge,Unsupported,Timeout,Unsafe,Rejected,NotConnected};
struct ELRSParameterValue {uint8_t type=0;int32_t raw=0;uint32_t descriptorHash=0;ELRSParameterValue(){}ELRSParameterValue(uint8_t t,int32_t v):type(t),raw(v){}};
struct ELRSParameterView {uint8_t device=0,field=0,parent=0,type=0;uint32_t generation=0;ELRSParameterValue value,minimum,maximum;uint8_t precision=0;bool writable=false;const char *caption="parameter",*labels="",*units="",*role="";bool hidden()const{return false;}bool commandAllowed()const{return false;}const char*name()const{return caption;}const char*options()const{return labels;}const char*unit()const{return units;}const char*info()const{return "";}bool optionAt(uint8_t i,const char*&p,size_t&n)const{p=labels;while(i--){p=strchr(p,';');if(!p)return false;++p;}const char *end=strchr(p,';');n=end?(size_t)(end-p):strlen(p);return n!=0;}};
const char *remoteParameterRole(const ELRSParameterView &p){return p.role;}
ELRSRadioPreferences remoteRadioPreferences,savedPreferences;
bool remoteRadioVersioned=false,persistPass=true;unsigned persistCalls=0;
bool write_settings(){persistCalls++;if(persistPass)savedPreferences=remoteRadioPreferences;return persistPass;}
void remoteObserveRfChoice(const ELRSParameterView &,uint32_t){}
struct ELRSDeviceInfo {uint8_t address=0,fieldCount=0,parameterVersion=0;uint32_t generation=0,serial=0,hardwareVersion=0,softwareVersion=0;const char *name="receiver";};
struct ELRSOperationStatus {uint32_t id=0,generation=1;ELRSOperationState state=ELRSOperationState::Idle;ELRSDeviceError error=ELRSDeviceError::None;uint8_t device=1,field=2;const char *message="idle";};
struct RadioStatus {bool rfConnectionKnown=false,rfConnected=false,modelMatched=false,armedKnown=false,armed=false;uint32_t statusAgeMs=0;};
struct DeviceConfig {
    int calls=0;uint32_t lastId=0;ELRSOperationStatus op;ELRSParameterView view;
    bool busy()const{return radioBusy;} unsigned deviceCount()const{return 1;} const ELRSDeviceInfo *deviceAt(unsigned)const{static ELRSDeviceInfo d;return &d;}
    RadioStatus radioStatus(uint32_t)const{RadioStatus s;s.modelMatched=true;return s;}const ELRSOperationStatus&status()const{return op;}const ELRSParameterView*parameter()const{return &view;}
    bool knownModel=false;uint8_t currentModel=0;
    bool mutationSupported()const{return true;}bool modelIdKnown()const{return knownModel;}uint8_t modelId()const{return currentModel;}uint16_t finalChannel5()const{return 172;}
    bool discover(uint32_t id){calls++;lastId=id;return true;}bool readRadioStatus(uint32_t id){calls++;lastId=id;return true;}bool read(uint8_t d,uint8_t f,uint32_t g,uint32_t id){if(d!=1||f!=2||g!=1)return false;calls++;lastId=id;return true;}
    bool write(uint8_t d,uint8_t f,uint32_t g,const ELRSParameterValue&,const ELRSParameterValue&,uint32_t id){if((d!=1&&d!=238)||f!=2||g!=1)return false;calls++;lastId=id;op.id=id;op.device=d;op.field=f;op.state=ELRSOperationState::Waiting;return true;}
    bool command(uint8_t d,uint8_t f,uint32_t g,uint8_t,uint32_t id){if(d!=1||f!=2||g!=1)return false;calls++;lastId=id;return true;}bool selectModel(uint8_t,uint32_t g,uint32_t id){if(g!=1)return false;calls++;lastId=id;return true;}bool assignReceiver(uint8_t d,uint8_t,uint32_t g,uint32_t id){if(d!=1||g!=1)return false;calls++;lastId=id;return true;}
};
struct ELRSMode {DeviceConfig config;ELRSRadioPreferences runtimePreferences;DeviceConfig&deviceConfig(){return config;}bool finalChannel5()const{return false;}bool setRadioPreferences(const ELRSRadioPreferences&r,bool=false,bool=true){runtimePreferences=r;return true;}void pauseModulePreferenceApply(){}} elrsMode;
bool opModeCRSF=true,haveNewBoard=true;
void serviceCRSF(bool){}

#include "remote_config_portal.h"

static void requestAuth(const char *nonce="",const char *origin="http://192.168.4.1"){
    server.requestHeaders.clear();server.requestHeaders["Origin"]=origin;if(*nonce)server.requestHeaders["X-Remote-Nonce"]=nonce;
}
static std::string responseNonce(){DynamicJsonDocument d(256);assert(!deserializeJson(d,server.body));return d["nonce"].as<std::string>();}
static void resetResponse(){server.response=0;server.body.clear();server.responseBytes=0;server.headersOut.clear();}
static void issueSession(){resetResponse();requestAuth();remotePortalSession();assert(server.response==200);assert(remotePortalNonce[0]);}
static void readyProtected(){settings.appw[7]=0;strcpy(settings.appw,"portalpass");server.socket.local={192,168,4,1};server.socket.peer={192,168,4,22};requestAuth();}
int main(){
    remoteSetupPortal();
    for(const auto &route:server.routes)if(route.path=="/remote-control")route.handler();
    assert(server.response==302);
#ifdef HAVE_CRSF
    assert(server.headersOut["Location"]=="/param3#remote-controls");
    haveNewBoard=false;resetResponse();
    for(const auto &route:server.routes)if(route.path=="/remote-control")route.handler();
    assert(server.response==302&&server.headersOut["Location"]=="/param#remote-controls");
    haveNewBoard=true;
#else
    assert(server.headersOut["Location"]=="/param#remote-controls");
#endif
    resetResponse();
    for(const auto &route:server.routes)if(route.path=="/remote/ui.js")route.handler();
    assert(server.response==200&&server.headersOut["Content-Encoding"]=="gzip"&&server.responseBytes>0);
    assert(server.routes.size()==10);
    const char *expectedRoutes[]={"/remote-control","/remote/ui.js","/remote/session","/remote/devices","/remote/operation","/remote/action","/remote/upload","/remote/export","/remote/apply","/remote/review"};
    for(const char *path:expectedRoutes){bool found=false;for(const auto &route:server.routes)if(route.path==path)found=true;assert(found);}
    std::string nonce;
    remotePortalDevices();assert(server.response==200&&server.body.find("protectedAP")!=std::string::npos&&server.body.find("portalpass")==std::string::npos);
#ifdef HAVE_CRSF
    assert(server.body.find("\"modelMatched\":true")!=std::string::npos);
#endif
    readyProtected();requestAuth("","http://192.168.4.1.evil");remotePortalSession();assert(server.response==403);
    readyProtected();server.socket.peer={192,168,5,22};remotePortalSession();assert(server.response==403);
    readyProtected();server.socket.local={10,0,0,1};remotePortalSession();assert(server.response==403);
    readyProtected();server.socket.local={192,168,4,1};requestAuth("","http://192.168.4.1.evil");remotePortalSession();assert(server.response==403);
    readyProtected();requestAuth();server.requestHeaders.erase("Origin");server.requestHeaders["Referer"]="http://192.168.4.1.evil/remote";remotePortalSession();assert(server.response==403);
    readyProtected();server.requestMethod=HTTP_POST;server.requestHeaders.clear();server.requestHeaders["Referer"]="http://192.168.4.1/remote-control";remotePortalSession();assert(server.response==403);
    issueSession();nonce=responseNonce();server.requestMethod=HTTP_GET;requestAuth(nonce.c_str());server.requestHeaders.erase("Origin");server.requestHeaders["Referer"]="http://192.168.4.1/remote-control";remotePortalOperation();assert(server.response==200);server.requestMethod=HTTP_POST;
    assert(server.body.find("\"state\":\"idle\"")!=std::string::npos);
    assert(nonce.size()==32);requestAuth(nonce.c_str());remotePortalSession();assert(server.response==200&&responseNonce()!=nonce);requestAuth(nonce.c_str());remotePortalAction();assert(server.response==403);

    nowMs=100;issueSession();nonce=responseNonce();remoteSettingsBusyFlag=true;resetResponse();requestAuth(nonce.c_str());remotePortalSession();assert(server.response==409);remoteSettingsBusyFlag=false;
#ifdef HAVE_CRSF
    remotePortalNonce[0]=0;radioBusy=true;resetResponse();requestAuth();remotePortalSession();assert(server.response==409);radioBusy=false;issueSession();nonce=responseNonce();
#endif
    resetResponse();requestAuth("00000000000000000000000000000000");remotePortalAction();assert(server.response==403);
    resetResponse();requestAuth(nonce.c_str(),"http://192.168.4.1.evil");remotePortalAction();assert(server.response==403);
    server.socket.peer={192,168,4,23};resetResponse();requestAuth(nonce.c_str());remotePortalAction();assert(server.response==403);server.socket.peer={192,168,4,22};
    nowMs=remotePortalExpires;resetResponse();requestAuth(nonce.c_str());remotePortalAction();assert(server.response==403);
    nowMs=200;issueSession();nonce=responseNonce();
#ifdef HAVE_CRSF
    server.args={{"action","read"},{"device","+1"},{"field","2"},{"generation","1"}};resetResponse();requestAuth(nonce.c_str());remotePortalAction();assert(server.response==400&&elrsMode.config.calls==0&&server.body.find("portalpass")==std::string::npos);
    server.args={{"action","read"},{"device","1"},{"field","0"},{"generation","1"}};resetResponse();requestAuth(nonce.c_str());remotePortalAction();assert(server.response==400&&elrsMode.config.calls==0);
    server.args={{"action","read"},{"device","99"},{"field","2"},{"generation","1"}};resetResponse();requestAuth(nonce.c_str());remotePortalAction();assert(server.response==409&&elrsMode.config.calls==0);
    server.args={{"action","read"},{"device","1"},{"field","3"},{"generation","1"}};resetResponse();requestAuth(nonce.c_str());remotePortalAction();assert(server.response==409&&elrsMode.config.calls==0);
    server.args={{"action","write"},{"device","1"},{"field","2"},{"generation","1"},{"confirmed","1"},{"type","10"},{"hash","9"},{"expected","1"},{"value","2"}};resetResponse();requestAuth(nonce.c_str());remotePortalAction();assert(server.response==400&&elrsMode.config.calls==0);
    server.args["type"]="1";server.args["expected"]=" 1";resetResponse();requestAuth(nonce.c_str());remotePortalAction();assert(server.response==400&&elrsMode.config.calls==0);
    server.args["expected"]="1";server.args["value"]="-2147483649";resetResponse();requestAuth(nonce.c_str());remotePortalAction();assert(server.response==400&&elrsMode.config.calls==0);
    server.args["value"]="2147483647";server.args.erase("confirmed");resetResponse();requestAuth(nonce.c_str());remotePortalAction();assert(server.response==400&&elrsMode.config.calls==0);
    server.args["confirmed"]="1";resetResponse();requestAuth(nonce.c_str());remotePortalAction();assert(server.response==202&&elrsMode.config.calls==1);
    server.args={{"action","command"},{"device","1"},{"field","2"},{"generation","1"},{"status","1"}};resetResponse();requestAuth(nonce.c_str());remotePortalAction();assert(server.response==400&&elrsMode.config.calls==1);
    server.args["confirmed"]="1";resetResponse();requestAuth(nonce.c_str());remotePortalAction();assert(server.response==202&&elrsMode.config.calls==2);
#else
    server.args={{"action","read"}};resetResponse();requestAuth(nonce.c_str());remotePortalAction();assert(server.response==400&&elrsMode.config.calls==0);
#endif

    server.args={{"kind","model"}};remotePortalPreview.valid=false;remotePortalUploadOk=true;resetResponse();requestAuth(nonce.c_str());remotePortalApply();assert(server.response==400);
    remotePortalPreview.valid=true;strcpy(remotePortalPreview.fileHash,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");server.args={{"confirmed","1"},{"fileHash","bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"}};resetResponse();requestAuth(nonce.c_str());remotePortalApply();assert(server.response==409);
    strcpy(remotePortalPreview.fileHash,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");server.args={{"confirmed","1"},{"fileHash","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}};remotePortalPreview.expiresAt=nowMs-1;resetResponse();requestAuth(nonce.c_str());remotePortalApply();assert(server.response==409&&!restarted);
    remotePortalPreview.valid=true;remotePortalPreview.expiresAt=nowMs+1000;remotePortalPreview.settingsGeneration=currentGeneration+1;resetResponse();requestAuth(nonce.c_str());remotePortalApply();assert(server.response==409&&!restarted);
    remotePortalPreview.valid=true;remotePortalPreview.settingsGeneration=currentGeneration;remoteSetMaintenance(true);resetResponse();requestAuth(nonce.c_str());remotePortalApply();assert(server.response==200&&restarted);

    issueSession();nonce=responseNonce();
    auto startUpload=[&](const char *kind){server.args={{"kind",kind}};server.currentUpload={UPLOAD_FILE_START,nullptr,0};requestAuth(nonce.c_str());remotePortalUpload();};
    auto finishUpload=[&](){resetResponse();requestAuth(nonce.c_str());remotePortalUploadDone();};
    startUpload("model");assert(remotePortalUploading&&remotePortalUploadOk);
    std::vector<uint8_t> tooBig(REMOTE_MODEL_MAX_BYTES+1,'x');server.currentUpload={UPLOAD_FILE_WRITE,tooBig.data(),tooBig.size()};requestAuth(nonce.c_str());remotePortalUpload();assert(!remotePortalUploadOk&&remotePortalUploadBytes==tooBig.size());
    server.currentUpload={UPLOAD_FILE_END,nullptr,0};remotePortalUpload();assert(!remotePortalUploading);
    resetResponse();requestAuth(nonce.c_str());remotePortalUploadDone();assert(server.response==413||server.response==400);
    startUpload("backup");File::failWrite=true;uint8_t block[8]={};server.currentUpload={UPLOAD_FILE_WRITE,block,sizeof(block)};remotePortalUpload();assert(!remotePortalUploadOk);File::failWrite=false;
    server.currentUpload={UPLOAD_FILE_ABORTED,nullptr,0};remotePortalUpload();assert(!remotePortalUploading);finishUpload();
    startUpload("bad");assert(!remotePortalUploading&&remotePortalUploadError.code==RemoteTransferCode::InvalidValue);finishUpload();
    startUpload("model");server.currentUpload={UPLOAD_FILE_START,nullptr,0};remotePortalUpload();assert(!remotePortalUploadOk&&remotePortalUploadError.code==RemoteTransferCode::InvalidValue);server.currentUpload={UPLOAD_FILE_ABORTED,nullptr,0};remotePortalUpload();finishUpload();
    startUpload("model");uint8_t small[]={1,2,3};server.currentUpload={UPLOAD_FILE_WRITE,small,sizeof(small)};remotePortalUpload();File::shortRead=true;server.currentUpload={UPLOAD_FILE_END,nullptr,0};remotePortalUpload();assert(!remotePortalUploadOk);File::shortRead=false;finishUpload();
    startUpload("backup");std::vector<uint8_t> backupTooBig(REMOTE_BACKUP_MAX_BYTES+1,'x');server.currentUpload={UPLOAD_FILE_WRITE,backupTooBig.data(),backupTooBig.size()};remotePortalUpload();assert(!remotePortalUploadOk);server.currentUpload={UPLOAD_FILE_END,nullptr,0};remotePortalUpload();finishUpload();
    startUpload("model");std::vector<uint8_t> streamed(REMOTE_MODEL_MAX_BYTES,'x');File::maxWrite=0;server.currentUpload={UPLOAD_FILE_WRITE,streamed.data(),streamed.size()};remotePortalUpload();server.currentUpload={UPLOAD_FILE_END,nullptr,0};remotePortalUpload();assert(remotePortalUploadOk&&File::maxWrite<=256);finishUpload();
    startUpload("model");server.currentUpload={UPLOAD_FILE_WRITE,small,sizeof(small)};remotePortalUpload();server.currentUpload={UPLOAD_FILE_END,nullptr,0};remotePortalUpload();resetResponse();requestAuth(nonce.c_str());remotePortalUploadDone();assert(server.response==200&&server.body.find("portalpass")==std::string::npos);
    DynamicJsonDocument preview(REMOTE_TRANSFER_JSON_CAPACITY);assert(!deserializeJson(preview,server.body));assert(preview["stores"].size()==7&&preview["current"]["inputs"].size()==4&&preview["proposed"]["logical"].size()==ELRS_LOGIC_COUNT&&preview["proposed"]["mixes"].size()==ELRS_MIX_COUNT);assert(preview["current"]["inputs"][0][1]==6&&preview["proposed"]["inputs"][0][1]==12&&preview["stores"][0]["changed"]==true);assert(preview["current"]["power"]=="25"&&preview["proposed"]["power"]=="100");
    startUpload("model");server.currentUpload={UPLOAD_FILE_WRITE,small,sizeof(small)};remotePortalUpload();server.currentUpload={UPLOAD_FILE_END,nullptr,0};remotePortalUpload();REMOTE_TRANSFER_JSON_CAPACITY=128;resetResponse();requestAuth(nonce.c_str());remotePortalUploadDone();REMOTE_TRANSFER_JSON_CAPACITY=16384;assert(server.response==413&&!remotePortalPreview.valid&&server.body.find("portalpass")==std::string::npos);
    reviewPending=true;reviewComplete=false;server.args={{"confirmed","1"}};resetResponse();requestAuth(nonce.c_str());remotePortalReview();assert(server.response==409&&reviewPending);reviewComplete=true;resetResponse();requestAuth(nonce.c_str());remotePortalReview();assert(server.response==200&&!reviewPending);
#ifdef HAVE_CRSF
    issueSession();nonce=responseNonce();auto &config=elrsMode.config;auto &p=config.view;
    p.device=238;p.field=2;p.generation=1;p.type=9;p.value.raw=1;p.labels="25;250";p.units="mW";p.role="power";p.caption="Max Power";
    server.args={{"action","write"},{"device","238"},{"field","2"},{"generation","1"},{"confirmed","1"},{"type","9"},{"hash","9"},{"expected","0"},{"value","1"}};
    resetResponse();requestAuth(nonce.c_str());remotePortalAction();assert(server.response==202);
    remotePortalOperation();assert(persistCalls==0);
    config.op.state=ELRSOperationState::Complete;remotePortalApplyPreferences();resetResponse();remotePortalOperation();
    assert(persistCalls==1&&remoteRadioVersioned&&!strcmp(savedPreferences.maxPower.label,"250")&&!strcmp(savedPreferences.maxPower.unit,"mW"));
    assert(!strcmp(elrsMode.runtimePreferences.maxPower.label,"250"));remotePortalOperation();assert(persistCalls==1);
    server.args={{"action","model"},{"generation","1"},{"modelId","12"}};resetResponse();remotePortalAction();assert(server.response==202);
    config.op.id=config.lastId;config.op.state=ELRSOperationState::Complete;config.knownModel=true;config.currentModel=12;p.role="modelMatch";p.caption="Model Match (ID:12)";
    resetResponse();remotePortalOperation();assert(persistCalls==2&&savedPreferences.modelId==12&&elrsMode.runtimePreferences.modelId==12);
    persistPass=false;p.role="power";p.caption="Max Power";p.value.raw=0;
    server.args={{"action","write"},{"device","238"},{"field","2"},{"generation","1"},{"confirmed","1"},{"type","9"},{"hash","9"},{"expected","1"},{"value","0"}};
    resetResponse();remotePortalAction();assert(server.response==202);config.op.state=ELRSOperationState::Complete;
    resetResponse();remotePortalOperation();assert(persistCalls==3&&server.body.find("saving Remote preferences failed")!=std::string::npos&&server.body.find("\"state\":\"unknown\"")!=std::string::npos);
    assert(!strcmp(remoteRadioPreferences.maxPower.label,"25")&&!strcmp(elrsMode.runtimePreferences.maxPower.label,"25"));remotePortalOperation();assert(persistCalls==3);
#endif
    puts("HTTP portal callback admission, strict input, preview binding, and upload failure checks passed");
}
'''

compile_and_run(fixture, [], ['-I' + str(ROOT / '.pio/libdeps/esp32dev/ArduinoJson/src')])
compile_and_run(fixture, [], ['-I' + str(ROOT / '.pio/libdeps/esp32dev/ArduinoJson/src'), '-U HAVE_CRSF'])
