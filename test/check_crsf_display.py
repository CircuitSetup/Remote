"""Run the real telemetry HTTP formatter and portal builder with received frames."""
import json
import os
import tempfile
from pathlib import Path
from check_crsf_adc import ROOT, compile_and_run

portal = (ROOT / 'src/src/CRSF/crsf_wifi.h').read_text()
native = (ROOT / 'test/native_elrs/test_native_elrs.cpp').read_text()

def function(signature):
    start = portal.index(signature + '\n{')
    return portal[start:portal.index('\n}', start) + 2] + '\n'

production = function('static bool crsfParseDisplayNumber(const String &value, float &number)')
production += function('static bool crsfParseDisplayParams(ELRSDisplayConfig &config)')
production += portal[portal.index('static const char crsfDisplayScript[]'):portal.index('static void handleELRSRawRead()')]
fixture = r'''
#include <cassert>
#include <cmath>
#include <ArduinoJson.h>
static_assert(ARDUINOJSON_USE_DOUBLE == 1, "Use the installed default numeric precision");
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include "Arduino.h"
#include "src/CRSF/elrs_crsf_core.h"
using String = std::string;
unsigned long testNow = 0;
'''
fixture += native[native.index('constexpr uint8_t AXIS_AILERON'):native.index('static std::vector<uint8_t> makeFrameWithSync')]
style = (ROOT / 'src/src/WiFiManager/wm_strings_en.h').read_text()
fixture += '#define HTTP_BLUE "#4f529d"\n#define HTTP_RED "#be5c9c"\n#define HTTP_BUTTON_TEXT "#fff"\n'
fixture += style[style.index('static const char HTTP_STYLE[]'):style.index('\n#ifndef WM_50S_STYLE', style.index('static const char HTTP_STYLE[]'))].replace('PROGMEM', '')
fixture += r'''
FakeHost host;
ELRSCrsfCore core;
ELRSDisplayConfig crsfDisplayConfig = {1,1,0.5f,0};
ELRSDisplayConfig loadELRSDisplayConfig() { return crsfDisplayConfig; }
bool haveNewBoard = true, opModeCRSF = true, opModePropCRSF = true;
constexpr int WM_CP_DESTROY = 0, WM_CP_LEN = 1;
size_t wmLenBuf;
struct {
    ELRSTelemetrySample telemetrySample(uint8_t source, uint32_t now) { return core.telemetrySample(source, now); }
    uint8_t speedDisplayUnits() { return ELRS_SPEED_UNITS_MPH; }
} elrsMode;
struct Server {
    std::map<std::string,std::string> args;
    int status = 0;
    std::string body;
    bool hasArg(const char *key) { return args.count(key); }
    String arg(const char *key) { return args[key]; }
    void send(int code, const char *, const char *text) {
        status = code; body = text;
        if(code == 200) {
            FILE *file = fopen(getenv("CRSF_DISPLAY_JSON"), "a"); assert(file);
            fprintf(file, "%s\n", text); fclose(file);
        }
    }
} server;
struct { Server *server = &::server; } wm;
'''
cases = r'''
int main() {
    float number;
    for(const auto &item : std::vector<std::pair<const char *, float>>{{"1.25",1.25f},{".5",0.5f},{"1.",1.0f},{"+2e-1",0.2f},{"-2E+1",-20.0f},{"-0",-0.0f},{"1e-40",1e-40f},{"0e999",0.0f}}) {
        assert(crsfParseDisplayNumber(item.first, number));
        assert(number == item.second);
    }
    assert(crsfParseDisplayNumber("-0", number) && std::signbit(number));
    for(const char *bad : {".", "+", "-", "e1", "1e", "1e+", "1..2", "--1", "1-2", "1e2e3", "1e999"}) {
        assert(!crsfParseDisplayNumber(bad, number));
    }
    auto config = defaultConfig(); config.propControls = true;
    assert(core.begin(host, config, 0));
    handleELRSTelemetryRead(); assert(server.status == 200);
    host.queueFrame(makeFrame(0x02, {0,0,0,0,0,0,0,0,3,232,0,0,3,232,0}));
    core.loop(host, 0, 0); handleELRSTelemetryRead();
    assert(server.status == 200 && server.body.find("\"text\":\"31.1\"") != String::npos);
    server.args = {{"cdmul","1"},{"cdoff","0"},{"cspdu","0"},{"cddec","0"}};
    handleELRSTelemetryRead(); assert(server.body.find("\"text\":\"100\"") != String::npos);
    for(const char *id : {"7","8","9"}) {
        server.args = {{"cdsrc",id}}; handleELRSTelemetryRead(); assert(server.status == 200);
    }
    testNow = 2000; core.loop(host, testNow, 0);
    host.queueFrame(makeFrame(0x14, {105,0,88,253,0,0,0,0,0,0}));
    core.loop(host, testNow, 0); server.args.clear(); handleELRSTelemetryRead();
    assert(server.body.find("\"text\":\"---\"") != String::npos);
    server.args = {{"cdsrc","0"},{"cdmul","1000"},{"cdoff","999"}};
    handleELRSTelemetryRead(); assert(server.body.find("\"text\":\"88\"") != String::npos);
    server.args = {{"cdsrc","14"}}; handleELRSTelemetryRead();
    assert(server.body.find("\"text\":\"\"") != String::npos);
    opModePropCRSF = false; handleELRSTelemetryRead();
    assert(server.body.find("\"text\":\"88\"") != String::npos);
    server.args = {{"cdsrc","13"}}; handleELRSTelemetryRead();
    assert(server.body.find("\"text\":\"\"") != String::npos);
    for(const char *field : {"cdsrc","cddec","cdmul","cdoff","cspdu"}) {
        for(const auto &bad : std::vector<std::string>{"", "NaN", "inf", "0x1p2", "1junk", " 1", ".", "+", "1e", "1e+", "1..2", "--1", "1-2", "1e2e3", "1e999", "999999999999999999999999999", String("1\0x",3)}) {
            server.args = {{field,bad}}; handleELRSTelemetryRead(); assert(server.status == 400);
        }
    }
    for(const auto &item : std::vector<std::pair<const char *, const char *>>{{"cdmul","1000"},{"cdmul","-1000"},{"cdoff","999"},{"cdoff","-999"},{"cdmul","1e-40"},{"cdoff","0e999"}}) {
        server.args = {{item.first,item.second}}; handleELRSTelemetryRead(); assert(server.status == 200);
    }
    for(const auto &item : std::vector<std::pair<const char *, const char *>>{{"cdmul","1000.01"},{"cdmul","-1000.01"},{"cdoff","999.01"},{"cdoff","-999.01"},{"cdmul","1e999"},{"cdoff","1e+"}}) {
        server.args = {{item.first,item.second}}; handleELRSTelemetryRead(); assert(server.status == 400);
    }
    // A zero sample must remain selectable and appear numerically only in preview.
    opModePropCRSF = true;
    host.queueFrame(makeFrame(0x14, {0,0,0,0,0,0,0,0,0,0})); core.loop(host, testNow, 0);
    for(const char *id : {"10","11","12"}) {
        server.args = {{"cdsrc",id}}; handleELRSTelemetryRead(); assert(server.status == 200);
    }
    assert(crsfDisplayConfig.source == 1 && crsfDisplayConfig.multiplier == 0.5f && host.displayShows == 0);
    const char *html = wmBuildCRSFDisplay(nullptr, 2); assert(html);
    assert(strstr(html, "id='cdsrcvalue' name='cdsrc' value='1'"));
    assert(strstr(html, "value='1' selected disabled>GPS speed (unavailable)"));
    assert(!strstr(html, "<select id='cdsrc' name="));
    assert(*(const size_t*)wmBuildCRSFDisplay(nullptr, WM_CP_LEN) == strlen(html) + 1);
    if(const char *path = getenv("CRSF_DISPLAY_PREVIEW")) {
        FILE *file = fopen(path, "w"); assert(file);
        fprintf(file, "<!doctype html><html><meta charset='utf-8'><title>Telemetry display preview</title><meta name='viewport' content='width=device-width,initial-scale=1'>%s</style><body><div id='wrap'><form>%s</form></div></body></html>", HTTP_STYLE, html);
        fclose(file);
    }
    wmBuildCRSFDisplay(html, WM_CP_DESTROY);
    puts("CRSF telemetry preview, validation, and pending-source form checks passed");
}
'''
with tempfile.TemporaryDirectory(prefix='crsf-display-') as work:
    path = Path(work) / 'responses.jsonl'
    os.environ['CRSF_DISPLAY_JSON'] = str(path)
    compile_and_run(fixture + production + cases, ['elrs_crsf_core.cpp', 'elrs_crsf_transport.cpp', 'elrs_input_model.cpp'], ['-I' + str(ROOT / '.pio/libdeps/esp32dev/ArduinoJson/src')])
    responses = [json.loads(line) for line in path.read_text().splitlines()]
    assert len(responses) == 20
    assert responses[0]['sources'] == [], 'Sources must be absent before reception'
    assert [row['id'] for row in responses[1]['sources']] == [1,7,8,9]
    assert responses[1]['sources'][0] == {'id':1,'label':'GPS speed','unit':'km/h'}
    assert abs(responses[1]['preview']['value'] - 62.1371192) < 0.001 and responses[1]['preview']['unit'] == 'mph'
    assert [response['preview']['value'] for response in responses[3:6]] == [0,0,0]
    assert all(response['preview']['available'] for response in responses[3:6])
    assert [row['id'] for row in responses[6]['sources']] == [10,11,12]
    assert responses[6]['preview']['value'] is None and not responses[6]['preview']['available']
    for response in responses:
        assert all(set(row) == {'id','label','unit'} for row in response['sources'])
    assert [response['preview']['value'] for response in responses[-3:]] == [0,0,0]
    assert all(response['preview']['available'] for response in responses[-3:])
    del os.environ['CRSF_DISPLAY_JSON']
