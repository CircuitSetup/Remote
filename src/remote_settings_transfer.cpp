#include "remote_settings_transfer.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <mbedtls/sha256.h>
#endif

const char *remoteStoreName(uint8_t id) {
    static const char *const names[]={"main","mqtt","ip","identity","secondary","tertiary","crsf"};
    return id<7 ? names[id] : "invalid";
}
const char *remoteStorePath(uint8_t id) {
    static const char *const paths[]={"/remconfig.json","/remhacfg.json","/remipcfg","/remidid","/rem2cfg","/rem3cfg","/crsfcfg"};
    return id<7 ? paths[id] : nullptr;
}
const char *remoteTransferCodeName(RemoteTransferCode code) {
    static const char *const names[]={"NONE","TOO_LARGE","UNSUPPORTED_SCHEMA","MISSING_SECTION","INVALID_VALUE","DANGLING_SOURCE","UNSUPPORTED_BEHAVIOR","STALE_PREVIEW","MEDIA_UNAVAILABLE","WRITE_FAILED","ROLLBACK_REQUIRED","RECOVERY_ANCHOR_UNAVAILABLE","BUSY","IDENTITY_CHOICE_REQUIRED"};
    return names[(uint8_t)code];
}
bool remoteTransferFail(RemoteTransferError &e,RemoteTransferCode code,const char *path,const char *message) {
    e.code=code; snprintf(e.path,sizeof(e.path),"%s",path); snprintf(e.message,sizeof(e.message),"%s",message); return false;
}
void remoteSha256(const uint8_t *bytes,size_t length,char hash[65]) {
    uint8_t digest[32]={};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_HASH_HANDLE h=nullptr;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0) {
        if(BCryptCreateHash(alg,&h,nullptr,0,nullptr,0,0)==0) {
            BCryptHashData(h,const_cast<PUCHAR>(bytes),(ULONG)length,0); BCryptFinishHash(h,digest,32,0); BCryptDestroyHash(h);
        }
        BCryptCloseAlgorithmProvider(alg,0);
    }
#else
    mbedtls_sha256_ret(bytes,length,digest,0);
#endif
    for(unsigned i=0;i<32;i++) snprintf(hash+i*2,3,"%02x",digest[i]);
}
uint8_t remoteFrameChecksum(const uint8_t *b,size_t len) {
    uint16_t s=0; while(len--) s+=*b++; s=(s>>8)+(s&255); s+=s>>8; return (uint8_t)~s;
}
ELRSCrsfSettingsBlob remoteDefaultCrsfSettings() {
#ifndef HAVE_CRSF
    return {};
#else
    ELRSCrsfSettingsBlob s={};
    for(int i=0;i<4;i++) {s.axisProfile[i]=elrsDefaultInputAxisProfile(); s.outputLimits[i]=elrsDefaultOutputLimits();}
    s.gimbalRouting=elrsDefaultGimbalRouting(); s.switchRouting=elrsDefaultSwitchRouting();
    s.adcHysteresis=s.throttleIdleDeadband=ELRS_INPUT_TOLERANCE_DEFAULT;
    s.displayConfig=elrsDefaultDisplayConfig(); s.vehicleConfig=elrsDefaultVehicleConfig(); s.logicVersion=2; return s;
#endif
}
bool elrsIsValidCrsfSettingsBlob(const ELRSCrsfSettingsBlob &s) {
#ifndef HAVE_CRSF
    return false;
#else
    for(unsigned i=0;i<4;i++) if(!elrsIsValidInputAxisProfile(s.axisProfile[i]) || !elrsIsValidOutputLimits(s.outputLimits[i])) return false;
    for(uint8_t a:s.localActions) if(a>1) return false;
    return s.logicVersion==2 && s.adcHysteresis<=32 && s.throttleIdleDeadband<=32 &&
        elrsIsValidInputRouting(s.gimbalRouting,s.switchRouting) && elrsIsValidDisplayConfig(s.displayConfig) &&
        elrsIsValidVehicleConfig(s.vehicleConfig) && elrsIsValidModelReferences(s.logic,s.mixes);
#endif
}
bool remoteDecodeCrsf(const uint8_t *bytes,size_t len,ELRSCrsfSettingsBlob &s,RemoteTransferError &e) {
#ifndef HAVE_CRSF
    return remoteTransferFail(e,RemoteTransferCode::UnsupportedBehavior,"stores.crsf","CRSF is not compiled");
#else
    if(len<3 || len>300 || (size_t)(bytes[0]|bytes[1]<<8)!=len-3 || remoteFrameChecksum(bytes,len-1)!=bytes[len-1])
        return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"stores.crsf","Invalid binary framing");
    const size_t n=len-3;
    static const uint16_t complete[]={24,48,52,54,56,68,72,76,80,84,96,106,119,120,209,297};
    bool known=false; for(auto v:complete) known|=n==v;
    if(!known || (n==209 && bytes[122]!=1) || (n==297 && bytes[122]!=2))
        return remoteTransferFail(e,RemoteTransferCode::UnsupportedSchema,"stores.crsf","Incomplete or unsupported CRSF layout");
    s=remoteDefaultCrsfSettings();
    if(n==24) {
        for(unsigned i=0;i<4;i++) { ELRSAxisCalibrationData c; memcpy(&c,bytes+2+i*6,6); s.axisProfile[i].minimum=c.minimum; s.axisProfile[i].center=c.center; s.axisProfile[i].maximum=c.maximum; }
    } else {
        memcpy(&s,bytes+2,n>=209 ? 120 : n);
        if(n==209) {memcpy(&s.logic,bytes+123,40); memcpy(&s.mixes,bytes+163,48);}
        if(n==297) memcpy(&s,bytes+2,n);
    }
    if(!elrsIsValidCrsfSettingsBlob(s)) return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"stores.crsf","Invalid calibration, routing, limits or references");
    return true;
#endif
}
namespace {
struct TextField {const char *key; uint16_t offset; uint8_t bytes; int16_t minimum,maximum;};
#define FIELD(k,f,lo,hi) {k,offsetof(Settings,f),sizeof(((Settings*)0)->f),lo,hi}
const TextField mainFields[]={
    FIELD("ssid",ssid,-1,0),FIELD("pass",pass,-1,0),FIELD("bssid",bssid,-1,0),
    FIELD("cmsid",cm_ssid,-1,0),FIELD("cmpwd",cm_pass,-1,0),FIELD("cmbid",cm_bssid,-1,0),
    FIELD("hostName",hostName,-1,0),FIELD("wifiConRetries",wifiConRetries,1,10),FIELD("rcOFP",reconOnFP,0,1),
    FIELD("systemID",systemID,-1,0),FIELD("appw",appw,-1,0),FIELD("apch",apChnl,0,11),FIELD("wAOD",wifiAPOffDelay,0,99),FIELD("rAOFP",reactAPOnFP,0,1),
    FIELD("at",autoThrottle,0,1),FIELD("coast",coast,0,1),FIELD("pTUT",playTUT,0,1),FIELD("playClick",playClick,0,1),FIELD("playALsnd",playALsnd,0,1),
    FIELD("tcdIP",tcdIP,-1,0),FIELD("pwM",pwrMst,0,1),FIELD("reB",refBut,0,8),FIELD("CfgOnSD",CfgOnSD,0,1),
    FIELD("oorst",oorst,0,1),FIELD("oott",ooTT,0,1),FIELD("resat",resAT,0,1),
#ifdef ALLOW_DIS_UB
    FIELD("disBP",disBPack,0,1),
#endif
    FIELD("b0Mt",bPb0Maint,0,1),FIELD("b1Mt",bPb1Maint,0,1),FIELD("b2Mt",bPb2Maint,0,1),FIELD("b3Mt",bPb3Maint,0,1),
    FIELD("b4Mt",bPb4Maint,0,1),FIELD("b5Mt",bPb5Maint,0,1),FIELD("b6Mt",bPb6Maint,0,1),FIELD("b7Mt",bPb7Maint,0,1),
    FIELD("b0MtO",bPb0MtO,0,1),FIELD("b1MtO",bPb1MtO,0,1),FIELD("b2MtO",bPb2MtO,0,1),FIELD("b3MtO",bPb3MtO,0,1),
    FIELD("b4MtO",bPb4MtO,0,1),FIELD("b5MtO",bPb5MtO,0,1),FIELD("b6MtO",bPb6MtO,0,1),FIELD("b7MtO",bPb7MtO,0,1),
    FIELD("uPLED",usePwrLED,0,1),FIELD("pLEDFP",pwrLEDonFP,0,1),FIELD("uLvLM",useLvlMtr,0,1),FIELD("uLvLMFP",LvLMtronFP,0,1),
#ifdef HAVE_PM
    FIELD("uPM",usePwrMon,0,1),FIELD("bTy",batType,0,4),FIELD("bCa",batCap,1000,6000),
#endif
#ifdef HAVE_CRSF
    FIELD("opMode",opMode,0,2),FIELD("eWAP",crsfap,0,1),FIELD("ePRHz",elrsPktRate,0,4),FIELD("eSUnit",elrsSpdUnit,0,1),
    FIELD("eTlmR",elrsTlmRatio,0,6),FIELD("eMxPwr",elrsMaxPower,0,5),FIELD("eDynP",elrsDynPower,0,1),
#endif
};
const TextField mqttFields[]={
#ifdef HAVE_MQTT
    FIELD("useMQTT",useMQTT,0,1),FIELD("mqttServer",mqttServer,-1,0),FIELD("mqttV",mqttVers,0,1),FIELD("mqttUser",mqttUser,-1,0),
#ifdef HAVE_MQTT_MP
    FIELD("pMP",pubMP,0,1),
#endif
#define MQTT_ROW(n,i) FIELD("mqttb" n "t",mqttbt[i],-1,0),FIELD("mqttb" n "o",mqttbo[i],-1,0),FIELD("mqttb" n "f",mqttbf[i],-1,0)
    MQTT_ROW("1",0),MQTT_ROW("2",1),MQTT_ROW("3",2),MQTT_ROW("4",3),MQTT_ROW("5",4),MQTT_ROW("6",5),MQTT_ROW("7",6),MQTT_ROW("8",7)
#endif
};
#undef FIELD
bool text(JsonVariantConst v,char *dest,size_t cap) {
    if(!v.is<const char*>()) return false;
    const JsonString str=v.as<JsonString>(); const char *s=str.c_str(); const size_t n=str.size();
    if(n>=cap || strlen(s)!=n) return false;
    for(size_t i=0;i<n;i++) {
        const uint8_t b=s[i]; if(b<0x80) continue;
        unsigned count=b>=0xc2 && b<=0xdf ? 1 : b>=0xe0 && b<=0xef ? 2 : b>=0xf0 && b<=0xf4 ? 3 : 9;
        if(count==9 || i+count>=n) return false;
        const uint8_t second=s[i+1];
        if((b==0xe0 && second<0xa0)||(b==0xed && second>=0xa0)||(b==0xf0 && second<0x90)||(b==0xf4 && second>=0x90)) return false;
        while(count--) if(((uint8_t)s[++i]&0xc0)!=0x80) return false;
    }
    memcpy(dest,s,n+1); return true;
}
bool keys(JsonObjectConst o,const char *allowed) {
    if(o.isNull()) return false;
    for(JsonPairConst p:o) {
        const char *k=p.key().c_str(), *at=allowed; bool found=false; size_t n=strlen(k);
        if(!n || strchr(k,'|')) return false;
        while((at=strstr(at,k))) {if((at==allowed || at[-1]=='|') && (at[n]==0 || at[n]=='|')) {found=true; break;} ++at;}
        if(!found) return false;
    }
    return true;
}
bool integer(JsonVariantConst v,int lo,int hi,int &out) {if(!v.is<int>()) return false; out=v.as<int>(); return out>=lo && out<=hi;}
bool number(JsonVariantConst v,float &out) {if(!v.is<float>()) return false; out=v.as<float>(); return std::isfinite(out);}
bool radio(JsonObjectConst o,ELRSRadioPreferences &r,bool stored) {
#ifndef HAVE_CRSF
    return false;
#else
    int n;
    if(!keys(o,stored ? "version|modelId|telemetry|maxPower|dynamic" : "crsfUpdateHz|modelId|telemetry|maxPower|dynamic|requirements")) return false;
    if(stored && (!integer(o["version"],1,1,n))) return false;
    if(!integer(o["modelId"],0,63,n)) return false; r.modelId=n;
    JsonObjectConst p=o["maxPower"];
    return text(o["telemetry"],r.telemetry,sizeof(r.telemetry)) && text(o["dynamic"],r.dynamic,sizeof(r.dynamic)) &&
        keys(p,"label|unit") && text(p["label"],r.maxPower.label,sizeof(r.maxPower.label)) && text(p["unit"],r.maxPower.unit,sizeof(r.maxPower.unit)) && elrsIsValidRadioPreferences(r);
#endif
}
void encodeRadio(JsonObject o,const ELRSRadioPreferences &r,bool stored) {
    if(stored) o["version"]=1; o["modelId"]=r.modelId; o["telemetry"]=r.telemetry;
    JsonObject p=o.createNestedObject("maxPower"); p["label"]=r.maxPower.label; p["unit"]=r.maxPower.unit; o["dynamic"]=r.dynamic;
}
void encodeFloat(JsonObject o,const char *key,float value) {
    // Nine significant digits preserve float32; the decimal keeps signed zero.
    char decimal[32];
    if(value==0) strcpy(decimal,std::signbit(value)?"-0.0":"0.0");
    else snprintf(decimal,sizeof(decimal),"%.9g",(double)value);
    o[key]=serialized(decimal);
}
const char base64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
void encode64(const uint8_t *b,size_t n,char *out) {
    size_t k=0; for(size_t i=0;i<n;i+=3) {unsigned v=b[i]<<16 | (i+1<n?b[i+1]<<8:0) | (i+2<n?b[i+2]:0); out[k++]=base64[v>>18]; out[k++]=base64[(v>>12)&63]; out[k++]=i+1<n?base64[(v>>6)&63]:'='; out[k++]=i+2<n?base64[v&63]:'=';} out[k]=0;
}
bool decode64(JsonVariantConst v,uint8_t *b,size_t cap,size_t &n) {
    if(!v.is<const char*>()) return false; const char *s=v.as<const char*>(); size_t len=strlen(s); n=0;
    if(len%4 || len>400) return false;
    for(size_t i=0;i<len;i+=4) {unsigned value=0; int padding=0;
        for(int j=0;j<4;j++) {const char *p=s[i+j]=='=' ? nullptr : strchr(base64,s[i+j]); if(!p && (s[i+j]!='=' || j<2 || i+4!=len)) return false; if(padding && p) return false; if(!p) padding++; value=(value<<6)|(p ? p-base64 : 0);}
        if(n+3-padding>cap || (padding==1 && (value&255)) || (padding==2 && (value&65535))) return false;
        for(int j=0;j<3-padding;j++) b[n++]=(value>>(16-8*j))&255;
    }
    return true;
}
}
bool remoteEncodeSettings(JsonObject o,const Settings &s,const ELRSRadioPreferences &r,bool mqtt,RemoteSettingsEncoding encoding,bool newBoard) {
    const TextField *fields=mqtt ? mqttFields:mainFields; size_t count=mqtt ? sizeof(mqttFields)/sizeof(TextField):sizeof(mainFields)/sizeof(TextField);
    for(size_t i=0;i<count;i++) {
        const char *key=fields[i].key;
        if(!mqtt && encoding==RemoteSettingsEncoding::StoredFile) {
            if(!s.ssid[0] && s.ssid[1]=='X' && i<3)continue;
#ifdef HAVE_CRSF
            if(!newBoard && (key[0]=='e' || !strcmp(key,"opMode")))continue;
#endif
        }
        o[key]=(const char*)&s+fields[i].offset;
    }
    #ifdef HAVE_CRSF
    if(!mqtt && (encoding==RemoteSettingsEncoding::Transfer || newBoard)) encodeRadio(o.createNestedObject("eRadio"),r,true);
    #endif
    return true;
}
bool remoteDecodeRadioPreferences(JsonObjectConst o,ELRSRadioPreferences &r) {return radio(o,r,true);}
bool remoteDecodeSettings(JsonObjectConst o,Settings &s,ELRSRadioPreferences &r,bool mqtt,RemoteTransferError &e) {
    const TextField *fields=mqtt ? mqttFields:mainFields; size_t count=mqtt ? sizeof(mqttFields)/sizeof(TextField):sizeof(mainFields)/sizeof(TextField);
    bool hasRadio=false;
#ifdef HAVE_CRSF
    hasRadio=!mqtt && o.containsKey("eRadio");
#endif
    if(o.isNull() || o.size()!=count+hasRadio) return remoteTransferFail(e,RemoteTransferCode::MissingSection,mqtt?"stores.mqtt":"stores.main","Every stored setting is required; unknown keys are rejected");
    for(size_t i=0;i<count;i++) {
        const auto &f=fields[i]; char *dest=(char*)&s+f.offset;
        if(!text(o[f.key],dest,f.bytes)) return remoteTransferFail(e,RemoteTransferCode::InvalidValue,f.key,"Invalid text or byte limit");
        if(f.minimum>=0) {
            bool digits=*dest && !(dest[0]=='0' && dest[1]);for(const char *p=dest;*p;p++)digits&=*p>='0'&&*p<='9';
            long n=strtol(dest,nullptr,10);if(!digits || n<f.minimum || n>f.maximum) return remoteTransferFail(e,RemoteTransferCode::InvalidValue,f.key,"Invalid stored numeric string");
        }
    }
    if(!mqtt) {
#ifdef HAVE_CRSF
        if(o.containsKey("eRadio")) {if(!radio(o["eRadio"],r,true)) return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"stores.main.eRadio","Invalid versioned radio preferences");}
        else r=elrsRadioPreferencesFromLegacy(atoi(s.elrsTlmRatio),atoi(s.elrsMaxPower),atoi(s.elrsDynPower));
#endif
    }
    return true;
}
bool remoteEncodeModel(JsonObject o,const RemoteSettingsSnapshot &s,RemoteTransferError &e) {
#ifndef HAVE_CRSF
    return remoteTransferFail(e,RemoteTransferCode::UnsupportedBehavior,"model","CRSF is not compiled");
#else
    if(!elrsIsValidCrsfSettingsBlob(s.crsf)) return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"model","Current model is invalid");
    o["format"]="remote-model"; o["schemaVersion"]=1; o["sourceContractVersion"]=2;
    auto provenance=o.createNestedObject("source"); provenance["firmware"]="Remote"; provenance["mapping"]="Remote physical inputs";
    auto inputs=o.createNestedArray("inputs");
    for(unsigned i=0;i<4;i++) {auto a=inputs.createNestedObject(); const auto &p=s.crsf.axisProfile[i]; a["reverse"]=(bool)p.reverse; a["deadband"]=p.deadband; a["expo"]=p.expo; auto limits=a.createNestedArray("limits"); limits.add(s.crsf.outputLimits[i].minimumUs); limits.add(s.crsf.outputLimits[i].maximumUs);}
    auto routing=o.createNestedObject("routing"); auto g=routing.createNestedArray("gimbals");
    g.add(s.crsf.gimbalRouting.aileronChannel); g.add(s.crsf.gimbalRouting.elevatorChannel); g.add(s.crsf.gimbalRouting.rudderChannel); g.add(s.crsf.gimbalRouting.throttleChannel);
    auto sw=routing.createNestedArray("switches"); for(uint8_t c:s.crsf.switchRouting.channels) sw.add(c);
    o.createNestedObject("tolerances")["throttleIdleDeadband"]=s.crsf.throttleIdleDeadband;
    auto actions=o.createNestedArray("localActions"); for(uint8_t c:s.crsf.localActions) actions.add(c);
    auto logic=o.createNestedArray("logical"); for(const auto &r:s.crsf.logic.rules) {auto a=logic.createNestedArray(); a.add(r.function); a.add(r.sourceA); a.add(r.sourceB); a.add(r.threshold); a.add(r.andSwitch);}
    auto mixes=o.createNestedArray("mixes"); for(const auto &r:s.crsf.mixes.lines) {auto a=mixes.createNestedArray(); a.add(r.channel); a.add(r.source); a.add(r.switchSource); a.add(r.weight); a.add(r.offset); a.add(r.multiplex);}
    auto d=o.createNestedObject("display"); d["source"]=s.crsf.displayConfig.source; d["decimalPlaces"]=s.crsf.displayConfig.decimalPlaces; encodeFloat(d,"multiplier",s.crsf.displayConfig.multiplier); encodeFloat(d,"offset",s.crsf.displayConfig.offset);
    auto v=o.createNestedObject("vehicle"); v["motorPoles"]=s.crsf.vehicleConfig.motorPoles; encodeFloat(v,"gearRatio",s.crsf.vehicleConfig.gearRatio); encodeFloat(v,"tireDiameterMm",s.crsf.vehicleConfig.tireDiameterMm); encodeFloat(v,"scaleFactor",s.crsf.vehicleConfig.scaleFactor); v["rpmType"]=s.crsf.vehicleConfig.rpmType;
#ifdef HAVE_CRSF
    o["speedUnits"]=s.settings.elrsSpdUnit[0]=='1'?"mph":"kmh";
    auto r=o.createNestedObject("radio"); encodeRadio(r,s.radio,false); const uint16_t rates[]={50,100,150,250,500}; r["crsfUpdateHz"]=rates[atoi(s.settings.elrsPktRate)];
    JsonArray required;for(unsigned i=0;i<s.rfChoiceCount;i++) {const auto &choice=s.rfChoices[i];if(!strcmp(choice.role,"power") || !strcmp(choice.role,"telemetry") || !strcmp(choice.role,"dynamic"))continue;
        if(required.isNull())required=r.createNestedArray("requirements");auto p=required.createNestedObject();p["role"]=choice.role;p["label"]=choice.label;p["unit"]=choice.unit;}
#endif
    return true;
#endif
}
static bool decodeModel(JsonObjectConst o,const RemoteSettingsSnapshot &target,RemoteSettingsSnapshot &s,RemoteTransferError &e) {
#ifndef HAVE_CRSF
    return remoteTransferFail(e,RemoteTransferCode::UnsupportedBehavior,"model","CRSF is not compiled");
#else
    if(!keys(o,"format|schemaVersion|sourceContractVersion|source|inputs|routing|tolerances|localActions|logical|mixes|display|vehicle|speedUnits|radio")) return remoteTransferFail(e,RemoteTransferCode::UnsupportedBehavior,"model","Unknown model section");
    int n;
    if(!integer(o["sourceContractVersion"],2,2,n)) return remoteTransferFail(e,RemoteTransferCode::UnsupportedSchema,"sourceContractVersion","Requires source contract 2");
    auto bad=[&e](const char *path){return remoteTransferFail(e,RemoteTransferCode::InvalidValue,path,"Missing, unsupported or invalid value");};
    JsonArrayConst a=o["inputs"]; if(a.size()!=4) return bad("inputs");
    for(unsigned i=0;i<4;i++) {JsonObjectConst x=a[i]; auto &p=s.crsf.axisProfile[i]; JsonArrayConst lim=x["limits"];
        if(!keys(x,"reverse|deadband|expo|limits") || !x["reverse"].is<bool>() || lim.size()!=2) return bad("inputs");
        p.reverse=x["reverse"].as<bool>(); if(!integer(x["deadband"],0,2047,n)) return bad("inputs.deadband"); p.deadband=n;
        if(!integer(x["expo"],0,100,n)) return bad("inputs.expo"); p.expo=n;
        if(!integer(lim[0],500,2500,n)) return bad("inputs.limits"); s.crsf.outputLimits[i].minimumUs=n;
        if(!integer(lim[1],500,2500,n)) return bad("inputs.limits"); s.crsf.outputLimits[i].maximumUs=n;
    }
    JsonObjectConst routing=o["routing"]; JsonArrayConst g=routing["gimbals"], sw=routing["switches"], actions=o["localActions"];
    if(!keys(routing,"gimbals|switches") || g.size()!=4 || sw.size()!=12 || actions.size()!=12) return bad("routing/localActions");
    uint8_t channels[4]; for(unsigned i=0;i<4;i++) {if(!integer(g[i],0,16,n)) return bad("routing.gimbals"); channels[i]=n;}
    s.crsf.gimbalRouting={channels[0],channels[1],channels[3],channels[2]};
    for(unsigned i=0;i<12;i++) {if(!integer(sw[i],0,16,n)) return bad("routing.switches"); s.crsf.switchRouting.channels[i]=n; if(!integer(actions[i],0,1,n)) return bad("localActions"); s.crsf.localActions[i]=n;}
    if(!keys(o["tolerances"],"throttleIdleDeadband") || !integer(o["tolerances"]["throttleIdleDeadband"],0,32,n)) return bad("tolerances"); s.crsf.throttleIdleDeadband=n;
    JsonArrayConst logic=o["logical"],mix=o["mixes"]; if(logic.size()!=16 || mix.size()!=16) return bad("logical/mixes");
    for(unsigned i=0;i<16;i++) {
        JsonArrayConst l=logic[i],m=mix[i]; if(l.size()!=5 || m.size()!=6) return bad("logical/mixes");
        auto &r=s.crsf.logic.rules[i]; auto &line=s.crsf.mixes.lines[i];
        if(!integer(l[0],0,5,n)) return bad("logical.function"); r.function=n;
        if(!integer(l[1],0,255,n)) return bad("logical.sourceA"); r.sourceA=n;
        if(!integer(l[2],0,255,n)) return bad("logical.sourceB"); r.sourceB=n;
        if(!integer(l[3],-100,100,n)) return bad("logical.threshold"); r.threshold=n;
        if(!integer(l[4],0,255,n)) return bad("logical.andSwitch"); r.andSwitch=n;
        if(!integer(m[0],0,16,n)) return bad("mixes.channel"); line.channel=n;
        if(!integer(m[1],0,255,n)) return bad("mixes.source"); line.source=n;
        if(!integer(m[2],0,255,n)) return bad("mixes.switchSource"); line.switchSource=n;
        if(!integer(m[3],-100,100,n)) return bad("mixes.weight"); line.weight=n;
        if(!integer(m[4],-100,100,n)) return bad("mixes.offset"); line.offset=n;
        if(!integer(m[5],0,2,n)) return bad("mixes.multiplex"); line.multiplex=n;
    }
    if(!elrsIsValidModelReferences(s.crsf.logic,s.crsf.mixes)) return remoteTransferFail(e,RemoteTransferCode::DanglingSource,"logical/mixes","Source is invalid, empty or forward referenced");
    JsonObjectConst d=o["display"],v=o["vehicle"];
    if(!keys(d,"source|decimalPlaces|multiplier|offset") || !keys(v,"motorPoles|gearRatio|tireDiameterMm|scaleFactor|rpmType")) return bad("display/vehicle");
    if(!integer(d["source"],0,16,n)) return bad("display.source"); s.crsf.displayConfig.source=n;
    if(!integer(d["decimalPlaces"],0,255,n)) return bad("display.decimalPlaces"); s.crsf.displayConfig.decimalPlaces=n;
    float f;
    if(!number(d["multiplier"],f)) return bad("display.multiplier"); s.crsf.displayConfig.multiplier=f;
    if(!number(d["offset"],f)) return bad("display.offset"); s.crsf.displayConfig.offset=f;
    if(!integer(v["motorPoles"],2,64,n)) return bad("vehicle.motorPoles"); s.crsf.vehicleConfig.motorPoles=n;
    if(!integer(v["rpmType"],0,1,n)) return bad("vehicle.rpmType"); s.crsf.vehicleConfig.rpmType=n;
    if(!number(v["gearRatio"],f)) return bad("vehicle.gearRatio"); s.crsf.vehicleConfig.gearRatio=f;
    if(!number(v["tireDiameterMm"],f)) return bad("vehicle.tireDiameterMm"); s.crsf.vehicleConfig.tireDiameterMm=f;
    if(!number(v["scaleFactor"],f)) return bad("vehicle.scaleFactor"); s.crsf.vehicleConfig.scaleFactor=f;
    if(!elrsIsValidCrsfSettingsBlob(s.crsf)) return bad("inputs/routing/display/vehicle");
    if(!target.newBoard) return remoteTransferFail(e,RemoteTransferCode::UnsupportedBehavior,"inputs","Target has no CRSF inputs");
#ifdef HAVE_CRSF
    const char *units=o["speedUnits"]; if(!units || (strcmp(units,"mph") && strcmp(units,"kmh"))) return bad("speedUnits"); strcpy(s.settings.elrsSpdUnit,strcmp(units,"mph")?"0":"1");
    JsonObjectConst r=o["radio"]; if(!radio(r,s.radio,false) || !integer(r["crsfUpdateHz"],50,500,n) || !elrsPacketRateSupported(n)) return bad("radio");
    const int rates[]={50,100,150,250,500}; for(int i=0;i<5;i++) if(n==rates[i]) {s.settings.elrsPktRate[0]='0'+i; s.settings.elrsPktRate[1]=0;}
    // Required RF behavior needs a separately obtained, current capability proof.
    if(r.containsKey("requirements")) {
        JsonArrayConst requirements=r["requirements"];
        if(requirements.isNull() || requirements.size()>8) return bad("radio.requirements");
        uint8_t used=0;
        for(JsonObjectConst required:requirements) {
            RemoteRfChoice choice;
            if(!keys(required,"role|label|unit") || !text(required["role"],choice.role,sizeof(choice.role)) || !text(required["label"],choice.label,sizeof(choice.label)) || !text(required["unit"],choice.unit,sizeof(choice.unit))) return bad("radio.requirements");
            bool found=false;
            for(unsigned i=0;i<target.rfChoiceCount;i++) {
                const auto &p=target.rfChoices[i];
                if(!strcmp(p.role,choice.role) && !strcmp(p.label,choice.label) && !strcmp(p.unit,choice.unit) && p.descriptorHash && !(used&(1<<i))) {found=true;used|=1<<i;break;}
            }
            if(!found || !target.deviceGeneration || !target.capabilityFingerprint) return remoteTransferFail(e,RemoteTransferCode::UnsupportedBehavior,"radio.requirements","Required RF behavior is not proven active in current live descriptors; apply it separately and preview again");
        }
    }
#endif
    s.stores[0].state=s.stores[6].state=RemoteStoreState::Present;
    return true;
#endif
}
bool remoteSerializeStore(const RemoteSettingsSnapshot &s,uint8_t id,uint8_t *out,size_t cap,size_t &len,RemoteTransferError &e,RemoteSettingsEncoding encoding) {
    len=0; if(id>=7) return false; if(s.stores[id].state!=RemoteStoreState::Present) return true;
    if(id<2) {DynamicJsonDocument doc(4096); remoteEncodeSettings(doc.to<JsonObject>(),s.settings,s.radio,id==1,encoding,s.newBoard); len=measureJson(doc); if(doc.overflowed() || len>cap) return remoteTransferFail(e,RemoteTransferCode::TooLarge,remoteStoreName(id),"Store exceeds serialization bound"); return serializeJson(doc,out,cap)==len;}
    const void *payload=nullptr; size_t size=0;
    switch(id) {case 2:payload=&s.ip;size=sizeof(s.ip);break;case 3:payload=&s.identity;size=4;break;case 4:payload=&s.secondary;size=20;break;case 5:payload=&s.tertiary;size=4;break;case 6:payload=&s.crsf;size=297;break;}
    len=size+3; if(cap<len) return false; out[0]=size&255;out[1]=size>>8;memcpy(out+2,payload,size);out[len-1]=remoteFrameChecksum(out,len-1);return true;
}
bool remoteEncodeBackup(JsonObject o,const RemoteSettingsSnapshot &s,RemoteTransferError &e) {
    o["format"]="remote-device-backup";o["schemaVersion"]=1;
    auto source=o.createNestedObject("source");source["firmware"]="Remote";source["board"]="esp32-remote";source["remoteId"]=s.identity;
    auto features=source.createNestedObject("features"); features["crsf"]=s.newBoard;
#ifdef HAVE_MQTT
    features["mqtt"]=true;
#else
    features["mqtt"]=false;
#endif
    auto policy=o.createNestedObject("storage");policy["flashRO"]=s.flashRO;policy["cfgOnSD"]=s.settings.CfgOnSD[0]!='0';policy["sdPresent"]=s.sdAvailable;
    o["excluded"]="Firmware, audio and other media assets; external radio settings use a separate file";
    auto stores=o.createNestedArray("stores"); uint8_t *bytes=(uint8_t*)malloc(16384);if(!bytes) return remoteTransferFail(e,RemoteTransferCode::TooLarge,"stores","No serialization memory");
    bool ok=true;
    for(uint8_t i=0;i<7 && ok;i++) {
        auto entry=stores.createNestedObject();entry["id"]=remoteStoreName(i);entry["state"]=s.stores[i].state==RemoteStoreState::Present?"present":s.stores[i].state==RemoteStoreState::Disabled?"disabled":"absent";entry["storageVersion"]=i==6?2:1;entry["encoding"]=i<2?"json":"base64";
        size_t len=0; ok=remoteSerializeStore(s,i,bytes,i==0?8192:16384,len,e);char hash[65];remoteSha256(bytes,len,hash);entry["byteLength"]=len;entry["sha256"]=hash;
        if(s.stores[i].state==RemoteStoreState::Present) {if(i<2) remoteEncodeSettings(entry.createNestedObject("data"),s.settings,s.radio,i==1);else {char b64[401];encode64(bytes,len,b64);entry["data"]=b64;}}
    }
    free(bytes);return ok;
}
static bool decodeBackup(JsonObjectConst o,const RemoteSettingsSnapshot &target,RemoteSettingsSnapshot &s,RemoteTransferError &e,bool clone) {
    if(!keys(o,"format|schemaVersion|source|storage|stores|excluded|external")) return remoteTransferFail(e,RemoteTransferCode::UnsupportedBehavior,"backup","Unknown backup section");
    JsonObjectConst src=o["source"],policy=o["storage"];const char *board=src["board"];
    if(!board || strcmp(board,"esp32-remote") || !src["features"]["crsf"].is<bool>() || src["features"]["crsf"].as<bool>()!=target.newBoard) return remoteTransferFail(e,RemoteTransferCode::UnsupportedBehavior,"source.features","Target board/features differ");
#ifdef HAVE_MQTT
    if(src["features"]["mqtt"]!=true) return remoteTransferFail(e,RemoteTransferCode::UnsupportedBehavior,"source.features.mqtt","MQTT build differs");
#else
    if(src["features"]["mqtt"]!=false) return remoteTransferFail(e,RemoteTransferCode::UnsupportedBehavior,"source.features.mqtt","MQTT build differs");
#endif
    if(!policy["flashRO"].is<bool>() || policy["flashRO"].as<bool>()!=target.flashRO) return remoteTransferFail(e,RemoteTransferCode::UnsupportedBehavior,"storage.flashRO","Storage policy differs; explicit remapping required");
    JsonArrayConst entries=o["stores"];if(entries.size()!=7) return remoteTransferFail(e,RemoteTransferCode::MissingSection,"stores","All seven stores are required");
    bool seen[7]={};uint8_t bytes[300];
    for(JsonObjectConst entry:entries) {
        if(!keys(entry,"id|state|storageVersion|byteLength|sha256|encoding|data")) return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"stores","Unknown store metadata");
        const char *id=entry["id"],*state=entry["state"],*encoding=entry["encoding"];uint8_t i=0;while(i<7 && (!id || strcmp(id,remoteStoreName(i)))) ++i;
        if(i==7 || seen[i] || !state || !encoding || strcmp(encoding,i<2?"json":"base64")) return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"stores","Unknown or duplicate store/encoding");seen[i]=true;
        int version,len; if(!integer(entry["storageVersion"],1,i==6?2:1,version) || !integer(entry["byteLength"],0,i==0?8192:i==1?16384:300,len)) return remoteTransferFail(e,RemoteTransferCode::InvalidValue,id,"Invalid length/version");
        if(!strcmp(state,"present")) s.stores[i].state=RemoteStoreState::Present; else if(!strcmp(state,"absent")) s.stores[i].state=RemoteStoreState::Absent; else if(!strcmp(state,"disabled")) s.stores[i].state=RemoteStoreState::Disabled; else return remoteTransferFail(e,RemoteTransferCode::InvalidValue,id,"Unknown presence state");
        char hash[65]; if(!text(entry["sha256"],hash,sizeof(hash)) || strlen(hash)!=64) return remoteTransferFail(e,RemoteTransferCode::InvalidValue,id,"Invalid hash");
        if(s.stores[i].state!=RemoteStoreState::Present) {char empty[65];remoteSha256(nullptr,0,empty);if(len || entry.containsKey("data") || strcmp(hash,empty)) return remoteTransferFail(e,RemoteTransferCode::InvalidValue,id,"Absent store has payload");continue;}
        if(i<2) {
            if(!remoteDecodeSettings(entry["data"],s.settings,s.radio,i==1,e)) return false;
            uint8_t *canonical=(uint8_t*)malloc(i?16384:8192); if(!canonical) return remoteTransferFail(e,RemoteTransferCode::TooLarge,id,"No serialization memory");
            size_t n=0;bool ok=remoteSerializeStore(s,i,canonical,i?16384:8192,n,e);char actual[65];remoteSha256(canonical,n,actual);free(canonical);if(!ok || n!=(size_t)len || strcmp(hash,actual)) return remoteTransferFail(e,RemoteTransferCode::InvalidValue,id,"Canonical length/hash mismatch");
        } else {
            size_t n=0;char actual[65];if(!decode64(entry["data"],bytes,sizeof(bytes),n) || n!=(size_t)len || n<3 || (size_t)(bytes[0]|bytes[1]<<8)!=n-3 || remoteFrameChecksum(bytes,n-1)!=bytes[n-1]) return remoteTransferFail(e,RemoteTransferCode::InvalidValue,id,"Invalid binary payload");
            remoteSha256(bytes,n,actual);if(strcmp(actual,hash)) return remoteTransferFail(e,RemoteTransferCode::InvalidValue,id,"Binary hash mismatch");
            const size_t sizes[]={0,0,80,4,20,4,297};if(i!=6 && n!=sizes[i]+3) return remoteTransferFail(e,RemoteTransferCode::UnsupportedSchema,id,"Unsupported binary layout");
            switch(i) {case 2:memcpy(&s.ip,bytes+2,80);break;case 3:memcpy(&s.identity,bytes+2,4);break;case 4:memcpy(&s.secondary,bytes+2,20);break;case 5:memcpy(&s.tertiary,bytes+2,4);break;case 6:if(!remoteDecodeCrsf(bytes,n,s.crsf,e)) return false;break;}
        }
    }
    if(s.stores[0].state!=RemoteStoreState::Present || s.stores[3].state!=RemoteStoreState::Present || (target.newBoard && s.stores[6].state!=RemoteStoreState::Present) || (!target.newBoard && s.stores[6].state!=RemoteStoreState::Disabled)) return remoteTransferFail(e,RemoteTransferCode::MissingSection,"stores","Required effective stores are missing");
#ifdef HAVE_MQTT
    if(s.stores[1].state!=RemoteStoreState::Present) return remoteTransferFail(e,RemoteTransferCode::MissingSection,"stores.mqtt","Effective MQTT settings required");
#else
    if(s.stores[1].state!=RemoteStoreState::Disabled) return remoteTransferFail(e,RemoteTransferCode::UnsupportedBehavior,"stores.mqtt","MQTT is not compiled");
#endif
    if(s.stores[2].state==RemoteStoreState::Absent) s.ip={};
    if(s.identity!=target.identity && !clone) return remoteTransferFail(e,RemoteTransferCode::IdentityChoiceRequired,"stores.identity","Explicitly choose identity cloning on a different Remote");
    if(s.identity!=src["remoteId"].as<uint32_t>() || s.secondary.brightness>15 || s.secondary.curVolume>20 || s.secondary.movieMode>1 || s.secondary.displayTCDSMode>1 || s.secondary.showUpdAvail>1 || s.secondary.carMode>1 || s.tertiary.musFolderNum>9 || s.tertiary.mpShuffle>1) return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"stores","Invalid identity or secondary/tertiary values");
    for(const char *p:{s.ip.ip,s.ip.gateway,s.ip.netmask,s.ip.dns}) if(!memchr(p,0,20)) return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"stores.ip","Unterminated IP setting");
    return true;
}
bool remoteDecodeTransfer(JsonVariantConst root,RemoteTransferKind kind,const RemoteSettingsSnapshot &target,RemoteSettingsSnapshot &candidate,RemoteTransferError &e,bool clone) {
    int n;const char *format=root["format"];
    if(!format || strcmp(format,kind==RemoteTransferKind::DeviceBackup?"remote-device-backup":"remote-model") || !integer(root["schemaVersion"],1,1,n)) return remoteTransferFail(e,RemoteTransferCode::UnsupportedSchema,"schemaVersion","Unknown format/schema version");
    candidate=target;e={};
    return kind==RemoteTransferKind::ModelProfile ? decodeModel(root.as<JsonObjectConst>(),target,candidate,e) : decodeBackup(root.as<JsonObjectConst>(),target,candidate,e,clone);
}
