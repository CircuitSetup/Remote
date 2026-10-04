/*
 * -------------------------------------------------------------------
 * Remote Control
 * (C) 2024-2026 Thomas Winischhofer (A10001986)
 * https://github.com/realA10001986/Remote
 * https://remote.out-a-ti.me
 *
 * CRSF bridge to the normal prop controls
 *
 * -------------------------------------------------------------------
 * License: Modified MIT NON-AI
 * 
 * Permission is hereby granted, free of charge, to any person 
 * obtaining a copy of this software and associated documentation 
 * files (the "Software"), to deal in the Software without restriction, 
 * including without limitation the rights to use, copy, modify, 
 * merge, publish, distribute, sublicense, and/or sell copies of the 
 * Software, and to permit persons to whom the Software is furnished to 
 * do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 * 
 * Links inside the Software pointing to the original source must not 
 * be changed or removed.
 *
 * In addition, the following restrictions apply:
 * 
 * 1. The Software and any modifications made to it may not be used 
 * for the purpose of training or improving machine learning algorithms, 
 * including but not limited to artificial intelligence, natural 
 * language processing, or data mining. This condition applies to any 
 * derivatives, modifications, or updates based on the Software code. 
 * Any usage of the Software in an AI-training dataset is considered a 
 * breach of this License.
 *
 * 2. The Software may not be included in any dataset used for 
 * training or improving machine learning algorithms, including but 
 * not limited to artificial intelligence, natural language processing, 
 * or data mining.
 *
 * 3. Any person or organization found to be in violation of these 
 * restrictions will be subject to legal action and may be held liable 
 * for any damages resulting from such use.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY 
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, 
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _CRSF_MAIN_H
#define _CRSF_MAIN_H

#ifdef HAVE_CRSF

#include "crsf_settings.h"
#include "elrs_crsf.h"

// Included once by remote_main.cpp after its private controls and declarations.
bool opModePropCRSF = false;

static bool crsfStarted = false;
static uint16_t crsfLocalStates = 0;
static uint16_t crsfLocalValidMask = 0;
static uint16_t crsfLocalInvalidMask = 0;
static bool crsfLocalPending = false;

static bool crsfTelemetryOwnsDisplay()
{
    return opModePropCRSF && crsfStarted && elrsMode.telemetryDisplayAssigned() &&
        !(csf & (CSF_CALIBMD | CSF_TT | CSF_TCDINP0)) && !offDisplayTimer &&
        (!(csf & CSF_OFF) || displayTCDSMode);
}

static void crsf_start()
{
    crsfStarted = crsf_begin(
        (uint16_t)crsf_getPacketRate(atoi(settings.elrsPktRate)),
        crsf_getSpeedUnits(atoi(settings.elrsSpdUnit)),
        crsf_getTelemetryRatio(atoi(settings.elrsTlmRatio)),
        crsf_getMaxPower(atoi(settings.elrsMaxPower)),
        crsf_getDynamicPower(atoi(settings.elrsDynPower)),
        useBPack ? &butPack : NULL,
        useBPack,
        &remdisplay,
        &pwrled,
        &bLvLMeter,
        &remledStop,
        usePwrLED,
        useLvlMtr,
        pwrLEDonFP,
        LvLMtronFP,
        wifiOnFakePowerOn
    );
}

void queueCRSFLocalSwitches(uint16_t states, uint16_t validMask)
{
    // Dispatch after the core returns so network waits can keep serving CRSF.
    crsfLocalStates = states;
    crsfLocalValidMask = validMask;
    // Retain read failures across waits to cancel pending presses on recovery.
    crsfLocalInvalidMask |= ~validMask & 0x0fff;
    crsfLocalPending = true;
}

bool crsfLocalActionsEnabled()
{
    uint8_t localActions[12];
    loadELRSInputConfig(NULL, 0, NULL, NULL, NULL, NULL, NULL, localActions);
    for(int i = 0; i < 12; i++) {
        if(localActions[i]) return true;
    }
    return false;
}

static void processCRSFLocalSwitches(uint16_t states, uint16_t validMask)
{
    powerState = (states & (1 << 1)) != 0;
    brakeState = (states & (1 << 0)) != 0;
    if(powerState) csf &= ~CSF_OFF;
    else csf |= CSF_OFF;

    if(!(validMask & (1 << 1))) isFPBKeyChange = false;
    if(!(validMask & (1 << 0))) isBrakeKeyChange = false;
    powerswitch.scan(powerState, (validMask & (1 << 1)) != 0);
    brake.scan(brakeState, (validMask & (1 << 0)) != 0);
    if(isFPBKeyChange) {
        isFPBKeyChange = false;
        triggerCompleteUpdate = true;
        if(isFPBKeyPressed) {
            play_file(powerOnSnd, PA_INTRMUS|PA_ALLOWSD|PA_DYNVOL);
        } else {
            mp_stop(true);
            stopAudio();
            flushDelayedSave();
            if(havePOFFsnd) play_file(powerOffSnd, PA_INTRMUS|PA_ALLOWSD|PA_DYNVOL);
        }
    }
    if(isBrakeKeyChange) {
        isBrakeKeyChange = false;
        triggerCompleteUpdate = true;
        if(powerState) {
            if(isBrakeKeyPressed) play_file(brakeOnSnd, PA_ALLOWSD|PA_DYNVOL);
            else if(haveBOFFsnd) play_file(brakeOffSnd, PA_ALLOWSD|PA_DYNVOL);
        }
    }

    if(!(validMask & (1 << 2))) isbuttonAKeyChange = false;
    if(!(validMask & (1 << 3))) isbuttonBKeyChange = false;
    buttonA.scan((states & (1 << 2)) != 0, (validMask & (1 << 2)) != 0);
    buttonB.scan((states & (1 << 3)) != 0, (validMask & (1 << 3)) != 0);

    if(useBPack) {
        for(int i = 0; i < PACK_SIZE; i++) {
            if(!(validMask & (1 << (i + 4)))) isbutPackKeyChange[i] = false;
        }
        butPack.scan((uint8_t)(states >> 4), (uint8_t)(validMask >> 4));
    }
}

void serviceCRSF(bool withLocalActions)
{
    static bool servicing = false;
    if(!crsfStarted || servicing) return;
    servicing = true;
    if(!opModePropCRSF) {
        #ifdef HAVE_PM
        battWarn = pwrMon.loop();
        #else
        battWarn = 0;
        #endif
    }
    crsf_loop(battWarn);
    if(opModePropCRSF) {
        int16_t axes[4] = { 0 };
        const bool valid = readELRSCurrentRawAxes(axes);
        rotEnc.useSampledPosition(axes[3], valid);
    }
    servicing = false;
    if(withLocalActions && crsfLocalPending) {
        crsfLocalPending = false;
        const uint16_t validMask = crsfLocalValidMask & ~crsfLocalInvalidMask;
        crsfLocalInvalidMask = 0;
        processCRSFLocalSwitches(crsfLocalStates, validMask);
    }
}

static void crsf_standalone_keepalive()
{
    if(triggerCompleteUpdate || triggerRefill || (useBTTFN && millis() - lastCommandSent > 10*1000)) {
        triggerCompleteUpdate = false;
        bttfn_remote_send_combined(powerState, brakeState, 0);
    }
}

static bool bttfn_send_command(uint8_t cmd, uint8_t p1, uint8_t p2);

static bool crsf_handle_bttfn_update(bool &brakestate)
{
    if(!opModeCRSF || opModePropCRSF) return false;

    uint8_t localActions[12];
    loadELRSInputConfig(NULL, 0, NULL, NULL, NULL, NULL, NULL, localActions);
    if(localActions[1]) {
        brakestate = brakestate && localActions[0];
        return false;
    }

    // COMBINED always replaces TCD power, brake and speed. A disabled FakePower
    // must use state-free keepalives, including sends from O.O/RESET or retries.
    if(triggerRefill) {
        constexpr uint8_t refillCommand = 103;
        if(!bttfn_send_command(refillCommand, 0, 0)) {
            triggerCompleteUpdate = true;
            return true;
        }
        triggerRefill = false;
    }
    triggerCompleteUpdate = !bttfn_send_command(BTTFN_REMCMD_PING, 0, 0);
    return true;
}

#endif
#endif
