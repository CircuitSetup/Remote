/*
 * -------------------------------------------------------------------
 * Remote Control
 * (C) 2024 Thomas Winischhofer (A10001986)
 * https://github.com/realA10001986/Remote
 * https://remote.out-a-ti.me
 *
 * Main
 *
 * -------------------------------------------------------------------
 * License: MIT NON-AI
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

#ifndef _REMOTE_MAIN_H
#define _REMOTE_MAIN_H

#include "display.h"
#include "input.h"

// Durations of tt phases as defined by TCD
#define P0_DUR          5000    // etto lead
#define P1_DUR          6600    // time tunnel phase

extern unsigned long powerupMillis;

extern uint32_t myRemID;

extern REMRotEnc rotEnc;

extern remDisplay remdisplay;
extern remLED remledPwr;
extern remLED remledStop;

extern bool useRotEnc;
extern bool useBPack;

extern bool FPBUnitIsOn;

extern uint16_t visMode;
extern bool movieMode;
extern bool displayGPSMode;

extern bool TTrunning;

extern bool bttfnTT;

extern bool networkTimeTravel;
extern bool networkReentry;
extern bool networkAbort;
extern bool networkAlarm;
extern uint16_t networkLead;
extern uint16_t networkP1;

extern uint16_t tcdIsInP0;

void main_boot();
void main_boot2();
void main_setup();
void main_loop();

void flushDelayedSave();
#ifdef REMOTE_HAVEAUDIO
bool increaseVolume();
bool decreaseVolume();
#endif

void timeTravel(uint16_t P0Dur = P0_DUR, uint16_t P1Dur = P1_DUR);

void showWaitSequence();
void endWaitSequence();
void showCopyError();

void prepareTT();
void wakeup();

void display_ip();

void updateVisMode();

#ifdef REMOTE_HAVEAUDIO
void switchMusicFolder(uint8_t nmf);
void waitAudioDone(bool withBTTFN = false);
#endif

void mydelay(unsigned long mydel, bool withBTTFN = false);

void bttfn_loop();
bool BTTFNTriggerTT();

void bttfn_remote_unregister();

#endif
