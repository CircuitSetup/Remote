"""Check CRSF-owned portal routes, session gates, and bounded request handling."""
import re
import unittest
from pathlib import Path

from check_crsf_adc import compile_and_run

ROOT = Path(__file__).resolve().parents[1]
PORTAL = (ROOT / "src/src/CRSF/crsf_config_portal.h").read_text(encoding="utf-8")
FILES = (ROOT / "src/src/CRSF/crsf_model_files.cpp").read_text(encoding="utf-8")


def function(source, signature):
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
                return source[match.start():offset + 1]
    raise AssertionError(f"unterminated function matching {signature}")


class CRSFPortalHTTPTests(unittest.TestCase):
    def test_routes_keep_expected_methods_and_callbacks(self):
        setup = function(PORTAL, r"static void remoteSetupPortal\(\)")
        routes = re.findall(r'->on\("([^\"]+)",\s*(HTTP_(?:GET|POST)),\s*', setup)
        self.assertEqual(routes, [(path, method) for path, method in (
            ("/remote-control", "HTTP_GET"), ("/remote/ui.js", "HTTP_GET"),
            ("/remote/session", "HTTP_POST"), ("/remote/devices", "HTTP_GET"),
            ("/remote/operation", "HTTP_GET"), ("/remote/action", "HTTP_POST"),
            ("/remote/upload", "HTTP_POST"), ("/remote/export", "HTTP_GET"),
            ("/remote/apply", "HTTP_POST"), ("/remote/review", "HTTP_POST"))])
        for path, method, callback in (
            ("/remote/session", "HTTP_POST", "remotePortalSession"),
            ("/remote/devices", "HTTP_GET", "remotePortalDevices"),
            ("/remote/operation", "HTTP_GET", "remotePortalOperation"),
            ("/remote/action", "HTTP_POST", "remotePortalAction"),
            ("/remote/upload", "HTTP_POST", "remotePortalUploadDone,remotePortalUpload"),
            ("/remote/export", "HTTP_GET", "remotePortalExport"),
            ("/remote/apply", "HTTP_POST", "remotePortalApply"),
            ("/remote/review", "HTTP_POST", "remotePortalReview"),
        ):
            self.assertIn(f'->on("{path}",{method},{callback}', setup)
        self.assertIn('const char *page="/param3#remote-controls"', setup)
        self.assertIn('sendHeader("Content-Encoding","gzip")', setup)
        self.assertIn('"Content-Encoding","gzip"', setup)

    def test_remote_routes_share_connected_network_gate(self):
        gate = function(PORTAL, r"static bool remotePortalGate\(\)")
        for check in ("remotePortalActiveNetwork()", "remotePortalOrigin()", "remotePortalToken()"):
            self.assertIn(check, gate)
        self.assertNotIn("apOnly", gate)
        self.assertNotIn("settings.appw", PORTAL)
        for name in ("Export", "Apply", "Review", "Operation", "Action"):
            self.assertIn('if(!remotePortalGate())return', function(PORTAL, rf"static void remotePortal{name}\(\)"))
        self.assertIn('doc["throttleReady"]=elrsMode.throttleReady()', function(PORTAL, r"static void remotePortalDevices\(\)"))
        for check in ("remotePortalActiveNetwork()", "remotePortalOrigin()"):
            self.assertIn(check, function(PORTAL, r"static void remotePortalSession\(\)"))
        self.assertIn('doc["localAP"]=remotePortalLocalAP()', function(PORTAL, r"static void remotePortalDevices\(\)"))
        upload = function(PORTAL, r"static void remotePortalUpload\(\)")
        self.assertIn("if(!remotePortalGate())return", upload)
        upload_done = function(PORTAL, r"static void remotePortalUploadDone\(\)")
        self.assertIn("if(!remotePortalGate())return", upload_done)

    def test_radio_mutations_are_bounded_and_require_explicit_confirmation(self):
        action = function(PORTAL, r"static void remotePortalAction\(\)")
        self.assertIn('if(!opModeCRSF)', action)
        self.assertIn('action=="write"', action)
        for field in ('"device",255', '"field",255', '"generation",UINT32_MAX', '"confirmed",1', '"type",9', '"status",5', '"modelId",63'):
            self.assertIn(field, action)
        self.assertIn('"expected"', action)
        self.assertIn('"value"', action)
        self.assertIn('"operationId"', action)

    def test_upload_and_apply_fail_closed_on_size_write_preview_or_save_errors(self):
        upload = function(PORTAL, r"static void remotePortalUpload\(\)")
        for contract in ('++remotePortalUploadFiles!=1', 'REMOTE_BACKUP_MAX_BYTES', 'REMOTE_MODEL_MAX_BYTES',
                         'remotePortalUploadFile.write', 'remotePortalUploadFile.size()==remotePortalUploadBytes'):
            self.assertIn(contract, upload)
        done = function(PORTAL, r"static void remotePortalUploadDone\(\)")
        self.assertIn('if(!remotePortalUploadOk)', done)
        self.assertIn('validateRemoteTransfer(', done)
        export = function(PORTAL, r"static void remotePortalExport\(\)")
        self.assertIn('kind!="model"&&kind!="backup"', export)
        self.assertIn('remote.crsf-backup.json', export)
        apply = function(PORTAL, r"static void remotePortalApply\(\)")
        self.assertIn('!remotePortalPreview.valid', apply)
        self.assertIn('applyRemoteTransfer(remotePortalPreview,e)', apply)
        self.assertIn('remotePortalError(e,409)', apply)
        self.assertIn('esp_restart()', apply)

    def test_backup_is_crsf_scoped_and_live_save_errors_are_visible(self):
        self.assertIn('"remote-crsf-backup"', FILES)
        self.assertNotIn('"remote-backup"', FILES)
        self.assertNotIn('credentials', FILES)
        self.assertNotIn('cloneIdentity', FILES)
        review = function(PORTAL, r"static void remotePortalReview\(\)")
        self.assertIn('!remoteCompleteRadioReview()', review)
        self.assertIn('"RADIO_REVIEW_REQUIRED"', review)

    def test_actual_origin_nonce_and_numeric_gates(self):
        signatures = (
            r"static bool remotePortalLocalAP\(\)",
            r"static bool remotePortalActiveNetwork\(\)",
            r"static bool remotePortalOrigin\(\)",
            r"static bool remotePortalToken\(bool expiry=true\)",
            r"static bool remotePortalGate\(\)",
            r"static bool remotePortalUnsigned\(const char \*name,uint32_t max,uint32_t &value\)",
            r"static bool remotePortalSigned\(const char \*name,int32_t &value\)",
        )
        production = "\n".join(function(PORTAL, signature) for signature in signatures)
        harness = r'''#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
class String {
 std::string v;
public:
 String()=default;String(const char*s):v(s?s:""){}String(std::string s):v(std::move(s)){}
 size_t length()const{return v.size();}bool isEmpty()const{return v.empty();}const char*c_str()const{return v.c_str();}
 char operator[](size_t i)const{return v[i];}void remove(size_t i){v.erase(i);}
 bool endsWith(const char*s)const{return v.size()>=strlen(s)&&v.compare(v.size()-strlen(s),strlen(s),s)==0;}
 bool startsWith(const String&s)const{return v.rfind(s.v,0)==0;}
 friend String operator+(const String&a,const String&b){return a.v+b.v;}friend String operator+(const String&a,const char*b){return a.v+b;}
 friend bool operator==(const String&a,const String&b){return a.v==b.v;}friend bool operator==(const String&a,const char*b){return a.v==b;}friend bool operator!=(const String&a,const String&b){return !(a==b);}
};
class IPAddress{uint8_t a[4]{};public:IPAddress()=default;IPAddress(int x,int y,int z,int w):a{(uint8_t)x,(uint8_t)y,(uint8_t)z,(uint8_t)w}{}uint8_t operator[](int i)const{return a[i];}String toString()const{return std::to_string(a[0])+"."+std::to_string(a[1])+"."+std::to_string(a[2])+"."+std::to_string(a[3]);}friend bool operator==(const IPAddress&x,const IPAddress&y){return !memcmp(x.a,y.a,4);}friend bool operator!=(const IPAddress&x,const IPAddress&y){return !(x==y);}};
struct Client{IPAddress local{192,168,4,1},peer{192,168,4,22};IPAddress localIP()const{return local;}IPAddress remoteIP()const{return peer;}};
constexpr int HTTP_GET=0;int requestMethod=HTTP_GET;
#define WL_CONNECTED 3
struct Server{Client c;std::map<std::string,std::string>headers,args;Client&client(){return c;}String header(const char*n){return headers.count(n)?String(headers[n]):String();}String hostHeader(){return "192.168.4.1";}String arg(const char*n){return args.count(n)?String(args[n]):String();}int method()const{return requestMethod;}} server;
 struct Settings{char hostName[32]="remote";}settings;
struct Wifi{IPAddress softAPIP()const{return {192,168,4,1};}IPAddress localIP()const{return {192,168,4,1};}int status()const{return WL_CONNECTED;}}WiFi;
struct Manager{Server*server=&::server;}wm;
char remotePortalNonce[33]="0123456789abcdef0123456789abcdef";IPAddress remotePortalOwner{192,168,4,22};uint32_t remotePortalExpires=2000,now=1000;
unsigned long millis(){return now;}int gateError=0;void remotePortalError(int code,const char*,const char*,const char*){gateError=code;}
'''
        harness += production + r'''
int main(){
 uint32_t u=0;int32_t s=0;
 server.headers["Origin"]="http://192.168.4.1";server.headers["X-Remote-Nonce"]=remotePortalNonce;
 assert(remotePortalLocalAP()&&remotePortalOrigin()&&remotePortalToken()&&remotePortalGate());
 server.headers["Origin"]="http://192.168.4.1.evil";assert(!remotePortalGate()&&gateError==403);
 server.headers["Origin"]="http://192.168.4.1";server.c.peer={192,168,5,22};assert(!remotePortalLocalAP()&&!remotePortalGate());server.c.peer={192,168,4,22};
 server.headers["X-Remote-Nonce"]="00000000000000000000000000000000";assert(!remotePortalToken());server.headers["X-Remote-Nonce"]=remotePortalNonce;
 now=remotePortalExpires;assert(!remotePortalToken());now=1000;
 server.args["n"]="63";assert(remotePortalUnsigned("n",63,u)&&u==63);server.args["n"]="+1";assert(!remotePortalUnsigned("n",63,u));server.args["n"]="64";assert(!remotePortalUnsigned("n",63,u));
 server.args["n"]="-2147483648";assert(remotePortalSigned("n",s)&&s==INT32_MIN);server.args["n"]="2147483648";assert(!remotePortalSigned("n",s));server.args["n"]=" 1";assert(!remotePortalSigned("n",s));
}
'''
        compile_and_run(harness, [], [])

    def test_default_gate_accepts_connected_station_and_rejects_invalid_session(self):
        signatures = (
            r"static bool remotePortalLocalAP\(\)",
            r"static bool remotePortalActiveNetwork\(\)",
            r"static bool remotePortalOrigin\(\)",
            r"static bool remotePortalToken\(bool expiry=true\)",
            r"static bool remotePortalGate\(\)",
        )
        production = "\n".join(function(PORTAL, signature) for signature in signatures)
        harness = r'''#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#undef assert
#define assert(condition) do { if(!(condition)) return __LINE__; } while(0)
class String {
 std::string v;
public:
 String()=default;String(const char*s):v(s?s:""){}String(std::string s):v(std::move(s)){}
 size_t length()const{return v.size();}bool isEmpty()const{return v.empty();}const char*c_str()const{return v.c_str();}
 char operator[](size_t i)const{return v[i];}void remove(size_t i){v.erase(i);}
 bool endsWith(const char*s)const{return v.size()>=strlen(s)&&v.compare(v.size()-strlen(s),strlen(s),s)==0;}
 bool startsWith(const String&s)const{return v.rfind(s.v,0)==0;}
 friend String operator+(const String&a,const String&b){return a.v+b.v;}friend String operator+(const String&a,const char*b){return a.v+b;}
 friend bool operator==(const String&a,const String&b){return a.v==b.v;}friend bool operator==(const String&a,const char*b){return a.v==b;}friend bool operator!=(const String&a,const String&b){return !(a==b);}
};
class IPAddress{uint8_t a[4]{};public:IPAddress()=default;IPAddress(int x,int y,int z,int w):a{(uint8_t)x,(uint8_t)y,(uint8_t)z,(uint8_t)w}{}uint8_t operator[](int i)const{return a[i];}String toString()const{return std::to_string(a[0])+"."+std::to_string(a[1])+"."+std::to_string(a[2])+"."+std::to_string(a[3]);}friend bool operator==(const IPAddress&x,const IPAddress&y){return !memcmp(x.a,y.a,4);}friend bool operator!=(const IPAddress&x,const IPAddress&y){return !(x==y);}};
struct Client{IPAddress local{192,168,4,1},peer{192,168,4,22};IPAddress localIP()const{return local;}IPAddress remoteIP()const{return peer;}};
constexpr int HTTP_GET=0;int requestMethod=HTTP_GET;
#define WL_CONNECTED 3
struct Server{Client c;std::map<std::string,std::string>headers;std::string host="192.168.4.1";Client&client(){return c;}String header(const char*n){return headers.count(n)?String(headers[n]):String();}String hostHeader(){return host;}int method()const{return requestMethod;}} server;
struct Settings{char hostName[32]="remote";}settings;
struct Wifi{IPAddress ap{192,168,4,1},sta{10,0,0,8};bool connected=false;IPAddress softAPIP()const{return ap;}IPAddress localIP()const{return sta;}int status()const{return connected?3:0;}}WiFi;
struct Manager{Server*server=&::server;}wm;
char remotePortalNonce[33]="0123456789abcdef0123456789abcdef";IPAddress remotePortalOwner{10,0,0,22};uint32_t remotePortalExpires=2000,now=1000;
unsigned long millis(){return now;}int gateError=0;void remotePortalError(int code,const char*,const char*,const char*){gateError=code;}
'''
        harness += production + r'''
int main(){
 server.c.local={10,0,0,8};server.c.peer={10,0,0,22};server.host="10.0.0.8";server.headers["Origin"]="http://10.0.0.8";server.headers["X-Remote-Nonce"]=remotePortalNonce;WiFi.connected=true;
 assert(!remotePortalLocalAP()&&remotePortalActiveNetwork()&&remotePortalOrigin());
 assert(remotePortalToken()&&remotePortalGate());
 server.c.local={10,0,0,9};assert(!remotePortalActiveNetwork()&&!remotePortalGate());server.c.local={10,0,0,8};
 server.host="10.0.0.8.evil";server.headers["Origin"]="http://10.0.0.8.evil";assert(!remotePortalOrigin()&&!remotePortalGate());
 server.host="10.0.0.8";server.headers["Origin"]="http://10.0.0.8.evil";assert(!remotePortalOrigin()&&!remotePortalGate());
 server.headers["Origin"]="http://10.0.0.8";WiFi.connected=false;assert(!remotePortalActiveNetwork()&&!remotePortalGate());WiFi.connected=true;
 server.headers["X-Remote-Nonce"]="00000000000000000000000000000000";assert(!remotePortalGate());server.headers["X-Remote-Nonce"]=remotePortalNonce;server.headers.erase("X-Remote-Nonce");assert(!remotePortalGate());server.headers["X-Remote-Nonce"]=remotePortalNonce;
 server.c.peer={10,0,0,23};assert(!remotePortalToken()&&!remotePortalGate());server.c.peer={10,0,0,22};now=remotePortalExpires;assert(!remotePortalToken()&&!remotePortalGate());
 // The same default gate accepts a valid Remote AP connection.
 now=1000;server.c.local={192,168,4,1};server.c.peer={192,168,4,22};server.host="remote.local";server.headers["Origin"]="http://remote.local";remotePortalOwner={192,168,4,22};
 assert(remotePortalLocalAP()&&remotePortalActiveNetwork()&&remotePortalOrigin()&&remotePortalGate());
}
'''
        compile_and_run(harness, [], [])

    def test_actual_apply_callback_never_restarts_on_failure_and_reboots_after_response(self):
        apply = function(PORTAL, r"static void remotePortalApply\(\)")
        unsigned = function(PORTAL, r"static bool remotePortalUnsigned\(const char \*name,uint32_t max,uint32_t &value\)")
        harness = r'''#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
class String{std::string v;public:String()=default;String(const char*s):v(s?s:""){}String(std::string s):v(std::move(s)){}size_t length()const{return v.size();}char operator[](size_t i)const{return v[i];}const char*c_str()const{return v.c_str();}friend bool operator!=(const String&a,const String&b){return a.v!=b.v;}};
struct Server{std::map<std::string,std::string>args;String arg(const char*n){return args.count(n)?String(args[n]):String();}}server;struct Manager{Server*server=&::server;}wm;
struct Preview{bool valid=true;char fileHash[65]="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";}remotePortalPreview;
struct RemoteTransferError{};int response=0,gate=true,applyPass=true,restarts=0,prepareCalls=0,applyCalls=0;int events[4]={},eventCount=0;
struct JsonProxy{void operator=(bool){}};template<size_t N>struct StaticJsonDocument{JsonProxy operator[](const char*){return {};}};
bool remotePortalGate(){return gate;}void remotePortalError(int code,const char*,const char*,const char*){response=code;}void remotePortalError(const RemoteTransferError&,int code){response=code;}
bool applyRemoteTransfer(const Preview&,RemoteTransferError&){applyCalls++;return applyPass;}
template<class T>void remotePortalJson(T&){response=200;events[eventCount++]=1;}void remotePortalService(){events[eventCount++]=4;}void prepareReboot(){prepareCalls++;events[eventCount++]=2;}void esp_restart(){restarts++;events[eventCount++]=3;}
'''
        harness += unsigned + apply + r'''
int main(){
 uint32_t confirmed=1;server.args["confirmed"]="1";server.args["fileHash"]=remotePortalPreview.fileHash;
 gate=false;remotePortalApply();assert(response==0&&restarts==0&&applyCalls==0);gate=true;
 server.args["fileHash"]="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";remotePortalApply();assert(response==409&&restarts==0&&applyCalls==0);
 server.args["fileHash"]=remotePortalPreview.fileHash;applyPass=false;remotePortalApply();assert(response==409&&restarts==0&&applyCalls==1&&!remotePortalPreview.valid);
 remotePortalPreview.valid=true;applyPass=true;response=0;eventCount=0;remotePortalApply();
 assert(response==200&&applyCalls==2&&prepareCalls==1&&restarts==1);
 int sent=-1,prepared=-1,restarted=-1;for(int i=0;i<eventCount;i++){if(events[i]==1)sent=i;if(events[i]==2)prepared=i;if(events[i]==3)restarted=i;}
 assert(sent==0&&prepared>sent&&restarted>prepared);
}
'''
        compile_and_run(harness, [], [])


if __name__ == "__main__":
    unittest.main()
