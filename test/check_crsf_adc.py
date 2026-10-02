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
extern uint8_t lastRcFrame[26];
struct HardwareSerial {
    HardwareSerial(int = 0) {}
    void begin(unsigned long, int, int, int, bool) {}
    void end() {}
    int available() { return 0; }
    int read() { return -1; }
    size_t write(const uint8_t *data, size_t n) { if(n == 26 && data[2] == 0x16) memcpy(lastRcFrame, data, n); return n; }
    void flush() {}
    void println(const char *) {}
};
extern HardwareSerial Serial;
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
}
'''
def compile_and_run(source, sources):
    with tempfile.TemporaryDirectory(prefix='crsf-check-') as work:
        work = Path(work)
        for name, contents in HEADERS.items():
            (work / name).write_text(contents)
        (work / 'check.cpp').write_text(source)
        binary = work / ('check.exe' if os.name == 'nt' else 'check')
        command = [COMPILER, '-std=gnu++11', '-DHAVE_CRSF', '-I' + str(work), '-I' + str(ROOT / 'src')]
        command += [str(ROOT / 'src/src/CRSF' / name) for name in sources]
        command += [str(work / 'check.cpp'), '-o', str(binary)]
        subprocess.run(command, check=True)
        environment = os.environ.copy()
        environment['PATH'] = str(Path(COMPILER).parent) + os.pathsep + environment.get('PATH', '')
        subprocess.run([str(binary)], check=True, env=environment)


if __name__ == '__main__':
    compile_and_run(TEST, ['elrs_crsf.cpp', 'elrs_crsf_core.cpp', 'elrs_crsf_transport.cpp', 'elrs_input_model.cpp'])
