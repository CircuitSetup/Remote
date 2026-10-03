from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
COMPILER = shutil.which('g++') or str(Path.home() / '.platformio/packages/toolchain-gccmingw32/bin/g++.exe')
HEADERS = {
'Arduino.h': r'''
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define INPUT 0
#define INPUT_PULLUP 1
#define OUTPUT 2
#define LOW 0
#define HIGH 1
#define SERIAL_8N1 0
extern unsigned long testNow;
inline unsigned long millis() { return testNow; }
inline unsigned long micros() { return testNow * 1000UL; }
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return HIGH; }
inline void delayMicroseconds(unsigned long) {}
''',
'HardwareSerial.h': r'''
#pragma once
#include "Arduino.h"
#include <stdarg.h>
#include <string>
#include <vector>
extern uint8_t lastRcFrame[26];
extern std::vector<std::string> adcLogs;
extern std::vector<std::string> probeLogs;
struct HardwareSerial {
    HardwareSerial(int = 0) {}
    void begin(unsigned long, int, int, int, bool) {}
    void end() {}
    int available() { return 0; }
    int read() { return -1; }
    size_t write(const uint8_t *data, size_t n) { if(n == 26 && data[2] == 0x16) memcpy(lastRcFrame, data, n); return n; }
    void flush() {}
    void println(const char *) {}
    void printf(const char *format, ...) {
        char text[256];
        va_list args;
        va_start(args, format);
        vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        if(strncmp(text, "ELRS/CRSF ADC raw:", 18) == 0) adcLogs.emplace_back(text);
        if(strncmp(text, "ELRS/CRSF ADC: ADS1015 probe", 26) == 0) probeLogs.emplace_back(text);
    }
};
extern HardwareSerial Serial;
''',
'WiFi.h': r'''
#pragma once
#include <string>
struct TestIP { std::string toString() { return "0.0.0.0"; } };
struct TestWiFi {
    int getMode() { return 0; }
    int status() { return 0; }
    TestIP localIP() { return {}; }
    TestIP softAPIP() { return {}; }
};
static TestWiFi WiFi;
''',
'Wire.h': r'''
#pragma once
#include "Arduino.h"
struct TwoWire {
    int16_t values[4] = { 1000, 800, 600, 400 };
    bool connected = true;
    int failure = 0;
    int writes = 0, channel = 0, byte = 0;
    void beginTransmission(uint8_t) { writes = 0; }
    void write(uint8_t value) { if (++writes == 2) channel = ((value >> 4) & 7) - 4; }
    int endTransmission(bool = true) { return !connected || (failure == 1 && writes == 3) || (failure == 2 && writes == 1); }
    int requestFrom(uint8_t, uint8_t n) { byte = 0; return failure == 3 ? 1 : n; }
    int read() { uint16_t raw = (uint16_t)values[channel] << 4; return byte++ ? raw & 255 : raw >> 8; }
};
extern TwoWire Wire;
'''
}
TEST = r'''
#include <cassert>
#include "HardwareSerial.h"
#include "src/CRSF/elrs_crsf.h"
unsigned long testNow = 0;
HardwareSerial Serial;
uint8_t lastRcFrame[26] = {};
std::vector<std::string> adcLogs;
std::vector<std::string> probeLogs;
TwoWire Wire;
void remDisplay::on() {}
void remDisplay::setText(const char *) {}
void remDisplay::setSpeed(int) {}
void remDisplay::show() {}
void remLED::setState(bool) {}
bool remLED::getState() { return false; }
bool ButtonPack::sampleStates(uint8_t &) { return false; }
void loadELRSCalibration(ELRSAxisCalibrationData *cal, int count) {
    for (int i = 0; i < count; ++i) cal[i] = {0, 1024, 2047};
}
bool saveELRSCalibration(const ELRSAxisCalibrationData *, int) { return true; }
int main() {
    ELRSCrsfMode mode;
    int16_t axes[4];
    assert(!mode.readCurrentRawAxes(nullptr));
    assert(!mode.readCurrentRawAxes(axes));
    ELRSInputAxisProfile profiles[4];
    for (int i = 0; i < 4; ++i) profiles[i] = elrsDefaultInputAxisProfile();
    assert(mode.begin(250, 0, 0, 0, 0, profiles, elrsDefaultGimbalRouting(),
                      nullptr, false, nullptr, nullptr, nullptr, nullptr,
                      false, false, false, false, nullptr));
    const int16_t atRest[4] = {1000, 800, 600, 400};
    for (int i = 0; i < 4; ++i) Wire.values[i] += 20;
    testNow = 20;
    mode.loop(0); // IIR moves five counts, inside the configured jitter tolerance.
    assert(mode.readCurrentRawAxes(axes));
    for (int i = 0; i < 4; ++i) assert(axes[i] == atRest[i]);
    for (int i = 0; i < 4; ++i) Wire.values[i] -= 20;
    profiles[0].minimum = 1000; profiles[0].center = 1200; profiles[0].maximum = 1400;
    profiles[3].minimum = 400; profiles[3].center = 800; profiles[3].maximum = 1200;
    assert(mode.begin(250, 0, 0, 0, 0, profiles, elrsDefaultGimbalRouting(),
                      nullptr, false, nullptr, nullptr, nullptr, nullptr,
                      false, false, false, false, nullptr));
    Wire.values[0] -= 20; Wire.values[3] += 20;
    testNow += 20;
    mode.loop(0); // Endpoint and idle noise must also hold inside the tolerance.
    assert(mode.readCurrentRawAxes(axes));
    for (int i = 0; i < 4; ++i) assert(axes[i] == atRest[i]);
    Wire.values[0] += 20; Wire.values[3] -= 20;
    for (int i = 0; i < 4; ++i) profiles[i] = elrsDefaultInputAxisProfile();
    assert(mode.begin(250, 0, 0, 0, 0, profiles, elrsDefaultGimbalRouting(),
                      nullptr, false, nullptr, nullptr, nullptr, nullptr,
                      false, false, false, false, nullptr));
    for (int i = 0; i < 4; ++i) Wire.values[i] += 400;
    testNow += 20;
    mode.loop(0);
    assert(mode.readCurrentRawAxes(axes));
    const int16_t expected[4] = {1100, 900, 700, 500};
    for (int i = 0; i < 4; ++i) assert(axes[i] == expected[i]);
    // A portal refresh must not read again or advance the control filter.
    for (int i = 0; i < 4; ++i) Wire.values[i] += 400;
    assert(mode.readCurrentRawAxes(axes));
    for (int i = 0; i < 4; ++i) assert(axes[i] == expected[i]);
    Wire.connected = false;
    assert(mode.begin(250, 0, 0, 0, 0, profiles, elrsDefaultGimbalRouting(),
                      nullptr, false, nullptr, nullptr, nullptr, nullptr,
                      false, false, false, false, nullptr));
    assert(!mode.readCurrentRawAxes(axes));
    Wire.connected = true;
    assert(!mode.readCurrentRawAxes(axes)); // Probe, then wait for the control loop.
    testNow += 20;
    mode.loop(0);
    assert(mode.readCurrentRawAxes(axes));
    for (int i = 0; i < 4; ++i) assert(axes[i] == Wire.values[i]);
    // Every ADC transaction error must age the core input and hide stale portal data.
    for (int failure = 1; failure <= 3; ++failure) {
        Wire.failure = failure;
        testNow += 120;
        mode.loop(0);
        assert(mode.getStatus().faultFlags & ELRS_FAULT_ADC_STALE);
        assert((((lastRcFrame[5] >> 6) | (lastRcFrame[6] << 2) | (lastRcFrame[7] << 10)) & 0x7ff) == 992);
        assert(!mode.readCurrentRawAxes(axes));
        Wire.failure = 0;
        testNow += 20;
        mode.loop(0);
        assert(!(mode.getStatus().faultFlags & ELRS_FAULT_ADC_STALE));
        assert(mode.readCurrentRawAxes(axes));
        for (int i = 0; i < 4; ++i) assert(axes[i] == Wire.values[i]);
    }
    Wire.connected = false;
    testNow += 120;
    mode.loop(0);
    assert(mode.getStatus().faultFlags & ELRS_FAULT_ADC_STALE);
    assert(!mode.readCurrentRawAxes(axes));
    Wire.connected = true;
    testNow += 20;
    mode.loop(0); // Recovery must not require a portal request.
    assert(mode.readCurrentRawAxes(axes));
    puts("CRSF portal filtered ADC check passed");
    // Exercise neutral through the actual IIR, held output, and packed UART frame.
    for(int descending = 0; descending < 2; descending++) {
        for(int reverse = 0; reverse < 2; reverse++) {
            for(int side = -1; side <= 1; side += 2) {
                for(int i = 0; i < 4; i++) {
                    profiles[i] = elrsDefaultInputAxisProfile();
                    profiles[i].minimum = descending ? 1020 : 1000;
                    profiles[i].center = 1010;
                    profiles[i].maximum = descending ? 1000 : 1020;
                    profiles[i].reverse = reverse;
                    Wire.values[i] = 1010 + side * 4;
                }
                assert(mode.begin(250, 0, 0, 0, 0, profiles, {4,3,2,1},
                                  nullptr, false, nullptr, nullptr, nullptr, nullptr,
                                  false, false, false, false, nullptr, 5, 0));
                for(int i = 0; i < 4; i++) Wire.values[i] = 1010;
                for(int step = 0; step < 100; step++) { testNow += 20; mode.loop(0); }
                assert(mode.readCurrentRawAxes(axes));
                for(int i = 0; i < 4; i++) {
                    assert(axes[i] == 1010);
                    const int bit = i * 11, byte = 3 + bit / 8;
                    const uint32_t packed = lastRcFrame[byte] | (lastRcFrame[byte+1] << 8) | (lastRcFrame[byte+2] << 16);
                    assert(((packed >> (bit % 8)) & 0x7ff) == 992);
                }
            }
        }
    }
    puts("CRSF real ADC filter return to neutral check passed");
    // Limits affect transmitted travel, while portal calibration still sees raw ADC.
    const ELRSOutputLimits limits[4] = {{1200,1800}, {1200,1800}, {1200,1800}, {1200,1800}};
    const int16_t rawPoints[3] = {0,1024,2047};
    const uint16_t expectedTicks[3] = {500,992,1483};
    for(int point = 0; point < 3; point++) {
        for(int i = 0; i < 4; i++) {
            profiles[i] = elrsDefaultInputAxisProfile();
            Wire.values[i] = rawPoints[point];
        }
        assert(mode.begin(250, 0, 0, 0, 0, profiles, elrsDefaultGimbalRouting(),
                          nullptr, false, nullptr, nullptr, nullptr, nullptr,
                          false, false, false, false, nullptr, 5, 5, nullptr, limits));
        testNow += 20;
        mode.loop(0);
        assert(mode.readCurrentRawAxes(axes));
        for(int i = 0; i < 4; i++) {
            assert(axes[i] == rawPoints[point]);
            const int bit = i * 11, byte = 3 + bit / 8;
            const uint32_t packed = lastRcFrame[byte] | (lastRcFrame[byte+1] << 8) | (lastRcFrame[byte+2] << 16);
            assert(((packed >> (bit % 8)) & 0x7ff) == expectedTicks[point]);
        }
    }
    puts("CRSF adapter output limits and raw calibration check passed");
#ifdef REMOTE_DBG
    // The real adapter must print at most once per 200ms during rapid motion.
    auto resetLogging = [&](uint32_t now) {
        testNow = now;
        adcLogs.clear();
        probeLogs.clear();
        for(int i = 0; i < 4; i++) Wire.values[i] = 1000;
        assert(mode.begin(250, 0, 0, 0, 0, profiles, elrsDefaultGimbalRouting(),
                          nullptr, false, nullptr, nullptr, nullptr, nullptr,
                          false, false, false, false, nullptr));
        assert(adcLogs.size() == (Wire.connected ? 1U : 0U)); // First valid sample prints at millis()==0.
        assert(probeLogs.size() == 1); // First probe/rebegin logs immediately.
    };
    resetLogging(0);
    for(testNow = 4; testNow <= 400; testNow += 4) {
        for(int i = 0; i < 4; i++) Wire.values[i] = (testNow / 4) % 2 ? 0 : (testNow <= 200 ? 2047 : 1000);
        mode.loop(0);
        assert(adcLogs.size() == 1 + testNow / 200);
    }
    assert(mode.readCurrentRawAxes(axes));
    char latest[100];
    snprintf(latest, sizeof(latest), "ELRS/CRSF ADC raw: A0=%d A1=%d A2=%d A3=%d\n",
             axes[0], axes[1], axes[2], axes[3]);
    assert(adcLogs.back() == latest);
    // Suppressed changes stay relative to the last printed sample.
    resetLogging(0);
    for(int i = 0; i < 4; i++) Wire.values[i] = 1100;
    testNow = 4; mode.loop(0); // Filtered 1025: a qualifying but suppressed change.
    assert(adcLogs.size() == 1);
    for(int i = 0; i < 4; i++) Wire.values[i] = 1025;
    testNow = 8; mode.loop(0);
    testNow = 200; mode.loop(0);
    assert(adcLogs.size() == 2);
    // Unchanged and exactly-threshold samples do not print at the deadline.
    resetLogging(0);
    testNow = 200; mode.loop(0);
    assert(adcLogs.size() == 1);
    for(int i = 0; i < 4; i++) Wire.values[i] = 1080;
    testNow = 400; mode.loop(0); // Filtered 1020, threshold is strictly >20.
    assert(adcLogs.size() == 1);
    testNow = 600; mode.loop(0); // Filtered 1035, cumulative delta now qualifies.
    assert(adcLogs.size() == 2);
    for(int i = 0; i < 4; i++) Wire.values[i] = 1035;
    testNow = 800; mode.loop(0);
    assert(adcLogs.size() == 2);
    for(int i = 0; i < 4; i++) Wire.values[i] = 1075;
    testNow = 1000; mode.loop(0); // Filtered 1045, ten counts since last print.
    assert(adcLogs.size() == 2);
    // Millis wrap must preserve the elapsed interval.
    resetLogging(UINT32_MAX - 99);
    for(int i = 0; i < 4; i++) Wire.values[i] = 1400;
    testNow = 96; mode.loop(0); // 196ms elapsed across wrap.
    assert(adcLogs.size() == 1);
    testNow = 100; mode.loop(0); // 200ms elapsed across wrap.
    assert(adcLogs.size() == 2);
    puts("CRSF adapter ADC diagnostic rate limit check passed");
    // Disconnected retries must not flood Serial, including two probes in begin().
    Wire.connected = false;
    resetLogging(0);
    assert(probeLogs.back().find("failed") != std::string::npos);
    for(testNow = 4; testNow <= 204; testNow += 4) {
        mode.loop(0);
        assert(probeLogs.size() == 1 + testNow / 200);
    }
    Wire.connected = true;
    testNow = 208; mode.loop(0); // Probe-ok may be suppressed; controls must recover.
    assert(probeLogs.size() == 2);
    assert(adcLogs.size() == 1);
    assert(mode.readCurrentRawAxes(axes));
    for(int i = 0; i < 4; i++) assert(axes[i] == 1000);
    assert(!(mode.getStatus().faultFlags & (ELRS_FAULT_ADC_MISSING | ELRS_FAULT_ADC_STALE)));
    for(int i = 0; i < 4; i++) Wire.values[i] = 1400;
    testNow = 212; mode.loop(0);
    assert(adcLogs.size() == 1);
    testNow = 408; mode.loop(0);
    assert(adcLogs.size() == 2);
    resetLogging(408); // Rebegin permits another first probe at the same timestamp.
    assert(probeLogs.back().find("ok") != std::string::npos);
    Wire.connected = false;
    resetLogging(UINT32_MAX - 99);
    testNow = 96; mode.loop(0);
    assert(probeLogs.size() == 1);
    testNow = 100; mode.loop(0);
    assert(probeLogs.size() == 2);
    Wire.connected = true;
    puts("CRSF adapter ADC probe rate limit and recovery check passed");
#else
    assert(adcLogs.empty());
    assert(probeLogs.empty());
#endif
}
'''
def compile_and_run(source, sources, flags=()):
    with tempfile.TemporaryDirectory(prefix='crsf-check-') as work:
        work = Path(work)
        for name, contents in HEADERS.items():
            (work / name).write_text(contents)
        (work / 'check.cpp').write_text(source)
        binary = work / ('check.exe' if os.name == 'nt' else 'check')
        command = [COMPILER, '-std=gnu++11', '-DHAVE_CRSF', '-I' + str(work), '-I' + str(ROOT / 'src')]
        command += list(flags)
        command += [str(ROOT / 'src/src/CRSF' / name) for name in sources]
        command += [str(work / 'check.cpp'), '-o', str(binary)]
        subprocess.run(command, check=True)
        environment = os.environ.copy()
        environment['PATH'] = str(Path(COMPILER).parent) + os.pathsep + environment.get('PATH', '')
        subprocess.run([str(binary)], check=True, env=environment)


if __name__ == '__main__':
    sources = ['elrs_crsf.cpp', 'elrs_crsf_core.cpp', 'elrs_crsf_transport.cpp', 'elrs_input_model.cpp']
    compile_and_run(TEST, sources)
    compile_and_run(TEST, sources, ['-DREMOTE_DBG'])
