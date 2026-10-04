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
    return block(source, match.start())


def block(source, start):
    brace = source.index('{', start)
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
#define CSF_TCDINP0O 128
#define CSF_TCDINP0T 256
#define PA_THRUP 8
#define PA_ALLOWSD 1
#define PA_INTRMUS 2
#define PA_DYNVOL 4
#define BTTFN_REMCMD_PING 1
#define BTTFN_REMCMD_COMBINED 3
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
bool useBTTFN = true, displayTCDSMode = false, sendSucceeds = true;
unsigned long lastCommandSent = 0;
struct Packet { uint8_t command, flags, speed; };
std::vector<Packet> packets;
bool bttfn_send_command(uint8_t command, uint8_t flags, uint8_t speed) {
    packets.push_back({command, flags, speed});
    if(!sendSucceeds) return false;
    if(command == BTTFN_REMCMD_COMBINED) combined++;
    lastCommandSent = now;
    return true;
}
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
static void bttfn_remote_send_combined(bool,bool,uint8_t);
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
struct DisplaySink {
    int speed = 0, shows = 0; std::string visible;
    void on() {}
    void setSpeed(int v) { speed = v; }
    int getSpeedPostDot() { return speed % 10; }
    void show() { shows++; visible = std::to_string(speed); }
} remdisplay;
struct {
    bool assigned = false; int renders = 0; std::string telemetry = "50.0";
    bool telemetryDisplayAssigned() { return assigned; }
    void renderAssignedDisplay(unsigned long, int) { renders++; remdisplay.visible = telemetry; remdisplay.shows++; }
} elrsMode;
int currSpeedF = 0, tcdCurrSpeed = 0, currTCDSpeedOld = 0;
unsigned long lastSpeedUpd = 0, tcdClickNow = 0, tcdInP0now = 0, tcdSpdChgNow = 0;
int tcdSpeedP0 = 0, tcdSpeedP0Old = 0, remSpdAtP0Start = 0, tcdSpdFake100 = 0;
int throttleUpSoundThresholdP0 = 20, accelDelays[5] = {0};
bool tcdIsInP0stalled = false, haveThUp = false, doForceDispUpd = false;
const char *throttleUpSnd = "throttle";
void play_click() {} void play_throttleup() {}
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
    // A telemetry-only local action must not register TCD speed/power control.
    memset(configuredLocalActions, 0, sizeof(configuredLocalActions));
    configuredLocalActions[2] = 1; // O.O only; FakePower and Stop remain unchecked.
    opModeCRSF = true; powerMaster = true; displayTCDSMode = false;
    triggerCompleteUpdate = triggerRefill = false;
    processCRSFLocalSwitches(3, 1 << 2);
    assert(powerState && brakeState && !(csf & CSF_OFF)); // Keep local button context.
    packets.clear(); lastCommandSent = now; now += 10001;
    crsf_standalone_keepalive();
    assert(packets.size() == 1 && packets[0].command == 1 && packets[0].flags == 0);
    // Every send path uses the same opt-in rule, including power-master actions.
    packets.clear(); bttfn_remote_send_combined(true, true, 42);
    assert(packets.size() == 1 && packets[0].command == 1 && packets[0].flags == 0);
    packets.clear(); sendSucceeds = false;
    bttfn_remote_send_combined(true, true, 42);
    assert(packets.size() == 1 && packets[0].command == 1 && triggerCompleteUpdate);
    packets.clear(); sendSucceeds = true; crsf_standalone_keepalive();
    assert(packets.size() == 1 && packets[0].command == 1 && !triggerCompleteUpdate);
    // Refill is independent of FakePower/Stop and keeps its retry after failure.
    packets.clear(); triggerRefill = true; sendSucceeds = false;
    bttfn_remote_send_combined(true, true, 42);
    assert(packets.size() == 1 && packets[0].command == 103);
    assert(triggerRefill && triggerCompleteUpdate);
    packets.clear(); sendSucceeds = true; crsf_standalone_keepalive();
    assert(packets.size() == 2 && packets[0].command == 103 && packets[1].command == 1);
    assert(!triggerRefill && !triggerCompleteUpdate);
    // Stop alone must not accidentally give the Remote speed or power ownership.
    configuredLocalActions[2] = 0; configuredLocalActions[0] = 1;
    packets.clear(); bttfn_remote_send_combined(true, true, 0);
    assert(packets.size() == 1 && packets[0].command == 1 && packets[0].flags == 0);
    // FakePower opt-in enables combined control; Stop needs its own opt-in.
    configuredLocalActions[0] = 0; configuredLocalActions[1] = 1;
    packets.clear(); bttfn_remote_send_combined(true, true, 0);
    assert(packets.size() == 1 && packets[0].command == 3 && packets[0].flags == 9);
    configuredLocalActions[0] = 1;
    packets.clear(); bttfn_remote_send_combined(true, true, 0);
    assert(packets.size() == 1 && packets[0].command == 3 && packets[0].flags == 11);
    // Legacy and combined prop mode retain all of their normal controls.
    memset(configuredLocalActions, 0, sizeof(configuredLocalActions));
    for(int mode = 0; mode < 2; mode++) {
        opModeCRSF = mode != 0; opModePropCRSF = mode != 0;
        packets.clear(); bttfn_remote_send_combined(true, true, 42);
        assert(packets.size() == 1 && packets[0].command == 3 && packets[0].flags == 11 && packets[0].speed == 42);
    }
    // Execute normal prop display/sends and P0 from the actual main loop.
    opModePropCRSF = crsfStarted = true; csf = 0; offDisplayTimer = false;
    currSpeedF = 123; throttlePos = 1; packets.clear();
    normalProgress(110);
    assert(remdisplay.visible == "123" && packets.back().speed == 12);
    elrsMode.assigned = true; currSpeedF = 137; packets.clear();
    int shows = remdisplay.shows; normalProgress(123); renderTelemetry();
    assert(remdisplay.speed == 137 && remdisplay.shows == shows + 1);
    assert(remdisplay.visible == "50.0" && packets.back().speed == 13);
    elrsMode.telemetry = "---"; now += 2000; renderTelemetry();
    assert(remdisplay.visible == "---" && currSpeedF == 137);
    elrsMode.telemetry = "50.0"; renderTelemetry(); assert(remdisplay.visible == "50.0");
    // Native holds and hidden fake-off screens do not render telemetry.
    for(uint32_t hold : {CSF_CALIBMD, CSF_TT, CSF_TCDINP0}) {
        csf = hold; int prior = elrsMode.renders; renderTelemetry(); assert(elrsMode.renders == prior);
    }
    csf = 0; offDisplayTimer = true; assert(!crsfTelemetryOwnsDisplay());
    offDisplayTimer = false; csf = CSF_OFF; displayTCDSMode = false;
    assert(!crsfTelemetryOwnsDisplay());
    displayTCDSMode = true; tcdCurrSpeed = 42; currTCDSpeedOld = -2;
    shows = remdisplay.shows; fakeOffSpeed(); renderTelemetry();
    assert(remdisplay.speed == 420 && remdisplay.shows == shows + 1);
    // Keep the prop fraction cached under telemetry for a subsequent P0 stall.
    csf = 0; currSpeedF = 137; normalProgress(130); renderTelemetry();
    csf = CSF_TCDINP0; tcdIsInP0stalled = true; tcdSpeedP0 = 15; tcdSpeedP0Old = -1;
    followP0(); renderTelemetry();
    assert(currSpeedF == 157 && remdisplay.visible == "157");
    csf = 0; renderTelemetry(); assert(remdisplay.visible == "50.0");
    puts("CRSF controls, telemetry ownership, P0, and persistence checks passed");
}
'''


if __name__ == '__main__':
    source = (ROOT / 'src/remote_main.cpp').read_text()
    main_loop = function(source, 'main_loop')
    source += '\n' + (ROOT / 'src/src/CRSF/crsf_main.h').read_text()
    input_header = (ROOT / 'src/input.h').read_text()
    button_class = input_header[input_header.index('typedef enum {'):input_header.index('/*\n * ButtonPack class')]
    input_source = (ROOT / 'src/input.cpp').read_text()
    input_source += '\n' + (ROOT / 'src/src/CRSF/crsf_input.h').read_text()
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
             'butPackKeyLongPressed', 'butPackKeyLongPressStop',
             'powKeyPressed', 'powKeyLongPressStop', 'brakeKeyPressed', 'brakeKeyLongPressStop',
             'processCRSFLocalSwitches', 'crsfLocalActionsEnabled', 'serviceCRSF',
             'queueCRSFLocalSwitches', 'myloop', 'mydelay',
             'crsf_standalone_keepalive', 'crsf_handle_bttfn_update',
             'bttfn_remote_send_combined', 'crsfTelemetryOwnsDisplay', 'showNormalSpeed']
    production = '\n\n'.join(function(source, name) for name in names)
    for name, marker in [('normalProgress', 'if(currSpeedF != sbf)'),
                         ('fakeOffSpeed', 'if((csf & CSF_OFF) && (!(csf & CSF_CALIBMD)) && !offDisplayTimer)'),
                         ('followP0', 'if(csf & CSF_TCDINP0)')]:
        body = block(main_loop, main_loop.index(marker))
        args = 'int sbf' if name == 'normalProgress' else ''
        prefix = 'int sb = sbf / 10;' if name == 'normalProgress' else ''
        production += f'\nvoid {name}({args}) {{ {prefix}\n{body}\n}}\n'
    hook = 'if(crsfTelemetryOwnsDisplay()) elrsMode.renderAssignedDisplay(millis(), battWarn);'
    assert hook in main_loop
    production += f'\nvoid renderTelemetry() {{ {hook} }}\n'
    # Execute the exact inline event bodies restored to the normal prop loop.
    for name, marker in [('handleButtonAEvent', 'if(isbuttonAKeyChange)'),
                         ('handleButtonBEvent', 'if(isbuttonBKeyChange)'),
                         ('handleButtonPackEvents', 'for(int i = 0; i < butPack.getPackSize(); i++)'),
                         ('updateDelayedSave', '// Save on-the-fly settings')]:
        start = main_loop.index(marker)
        if name == 'updateDelayedSave':
            start = main_loop.rfind('    if(', 0, start)
        body = block(main_loop, start)
        production += f'\nvoid {name}() {{\nconst bool crsfStandalone = opModeCRSF && !opModePropCRSF;\n{body}\n}}\n'
    with tempfile.TemporaryDirectory(prefix='crsf-local-actions-') as work:
        work = Path(work)
        cpp = work / 'check.cpp'
        cpp.write_text(STUBS + scanner + production + TEST)
        binary = work / ('check.exe' if os.name == 'nt' else 'check')
        subprocess.run([COMPILER, '-std=c++11', str(cpp), '-o', str(binary)], check=True)
        env = os.environ.copy()
        env['PATH'] = str(Path(COMPILER).parent) + os.pathsep + env.get('PATH', '')
        subprocess.run([str(binary)], check=True, env=env)
