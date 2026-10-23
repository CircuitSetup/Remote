#include "crsf_model_files.h"
#ifdef HAVE_CRSF
#include <cstring>
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <mbedtls/sha256.h>
#endif
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
        elrsIsValidVehicleConfig(s.vehicleConfig) && elrsIsValidModelReferences(s.logic,s.mixes) &&
        elrsIsValidTimeTravelConfig(s.timeTravel,s.gimbalRouting,s.mixes);
#endif
}
namespace {
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
    auto tt=o.createNestedObject("timeTravel"); tt["enabled"]=bool(s.crsf.timeTravel.enabled); tt["channel"]=s.crsf.timeTravel.channel;
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
    if(!keys(o,"format|schemaVersion|sourceContractVersion|source|inputs|routing|tolerances|localActions|logical|mixes|display|vehicle|speedUnits|radio|timeTravel")) return remoteTransferFail(e,RemoteTransferCode::UnsupportedBehavior,"model","Unknown model section");
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
    s.crsf.timeTravel = {};
    if(o.containsKey("timeTravel")) {
        JsonObjectConst tt=o["timeTravel"];
        if(!keys(tt,"enabled|channel") || !tt["enabled"].is<bool>() || !integer(tt["channel"],0,16,n)) return bad("timeTravel");
        s.crsf.timeTravel = {(uint8_t)tt["enabled"].as<bool>(),(uint8_t)n};
        if(!elrsIsValidTimeTravelConfig(s.crsf.timeTravel,s.crsf.gimbalRouting,s.crsf.mixes)) return bad("timeTravel.channel");
    }
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
    return true;
#endif
}
bool remoteEncodeBackup(JsonObject o,const RemoteSettingsSnapshot &s,RemoteTransferError &e) {
    o["format"]="remote-crsf-backup";o["schemaVersion"]=1;
    auto source=o.createNestedObject("source");source["firmware"]="Remote";source["remoteId"]=s.identity;
    if(!remoteEncodeModel(o.createNestedObject("model"),s,e))return false;
    auto calibration=o.createNestedArray("calibration");for(const auto &p:s.crsf.axisProfile){auto a=calibration.createNestedArray();a.add(p.minimum);a.add(p.center);a.add(p.maximum);}
    o["adcHysteresis"]=s.crsf.adcHysteresis;o["opMode"]=atoi(s.settings.opMode);o["connectWiFi"]=s.settings.crsfap[0]!='0';return true;
}
bool remoteDecodeTransfer(JsonVariantConst root,RemoteTransferKind kind,const RemoteSettingsSnapshot &target,RemoteSettingsSnapshot &candidate,RemoteTransferError &e) {
    candidate=target;JsonObjectConst o=root.as<JsonObjectConst>();int n;
    const char *format=o["format"];
    if(!format || strcmp(format,kind==RemoteTransferKind::ModelProfile?"remote-model":"remote-crsf-backup") || !integer(o["schemaVersion"],1,1,n))return remoteTransferFail(e,RemoteTransferCode::UnsupportedSchema,"format","Choose a supported CRSF backup or portable model");
    if(kind==RemoteTransferKind::ModelProfile)return decodeModel(o,target,candidate,e);
    if(!keys(o,"format|schemaVersion|source|model|calibration|adcHysteresis|opMode|connectWiFi"))return remoteTransferFail(e,RemoteTransferCode::UnsupportedBehavior,"backup","Unsupported CRSF backup section");
    JsonObjectConst source=o["source"];if(!keys(source,"firmware|remoteId") || !source["firmware"].is<const char *>() || !source["remoteId"].is<uint32_t>())return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"source","Invalid backup source");
    if(!decodeModel(o["model"],target,candidate,e))return false;
    JsonArrayConst cal=o["calibration"];if(cal.size()!=4)return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"calibration","Four complete raw-axis calibrations required");
    for(unsigned i=0;i<4;i++){JsonArrayConst a=cal[i];if(a.size()!=3 || !integer(a[0],-32768,32767,n))return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"calibration","Invalid calibration");candidate.crsf.axisProfile[i].minimum=n;
        if(!integer(a[1],-32768,32767,n))return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"calibration","Invalid calibration center");candidate.crsf.axisProfile[i].center=n;if(!integer(a[2],-32768,32767,n))return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"calibration","Invalid calibration maximum");candidate.crsf.axisProfile[i].maximum=n;}
    if(!integer(o["adcHysteresis"],0,32,n))return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"adcHysteresis","Invalid input filtering");candidate.crsf.adcHysteresis=n;
    if(!integer(o["opMode"],0,2,n) || !o["connectWiFi"].is<bool>())return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"opMode","Invalid CRSF portal settings");candidate.settings.opMode[0]='0'+n;candidate.settings.opMode[1]=0;candidate.settings.crsfap[0]=o["connectWiFi"].as<bool>()?'1':'0';candidate.settings.crsfap[1]=0;
    if(!elrsIsValidCrsfSettingsBlob(candidate.crsf))return remoteTransferFail(e,RemoteTransferCode::InvalidValue,"calibration","Invalid calibration, routing or model references");return true;
}

#endif
