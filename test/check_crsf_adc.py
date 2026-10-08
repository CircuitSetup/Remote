"""Exercise the actual ADC adapter with target clocks and timed I2C/UART."""
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
#include <vector>
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
#ifdef CRSF_TIMED_ADC_TEST
extern uint64_t testClockUs, testPinLevels;
extern uint32_t testMillisOrigin, testMicrosOrigin, maxDelayUs;
extern std::vector<uint64_t> oeOnTimes, oeOffTimes;
inline unsigned long millis() { return (uint32_t)(testMillisOrigin + testClockUs / 1000); }
inline unsigned long micros() { return (uint32_t)(testMicrosOrigin + testClockUs); }
inline void advanceTestTime(uint32_t us) { testClockUs += us; }
inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int value) {
    if(pin == 0) (value == LOW ? oeOnTimes : oeOffTimes).push_back(testClockUs);
}
inline int digitalRead(int pin) { return (testPinLevels >> pin) & 1; }
inline void delayMicroseconds(unsigned long us) { if(us > maxDelayUs) maxDelayUs = us; advanceTestTime(us); }
#else
extern unsigned long testNow;
inline unsigned long millis() { return (uint32_t)testNow; }
inline unsigned long micros() { return (uint32_t)(testNow * 1000UL); }
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return HIGH; }
inline void delayMicroseconds(unsigned long) {}
#endif
''',
'HardwareSerial.h': r'''
#pragma once
#include "Arduino.h"
#include <stdarg.h>
#include <string>
#include <vector>
struct TxEvent { uint64_t at; uint8_t type; size_t length; };
extern uint8_t lastRcFrame[26];
extern std::vector<TxEvent> txEvents;
extern std::vector<uint64_t> txStops, adcLogTimes, probeLogTimes;
extern std::vector<std::string> adcLogs, probeLogs;
struct HardwareSerial {
    uint32_t baud = 400000;
    size_t bytes = 0;
    HardwareSerial(int = 0) {}
    void begin(unsigned long b, int, int, int, bool) { baud = b; }
    void end() {}
    int available() { return 0; }
    int read() { return -1; }
    size_t write(const uint8_t *data, size_t n) {
        txEvents.push_back({testClockUs, data[2], n}); bytes = n;
        if(n == 26 && data[2] == 0x16) memcpy(lastRcFrame, data, n);
        return n;
    }
    void flush() { advanceTestTime((bytes * 10000000ULL + baud - 1) / baud); txStops.push_back(testClockUs); }
    void println(const char *) {}
    void printf(const char *format, ...) {
        char text[256]; va_list args; va_start(args, format);
        vsnprintf(text, sizeof(text), format, args); va_end(args);
        if(strncmp(text, "ELRS/CRSF ADC raw:", 18) == 0) { adcLogs.emplace_back(text); adcLogTimes.push_back(testClockUs); }
        if(strncmp(text, "ELRS/CRSF ADC: ADS1015 probe", 26) == 0) { probeLogs.emplace_back(text); probeLogTimes.push_back(testClockUs); }
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
#include <cassert>
#include "Arduino.h"
struct TwoWire {
    int16_t values[4] = {1000,800,600,400}, latched = 0;
    bool connected = true, stuck = false, changing = false;
    int failure = 0, failingChannel = -1;
    int writes = 0, channel = 0, byte = 0, reg = 0;
    uint8_t cfgHigh = 0, cfgLow = 0;
    uint64_t readyAt = 0;
    unsigned transactions = 0, sweeps = 0, starts = 0, probes = 0;
    std::vector<uint64_t> completions;
    void beginTransmission(uint8_t) { writes = 0; }
    void write(uint8_t value) {
        if(++writes == 1) reg = value;
        if(writes == 2) { cfgHigh = value; channel = ((value >> 4) & 7) - 4; }
        if(writes == 3) cfgLow = value;
    }
    bool fails(int kind) { return !connected || (failure == kind && (failingChannel < 0 || channel == failingChannel)); }
    int endTransmission(bool = true) {
        transactions++; advanceTestTime(writes == 3 ? 90 : (writes == 1 ? 45 : 23));
        if(!writes) probes++;
        if(fails(writes == 3 ? 1 : 2)) return 1;
        if(writes == 3) {
            assert(channel >= 0 && channel < 4);
            assert(cfgHigh == (0xC3 | (channel << 4)) && cfgLow == 0xE3);
            starts++; readyAt = testClockUs + 500;
            latched = changing ? 300 + (sweeps % 8) * 150 + channel * 20 : values[channel];
        }
        return 0;
    }
    int requestFrom(uint8_t, uint8_t n) {
        transactions++; advanceTestTime(68); byte = 0;
        return fails(3) ? 1 : n;
    }
    int read() {
        const bool ready = !stuck && testClockUs >= readyAt;
        uint16_t raw = reg == 1 ? ((cfgHigh & 0x7F) | (ready ? 0x80 : 0)) << 8 | cfgLow : (uint16_t)latched << 4;
        if(reg == 0) assert(ready); // Never return instantaneous conversions.
        const int value = byte++ ? raw & 255 : raw >> 8;
        if(reg == 0 && byte == 2 && channel == 3) { sweeps++; completions.push_back(testClockUs); }
        return value;
    }
};
extern TwoWire Wire;
'''
}
TEST = r'''
#include <cassert>
#include <algorithm>
#include <vector>
#include "HardwareSerial.h"
#include "remote_global.h"
#include "src/CRSF/elrs_crsf.h"
uint64_t testClockUs = 0, testPinLevels = UINT64_MAX;
uint32_t testMillisOrigin = 0, testMicrosOrigin = 0, maxDelayUs = 0;
HardwareSerial Serial;
uint8_t lastRcFrame[26] = {};
std::vector<TxEvent> txEvents;
std::vector<uint64_t> txStops, oeOnTimes, oeOffTimes, adcLogTimes, probeLogTimes;
std::vector<std::string> adcLogs, probeLogs;
TwoWire Wire;
std::string displayText;
int calibrationSaves = 0;
ELRSAxisCalibrationData savedCalibration[4];
remDisplay::remDisplay(uint8_t) {}
void remDisplay::on() {}
void remDisplay::setText(const char *text) { displayText = text; }
void remDisplay::setSpeed(int) {}
void remDisplay::show() {}
void remLED::setState(bool) {}
bool remLED::getState() { return false; }
bool ButtonPack::sampleStates(uint8_t &) { return false; }
void loadELRSCalibration(ELRSAxisCalibrationData *cal, int count) { for(int i = 0; i < count; i++) cal[i] = {0,1024,2047}; }
bool saveELRSCalibration(const ELRSAxisCalibrationData *cal, int count) {
    calibrationSaves++; for(int i = 0; i < count; i++) savedCalibration[i] = cal[i]; return true;
}
int localScans = 0;
void queueCRSFLocalSwitches(uint16_t, uint16_t) { localScans++; }
ELRSInputAxisProfile profiles[4];
static void resetFixture(uint32_t ms = 0, uint32_t us = 0) {
    testClockUs = 0; testMillisOrigin = ms; testMicrosOrigin = us; maxDelayUs = 0;
    Wire = TwoWire(); txEvents.clear(); txStops.clear(); oeOnTimes.clear(); oeOffTimes.clear();
    adcLogs.clear(); probeLogs.clear(); adcLogTimes.clear(); probeLogTimes.clear();
    testPinLevels = UINT64_MAX; displayText.clear(); calibrationSaves = 0;
    for(int i = 0; i < 4; i++) profiles[i] = elrsDefaultInputAxisProfile();
}
static void beginMode(ELRSCrsfMode &mode, uint16_t rate = 250, bool prop = false, const ELRSOutputLimits *limits = nullptr, uint16_t tolerance = 5, remDisplay *display = nullptr) {
    assert(mode.begin(rate, 0, 0, 0, 0, profiles, elrsDefaultGimbalRouting(),
        nullptr, false, display, nullptr, nullptr, nullptr, false, false, false, false,
        nullptr, tolerance, tolerance, nullptr, limits, nullptr, prop));
}
static void runFor(ELRSCrsfMode &mode, uint64_t us) {
    const uint64_t end = testClockUs + us;
    while(testClockUs < end) { mode.loop(0); if(testClockUs < end) advanceTestTime(20); }
}
static void completeSweep(ELRSCrsfMode &mode) {
    const unsigned wanted = Wire.sweeps + 1;
    const uint64_t end = testClockUs + 50000;
    while(Wire.sweeps < wanted && testClockUs < end) { mode.loop(0); if(Wire.sweeps < wanted) advanceTestTime(20); }
    assert(Wire.sweeps == wanted);
}
static uint16_t ticks(int channel) {
    const int bit = channel * 11, byte = 3 + bit / 8;
    const uint32_t packed = lastRcFrame[byte] | (lastRcFrame[byte+1] << 8) | (lastRcFrame[byte+2] << 16);
    return (packed >> (bit % 8)) & 0x7ff;
}
static void test_ads_pending_conversion_does_not_block_rc_slots() {
    resetFixture(); ELRSCrsfMode mode; beginMode(mode, 500, true);
    assert(maxDelayUs <= 150); // The old adapter blocks 500us per channel, including begin().
    int16_t axes[4]; assert(!mode.readCurrentRawAxes(nullptr)); assert(!mode.readCurrentRawAxes(axes));
    assert(!(mode.getStatus().faultFlags & ELRS_FAULT_ADC_MISSING));
    Wire.stuck = true; runFor(mode, 8000);
    assert(txEvents.size() >= 4 && Wire.sweeps == 0);
    assert(!(mode.getStatus().faultFlags & (ELRS_FAULT_ADC_MISSING | ELRS_FAULT_ADC_STALE)));
    assert(ticks(0) == 992 && ticks(2) == 992);
    Wire.stuck = false; completeSweep(mode); assert(mode.readCurrentRawAxes(axes));
    puts("test_ads_pending_conversion_does_not_block_rc_slots passed");
}
static void test_500hz_rc_cadence_during_ads_scan() {
    // Declare the bound before collecting results: poll 20us + max I2C pass 45+68us.
    constexpr uint32_t delta = 133;
    resetFixture(); ELRSCrsfMode mode; beginMode(mode, 500, true); Wire.changing = true;
    runFor(mode, 6000000); // Finish the finite bootstrap probes before measuring RC-only slots.
    const uint64_t origin = txEvents.front().at;
    txEvents.clear(); const unsigned before = Wire.sweeps;
    runFor(mode, 10000000);
    uint64_t sum = 0, minimum = UINT64_MAX, maximum = 0, maxLate = 0;
    for(size_t i = 0; i < txEvents.size(); i++) {
        assert(txEvents[i].type == 0x16 && txEvents[i].length == 26);
        const uint64_t late = (txEvents[i].at - origin) % 2000;
        maxLate = std::max(maxLate, late);
        if(i) { const uint64_t gap = txEvents[i].at - txEvents[i-1].at;
            sum += gap; minimum = std::min(minimum, gap); maximum = std::max(maximum, gap);
        }
    }
    printf("500Hz simulated: frames=%u sweeps=%u TX min/max/mean=%llu/%llu/%.3fus max lateness=%lluus delta=%uus\n",
        (unsigned)txEvents.size(), Wire.sweeps-before, minimum, maximum, (double)sum/(txEvents.size()-1), maxLate, delta);
    fflush(stdout);
    assert(txEvents.size() >= 4999 && txEvents.size() <= 5001);
    assert(minimum >= 2000-delta && maximum <= 2000+delta && maxLate <= delta);
    assert(Wire.sweeps-before > 1000);
    for(size_t i = 1; i < Wire.completions.size(); i++) assert(Wire.completions[i]-Wire.completions[i-1] < 10000);
    int16_t axes[4]; assert(mode.readCurrentRawAxes(axes));
    puts("test_500hz_rc_cadence_during_ads_scan passed");
}
static void test_axes_publish_only_complete_sweeps() {
    for(int failure = 1; failure <= 3; failure++) for(int channel = 0; channel < 4; channel++) {
        resetFixture(); ELRSCrsfMode mode; beginMode(mode); completeSweep(mode);
        int16_t old[4], axes[4]; assert(mode.readCurrentRawAxes(old));
        for(int i = 0; i < 4; i++) Wire.values[i] += 400;
        Wire.failure = failure; Wire.failingChannel = channel;
        const unsigned sweeps = Wire.sweeps;
        runFor(mode, 9000); assert(Wire.sweeps == sweeps);
        assert(!mode.readCurrentRawAxes(axes));
        for(int i = 0; i < 4; i++) Wire.values[i] = 1800 - i * 100;
        Wire.failure = 0; completeSweep(mode); assert(mode.readCurrentRawAxes(axes));
        for(int i = 0; i < 4; i++) assert(axes[i] == Wire.values[i]); // Discard partial values and reseed.
    }
    resetFixture(); ELRSCrsfMode mode; beginMode(mode); completeSweep(mode);
    int16_t axes[4]; for(int i = 0; i < 4; i++) Wire.values[i] += 400;
    runFor(mode, 700); // Conversion pending; the preceding complete sample remains coherent.
    assert(mode.readCurrentRawAxes(axes)); const int16_t old[4] = {1000,800,600,400};
    for(int i = 0; i < 4; i++) assert(axes[i] == old[i]);
    puts("test_axes_publish_only_complete_sweeps passed");
}
static void test_cached_axes_do_not_refresh_input_age() {
    for(bool fail : {false,true}) {
        resetFixture(UINT32_MAX-50, UINT32_MAX-1500); ELRSCrsfMode mode; beginMode(mode); completeSweep(mode);
        const uint32_t goodAt = millis();
        const uint64_t goodUs = testClockUs; int16_t axes[4];
        Wire.stuck = !fail; Wire.connected = !fail;
        runFor(mode, 98000);
        // Reader-only requests cannot probe, acquire, or refresh age.
        testClockUs = goodUs + 100000; // Same millisecond fraction as the completion.
        const unsigned transactions = Wire.transactions;
        assert(mode.readCurrentRawAxes(axes) == !fail);
        for(int n = 0; n < 1000; n++) mode.readCurrentRawAxes(axes);
        assert(Wire.transactions == transactions && (uint32_t)(millis()-goodAt) == 100);
        mode.loop(0); // Completion/transport may advance within this millisecond.
        testClockUs = goodUs + 101000;
        assert(!mode.readCurrentRawAxes(axes)); mode.loop(0);
        assert(mode.getStatus().faultFlags & ELRS_FAULT_ADC_STALE);
        runFor(mode, 4000); // Observe the fallback in the next scheduled RC slot.
        assert(ticks(0) == 992 && ticks(2) == 992);
        Wire.connected = true; Wire.stuck = false; completeSweep(mode); assert(mode.readCurrentRawAxes(axes));
        assert(!(mode.getStatus().faultFlags & (ELRS_FAULT_ADC_MISSING | ELRS_FAULT_ADC_STALE)));
    }
    puts("test_cached_axes_do_not_refresh_input_age passed");
}
static void test_identical_new_sweeps_remain_fresh() {
    resetFixture(); ELRSCrsfMode mode; beginMode(mode, 50); runFor(mode, 300000);
    int16_t axes[4]; assert(Wire.sweeps >= 14 && Wire.sweeps <= 16);
    assert(mode.readCurrentRawAxes(axes)); assert(!(mode.getStatus().faultFlags & ELRS_FAULT_ADC_STALE));
    puts("test_identical_new_sweeps_remain_fresh passed");
}
static void test_filter_and_hysteresis_update_once_per_new_sample() {
    resetFixture(); ELRSCrsfMode mode; beginMode(mode); completeSweep(mode);
    int16_t axes[4]; const int16_t old[4] = {1000,800,600,400};
    for(int i = 0; i < 4; i++) Wire.values[i] += 20;
    completeSweep(mode); assert(mode.readCurrentRawAxes(axes));
    for(int i = 0; i < 4; i++) assert(axes[i] == old[i]); // IIR +5, held inside tolerance.
    for(int i = 0; i < 4; i++) Wire.values[i] = old[i] + 405;
    const unsigned transactions = Wire.transactions;
    for(int poll = 0; poll < 1000; poll++) { assert(mode.readCurrentRawAxes(axes)); for(int i = 0; i < 4; i++) assert(axes[i] == old[i]); }
    assert(Wire.transactions == transactions);
    completeSweep(mode); assert(mode.readCurrentRawAxes(axes));
    for(int i = 0; i < 4; i++) assert(axes[i] == old[i] + 105); // 1005 -> 1105 exactly once.
    puts("test_filter_and_hysteresis_update_once_per_new_sample passed");
}
static void test_adapter_neutral_endpoints_limits_and_consumers() {
    for(bool prop : {false,true}) for(bool descending : {false,true}) for(bool reverse : {false,true}) {
        resetFixture(); ELRSCrsfMode mode;
        for(int i = 0; i < 4; i++) {
            profiles[i].minimum = descending ? 1020 : 1000; profiles[i].center = 1010;
            profiles[i].maximum = descending ? 1000 : 1020; profiles[i].reverse = reverse;
            Wire.values[i] = 1014;
        }
        beginMode(mode, 250, prop); completeSweep(mode);
        for(int i = 0; i < 4; i++) Wire.values[i] = 1010;
        runFor(mode, 400000); int16_t axes[4]; assert(mode.readCurrentRawAxes(axes));
        for(int i = 0; i < 4; i++) assert(axes[i] == 1010);
        assert(ticks(0) == 992 && ticks(1) == 992 && ticks(2) == 992 && ticks(3) == 992);
    }
    const ELRSOutputLimits limits[4] = {{1200,1800},{1200,1800},{1200,1800},{1200,1800}};
    const int16_t raw[] = {0,1024,2047}; const uint16_t expected[] = {500,992,1483};
    for(int point = 0; point < 3; point++) {
        resetFixture(); ELRSCrsfMode mode; for(int i = 0; i < 4; i++) Wire.values[i] = raw[point];
        beginMode(mode, 250, false, limits); runFor(mode, 12000); int16_t axes[4]; assert(mode.readCurrentRawAxes(axes));
        for(int i = 0; i < 4; i++) { assert(axes[i] == raw[point]); assert(ticks(i) == expected[point]); }
    }
    puts("CRSF real ADC filter neutral, endpoints, reversal, limits, standalone/prop readers passed");
}
static void pressCalibration(ELRSCrsfMode &mode, bool longPress = false) {
    testPinLevels &= ~(1ULL << CALIBB_IO_PIN); runFor(mode, longPress ? 2070000 : 60000);
    testPinLevels |= 1ULL << CALIBB_IO_PIN; runFor(mode, 60000);
}
static void test_calibration_discards_inflight_sweep_and_captures_fresh_once() {
    resetFixture(); ELRSCrsfMode mode; remDisplay display(0x70); beginMode(mode, 250, false, nullptr, 5, &display);
    completeSweep(mode); pressCalibration(mode, true); runFor(mode, 220000);
    assert(mode.isCalibrating() && displayText == "CEN");
    // Freeze A2 after A0/A1 were already staged, then request a different center.
    const uint64_t limit = testClockUs + 10000;
    while(Wire.channel != 2 && testClockUs < limit) { mode.loop(0); advanceTestTime(20); }
    assert(Wire.channel == 2); Wire.stuck = true;
    for(int i = 0; i < 4; i++) Wire.values[i] = 2000;
    const size_t frames = txEvents.size(); pressCalibration(mode);
    assert(txEvents.size() >= frames + 25);
    assert(mode.isCalibrating());
    // The unchanged stale-input overlay covers the prompt for one second.
    Wire.stuck = false; completeSweep(mode); runFor(mode, 1200000);
    assert(displayText == "TLO"); runFor(mode, 220000); assert(displayText == "TLO");
    for(int point = 0; point < 8; point++) {
        for(int i = 0; i < 4; i++) Wire.values[i] = point & 1 ? 2047 : 0;
        runFor(mode, 220000); pressCalibration(mode);
    }
    assert(!mode.isCalibrating() && calibrationSaves == 1);
    const int16_t centers[4] = {1250,1100,950,800};
    for(int i = 0; i < 4; i++) assert(savedCalibration[i].center == centers[i]);
    assert(maxDelayUs <= 150);
    puts("Actual adapter calibration discards partial sweep, captures fresh axes once, and keeps RC slots passed");
}
static void test_missing_adc_retry_restart_and_diagnostics() {
    resetFixture(); Wire.connected = false; ELRSCrsfMode mode; beginMode(mode); runFor(mode, 300000);
    int16_t axes[4]; assert(!mode.readCurrentRawAxes(axes)); assert(ticks(0) == 992 && ticks(2) == 992);
    assert(Wire.probes <= 17); const unsigned probes = Wire.probes;
    for(int i = 0; i < 1000; i++) assert(!mode.readCurrentRawAxes(axes)); assert(Wire.probes == probes);
    Wire.connected = true; completeSweep(mode); assert(mode.readCurrentRawAxes(axes));
    for(int i = 0; i < 4; i++) assert(axes[i] == Wire.values[i]);
    Wire.values[0] = 1800; runFor(mode, 700); beginMode(mode); assert(!mode.readCurrentRawAxes(axes));
    completeSweep(mode); assert(mode.readCurrentRawAxes(axes)); assert(axes[0] == 1800);
#ifdef REMOTE_DBG
    const size_t diagnosticStart = adcLogTimes.size();
    Wire.changing = true; runFor(mode, 600000);
    for(size_t i = diagnosticStart; i < adcLogTimes.size(); i++)
        assert(adcLogTimes[i]-adcLogTimes[i-1] >= 199000);
    for(size_t i = 1; i < probeLogTimes.size(); i++) if(probeLogTimes[i] < 300000) assert(probeLogTimes[i]-probeLogTimes[i-1] >= 199000);
#else
    assert(adcLogs.empty() && probeLogs.empty());
#endif
    puts("CRSF missing ADC bounded retries, restart, clean recovery, diagnostic rate limits passed");
}
int main(int argc, char **argv) {
    if(argc > 1) { test_500hz_rc_cadence_during_ads_scan(); return 0; }
    test_ads_pending_conversion_does_not_block_rc_slots();
    test_500hz_rc_cadence_during_ads_scan();
    test_axes_publish_only_complete_sweeps();
    test_cached_axes_do_not_refresh_input_age();
    test_identical_new_sweeps_remain_fresh();
    test_filter_and_hysteresis_update_once_per_new_sample();
    test_adapter_neutral_endpoints_limits_and_consumers();
    test_calibration_discards_inflight_sweep_and_captures_fresh_once();
    test_missing_adc_retry_restart_and_diagnostics();
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
    compile_and_run(TEST, sources, ['-DCRSF_TIMED_ADC_TEST'])
    compile_and_run(TEST, sources, ['-DCRSF_TIMED_ADC_TEST', '-DREMOTE_DBG'])
