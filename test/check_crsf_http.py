"""Exercise the CRSF portal server against slow reads, writes and idle clients."""
from check_crsf_adc import ROOT, compile_and_run
from check_crsf_portal_http import ARDUINOJSON_INCLUDE, function

portal = (ROOT / 'src/src/CRSF/crsf_config_portal.h').read_text()
wifi = (ROOT / 'src/remote_wifi.cpp').read_text()
header = ROOT / 'src/src/CRSF/crsf_http.h'
adapter = header.read_text() if header.exists() else ''
adapter = '\n'.join(line for line in adapter.splitlines() if not line.startswith(('#include', '#pragma'))) + '\n'

harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
using PGM_P = const char *;
uint32_t now=0,lastService=0,maxGap=0;
unsigned services=0;
unsigned long millis(){return now;}
void delay(unsigned ms){now+=ms;}
void yield(){}
void gpCallback(int){maxGap=std::max(maxGap,now-lastService);lastService=now;services++;}
constexpr int WM_LP_NONE=0,HTTP_GET=0,HTTP_POST=1;
constexpr size_t CONTENT_LENGTH_NOT_SET=size_t(-1);
constexpr unsigned HTTP_MAX_DATA_WAIT=5000,HTTP_MAX_SEND_WAIT=5000;
enum HTTPClientStatus{HC_NONE,HC_WAIT_READ,HC_WAIT_CLOSE};
struct Socket{
 bool connected=true,first=true,idle=false,partial=false;
 uint32_t ready=200;
 unsigned writes=0;
 std::string sent;
};
struct SocketHandle{
 std::shared_ptr<Socket> state;
 explicit SocketHandle(std::shared_ptr<Socket> peer):state(peer){}
 ~SocketHandle(){state->connected=false;}
};
class WiFiClient{
public:
 std::shared_ptr<SocketHandle> handle;
 virtual ~WiFiClient(){}
 explicit operator bool()const{return handle&&handle->state->connected;}
 bool connected(){return bool(*this);}
 virtual int available(){return connected()&&!handle->state->idle&&(handle->state->first||now>=handle->state->ready);}
 virtual int read(uint8_t*buffer,size_t size){
  now++;
  if(!connected()||!size)return -1;
  if(handle->state->first){handle->state->first=false;buffer[0]='G';return 1;}
  if(now<handle->state->ready)return -1;
  buffer[0]='*';return 1;
 }
 virtual int read(){uint8_t b=0;return read(&b,1)>0?b:-1;}
 virtual size_t write(const uint8_t*b,size_t n){now+=300;handle->state->sent.append((const char*)b,n);return n;}
 size_t write(const char*b,size_t n){return write((const uint8_t*)b,n);}
 void stop(){handle.reset();}
 void setTimeout(unsigned){}
 void setNoDelay(bool){}
};
// The real cooperative client's read/write servicing is covered by check_crsf_network.py.
class CRSFClient:public WiFiClient{
 void(*looper)()=nullptr;
public:
 using WiFiClient::read;
 void setLooper(void(*fn)()){looper=fn;}
 void setCooperative(bool){}
 int read(uint8_t*b,size_t n)override{if(looper)looper();return WiFiClient::read(b,n);}
 size_t write(const uint8_t*b,size_t n)override{
  if(!*this)return 0;
  handle->state->writes++;
  for(unsigned i=0;i<300;i++){if(looper)looper();now++;}
  if(handle->state->partial){handle->state->sent.append((const char*)b,n/2);stop();return 0;}
  handle->state->sent.append((const char*)b,n);return n;
 }
};
struct Listener{
 std::shared_ptr<Socket> next;
 WiFiClient available(){WiFiClient c;if(next)c.handle=std::make_shared<SocketHandle>(next);next.reset();return c;}
};
class WebServer{
public:
 Listener _server;
 std::map<std::string,std::function<void()>> routes;
 bool flash=false,followup=false;
 virtual ~WebServer(){}
 WebServer(int=80){}
 virtual void handleClient(){
  _currentClient=_server.available();
  if(_currentClient&&_currentClient.available()&&_parseRequest(_currentClient))_handleRequest();
 }
 WiFiClient client(){return _currentClient;}
 void collectHeaders(const char**,int){}
 template<class F>void on(const char*path,int,F fn){routes[path]=fn;}
 template<class F,class U>void on(const char*path,int,F fn,U){routes[path]=fn;}
 void sendHeader(const char*,const char*){}
 void send(int,const char*,const char*){}
 void send_P(int,const char*,const char*,size_t){}
protected:
 virtual size_t _currentClientWrite(const char*b,size_t n){return _currentClient.write(b,n);}
 virtual size_t _currentClientWrite_P(PGM_P b,size_t n){return _currentClientWrite(b,n);}
 bool _parseRequest(WiFiClient&c){while(c.connected()){int b=c.read();if(b=='*')return true;}return false;}
 void _handleRequest(){if(flash)_currentClientWrite_P("abcdef",6);else _currentClientWrite("abcdef",6);if(followup)_currentClientWrite("ignored",7);}
 WiFiClient _currentClient;
 HTTPClientStatus _currentStatus=HC_NONE;
 uint32_t _statusChange=0;
 size_t _contentLength=0;
 std::unique_ptr<int> _currentUpload,_currentRaw;
};
struct Manager{std::unique_ptr<WebServer>server;}wm;
bool opModeCRSF=true,haveNewBoard=true;
const char*R_updateacdone="/updateacdone";
const uint8_t remoteConfigUiGzip[]={0};constexpr size_t remoteConfigUiGzipLength=1;
void handleUploadDone(){}void handleUploading(){}
void handleELRSRawRead(){}void handleELRSTelemetryRead(){}
void remotePortalSession(){}void remotePortalDevices(){}void remotePortalOperation(){}
void remotePortalAction(){}void remotePortalUploadDone(){}void remotePortalUpload(){}
void remotePortalExport(){}void remotePortalApply(){}void remotePortalReview(){}
'''

cases = r'''
static void start(bool crsf=true){
 now=lastService=maxGap=services=0;opModeCRSF=crsf;
 wm.server.reset(new WebServer);setupWebServerCallback();
 assert(wm.server->routes.count("/updateacdone"));
 assert(wm.server->routes.count("/remote/session"));
}
int main(){
 for(bool flash:{false,true}){
  start();auto peer=std::make_shared<Socket>();wm.server->_server.next=peer;wm.server->flash=flash;
  wm.server->handleClient();
  assert(peer->sent=="abcdef");
  assert(services>=400&&maxGap<=1); // Real request parsing and writes must keep running the supplied service callback.
 }
 start();auto idle=std::make_shared<Socket>();idle->idle=true;wm.server->_server.next=idle;
 wm.server->handleClient();now=150;
 auto ready=std::make_shared<Socket>();ready->ready=0;wm.server->_server.next=ready;
 wm.server->handleClient();wm.server->handleClient();
 assert(ready->sent=="abcdef"&&now<1000); // Idle preconnections must not block ready requests.
 assert(idle->connected); // A browser may send its first request more than 100 ms after connecting.
 idle->idle=false;idle->ready=now;wm.server->handleClient();
 assert(idle->sent=="abcdef"&&!idle->connected);
 start();auto expired=std::make_shared<Socket>();expired->idle=true;wm.server->_server.next=expired;
 wm.server->handleClient();now=HTTP_MAX_DATA_WAIT+1;wm.server->handleClient();assert(!expired->connected);

 start();auto first=std::make_shared<Socket>();auto second=std::make_shared<Socket>();
 first->idle=second->idle=true;first->ready=second->ready=0;
 wm.server->_server.next=first;wm.server->handleClient();wm.server->_server.next=second;wm.server->handleClient();
 first->idle=second->idle=false;wm.server->handleClient();
 auto newcomer=std::make_shared<Socket>();newcomer->ready=0;wm.server->_server.next=newcomer;wm.server->handleClient();
 assert(second->sent=="abcdef"&&newcomer->sent.empty()); // New arrivals cannot starve a queued browser request.
 start();auto broken=std::make_shared<Socket>();broken->partial=true;wm.server->_server.next=broken;
 wm.server->followup=true;wm.server->handleClient();assert(!broken->connected&&broken->sent=="abc"&&broken->writes==1);
 start(false);auto legacy=std::make_shared<Socket>();legacy->ready=0;wm.server->_server.next=legacy;
 wm.server->handleClient();assert(legacy->sent=="abcdef"&&services==0);
}
'''

setup = function(portal, r'static void remoteSetupPortal\(\)')
callback = function(wifi, r'static void setupWebServerCallback\(\)\s*\{')
compile_and_run(harness + adapter + setup + callback + cases, [], ['-DHAVE_CRSF'])
print('CRSF HTTP keeps servicing slow request/response I/O, releases idle clients, and preserves portal routes')

writer = portal[portal.index('class RemotePortalWriter {'):portal.index('\n};', portal.index('class RemotePortalWriter {')) + 3]
json_sender = function(portal, r'static void remotePortalJson\(JsonDocument &doc,int status=200\)')
json_harness = r'''
#include <cassert>
#include <cstdint>
#include <string>
#include <ArduinoJson.h>
constexpr size_t CONTENT_LENGTH_UNKNOWN=size_t(-1);
uint32_t now=0,lastService=0,maxGap=0;unsigned services=0;
void remotePortalService(){maxGap=std::max(maxGap,now-lastService);lastService=now;services++;}
struct Server{
 size_t length=0;std::string body;
 void sendHeader(const char*,const char*){}
 void setContentLength(size_t n){length=n;}
 void send(int,const char*,const char*){}
 void sendContent(const char*data,size_t n){
  body.append(data,n);
  // ESP32 WebServer writes chunk footers through its base client, bypassing the cooperative write hook.
  if(length==CONTENT_LENGTH_UNKNOWN)now+=300;
 }
}server;
struct Manager{Server*server=&::server;}wm;
'''
json_case = r'''
int main(){
 StaticJsonDocument<1024> doc;doc["value"]=std::string(700,'x');
 remotePortalJson(doc);
 assert(server.length==server.body.size()&&services>=3&&maxGap<=1);
 StaticJsonDocument<1024> parsed;assert(!deserializeJson(parsed,server.body));
 assert(parsed["value"].as<std::string>()==std::string(700,'x'));
}
'''
compile_and_run(json_harness + writer + json_sender + json_case, [], ['-I' + str(ARDUINOJSON_INCLUDE)])
print('CRSF JSON responses declare their exact length and stream the complete body cooperatively')
