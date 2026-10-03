"""Host regression: real upload handler, Arduino String and WebServer response methods.
Run: python test/native_audio/check_upload_response.py
Requires the installed PlatformIO ESP32 framework and a host C++ compiler.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def function(text, signature):
    masked = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: ' ' * len(m[0]), text, flags=re.S)
    match = re.search(re.escape(signature) + r'\s*\{', masked)
    assert match, signature
    start, pos, depth = match.start(), match.end(), 1
    while depth:
        depth += (masked[pos] == '{') - (masked[pos] == '}')
        pos += 1
    return text[start:pos]


CPP = r"""
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include "WString.h"
#include "esp32-hal-log.h"
static const char Content_Length[]="Content-Length";
extern "C" char *ultoa(unsigned long,char*,int);
extern "C" char *utoa(unsigned int n,char*p,int b){return ultoa(n,p,b);}
static std::map<void*,size_t> blocks;
static size_t live=0,peak=0,body_size=0,body_duplicates=0;
static bool fail_body=false,fail_string=false;
static std::vector<std::string> events;
static void check_blocks(){ for(auto b:blocks) for(size_t i=0;i<16;i++) assert(((unsigned char*)b.first)[b.second+i]==0xa5); }
void *tracked_malloc(size_t n){
    if(fail_body){fail_body=false;return nullptr;}
    void *p=std::malloc(n+16384); assert(p); std::memset((char*)p+n,0xa5,16);
    blocks[p]=n;live+=n;peak=std::max(peak,live);body_size=n;return p;
}
void tracked_free(void *p){if(!p)return;check_blocks();auto it=blocks.find(p);assert(it!=blocks.end());live-=it->second;blocks.erase(it);std::free(p);}
void *tracked_realloc(void *p,size_t n){
    if(n>=body_size/2 && body_size>1000){body_duplicates++; if(fail_string)return nullptr;}
    check_blocks();size_t old=p?blocks.at(p):0;void *r=std::realloc(p,n+16);if(!r)return nullptr;
    if(p)blocks.erase(p);blocks[r]=n;live=live-old+n;peak=std::max(peak,live);std::memset((char*)r+n,0xa5,16);return r;
}
#define malloc tracked_malloc
#define realloc tracked_realloc
#define free tracked_free
#define STRLEN(x) (sizeof(x)-1)
#define UPL_BADERR 4
#define CONTENT_LENGTH_NOT_SET ((size_t)-2)
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)
namespace mime {enum{html};struct Entry{const char *mimeType;};static Entry mimeTable[]={{"text/html"}};}
struct Socket {
    std::string bytes;
    size_t write(const char *p,size_t n){check_blocks(); if(n){assert(p);bytes.append(p,n);}events.push_back("write");return n;}
    size_t write_P(const char *p,size_t n){return write(p,n);}
};
struct WebServer {
    Socket _currentClient;String _responseHeaders;size_t _contentLength=CONTENT_LENGTH_NOT_SET;
    bool _chunked=false,_corsEnabled=false;int _currentVersion=1;
    size_t _currentClientWrite(const char*b,size_t n){return _currentClient.write(b,n);}
    size_t _currentClientWrite_P(const char*b,size_t n){return _currentClient.write_P(b,n);}
    void sendHeader(const String&,const String&,bool first=false);
    void _prepareHeader(String&,int,const char*,size_t);
    String _responseCodeToString(int);
    void send(int,const char*,const String&);
    void send_P(int,PGM_P,PGM_P);
    void sendContent(const String&);void sendContent(const char*,size_t);
    void sendContent_P(PGM_P);void sendContent_P(PGM_P,size_t);
};
// WEB_METHODS
// HTML_CONSTANTS
struct WiFiManager {
    WebServer *server;
    const char *getHTTPSTART(int& i){i=HTTP_HEAD_TITLE_START;return HTTP_HEAD_START;}
    const char *getHTTPSCRIPT(){return HTTP_SCRIPT;}
    const char *getHTTPSTYLE(){return HTTP_STYLE;}
    const char *getHTTPSTYLEOK(){return HTTP_STYLE_MSG;}
} wm;
static int numUploads=0,opType[16],ACULerr[16];static bool haveSD=true,valid_pack=true;
static std::string names[16];static int removed=0;
static bool check_if_default_audio_present(){return valid_pack;}
static void removeACFile(int i){assert(i==0);removed++;}
static int getUploadFileNameLen(int i){return names[i].size();}
static char *getUploadFileName(int i){return names[i].empty()?nullptr:&names[i][0];}
static void freeUploadFileNames(){events.push_back("filenames");for(auto &n:names)n.clear();}
static void delay(int ms){events.push_back("delay"+std::to_string(ms));}
static void prepareReboot(){events.push_back("prepare");}
static void esp_restart(){events.push_back("restart");}
// REBOOT
// HANDLERS
struct Result{std::string bytes;std::vector<std::string> events;size_t peak,duplicates;int removed;};
static Result run(int mode,int scenario){
    assert(blocks.empty());live=peak=body_size=body_duplicates=0;removed=0;events.clear();
    haveSD=scenario!=2;valid_pack=scenario!=4;numUploads=scenario==3?16:1;
    std::fill(opType,opType+16,0);std::fill(ACULerr,ACULerr+16,0);
    for(int i=0;i<16;i++)names[i]="sample"+std::to_string(i)+".mp3";
    if(scenario==1 || scenario==4)opType[0]=1;
    if(scenario==3)for(int i=0;i<16;i++){names[i]=std::string(240,'a'+i)+".mp3";ACULerr[i]=(i%7)+1;}
    if(scenario==6)ACULerr[0]=3;
    fail_body=scenario==5 || scenario==6 || scenario==7;
    if(scenario==7)haveSD=false;
    fail_string=scenario==8;
    Result result;
    {
        WebServer server;wm.server=&server;
        if(mode==0)handleBaseline();else handleCandidate();
        check_blocks();result={server._currentClient.bytes,events,peak,body_duplicates,removed};
    }
    while(!blocks.empty())tracked_free(blocks.begin()->first);
    return result;
}
int main(){
    for(int scenario=0;scenario<9;scenario++){
        Result old=run(0,scenario),now=run(1,scenario);
        size_t split=now.bytes.find("\r\n\r\n");assert(split!=std::string::npos);
        std::string body=now.bytes.substr(split+4);assert(now.bytes.find("HTTP/1.1 200 OK\r\n")==0);
        assert(now.bytes.find("Content-Type: text/html\r\n")!=std::string::npos);
        assert(now.bytes.find("Content-Length: "+std::to_string(body.size())+"\r\n")!=std::string::npos);
        assert(now.events.front()=="filenames");
        assert(now.events[now.events.size()-4]=="delay1000" && now.events[now.events.size()-3]=="prepare" && now.events[now.events.size()-2]=="delay1000" && now.events.back()=="restart");
        if(scenario!=8){assert(old.bytes==now.bytes);assert(old.events==now.events);assert(old.removed==now.removed);}
        else {assert(old.bytes!=now.bytes);assert(body.find("Upload successful")!=std::string::npos);}
        if(scenario==2){assert(body.find("No SD card found")!=std::string::npos);assert(body.find("Upload successful")==std::string::npos);}
        if(scenario==4){assert(now.removed==1);assert(body.find("Bad file")!=std::string::npos);}
        if(scenario==5)assert(body=="DONE");if(scenario==6 || scenario==7)assert(body=="ERROR");
        if(body.size()>1000){assert(now.duplicates==0);assert(old.duplicates==1);if(scenario!=8)assert(old.peak-now.peak>=body.size());}
        printf("scenario=%d body=%u peak=%u->%u duplicates=%u->%u\n",scenario,(unsigned)body.size(),(unsigned)old.peak,(unsigned)now.peak,(unsigned)old.duplicates,(unsigned)now.duplicates);
    }
}
"""


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project',type=Path,default=Path(__file__).resolve().parents[2])
    parser.add_argument('--framework',type=Path,default=Path.home()/'.platformio/packages/framework-arduinoespressif32')
    parser.add_argument('--cc',default=shutil.which('g++') or str(Path.home()/'.platformio/packages/toolchain-gccmingw32/bin/g++.exe'))
    args=parser.parse_args(); project=args.project.resolve(); core=args.framework/'cores/esp32'
    app=(project/'src/remote_wifi.cpp').read_text(); web=(args.framework/'libraries/WebServer/src/WebServer.cpp').read_text()
    target=function(app,'static void handleUploadDone()')
    oldsend='String str(buf);\n    wm.server->send(200, "text/html", str);'
    new='wm.server->send_P(200, "text/html", buf);'
    baseline=target.replace(new,oldsend)
    methods=[ 'void WebServer::sendHeader(const String& name, const String& value, bool first)',
              'void WebServer::_prepareHeader(String& response, int code, const char* content_type, size_t contentLength)',
              'String WebServer::_responseCodeToString(int code)',
              'void WebServer::send(int code, const char* content_type, const String& content)',
              'void WebServer::send_P(int code, PGM_P content_type, PGM_P content)',
              'void WebServer::sendContent(const String& content)',
              'void WebServer::sendContent(const char* content, size_t contentLength)',
              'void WebServer::sendContent_P(PGM_P content)',
              'void WebServer::sendContent_P(PGM_P content, size_t size)' ]
    html='\n'.join(re.findall(r'^#define AA_(?:TITLE|ICON) .*$',app,re.M))+'\n'
    html+=app[app.index('static const char acul_part1[]'):app.index('static const char tcdList[]')]
    for name in ('myTitle','myHead'):html+=re.search(r'^static const char '+name+r'\[\].*$',app,re.M)[0]+'\n'
    strings=(project/'src/src/WiFiManager/wm_strings_en.h').read_text()
    html+='\n#define HTTP_RED "#be5c9c"\n#define HTTP_BLUE "#4f529d"\n#define HTTP_BUTTON_TEXT "#fff"\n#define HTTP_YUPD "#ebe74c"\n#define HTTP_HEAD_TITLE_START 203\n'
    for name in ('HTTP_HEAD_START','HTTP_SCRIPT','HTTP_STYLE','HTTP_STYLE_MSG'):
        html+=re.search(r'static const char '+name+r'\[\].*?;(?=\s*(?:static|#|/))',strings,re.S)[0]+'\n'
    cpp=CPP.replace('// WEB_METHODS','\n'.join(function(web,m) for m in methods)).replace('// HTML_CONSTANTS',html)
    cpp=cpp.replace('// REBOOT',function(app,'static void doReboot()')).replace('// HANDLERS',baseline.replace('handleUploadDone','handleBaseline')+'\n'+target.replace('handleUploadDone','handleCandidate'))
    with tempfile.TemporaryDirectory(prefix='remote-upload-check-') as directory:
        scratch=Path(directory)
        for name in ('WString.h','stdlib_noniso.h'):shutil.copyfile(core/name,scratch/name)
        (scratch/'WString.cpp').write_text((core/'WString.cpp').read_text().replace('#include "esp32-hal-log.h"','#include "esp32-hal-log.h"\n#include "allocation.h"'))
        (scratch/'stdlib_noniso.cpp').write_text((core/'stdlib_noniso.c').read_text())
        (scratch/'allocation.h').write_text('#include <stddef.h>\nvoid *tracked_malloc(size_t);void *tracked_realloc(void*,size_t);void tracked_free(void*);\n#define malloc tracked_malloc\n#define realloc tracked_realloc\n#define free tracked_free\n')
        (scratch/'Arduino.h').write_text('#include <stdio.h>\n#include <limits.h>\n#include <math.h>\n')
        (scratch/'esp_system.h').write_text('')
        (scratch/'esp32-hal-log.h').write_text('#define log_e(...)\n#define log_w(...)\n')
        (scratch/'pgmspace.h').write_text('#pragma once\n#include <string.h>\n#define PROGMEM\n#define PSTR(x) (x)\n#define memcpy_P memcpy\n#define strlen_P strlen\n#define memccpy_P memccpy\ntypedef const char *PGM_P;typedef const void *PGM_VOID_P;\n')
        (scratch/'check.cpp').write_text(cpp)
        exe=scratch/'check.exe'
        subprocess.run([args.cc,'-std=c++11','-O2','-I',str(scratch),str(scratch/'check.cpp'),str(scratch/'WString.cpp'),str(scratch/'stdlib_noniso.cpp'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)


if __name__=='__main__':main()
