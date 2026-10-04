"""Run the real button scanners with sampled input and cancellation."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

from check_crsf_adc import COMPILER, HEADERS, ROOT

NM = shutil.which('nm') or str(Path(COMPILER).with_name('nm.exe'))


def compile_and_run(source, flags=(), forbidden_symbols=()):
    with tempfile.TemporaryDirectory(prefix='crsf-local-inputs-') as work:
        work = Path(work)
        for name, contents in HEADERS.items():
            (work / name).write_text(contents)
        (work / 'check.cpp').write_text(source)
        binary = work / ('check.exe' if os.name == 'nt' else 'check')
        command = [COMPILER, '-std=gnu++11', '-I' + str(work), '-I' + str(ROOT / 'src')]
        command += list(flags)
        command += [str(ROOT / 'src/input.cpp'), str(work / 'check.cpp'), '-o', str(binary)]
        subprocess.run(command, check=True)
        environment = os.environ.copy()
        environment['PATH'] = str(Path(COMPILER).parent) + os.pathsep + environment.get('PATH', '')
        subprocess.run([str(binary)], check=True, env=environment)
        symbols = subprocess.check_output([NM, '-C', '--defined-only', str(binary)], text=True)
        for symbol in forbidden_symbols:
            assert symbol not in symbols, f'CRSF-only symbol exported without HAVE_CRSF: {symbol}'

HEADERS['Arduino.h'] = r'''
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <algorithm>
using std::min;
#define INPUT 0
#define INPUT_PULLUP 1
#define INPUT_PULLDOWN 3
#define LOW 0
#define HIGH 1
extern unsigned long testNow;
extern uint64_t testPinLevels;
inline unsigned long millis() { return testNow; }
inline void pinMode(int, int) {}
inline int digitalRead(int pin) { return (testPinLevels >> pin) & 1; }
inline void delay(unsigned long) {}
'''
HEADERS['Wire.h'] = r'''
#pragma once
#include "Arduino.h"
struct TwoWire {
    uint8_t port = 0xff;
    bool valid = true;
    int reads = 0;
    void beginTransmission(int) {}
    void write(uint8_t) {}
    int endTransmission(bool = true) { return 0; }
    int requestFrom(int, int n) { ++reads; return valid ? n : 0; }
    int read() { return port; }
};
extern TwoWire Wire;
'''

TEST = r'''
#include <cassert>
#include <vector>
#include "Arduino.h"
#include "input.h"
unsigned long testNow = 0;
uint64_t testPinLevels = UINT64_MAX;
TwoWire Wire;
std::vector<int> events;
void down() { events.push_back(1); }
void shortEnd() { events.push_back(2); }
void longStart() { events.push_back(3); }
void longEnd() { events.push_back(4); }
void packDown(int i) { events.push_back(10 * i + 1); }
void packShort(int i) { events.push_back(10 * i + 2); }
void packLong(int i) { events.push_back(10 * i + 3); }
void packEnd(int i) { events.push_back(10 * i + 4); }
void scan(RemButton &b, unsigned long now, bool active, bool valid = true) {
    testNow = now; b.scan(active, valid);
}
void scan(ButtonPack &b, unsigned long now, uint8_t states, uint8_t valid = 0xff) {
    testNow = now; b.scan(states, valid);
}
int main() {
    RemButton b;
    b.begin(1); b.setTiming(50, 300);
    b.attachPressDown(down); b.attachPressEnd(shortEnd);
    b.attachLongPressStart(longStart); b.attachLongPressStop(longEnd);
    scan(b, 100, true); scan(b, 120, false); // Bounce is discarded.
    assert(events.empty());
    scan(b, 200, true); scan(b, 260, true);
    scan(b, 300, false); scan(b, 360, false);
    assert((events == std::vector<int>{1, 2}));
    events.clear();
    scan(b, 400, true); scan(b, 460, true); scan(b, 701, true);
    scan(b, 750, false); scan(b, 810, false);
    assert((events == std::vector<int>{1, 3, 4}));
    events.clear();
    scan(b, 900, true); scan(b, 960, true);
    scan(b, 1000, true, false); scan(b, 1100, false);
    assert((events == std::vector<int>{1})); // No phantom short release.
    events.clear();
    scan(b, 1200, true); scan(b, 1501, true);
    scan(b, 1510, false); scan(b, 1520, false, false);
    scan(b, 1600, false);
    assert((events == std::vector<int>{3})); // No phantom long release.
    events.clear();
    b.setTiming(50, 50); // Maintained switches use long-start/stop.
    scan(b, 1700, true); scan(b, 1760, true);
    scan(b, 1800, false); scan(b, 1840, false); scan(b, 1860, false);
    assert((events == std::vector<int>{3, 4}));
    events.clear();
    // Existing GPIO scan forwards active-low levels to the same machine.
    testNow = 1900; testPinLevels &= ~(uint64_t(1) << 1); b.scan();
    testNow = 1960; b.scan();
    testNow = 2000; testPinLevels |= uint64_t(1) << 1; b.scan();
    testNow = 2060; b.scan();
    assert((events == std::vector<int>{3, 4}));

    const uint8_t addresses[] = {0x20, REM_BP_TYPE_PCA8574};
    ButtonPack pack(1, addresses);
    assert(pack.begin()); pack.setScanInterval(50);
    pack.attachPressDown(packDown); pack.attachPressEnd(packShort);
    pack.attachLongPressStart(packLong); pack.attachLongPressStop(packEnd);
    pack.setTiming(0, 50, 300); pack.setTiming(1, 50, 50);
    events.clear(); Wire.reads = 0;
    scan(pack, 2100, 1); scan(pack, 2110, 1);
    assert(events.empty()); // Sampled overload still respects scan interval.
    scan(pack, 2150, 1); scan(pack, 2200, 0); scan(pack, 2250, 0);
    assert((events == std::vector<int>{1, 2}));
    assert(Wire.reads == 0); // No duplicate I2C reads.
    events.clear();
    scan(pack, 2300, 3); scan(pack, 2350, 3); scan(pack, 2400, 3);
    scan(pack, 2401, 3, 0xfe); // Cancel button 0 even between scheduled scans.
    scan(pack, 2450, 0); scan(pack, 2500, 0);
    assert((events == std::vector<int>{1, 11, 13, 14}));
    events.clear();
    scan(pack, 2600, 1); scan(pack, 2650, 1); scan(pack, 2901, 1);
    scan(pack, 2951, 0); scan(pack, 2952, 0, 0xfe); scan(pack, 3001, 0);
    assert((events == std::vector<int>{1, 3}));
    events.clear();
    // Legacy polling keeps its timing and active-low normalization.
    testNow = 3100; Wire.port = 0xfe; pack.scan();
    testNow = 3110; pack.scan(); assert(Wire.reads == 1);
    testNow = 3150; pack.scan();
    testNow = 3200; Wire.port = 0xff; pack.scan();
    testNow = 3250; pack.scan();
    assert((events == std::vector<int>{1, 2}));
    assert(Wire.reads == 4);
    events.clear();
    testNow = 3300; Wire.port = 0xfe; pack.scan();
    testNow = 3350; pack.scan();
    testNow = 3400; Wire.valid = false; Wire.port = 0xff; pack.scan();
    testNow = 3410; Wire.valid = true; pack.scan();
    assert(Wire.reads == 7); // Failed polls still advance the legacy interval.
    testNow = 3450; pack.scan(); testNow = 3500; pack.scan();
    assert((events == std::vector<int>{1, 2})); // Legacy poll failure keeps pending state.

    REMRotEnc throttle(0, nullptr);
    throttle.setZeroPos(1000);
    assert(throttle.setMaxStepsUp(500) && throttle.setMaxStepsDown(-400));
    const int readsBeforeCache = Wire.reads;
    throttle.useSampledPosition(1500); assert(throttle.updateThrottlePos(true) == 100);
    throttle.useSampledPosition(600); assert(throttle.updateThrottlePos(true) == -100);
    throttle.useSampledPosition(1250); assert(throttle.updateThrottlePos(true) == 50);
    throttle.useSampledPosition(800); assert(throttle.updateThrottlePos(true) == -50);
    throttle.useSampledPosition(1500, false); assert(throttle.updateThrottlePos(true) == 0);
    assert(throttle.setMaxStepsUp(-500) && throttle.setMaxStepsDown(400));
    throttle.useSampledPosition(500); assert(throttle.updateThrottlePos(true) == 100);
    throttle.useSampledPosition(1400); assert(throttle.updateThrottlePos(true) == -100);
    throttle.useSampledPosition(1250); throttle.zeroPos(true);
    assert(throttle.getZeroPos() == 1250 && throttle.updateThrottlePos(true) == 0);
    throttle.useSampledPosition(1700); assert(throttle.setMaxStepsUp());
    throttle.useSampledPosition(1000); assert(throttle.setMaxStepsDown());
    assert(throttle.getMaxStepsUp() == 450 && throttle.getMaxStepsDown() == -250);
    throttle.useSampledPosition(900, false); assert(throttle.updateThrottlePos(true) == 0);
    assert(Wire.reads == readsBeforeCache);

    // ADC mode retains the existing deadband/scaling and begin restores polling.
    const uint8_t adcAddress[] = {0x48, REM_RE_TYPE_ADS1X15};
    REMRotEnc adc(1, adcAddress);
    assert(adc.begin(true, true));
    adc.setZeroPos(1000); adc.setMaxStepsUp(500); adc.setMaxStepsDown(-400);
    adc.useSampledPosition(1250); assert(adc.updateThrottlePos(true) == 41);
    adc.useSampledPosition(800); assert(adc.updateThrottlePos(true) == -25);
    adc.useSampledPosition(1000); assert(adc.updateThrottlePos(true) == 0);
    assert(Wire.reads == readsBeforeCache);
    adc.useSampledPosition(1500); adc.setZeroPos(0);
    Wire.port = 0; assert(adc.begin(true, true));
    assert(adc.updateThrottlePos(true) == 0 && Wire.reads == readsBeforeCache + 1);
    puts("CRSF sampled button scanners and cancellation check passed");
    puts("CRSF sampled throttle calibration, neutral failure and legacy polling check passed");
}
'''

LEGACY_TEST = r'''
#include <cassert>
#include <vector>
#include "Arduino.h"
#include "input.h"
unsigned long testNow = 0;
uint64_t testPinLevels = UINT64_MAX;
TwoWire Wire;
std::vector<int> events;
void down() { events.push_back(1); }
void shortEnd() { events.push_back(2); }
void packDown(int i) { events.push_back(10 * i + 1); }
void packShort(int i) { events.push_back(10 * i + 2); }
int main() {
    RemButton button;
    button.begin(1); button.setTiming(50, 300);
    button.attachPressDown(down); button.attachPressEnd(shortEnd);
    testNow = 100; testPinLevels &= ~(uint64_t(1) << 1); button.scan();
    testNow = 160; button.scan();
    testNow = 200; testPinLevels |= uint64_t(1) << 1; button.scan();
    testNow = 260; button.scan();
    assert((events == std::vector<int>{1, 2}));

    const uint8_t addresses[] = {0x20, REM_BP_TYPE_PCA8574};
    ButtonPack pack(1, addresses);
    assert(pack.begin()); pack.setScanInterval(50); pack.setTiming(0, 50, 300);
    pack.attachPressDown(packDown); pack.attachPressEnd(packShort);
    events.clear();
    testNow = 300; Wire.port = 0xfe; pack.scan();
    testNow = 350; pack.scan();
    testNow = 400; Wire.port = 0xff; pack.scan();
    testNow = 450; pack.scan();
    assert((events == std::vector<int>{1, 2}));
    puts("Legacy GPIO and I2C scanners check passed");
}
'''

if __name__ == '__main__':
    compile_and_run(TEST, ['-DHAVE_CRSF'])
    compile_and_run(LEGACY_TEST, forbidden_symbols=(
        'REMRotEnc::useSampledPosition',
        'RemButton::scan(bool, bool)',
        'ButtonPack::scan(unsigned char, unsigned char)',
    ))
