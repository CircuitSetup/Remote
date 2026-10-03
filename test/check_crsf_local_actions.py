"""Run the production Settings event dispatch with audio/network/storage sinks.

Catches dropped long/short Settings behavior, wrong power gating, ignored valid
masks, and local changes that never reach their delayed persistent save.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
COMPILER = shutil.which('g++') or str(Path.home() / '.platformio/packages/toolchain-gccmingw32/bin/g++.exe')


def function(source, name):
    match = re.search(r'^(?:static )?(?:void|bool) ' + name + r'\([^;]*?\)\s*\{', source, re.M)
    assert match, f'CRSF Settings dispatch missing: {name}'
    start = match.start()
    brace = source.index('{', match.start())
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


STUBS = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <cstring>
#include <vector>
#define HAVE_CRSF
#define HAVE_MQTT
#define ALLOW_DIS_UB
#define PACK_SIZE 8
#define CSF_OFF 1
#define CSF_TCDINP0 2
#define CSF_TT 16
#define CSF_CALIBMD 32
#define CSF_KEEPCOUNTING 64
#define PA_ALLOWSD 1
#define PA_INTRMUS 2
#define PA_DYNVOL 4
unsigned long now = 100, volchgtimer = 0, brichgtimer = 0, offDisplayNow = 0;
unsigned long volchgnow = 0, brichgnow = 0, vischgnow = 0;
unsigned long millis() { return now; }
unsigned long millisNonZero() { return now ? now : 1; }
unsigned long maxDelay = 0;
int wifiTicks = 0, audioTicks = 0, bttfnTicks = 0;
void delay(unsigned long duration) { now += duration; if(duration > maxDelay) maxDelay = duration; }
void wifi_loop() { wifiTicks++; }
void audio_loop() { audioTicks++; }
void bttfn_loop_quick() { bttfnTicks++; }
uint32_t csf = CSF_OFF;
int throttlePos = 0, currSpeed = 0, triggerTTonThrottle = 0, triggerIntTTonThrottle = 0;
bool opModeCRSF = true, powerState = false, brakeState = false, useBPack = true;
bool opModePropCRSF = false, crsfStarted = false;
uint16_t crsfLocalStates = 0, crsfLocalValidMask = 0;
uint16_t crsfLocalInvalidMask = 0;
bool crsfLocalPending = false;
int battWarn = 5, crsfTicks = 0, sampledPosition = 0;
bool sampledValid = false, rawAxesValid = true;
void serviceCRSF(bool withLocalActions = true);
bool recursiveService = false;
void crsf_loop(int) {
    crsfTicks++;
    if(recursiveService) serviceCRSF(false);
}
bool readELRSCurrentRawAxes(int16_t axes[4]) { axes[3] = 1234; return rawAxesValid; }
struct { void useSampledPosition(int position, bool valid) { sampledPosition = position; sampledValid = valid; } } rotEnc;
bool haveMusic = true, mpActive = false, autoThrottle = false, ooTT = false, resAT = false;
bool ooresBri = true, powerMaster = false, useRotEncVol = false, offDisplayTimer = false;
bool brakeWarning = false, havePOFFsnd = true, haveBOFFsnd = true, tcdIsBusy = false;
bool triggerRefill = false;
bool triggerCompleteUpdate = false;
int refillButton = 1;
bool isbuttonAKeyChange = false, isbuttonAKeyPressed = false, isbuttonAKeyLongPressed = false;
bool isbuttonBKeyChange = false, isbuttonBKeyPressed = false, isbuttonBKeyLongPressed = false;
bool isFPBKeyChange = false, isFPBKeyPressed = false, isBrakeKeyChange = false, isBrakeKeyPressed = false;
bool isbutPackKeyChange[8] = {}, isbutPackKeyPressed[8] = {}, isbutPackKeyLongPressed[8] = {};
bool buttonPackMomentary[8] = {}, buttonPackMtOnOnly[8] = {};
int MQTTbuttonOnLen[8] = {2}, MQTTbuttonOffLen[8] = {3};
struct { const char *mqttbt[8] = {"topic"}; const char *mqttbo[8] = {"ON"}; const char *mqttbf[8] = {"OFF"}; } settings;
struct { bool mpShuffle = false; } aud_state;
std::vector<std::string> sounds, messages;
int prevCount = 0, nextCount = 0, stopCount = 0, playCount = 0, audioStops = 0;
int volume = 5, brightness = 5, volumeDisplays = 0, brightnessDisplays = 0;
int savedVol = 0, savedBri = 0, savedVis = 0, combined = 0, flushes = 0, tt = 0;
bool networkAvailable = true;
uint8_t configuredLocalActions[12] = {};
void loadELRSInputConfig(void*,int,void*,void*,void*,void*,void*,uint8_t *flags) { memcpy(flags, configuredLocalActions, 12); }
const char *powerOnSnd = "/poweron.mp3", *powerOffSnd = "/poweroff.mp3";
const char *brakeOnSnd = "/brakeon.mp3", *brakeOffSnd = "/brakeoff.mp3";
void play_file(const char *path, int) { sounds.emplace_back(path); }
void play_bad() { sounds.emplace_back("bad"); }
void play_key(int key, bool longPress=false, bool stopOnly=false) {
    sounds.emplace_back(std::to_string(key) + (longPress ? "long" : stopOnly ? "stop" : "short"));
}
void mp_prev(bool) { prevCount++; }
void mp_next(bool) { nextCount++; }
void mp_stop(bool=false) { mpActive = false; stopCount++; }
void mp_play() { mpActive = true; playCount++; }
void stopAudio() { audioStops++; }
void mp_makeShuffle(bool active) { aud_state.mpShuffle = active; }
void increaseVolume() { volume++; volchgnow = now; }
void decreaseVolume() { volume--; volchgnow = now; }
void displayVolume() { volumeDisplays++; }
void increaseBrightness() { brightness++; brichgnow = now; }
void decreaseBrightness() { brightness--; brichgnow = now; }
void displayBrightness() { brightnessDisplays++; }
void updateVisMode() {}
void triggerSaveVis() { vischgnow = now; }
void toggleAutoThrottle() { autoThrottle = !autoThrottle; triggerSaveVis(); }
void bttfn_remote_send_combined(bool,bool,uint8_t) { combined++; }
bool bttfn_trigger_tt(bool probe) { if(!probe && networkAvailable && !tcdIsBusy) tt++; return networkAvailable && !tcdIsBusy; }
void saveCurVolume() { savedVol++; }
void saveBrightness() { savedBri++; }
void saveVis() { savedVis++; }
void flushDelayedSave() { flushes++; }
bool mqttPublish(const char *, const char *payload, unsigned int length) { messages.emplace_back(payload, length); return true; }
struct ButtonSink {
    bool active = false, valid = false;
    void scan(bool a,bool v) { active=a; valid=v; }
} buttonA, buttonB;
struct PackSink {
    uint8_t states = 0, validMask = 0;
    void scan(uint8_t s,uint8_t v) { states=s; validMask=v; }
    int getPackSize() { return 8; }
} butPack;
'''

TEST = r'''
int main() {
    serviceCRSF(); assert(crsfTicks == 0);
    crsfStarted = true; serviceCRSF(); assert(crsfTicks == 1 && !sampledValid);
    opModePropCRSF = true; serviceCRSF(); assert(crsfTicks == 2 && sampledValid && sampledPosition == 1234);
    rawAxesValid = false; serviceCRSF(); assert(crsfTicks == 3 && !sampledValid);
    recursiveService = true; serviceCRSF(false);
    assert(crsfTicks == 4);
    recursiveService = false; serviceCRSF();
    assert(crsfTicks == 5);
    queueCRSFLocalSwitches(2, 0);
    serviceCRSF(false); assert(crsfLocalPending);
    serviceCRSF(); assert(!crsfLocalPending && powerState);
    // A failed pack read during a network wait cancels pending Settings events,
    // even if the latest read has recovered before normal dispatch resumes.
    isbutPackKeyChange[0] = true;
    queueCRSFLocalSwitches(0, 0); serviceCRSF(false);
    queueCRSFLocalSwitches(0, 0x10); serviceCRSF();
    assert(!isbutPackKeyChange[0] && butPack.validMask == 0);
    queueCRSFLocalSwitches(0, 0x10); serviceCRSF();
    assert(butPack.validMask == 1);
    opModePropCRSF = false;
    powerswitch.setTiming(50, 50);
    powerswitch.attachLongPressStart(powKeyPressed);
    powerswitch.attachLongPressStop(powKeyLongPressStop);
    brake.setTiming(50, 50);
    brake.attachLongPressStart(brakeKeyPressed);
    brake.attachLongPressStop(brakeKeyLongPressStop);
    assert(!crsfLocalActionsEnabled());
    configuredLocalActions[11] = 1; assert(crsfLocalActionsEnabled());
    configuredLocalActions[11] = 0; assert(!crsfLocalActionsEnabled());
    // No opt-in still synchronizes FakePower and suppresses stale events.
    isbuttonAKeyChange = isbuttonBKeyChange = isbutPackKeyChange[0] = true;
    processCRSFLocalSwitches(2, 0);
    assert(!(csf & CSF_OFF) && powerState && !brakeState);
    assert(sounds.empty() && !buttonA.valid && !buttonB.valid && butPack.validMask == 0);
    assert(!isbuttonAKeyChange && !isbuttonBKeyChange && !isbutPackKeyChange[0]);
    processCRSFLocalSwitches(0x852, 0x854);
    assert(buttonA.valid && !buttonA.active && !buttonB.valid);
    assert(butPack.states == 0x85 && butPack.validMask == 0x85);
    // FakePower and Stop sounds require opt-in and stable scanner transitions.
    processCRSFLocalSwitches(0, 3);
    assert(sounds.empty());
    now += 60; processCRSFLocalSwitches(3, 3); assert(sounds.empty());
    now += 51; processCRSFLocalSwitches(3, 3);
    assert(sounds[sounds.size()-2] == "/poweron.mp3" && sounds.back() == "/brakeon.mp3");
    now += 1; processCRSFLocalSwitches(0, 3); assert(sounds.size() == 2);
    now += 10; processCRSFLocalSwitches(3, 3); assert(sounds.size() == 2); // Bounce back.
    now += 1; processCRSFLocalSwitches(0, 3);
    now += 50; processCRSFLocalSwitches(0, 3);
    assert(sounds.back() == "/poweroff.mp3" && audioStops == 1 && stopCount == 1 && flushes == 1);
    processCRSFLocalSwitches(0, 3); assert(sounds.size() == 3);
    now += 1; processCRSFLocalSwitches(3, 3);
    now += 51; processCRSFLocalSwitches(3, 3);
    assert(sounds[sounds.size()-2] == "/poweron.mp3" && sounds.back() == "/brakeon.mp3");
    now += 1; processCRSFLocalSwitches(2, 3);
    now += 50; processCRSFLocalSwitches(2, 3); assert(sounds.back() == "/brakeoff.mp3");
    // Losing validity during a held input cannot emit a release sound.
    processCRSFLocalSwitches(3, 3);
    now += 51; processCRSFLocalSwitches(3, 3);
    size_t beforeLoss = sounds.size();
    processCRSFLocalSwitches(0, 0); now += 100; processCRSFLocalSwitches(0, 3);
    assert(sounds.size() == beforeLoss);
    processCRSFLocalSwitches(2, 0);
    // O.O and RESET retain Settings music, shuffle, and auto-throttle choices.
    isbuttonAKeyPressed = isbuttonAKeyChange = true; handleButtonAEvent(); assert(prevCount == 1);
    isbuttonAKeyPressed = false; isbuttonAKeyLongPressed = isbuttonAKeyChange = true;
    handleButtonAEvent(); assert(mpActive && playCount == 1);
    isbuttonBKeyPressed = isbuttonBKeyChange = true; handleButtonBEvent(); assert(nextCount == 1);
    isbuttonBKeyPressed = false; isbuttonBKeyLongPressed = isbuttonBKeyChange = true;
    handleButtonBEvent(); assert(aud_state.mpShuffle && sounds.back() == "/shufon.mp3");
    resAT = true; isbuttonBKeyChange = true; handleButtonBEvent(); assert(autoThrottle && vischgnow == now);
    // Network TT is immediate in CRSF; never arm the classic speed simulation.
    ooTT = true; isbuttonAKeyLongPressed = false; isbuttonAKeyPressed = isbuttonAKeyChange = true;
    handleButtonAEvent(); assert(tt == 1 && triggerTTonThrottle == 0);
    networkAvailable = false; isbuttonAKeyChange = true; handleButtonAEvent(); assert(sounds.back() == "bad");
    opModePropCRSF = true; networkAvailable = true;
    isbuttonAKeyChange = true; handleButtonAEvent(); assert(tt == 1 && triggerTTonThrottle == 1);
    triggerTTonThrottle = 0; opModePropCRSF = false;
    csf = CSF_TT; isbuttonBKeyPressed = isbuttonBKeyChange = true;
    handleButtonBEvent(); assert(nextCount == 1); // Classic TT gating is shared.
    csf = 0;
    // Off-state Settings adjustments display on first press and change on second.
    csf = CSF_OFF;
    isbuttonAKeyChange = true; handleButtonAEvent(); assert(volume == 5 && volumeDisplays == 1);
    now += 100; isbuttonAKeyChange = true; handleButtonAEvent(); assert(volume == 6);
    isbuttonBKeyLongPressed = false; isbuttonBKeyPressed = isbuttonBKeyChange = true;
    handleButtonBEvent(); assert(volume == 5);
    isbuttonAKeyPressed = false; isbuttonAKeyLongPressed = isbuttonAKeyChange = true;
    handleButtonAEvent(); assert(brightness == 5 && brightnessDisplays == 1);
    now += 100; isbuttonAKeyChange = true; handleButtonAEvent(); assert(brightness == 6);
    ooresBri = false; isbuttonAKeyChange = true; handleButtonAEvent(); assert(powerMaster);
    isbuttonBKeyPressed = false; isbuttonBKeyLongPressed = isbuttonBKeyChange = true;
    handleButtonBEvent(); assert(!powerMaster);
    // Existing pack callbacks provide MQTT/refill and maintained ON-only audio.
    csf = 0; buttonPackMomentary[0] = true;
    butPackKeyPressed(0); assert(triggerRefill && messages.back() == "ON");
    butPackKeyPressStop(0); handleButtonPackEvents(); assert(messages.back() == "OFF" && sounds.back() == "1short");
    butPackKeyLongPressed(0); handleButtonPackEvents(); assert(sounds.back() == "1long");
    buttonPackMomentary[0] = false; buttonPackMtOnOnly[0] = true;
    butPackKeyLongPressStop(0); handleButtonPackEvents(); assert(sounds.back() == "1stop");
    butPackKeyLongPressed(0); handleButtonPackEvents(); assert(sounds.back() == "1short");
    csf = CSF_OFF; size_t count = messages.size(); butPackKeyPressed(0); assert(messages.size() == count);
    // CRSF local settings use the same 8/3-second persistence deadlines.
    volchgnow = brichgnow = vischgnow = 1000;
    now = 4001; updateDelayedSave(); assert(savedVis == 1 && savedBri == 0 && savedVol == 0);
    now = 9001; updateDelayedSave(); assert(savedBri == 1 && savedVol == 1);
    updateDelayedSave(); assert(savedBri == 1 && savedVol == 1 && savedVis == 1);
    // Cooperative prop waits keep sending CRSF and sampling the throttle.
    opModePropCRSF = true; rawAxesValid = true;
    maxDelay = 0; int priorCrsfTicks = crsfTicks;
    mydelay(20, true);
    assert(maxDelay == 1 && crsfTicks - priorCrsfTicks >= 20);
    assert(wifiTicks >= 20 && audioTicks >= 20 && bttfnTicks >= 20);
    assert(sampledValid && sampledPosition == 1234);
    opModePropCRSF = false; maxDelay = 0; priorCrsfTicks = crsfTicks;
    mydelay(20, false);
    assert(maxDelay == 10 && crsfTicks == priorCrsfTicks);
    puts("CRSF Settings behavior, audio/network callbacks, and persistence checks passed");
}
'''


if __name__ == '__main__':
    source = (ROOT / 'src/remote_main.cpp').read_text()
    input_header = (ROOT / 'src/input.h').read_text()
    button_class = input_header[input_header.index('typedef enum {'):input_header.index('/*\n * ButtonPack class')]
    input_source = (ROOT / 'src/input.cpp').read_text()
    scanner = button_class + 'RemButton::RemButton() {}\n'
    for name in ['scan', 'scanState', 'reset', 'transitionTo', 'setTiming', 'attachLongPressStart', 'attachLongPressStop']:
        if name == 'scan':
            normalized_source = input_source[input_source.index('void RemButton::scan(bool active, bool valid)'):]
            scanner += function(normalized_source, 'RemButton::scan') + '\n'
            continue
        scanner += function(input_source, 'RemButton::' + name) + '\n'
    scanner += 'RemButton powerswitch, brake;\n'
    names = ['mqtt_send_button_on', 'mqtt_send_button_off', 'buttonPackActionPress',
             'buttonPackActionLongPress', 'butPackKeyPressed', 'butPackKeyPressStop',
             'butPackKeyLongPressed', 'butPackKeyLongPressStop', 'handleButtonAEvent',
             'handleButtonBEvent', 'handleButtonPackEvents', 'updateDelayedSave',
             'powKeyPressed', 'powKeyLongPressStop', 'brakeKeyPressed', 'brakeKeyLongPressStop',
             'processCRSFLocalSwitches', 'crsfLocalActionsEnabled', 'serviceCRSF',
             'queueCRSFLocalSwitches', 'myloop', 'mydelay']
    production = '\n\n'.join(function(source, name) for name in names)
    with tempfile.TemporaryDirectory(prefix='crsf-local-actions-') as work:
        work = Path(work)
        cpp = work / 'check.cpp'
        cpp.write_text(STUBS + scanner + production + TEST)
        binary = work / ('check.exe' if os.name == 'nt' else 'check')
        subprocess.run([COMPILER, '-std=c++11', str(cpp), '-o', str(binary)], check=True)
        env = os.environ.copy()
        env['PATH'] = str(Path(COMPILER).parent) + os.pathsep + env.get('PATH', '')
        subprocess.run([str(binary)], check=True, env=env)
