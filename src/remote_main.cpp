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

#include "remote_global.h"

#include <Arduino.h>
#include <WiFi.h>
#include "display.h"
#include "input.h"
#ifdef REMOTE_HAVETEMP
#include "sensors.h"
#endif

#include "remote_main.h"
#include "remote_settings.h"
#include "remote_audio.h"
#include "remote_wifi.h"

// i2c slave addresses

#define DISPLAY_ADDR   0x70 // LED segment display

                            // rotary encoders / ADC
#define ADDA4991_ADDR  0x36 // [default]
#define DUPPAV2_ADDR   0x01 // [user configured: A0 closed]
#define DFRGR360_ADDR  0x54 // [default; SW1=0, SW2=0]
#define ADS1X15_ADDR   0x48 // Addr->GND

                            // rotary encoders for volume
#define ADDA4991V_ADDR 0x37 // [non-default: A0 closed]
#define DUPPAV2V_ADDR  0x03 // [user configured: A0+A1 closed]
#define DFRGR360V_ADDR 0x55 // [SW1=0, SW2=1]

#define PCA9554_ADDR   0x20 // all Ax -> GND
#define PCA9554A_ADDR  0x38 // all Ax -> GND
#define PCA8574_ADDR   0x21 // non-default
#define PCA8574A_ADDR  0x39 // non-default

unsigned long powerupMillis = 0;

// The segment display object
remDisplay remdisplay(DISPLAY_ADDR);

// The LED objects
remLED remledStop;
remLED pwrled;
remLED bLvLMeter;

// The rotary encoder/ADC object
REMRotEnc rotEnc(4, 
            (uint8_t[4*2]){ ADDA4991_ADDR, REM_RE_TYPE_ADA4991,
                            DUPPAV2_ADDR,  REM_RE_TYPE_DUPPAV2,
                            DFRGR360_ADDR, REM_RE_TYPE_DFRGR360,
                            ADS1X15_ADDR,  REM_RE_TYPE_ADS1X15,
                          });

static REMRotEnc rotEncV(3, 
            (uint8_t[3*2]){ ADDA4991V_ADDR, REM_RE_TYPE_ADA4991,
                            DUPPAV2V_ADDR,  REM_RE_TYPE_DUPPAV2,
                            DFRGR360V_ADDR, REM_RE_TYPE_DFRGR360
                          });                          
static REMRotEnc *rotEncVol;

// The Button / Switch objects
static RemButton powerswitch;
static RemButton brake;
static RemButton calib;

static RemButton buttonA;
static RemButton buttonB;

// The ButtonPack object
ButtonPack butPack(4, 
            (uint8_t[4*2]){ PCA9554_ADDR,  REM_BP_TYPE_PCA9554,
                            PCA9554A_ADDR, REM_BP_TYPE_PCA9554,
                            PCA8574_ADDR,  REM_BP_TYPE_PCA8574,
                            PCA8574A_ADDR, REM_BP_TYPE_PCA8574
                          });
                          
bool useRotEnc = false;
static bool useBPack = true;
static bool useRotEncVol = false;

//static bool playTTsounds = true;

static bool remoteAllowed = false;
uint32_t    myRemID = 0x12345678;

static bool pwrLEDonFP = true;
static bool usePwrLED = false;
static bool useLvlMtr = false;

static bool powerState = false;
static bool brakeState = false;
static bool triggerCompleteUpdate = false;

static int32_t throttlePos = 0, oldThrottlePos = 0;
static int     currSpeedF = 0;
static int     currSpeed = 0;
static unsigned long lastSpeedUpd = 0;
static bool    lockThrottle = false;
static bool    doCoast = false;
static bool    keepCounting = false;
bool           autoThrottle = false;

static bool          calibMode = false;
static bool          calibUp = false;

static bool          offDisplayTimer = false;
static unsigned long offDisplayNow = 0;

uint16_t             visMode = 0;

bool                 movieMode = true;

static bool          etmr = false;
static unsigned long enow = 0;

static bool ooTT = true;
static int  triggerTTonThrottle = 0;

static unsigned long accelDelay = 1000/10;
static int32_t       accelStep = 1;
static const unsigned long accelDelays[5] = { 50, 42, 35, 28, 20 };  // 1-100%
static const int32_t       accelSteps[5]  = {  1,  1,  1,  1,  1 };  //

/*
 *       0- 7:  90ms/mph
 *      20-24: 197ms/mph
 *      32-39: 200ms/mph
 *      55-59: 220ms/mph
 *      77-81: 300ms/mph
*/
static const unsigned long strictAccelFacts[5] = { 25, 21, 17, 14, 10 };
static const unsigned long strictAccelDelays[89] =
{
      0,  90,  90,  90,  90,  90,  90,  95,  95, 100,  // m0 - 9  10mph  0- 9: 0.83s  (m=measured, i=interpolated)
    105, 110, 115, 120, 125, 130, 135, 140, 145, 150,  // i10-19  20mph 10-19: 1.27s
    155, 160, 165, 170, 175, 180, 185, 190, 195, 200,  // i20-29  30mph 20-29: 1.77s
    200, 200, 202, 203, 204, 205, 206, 207, 208, 209,  // m30-39  40mph 30-39: 2s
    210, 211, 212, 213, 214, 215, 216, 217, 218, 219,  // i40-49  50mph 40-49: 2.1s
    220, 221, 222, 223, 224, 225, 226, 227, 228, 229,  // m50-59  60mph 50-59: 2.24s
    230, 233, 236, 240, 243, 246, 250, 253, 256, 260,  // i60-69  70mph 60-69: 2.47s
    263, 266, 270, 273, 276, 280, 283, 286, 290, 293,  // m70-79  80mph 70-79: 2.78s
    296, 300, 300, 303, 303, 306, 310, 310,   0        // i80-88  90mph 80-88: 2.4s   total 17.6 secs
};

static unsigned long lastCommandSent = 0;

static bool doForceDispUpd = false;

static bool isFPBKeyPressed = false;
static bool isFPBKeyChange = false;
static bool isBrakeKeyPressed = false;
static bool isBrakeKeyChange = false;
static bool isCalibKeyPressed = false;
static bool isCalibKeyLongPressed = false;
static bool isCalibKeyChange = false;
static bool isbuttonAKeyPressed = false;
static bool isbuttonAKeyLongPressed = false;
static bool isbuttonAKeyChange = false;
static bool isbuttonBKeyPressed = false;
static bool isbuttonBKeyLongPressed = false;
static bool isbuttonBKeyChange = false;

static bool isbutPackKeyPressed[PACK_SIZE] = { false };
static bool isbutPackKeyLongPressed[PACK_SIZE] = { false };
static bool isbutPackKeyChange[PACK_SIZE] = { false };
static bool buttonPackMomentary[PACK_SIZE] = { false };
static bool buttonPackMtOnOnly[PACK_SIZE] = { false };

static int  MQTTbuttonOnLen[PACK_SIZE] ={ 0 };
static int  MQTTbuttonOffLen[PACK_SIZE] ={ 0 };

bool networkTimeTravel = false;
bool networkReentry    = false;
bool networkAbort      = false;
bool networkAlarm      = false;
uint16_t networkLead   = P0_DUR;
uint16_t networkP1     = P1_DUR;

// Time travel status flags etc.
bool                 TTrunning = false;
static unsigned long TTstart = 0;
static unsigned long P0duration = P0_DUR;
static unsigned long P1duration = P1_DUR;
static bool          TTP0 = false;
static bool          TTP1 = false;
static bool          TTP2 = false;
static bool          TTFlag = false;

static bool          volchanged = false;
static unsigned long volchgnow = 0;
static bool          brichanged = false;
static unsigned long brichgnow = 0;
static bool          vischanged = false;
static unsigned long vischgnow = 0;

bool                 FPBUnitIsOn = true;
static unsigned long justBootedNow = 0;
static bool          bootFlag = false;
static bool          sendBootStatus = false;

#ifdef REMOTE_HAVEAUDIO
static bool          havePOFFsnd = false;
static bool          haveBOFFsnd = false;
static bool          haveThUp = false;
static const char    *powerOnSnd  = "/poweron.mp3";   // Default provided
static const char    *powerOffSnd = "/poweroff.mp3";  // No default sound
static const char    *brakeOnSnd  = "/brakeon.mp3";   // Default provided
static const char    *brakeOffSnd = "/brakeoff.mp3";  // No default sound
static const char    *throttleUpSnd = "/throttleup.mp3";   // Default provided (wav)
#endif

// BTTF network
#define BTTFN_VERSION              1
#define BTTFN_SUP_MC            0x80
#define BTTF_PACKET_SIZE          48
#define BTTF_DEFAULT_LOCAL_PORT 1338
#define BTTFN_POLL_INT          1300
#define BTTFN_POLL_INT_FAST      500
#define BTTFN_NOT_PREPARE  1
#define BTTFN_NOT_TT       2
#define BTTFN_NOT_REENTRY  3
#define BTTFN_NOT_ABORT_TT 4
#define BTTFN_NOT_ALARM    5
#define BTTFN_NOT_REFILL   6
#define BTTFN_NOT_FLUX_CMD 7
#define BTTFN_NOT_SID_CMD  8
#define BTTFN_NOT_PCG_CMD  9
#define BTTFN_NOT_WAKEUP   10
#define BTTFN_NOT_AUX_CMD  11
#define BTTFN_NOT_VSR_CMD  12
#define BTTFN_NOT_REM_CMD  13
#define BTTFN_NOT_REM_SPD  14
#define BTTFN_NOT_SPD      15
#define BTTFN_TYPE_ANY     0    // Any, unknown or no device
#define BTTFN_TYPE_FLUX    1    // Flux Capacitor
#define BTTFN_TYPE_SID     2    // SID
#define BTTFN_TYPE_PCG     3    // Plutonium gauge panel
#define BTTFN_TYPE_VSR     4    // VSR
#define BTTFN_TYPE_AUX     5    // Aux (user custom device)
#define BTTFN_TYPE_REMOTE  6    // Futaba remote control
#define BTTFN_REMCMD_PING       1   // Implicit "Register"/keep-alive
#define BTTFN_REMCMD_BYE        2   // Forced unregister
#define BTTFN_REMCMD_COMBINED   3   // All switches & speed combined
#define BTTFN_REM_MAX_COMMAND   BTTFN_REMCMD_COMBINED
#define BTTFN_SSRC_NONE         0
#define BTTFN_SSRC_GPS          1
#define BTTFN_SSRC_ROTENC       2
#define BTTFN_SSRC_REM          3
#define BTTFN_SSRC_P0           4
#define BTTFN_SSRC_P1           5
#define BTTFN_SSRC_P2           6
static const uint8_t BTTFUDPHD[4] = { 'B', 'T', 'T', 'F' };
static bool          useBTTFN = false;
static WiFiUDP       bttfUDP;
static UDP*          remUDP;
#ifdef BTTFN_MC
static WiFiUDP       bttfMcUDP;
static UDP*          remMcUDP;
#endif
static byte          BTTFUDPBuf[BTTF_PACKET_SIZE];
static unsigned long bttfnRemPollInt = BTTFN_POLL_INT;
static unsigned long BTTFNUpdateNow = 0;
static unsigned long BTFNTSAge = 0;
static unsigned long BTTFNTSRQAge = 0;
static bool          BTTFNPacketDue = false;
static bool          BTTFNWiFiUp = false;
static uint8_t       BTTFNfailCount = 0;
static uint32_t      BTTFUDPID = 0;
static unsigned long lastBTTFNpacket = 0;
static bool          BTTFNBootTO = false;
static bool          haveTCDIP = false;
static IPAddress     bttfnTcdIP;
static uint8_t       bttfnReqStatus = 0x52; // Request capabilities, status, speed
#ifdef BTTFN_MC
static uint32_t      tcdHostNameHash = 0;
static byte          BTTFMCBuf[BTTF_PACKET_SIZE];
static uint8_t       bttfnMcMarker = 0;
static IPAddress     bttfnMcIP(224, 0, 0, 224);
#endif 
static uint32_t      bttfnSeqCnt[BTTFN_REM_MAX_COMMAND+1] = { 1 };
static bool          bttfn_cmd_status = false;
static unsigned long bttfnCurrLatency = 0, bttfnPacketSentNow = 0;
static bool          tcdHasSpeedo = false;
static int16_t       tcdCurrSpeed = -1;
static bool          tcdSpdIsRotEnc = false;
static bool          tcdSpdIsRemote = false;
static int16_t       currSpeedOldGPS = -2;
bool                 displayGPSMode = false;
uint16_t             tcdIsInP0 = 0, tcdIsInP0Old = 1000;
static uint16_t      tcdSpeedP0 = 0, tcdSpeedP0Old = 1000;
static unsigned long tcdInP0now = 0;
static uint32_t      bttfnTCDSeqCnt = 0;
static uint16_t      tcdSpdFake100 = 0;
static unsigned long tcdSpdChgNow = 0;
static unsigned long tcdClickNow = 0;

static int      iCmdIdx = 0;
static int      oCmdIdx = 0;
static uint32_t commandQueue[16] = { 0 };

#define GET32(a,b)          \
    (((a)[b])            |  \
    (((a)[(b)+1]) << 8)  |  \
    (((a)[(b)+2]) << 16) |  \
    (((a)[(b)+3]) << 24))
#define SET32(a,b,c)                        \
    (a)[b]       = ((uint32_t)(c)) & 0xff;  \
    ((a)[(b)+1]) = ((uint32_t)(c)) >> 8;    \
    ((a)[(b)+2]) = ((uint32_t)(c)) >> 16;   \
    ((a)[(b)+3]) = ((uint32_t)(c)) >> 24;  

#ifdef REMOTE_HAVEAUDIO
static void displayVolume();
#endif
static void increaseBrightness();
static void decreaseBrightness();
static void displayBrightness();

static void condPLEDaBLvl(bool sLED, bool sLvl);

static void execute_remote_command();

static void play_startup();

static void powKeyPressed();
static void powKeyLongPressStop();
static void brakeKeyPressed();
static void brakeKeyLongPressStop();
static void calibKeyPressed();
static void calibKeyPressStop();
static void calibKeyLongPressed();
static void buttonAKeyPressed();
static void buttonAKeyPressStop();
static void buttonAKeyLongPressed();
static void buttonBKeyPressed();
static void buttonBKeyPressStop();
static void buttonBKeyLongPressed();

static void butPackKeyPressed(int);
static void butPackKeyPressStop(int);
static void butPackKeyLongPressed(int);
static void butPackKeyLongPressStop(int i);
static void buttonPackActionPress(int i, bool stopOnly);
static void buttonPackActionLongPress(int i);
#ifdef REMOTE_HAVEMQTT
static void mqtt_send_button_on(int i);
static void mqtt_send_button_off(int i);
#endif

#ifdef REMOTE_HAVEAUDIO
static void re_vol_reset();
#endif

static void myloop(bool withBTTFN);

static void bttfn_setup();
static void bttfn_loop_quick();
static void BTTFNCheckPacket();
#ifdef BTTFN_MC
static bool bttfn_checkmc();
#endif
static bool BTTFNTriggerUpdate();
static void BTTFNSendPacket();
static bool bttfn_send_command(uint8_t cmd, uint8_t p1, uint8_t p2);
static bool BTTFNTriggerTT(bool probe);
static void bttfn_remote_keepalive();
static void bttfn_remote_send_combined(bool powerstate, bool brakestate, uint8_t speed);

void main_boot()
{
}

void main_boot2()
{
    int i = 9;
    const int8_t resetanim[10][4] = {
        {  1,  0, 0, 0 },
        { 32,  1, 0, 0 },
        { 32, 32, 1, 0 },
        { 32, 32, 2, 0 },
        { 32, 32, 3, 0 },
        { 32, 32, 4, 0 },
        { 32,  4, 0, 0 },
        {  4,  0, 0, 0 },
        {  5,  0, 0, 0 },
        {  6,  0, 0, 0 }
    };
    
    // Init LEDs

    remledStop.begin(STOPOUT_PIN);    // "Stop" LED + Switch output

    // Init Power LED and Level Meter
    usePwrLED = (atoi(settings.usePwrLED) > 0);
    useLvlMtr = (atoi(settings.useLvlMtr) > 0);

    // Power LED
    pwrled.begin(PWRLED_PIN, usePwrLED);
    // Battery level meter (treated as LED)
    bLvLMeter.begin(LVLMETER_PIN, useLvlMtr);

    // Power LED on Fake Power (or Real Power)
    if(usePwrLED || useLvlMtr) {
        if(!(pwrLEDonFP = (atoi(settings.pwrLEDonFP) > 0))) {
            pwrled.setState(true);
            bLvLMeter.setState(true);
        }
    } else {
        pwrLEDonFP = false;
    }

    // Init LED segment display
    if(!remdisplay.begin()) {
        Serial.println("Display not found");
    } else {
        loadBrightness();
    }

    // Allow user to delete static IP data by holding Calibration button
    // while booting and pressing Button A ("O.O") twice within 10 seconds

    // Pre-maturely init button IOs (initialized again later)
    pinMode(CALIBB_IO_PIN, INPUT);
    pinMode(BUTA_IO_PIN, INPUT);
    delay(50);

    if(!digitalRead(CALIBB_IO_PIN)) {
        delay(50);
        if(!digitalRead(CALIBB_IO_PIN)) {

            unsigned long mnow = millis(), seqnow = 0;
            bool ssState, newSSState;
            int ssCount = 0;

            remdisplay.on();

            ssState = digitalRead(BUTA_IO_PIN);

            while(1) {

                if(millis() - seqnow > 50) {
                    seqnow = millis();
                    remdisplay.setText((char *)resetanim[i--]);
                    remdisplay.show();
                    if(!i) { i = 9; }
                }

                if((ssCount == 4) || (millis() - mnow > 10*1000)) break;

                if(digitalRead(BUTA_IO_PIN) != ssState) {
                    delay(50);
                    if((newSSState = digitalRead(BUTA_IO_PIN)) != ssState) {
                        ssCount++;
                        ssState = newSSState;
                    }
                } else {
                    delay(50);
                }
                
            }

            if(ssCount == 4) {

                remdisplay.setText("RST");
                remdisplay.show();

                Serial.println("Deleting ip config; temporarily clearing AP mode WiFi password");
    
                deleteIpSettings();
    
                // Set AP mode password to empty (not written, only until reboot!)
                settings.appw[0] = 0;

                while(!digitalRead(CALIBB_IO_PIN)) { }
            }

            remdisplay.clearBuf();
            remdisplay.show();
        }
    }

    showWaitSequence();
}

void main_setup()
{
    unsigned long now;
    bool initScanBP = false;
    
    Serial.println(F("DTM Remote Control version " REMOTE_VERSION " " REMOTE_VERSION_EXTRA));

    if(loadVis()) {                 // load visMode
        movieMode = !!(visMode & 0x01);
        displayGPSMode = !!(visMode & 0x02);
        autoThrottle = !!(visMode & 0x04);
    }
    updateConfigPortalVisValues();  // Update CP to current value(s)
    updateConfigPortalBriValues();

    doCoast = (atoi(settings.coast) > 0);
    #ifdef REMOTE_HAVEAUDIO
    ooTT = (atoi(settings.ooTT) > 0);
    #endif

    for(int i = 0; i < BTTFN_REM_MAX_COMMAND+1; i++) {
        bttfnSeqCnt[i] = 1;
    }

    buttonPackMomentary[0] = !(atoi(settings.bPb0Maint) > 0);
    buttonPackMomentary[1] = !(atoi(settings.bPb1Maint) > 0);
    buttonPackMomentary[2] = !(atoi(settings.bPb2Maint) > 0);
    buttonPackMomentary[3] = !(atoi(settings.bPb3Maint) > 0);
    buttonPackMomentary[4] = !(atoi(settings.bPb4Maint) > 0);
    buttonPackMomentary[5] = !(atoi(settings.bPb5Maint) > 0);
    buttonPackMomentary[6] = !(atoi(settings.bPb6Maint) > 0);
    buttonPackMomentary[7] = !(atoi(settings.bPb7Maint) > 0);

    #ifdef REMOTE_HAVEAUDIO
    buttonPackMtOnOnly[0] = (atoi(settings.bPb0MtO) > 0);
    buttonPackMtOnOnly[1] = (atoi(settings.bPb1MtO) > 0);
    buttonPackMtOnOnly[2] = (atoi(settings.bPb2MtO) > 0);
    buttonPackMtOnOnly[3] = (atoi(settings.bPb3MtO) > 0);
    buttonPackMtOnOnly[4] = (atoi(settings.bPb4MtO) > 0);
    buttonPackMtOnOnly[5] = (atoi(settings.bPb5MtO) > 0);
    buttonPackMtOnOnly[6] = (atoi(settings.bPb6MtO) > 0);
    buttonPackMtOnOnly[7] = (atoi(settings.bPb7MtO) > 0);
    #endif

    #ifdef REMOTE_HAVEMQTT
    for(int i = 0; i < PACK_SIZE; i++) {
        MQTTbuttonOnLen[i] = MQTTbuttonOffLen[i] = 0;
    }
    if(useMQTT) {
        for(int i = 0; i < PACK_SIZE; i++) {
            if(strlen(settings.mqttbt[i])) {
                MQTTbuttonOnLen[i] = strlen(settings.mqttbo[i]);
                MQTTbuttonOffLen[i] = strlen(settings.mqttbf[i]);
            }
        }
    }
    #endif

    // Invoke audio file installer if SD content qualifies
    #ifdef REMOTE_HAVEAUDIO
    #ifdef REMOTE_DBG
    Serial.println(F("Probing for audio data on SD"));
    #endif
    if(check_allow_CPA()) {
        showWaitSequence();
        if(prepareCopyAudioFiles()) {
            play_file("/_installing.mp3", PA_ALLOWSD, 1.0);
            waitAudioDone(false);
        }
        doCopyAudioFiles();
        // We never return here. The ESP is rebooted.
    }
    #endif

    #ifdef REMOTE_HAVEAUDIO
    playClicks = (atoi(settings.playClick) > 0);

    havePOFFsnd = check_file_SD(powerOffSnd);
    haveBOFFsnd = check_file_SD(brakeOffSnd);
    haveThUp = check_file_SD(throttleUpSnd);
    #endif

    // Initialize throttle
    if(rotEnc.begin()) {
        useRotEnc = true;
        loadCalib();
    } else {
        Serial.println("Rotary encoder/ADC throttle not found");
    }

    // Check for secondary RotEnc for volume on secondary i2c addresses
    // Not used - buttons for vol control suffice
    #if 0
    #ifdef REMOTE_HAVEAUDIO
    if(rotEncV.begin(false)) {
        useRotEncVol = true;
        rotEncVol = &rotEncV;
        re_vol_reset();
    }
    #endif
    #endif

    // Initialize switches and buttons
    powerswitch.begin(FPOWER_IO_PIN, true, true);  // active low, pullup
    powerswitch.setTiming(50, 50);
    powerswitch.attachLongPressStart(powKeyPressed);
    powerswitch.attachLongPressStop(powKeyLongPressStop);
    powerswitch.scan();

    brake.begin(STOPS_IO_PIN, false, false);       // active high, pulldown on board    
    brake.setTiming(50, 50);
    brake.attachLongPressStart(brakeKeyPressed);
    brake.attachLongPressStop(brakeKeyLongPressStop);
    brake.scan();

    calib.begin(CALIBB_IO_PIN, true, true);        // active low, pullup
    calib.setTiming(50, 2000);
    calib.attachPressDown(calibKeyPressed);
    calib.attachPressEnd(calibKeyPressStop);
    calib.attachLongPressStart(calibKeyLongPressed);
    calib.attachLongPressStop(calibKeyPressed);

    // Button A ("O.O")
    buttonA.begin(BUTA_IO_PIN, true, true);            // active low, pullup
    buttonA.setTiming(50, 2000);
    buttonA.attachPressDown(buttonAKeyPressed);
    buttonA.attachPressEnd(buttonAKeyPressStop);
    buttonA.attachLongPressStart(buttonAKeyLongPressed);
    buttonA.attachLongPressStop(buttonAKeyPressed);

    // Button B ("RESET")
    buttonB.begin(BUTB_IO_PIN, true, true);            // active low, pullup
    buttonB.setTiming(50, 2000);
    buttonB.attachPressDown(buttonBKeyPressed);
    buttonB.attachPressEnd(buttonBKeyPressStop);
    buttonB.attachLongPressStart(buttonBKeyLongPressed);
    buttonB.attachLongPressStop(buttonBKeyPressed);

    #ifdef ALLOW_DIS_UB
    if(!atoi(settings.disBPack)) {
    #endif
        if((useBPack = butPack.begin())) {
            butPack.setScanInterval(50);
            butPack.attachPressDown(butPackKeyPressed);
            butPack.attachPressEnd(butPackKeyPressStop);
            butPack.attachLongPressStart(butPackKeyLongPressed);
            butPack.attachLongPressStop(butPackKeyLongPressStop);
            for(int i = 0; i < butPack.getPackSize(); i++) {
                isbutPackKeyPressed[i] = false;
                isbutPackKeyLongPressed[i] = false;
                isbutPackKeyChange[i] = false;
                if(buttonPackMomentary[i]) {
                    butPack.setTiming(i, 50, 2000);
                } else {
                    butPack.setTiming(i, 50, 50);
                    initScanBP = true;
                }
            }
            if(initScanBP) {
                butPack.scan();
            }
        } else {
            #ifdef REMOTE_DBG
            Serial.println("ButtonPack not detected");
            #endif
        }
    #ifdef ALLOW_DIS_UB    
    } else {
        useBPack = false;
        #ifdef REMOTE_DBG
        Serial.println("ButtonPack disabled");
        #endif
    }
    #endif
    
    now = millis();

    #ifdef REMOTE_HAVEAUDIO
    if(!haveAudioFiles) {
        #ifdef REMOTE_DBG
        Serial.println(F("Current audio data not installed"));
        #endif
        remdisplay.on();
        remdisplay.setText("AUD");
        remdisplay.show();
        delay(1000);
        remdisplay.clearBuf();
        remdisplay.show();
    }
    #endif

    // Initialize BTTF network
    bttfn_setup();
    bttfn_loop();
  
    FPBUnitIsOn = false;
    justBootedNow = millis();
    bootFlag = true;

    // Short delay to allow .scan(s) in loop to detect powerswitch 
    // and brake changes in first iteration, and to bridge debounce
    // time for buttons 1-8 if maintained
    now = millis() - now;
    if(now < 60) {
        mydelay(60 - now, true);
    }

    // For maintained switches, do second scan and clear Change flag 
    // so initial position does not trigger an event
    if(initScanBP) {
        butPack.scan();
        for(int i = 0; i < butPack.getPackSize(); i++) {
            if(!buttonPackMomentary[i]) {
                isbutPackKeyChange[i] = false;
            }
        }
    }
}

void main_loop()
{
    unsigned long now = millis();
    bool forceDispUpd = false;
    bool wheelsChanged = false;

    if(triggerCompleteUpdate) {
        triggerCompleteUpdate = false;
        bttfn_remote_send_combined(powerState, brakeState, currSpeed);
    }

    // Scan power switch
    powerswitch.scan();
    if(isFPBKeyChange) {
        isFPBKeyChange = false;
        powerState = isFPBKeyPressed;
        #ifdef REMOTE_DBG
        if(bootFlag) {
            Serial.printf("Power change detected at boot\n");
        }
        #endif
        if(isFPBKeyPressed) {
            if(!FPBUnitIsOn) {
                // Fake power on:
                FPBUnitIsOn = true;

                // power LED and level meter
                condPLEDaBLvl(true, true);
  
                calibMode = false;    // Cancel calibration

                offDisplayTimer = false;

                etmr = false;

                // Re-connect if we're in AP mode but
                // there is a configured WiFi network
                if(!justBootedNow && wifiNeedReConnect()) {
                    showWaitSequence();
                    remdisplay.on();
                    wifiReConnect();
                    endWaitSequence();
                }
                justBootedNow = 0;

                currSpeedF = 0;
                currSpeed = 0;
                keepCounting = false;
                triggerTTonThrottle = 0;

                // Re-init brake
                remledStop.setState(true);

                // Display ON
                remdisplay.setBrightness(255);

                // Scan brake switch
                brake.scan();

                play_startup();

                // Scan brake switch again
                brake.scan();
                if(isBrakeKeyChange) {
                    isBrakeKeyChange = false;
                    brakeState = isBrakeKeyPressed;
                    // Do NOT play sound
                }

                remdisplay.on();
                remdisplay.setSpeed(currSpeedF);
                remdisplay.show();
                doForceDispUpd = false;

                tcdSpeedP0Old = 2000;
                tcdIsInP0 = tcdIsInP0Old = 0;

                lockThrottle = false;

                networkTimeTravel = false;

                bttfn_remote_send_combined(powerState, brakeState, currSpeed);

                #ifdef REMOTE_HAVEAUDIO
                play_file(powerOnSnd, PA_INTRMUS|PA_ALLOWSD|PA_DYNVOL, 1.0);
                #endif
    
                // FIXME - anything else?
            }
        } else {
            if(FPBUnitIsOn) {
                // Fake power off:
                FPBUnitIsOn = false;

                triggerTTonThrottle = 0;

                // power LED and level meter
                condPLEDaBLvl(false, false);

                // Stop musicplayer & audio in general
                #ifdef REMOTE_HAVEAUDIO
                mp_stop();
                stopAudio();
                #endif

                remledStop.setState(false);                

                TTrunning = false;
                offDisplayTimer = false;
                if(displayGPSMode) {
                    currSpeedOldGPS = -2;   // Trigger GPS speed display update
                }

                remdisplay.off();

                flushDelayedSave();

                bttfn_remote_send_combined(powerState, brakeState, currSpeed);

                #ifdef REMOTE_HAVEAUDIO
                if(havePOFFsnd) {
                    play_file(powerOffSnd, PA_INTRMUS|PA_ALLOWSD|PA_DYNVOL, 1.0);
                }
                #endif

                // FIXME - anything else?
            }
        }
    } else {
        if(bootFlag) {
            #ifdef REMOTE_DBG
            Serial.printf("no power change at boot detected\n");
            #endif
        
            // Send initial status to TCD
            sendBootStatus = true;
        }
    }

    // Scan brake switch
    if(FPBUnitIsOn) {
        brake.scan();
        if(isBrakeKeyChange) {
            isBrakeKeyChange = false;
            brakeState = isBrakeKeyPressed;
            bttfn_remote_send_combined(powerState, brakeState, currSpeed);
            sendBootStatus = false;

            #ifdef REMOTE_HAVEAUDIO
            if(brakeState) {
                play_file(brakeOnSnd, PA_ALLOWSD|PA_DYNVOL, 1.0);
            } else {
                if(haveBOFFsnd) {
                    play_file(brakeOffSnd, PA_ALLOWSD|PA_DYNVOL, 1.0);
                }
            }
            #endif
        }
    }

    // Button A "O.O":
    //    Fake-power on:
    //        If buttonPack is enabled/present:
    //            Short press: BTTFN-wide TT or MusicPlayer Previous Song (depending on option)
    //            Long press:  MusicPlayer Play/Stop
    //        If buttonPack is disabled/not present: 
    //            Short press: Play "key3"
    //            Long press:  MusicPlayer Play/Stop
    //    Fake-power off:
    //        Short press: Increase volume
    //        Long press: Increase brightness
    // Button B "RESET":
    //    Fake-power on: 
    //        If buttonPack is enabled/present:
    //            Short press: MusicPlayer Next Song
    //            Long press:  MusicPlayer: Toggle shuffle
    //        If buttonPack is disabled/not present: 
    //            Short press: Play "key6"
    //            Long press:  MusicPlayer Next Song
    //    Fake-power off:
    //        Short press: Decrease volume
    //        Long press: Decrease brightness
    buttonA.scan();
    if(isbuttonAKeyChange) {
        isbuttonAKeyChange = false;
        if(FPBUnitIsOn) {
            if(useBPack) {
                if(isbuttonAKeyPressed) {
                    if(ooTT) {
                        if(!TTrunning && !tcdIsInP0) {
                            #ifdef REMOTE_HAVEAUDIO
                            bool playBad = false;
                            #endif
                            if(!triggerTTonThrottle) {
                                if(BTTFNTriggerTT(true)) {
                                    triggerTTonThrottle = 1;
                                    #ifdef REMOTE_HAVEAUDIO
                                    play_file("/rdy.mp3", PA_INTRMUS|PA_ALLOWSD, 1.0);
                                    #endif
                                } else {
                                    #ifdef REMOTE_HAVEAUDIO
                                    playBad = true;
                                    #endif
                                }
                            } else if(triggerTTonThrottle == 1) {
                                triggerTTonThrottle = 0;
                                #ifdef REMOTE_HAVEAUDIO
                                playBad = true;
                                #endif
                            }
                            #ifdef REMOTE_HAVEAUDIO
                            if(playBad) {
                                play_bad();
                            }
                            #endif
                        }
                    } else {
                        #ifdef REMOTE_HAVEAUDIO
                        if(haveMusic) {
                            mp_prev(mpActive);
                        } else {
                            play_bad();
                        }
                        #endif
                    }
                } else if(isbuttonAKeyLongPressed) {
                    #ifdef REMOTE_HAVEAUDIO
                    if(haveMusic) {
                        if(mpActive) {
                            mp_stop();
                        } else {
                            mp_play();
                        }
                    } else {
                        play_bad();
                    }
                    #endif
                }
            #ifdef ALLOW_DIS_UB
            } else {
                if(isbuttonAKeyPressed) {
                    #ifdef REMOTE_HAVEAUDIO
                    play_key(3);
                    #endif
                } else if(isbuttonAKeyLongPressed) {
                    #ifdef REMOTE_HAVEAUDIO
                    if(haveMusic) {
                        if(mpActive) {
                            mp_stop();
                        } else {
                            mp_play();
                        }
                    } else {
                        play_bad();
                    }
                    #endif
                }
            #endif  // ALLOW_DIS_UB
            }
        } else if(!calibMode) {           // When off, but not in calibMode
            if(isbuttonAKeyPressed) {
                #ifdef REMOTE_HAVEAUDIO
                if(increaseVolume()) {
                    displayVolume();
                    offDisplayTimer = true;
                    offDisplayNow = millis();
                }
                play_file("/volchg.mp3", PA_INTRMUS|PA_ALLOWSD, 1.0);
                #endif
            } else if(isbuttonAKeyLongPressed) {
                increaseBrightness();
                displayBrightness();
                offDisplayTimer = true;
                offDisplayNow = millis();
            }
        }
    }
    buttonB.scan();
    if(isbuttonBKeyChange) {
        isbuttonBKeyChange = false;
        if(FPBUnitIsOn) {
            if(useBPack) {
                if(isbuttonBKeyPressed) {
                    #ifdef REMOTE_HAVEAUDIO
                    if(haveMusic) {
                        mp_next(mpActive);
                    } else {
                        play_bad();
                    }
                    #endif
                } else if(isbuttonBKeyLongPressed) {
                    #ifdef REMOTE_HAVEAUDIO
                    if(haveMusic) {
                        mp_makeShuffle(!mpShuffle);
                        play_file(mpShuffle ? "/shufon.mp3" : "/shufoff.mp3", PA_ALLOWSD, 1.0);
                    } else {
                        play_bad();
                    }
                    #endif
                }
            #ifdef ALLOW_DIS_UB
            } else {
                if(isbuttonBKeyPressed) {
                    #ifdef REMOTE_HAVEAUDIO
                    play_key(6);
                    #endif
                } else if(isbuttonBKeyLongPressed) {
                    #ifdef REMOTE_HAVEAUDIO
                    if(haveMusic) {
                        mp_next(mpActive);
                    } else {
                        play_bad();
                    }
                    #endif
                }
            #endif  // ALLOW_DIS_UB
            }
        } else if(!calibMode) {           // When off, but not in calibMode
            if(isbuttonBKeyPressed) {
                #ifdef REMOTE_HAVEAUDIO
                if(decreaseVolume()) {
                    displayVolume();
                    offDisplayTimer = true;
                    offDisplayNow = millis();
                }
                play_file("/volchg.mp3", PA_INTRMUS|PA_ALLOWSD, 1.0);
                #endif
            } else if(isbuttonBKeyLongPressed) {
                decreaseBrightness();
                displayBrightness();
                offDisplayTimer = true;
                offDisplayNow = millis();
            }
        }
    }

    // Calibration button:
    //    Fake-power is off: Throttle calibration
    //        - Short press registers current position as "center" (zero) position.
    //        - Long press enters calib mode for full throttle forward and backward:
    //             * ("UP is displayed) Hold throttle full up, 
    //             * short-press button, 
    //             * ("DN" is displayed) Hold throttle full down, 
    //             * short-press button.
    //          If long-pressed during calib mode, calibration is cancelled
    //    Fake-power on:
    //        Short press: Reset speed to 0
    // .      Long press:  Display IP address
    calib.scan();
    if(isCalibKeyChange) {
        isCalibKeyChange = false;
        if(!FPBUnitIsOn) {
            if(isCalibKeyPressed) {
                if(calibMode) {
                    if(calibUp) {
                        if(useRotEnc && rotEnc.setMaxStepsUp(0)) {
                            remdisplay.setText("DN");
                            remdisplay.show();
                            calibUp = false;
                        } else {
                            remdisplay.setText("ERR");
                            remdisplay.show();
                            offDisplayTimer = true;
                            offDisplayNow = millis();
                            calibMode = false;
                            condPLEDaBLvl(false, false);
                        }
                    } else {
                        if(useRotEnc && rotEnc.setMaxStepsDown(0)) {
                            calibMode = false;
                            saveCalib();
                            remdisplay.clearBuf();
                            remdisplay.show();
                            remdisplay.off();
                            currSpeedOldGPS = -2;   // force GPS speed display update
                        } else {
                            remdisplay.setText("ERR");
                            remdisplay.show();
                            offDisplayTimer = true;
                            offDisplayNow = millis();
                            calibMode = false;
                        }
                        condPLEDaBLvl(false, false);
                    }
                } else {
                    condPLEDaBLvl(true, true);
                    remdisplay.setText("CAL");
                    remdisplay.show();
                    remdisplay.on();
                    offDisplayTimer = true;
                    offDisplayNow = millis();
                    delay(pwrLEDonFP ? 2000 : 200);    // Stabilize voltage after turning on display, LED, level meter
                    if(useRotEnc) {
                        rotEnc.zeroPos(true);
                        if(!rotEnc.dynZeroPos()) {
                            saveCalib();
                        } 
                    }
                    condPLEDaBLvl(false, false);
                }
            } else if(isCalibKeyLongPressed) {
                if(calibMode) {
                    calibMode = false;
                    remdisplay.clearBuf();
                    remdisplay.show();
                    remdisplay.off();
                    condPLEDaBLvl(false, false);
                    currSpeedOldGPS = -2;   // force GPS speed display update
                } else {
                    calibMode = true;
                    offDisplayTimer = false;
                    remdisplay.setText("UP");
                    remdisplay.show();
                    remdisplay.on();
                    condPLEDaBLvl(true, true);
                    calibUp = true;
                }
            }
        } else {
            if(!TTrunning && !tcdIsInP0) {
                if(isCalibKeyPressed) {
                    if(!throttlePos && !tcdIsInP0 && !TTrunning) {
                        currSpeedF = 0;
                        currSpeed = 0;
                        keepCounting = false;
                        triggerTTonThrottle = 0;
                        bttfn_remote_send_combined(powerState, brakeState, currSpeed);
                        doForceDispUpd = true;
                    }
                } else if(isCalibKeyLongPressed) {
                    flushDelayedSave();
                    display_ip();
                    doForceDispUpd = true;
                    triggerTTonThrottle = 0;
                }
            }
        }
    }

    // Optional button pack: Up to 8 momentary buttons or maintained switches
    if(useBPack) {
        butPack.scan();
        for(int i = 0; i < butPack.getPackSize(); i++) {
            if(isbutPackKeyChange[i]) {
                isbutPackKeyChange[i] = false;
                if(FPBUnitIsOn) {
                    if(!buttonPackMomentary[i]) {
                        // Maintained: 
                        // If "audio on ON only" checked, play if switched ON, and stop
                        // on OFF if same sound triggered by ON is still playing.
                        // If "audio on ON only" unchecked: Every flip triggers keyX 
                        // playback. A second flip while the same keyX sound is still 
                        // being played, causes a stop.
                        buttonPackActionPress(i, (buttonPackMtOnOnly[i] && !isbutPackKeyPressed[i]));
                    } else {
                        // Momentary: Press triggers keyX, long press keyXl.
                        // A second press/longpress while the same keyX(l) 
                        // sound is still being played, causes a stop.
                        if(isbutPackKeyPressed[i]) {
                            buttonPackActionPress(i, false);
                        } else if(isbutPackKeyLongPressed[i]) {
                            buttonPackActionLongPress(i);
                        }
                    }
                }
            } 
        }
    }
    
    // Scan throttle position
    if(triggerTTonThrottle) {
        throttlePos = rotEnc.updateThrottlePos();
        if(FPBUnitIsOn && !TTrunning && !tcdIsInP0) {
            if(triggerTTonThrottle == 1 && throttlePos > 0) {
                triggerTTonThrottle++;
                BTTFNTriggerTT(false);
            }
        }
    } else if(!calibMode && !tcdIsInP0) {
        throttlePos = rotEnc.updateThrottlePos();
        if(FPBUnitIsOn && !TTrunning) {
            int tas = 0, tidx = 0;
            if(!throttlePos) {
                lockThrottle = false;
            }

            // Auto throttle
            if(keepCounting) {
                if(throttlePos < 0) {
                    keepCounting = false;
                } else if(oldThrottlePos > 0) {
                    if(throttlePos < oldThrottlePos) {
                        throttlePos = oldThrottlePos;
                    }
                }
            } else if(autoThrottle) {
                keepCounting = (throttlePos > 0 && !oldThrottlePos);
            }
            
            if(movieMode) {

                if((oldThrottlePos = throttlePos)) {
                    tas = sizeof(strictAccelFacts)/sizeof(strictAccelFacts[0]);
                    tidx = (abs(throttlePos)-1) * tas / 100;
                    accelDelay = strictAccelDelays[currSpeedF / 10] * strictAccelFacts[tidx] / 100;
                    accelStep = 1;
                } else {
                    //tidx = -1;
                    accelDelay = doCoast ? ((esp_random() % 20) + 40) : 0;
                    //accelStep = 0;
                }

            } else {
              
                if(throttlePos != oldThrottlePos) {
                    if((oldThrottlePos = throttlePos)) {
                        tas = sizeof(accelDelays)/sizeof(accelDelays[0]);
                        tidx = (abs(throttlePos)-1) * tas / 100;
                        accelDelay = accelDelays[tidx];
                        accelStep = accelSteps[tidx];
                    } else {
                        //tidx = -1;
                        accelDelay = doCoast ? ((esp_random() % 20) + 40) : 0;
                        //accelStep = 0;
                    }
                }

            }

            #ifdef REMOTE_HAVEAUDIO
            if(tidx < tas - 1 || throttlePos >= 0 || currSpeedF) {
                etmr = false;
            } else if(!etmr) {
                etmr = true;
                enow = millis();
            }
            if(etmr && millis() - enow > 5000) {
                etmr = false;
                play_file("/tmd.mp3", PA_ALLOWSD, 1.0);
            }
            #endif
            
            if(!lockThrottle) {
                if(millis() - lastSpeedUpd > accelDelay) {
                    int sbf = currSpeedF;
                    int sb  = sbf / 10;
                    if(throttlePos > 0) {
                        #ifdef REMOTE_HAVEAUDIO
                        if(!currSpeedF) {
                            if(haveThUp) {
                                play_file(throttleUpSnd, PA_THRUP|PA_INTRMUS|PA_ALLOWSD, 1.0);
                            } else {
                                play_file("/throttleup.wav", PA_THRUP|PA_INTRMUS|PA_WAV|PA_ALLOWSD, 1.0);
                            }
                        }
                        #endif
                        currSpeedF += accelStep;
                        if(currSpeedF > 880) {
                            currSpeedF = 880;
                            keepCounting = false;
                        }
                    } else if(throttlePos < 0) {
                        currSpeedF -= accelStep;
                        if(currSpeedF < 0) currSpeedF = 0;
                        keepCounting = false;
                    } else if(doCoast) {
                        currSpeedF -= (esp_random() % 3);
                        if(currSpeedF < 0) currSpeedF = 0;
                    }
                    if(currSpeedF != sbf) {
                        currSpeed = currSpeedF / 10;
                        if(currSpeed != sb) {
                            bttfn_remote_send_combined(powerState, brakeState, currSpeed);
                            #ifdef REMOTE_HAVEAUDIO
                            if(throttlePos > 0) {
                                play_click();
                            }
                            #endif
                        }
                        lastSpeedUpd = millis();                
                        remdisplay.setSpeed(currSpeedF);
                        #ifdef REMOTE_HAVEAUDIO
                        remdisplay.show();
                        #endif
                    }
                }
            }

            // Network-latency-depended display sync is nice'n'all but latency
            // measurement is dead as soon as audio comes into play. (1-2 vs 10-13).
            #ifndef REMOTE_HAVEAUDIO
            if(millis() - lastSpeedUpd > bttfnCurrLatency) {
                remdisplay.show();
            }
            #ifdef REMOTE_DBG
            if(bttfnCurrLatency > 10) {
                Serial.printf("latency %d\n", bttfnCurrLatency);
            }
            #endif
            #endif
        }
        if(!FPBUnitIsOn && !calibMode && !offDisplayTimer) {
            if(displayGPSMode) {
                if(tcdCurrSpeed != currSpeedOldGPS) {
                    remdisplay.on();
                    remdisplay.setSpeed(tcdCurrSpeed * 10);
                    remdisplay.show();
                    currSpeedOldGPS = tcdCurrSpeed;
                }
            } else {
                offDisplayTimer = true;
                offDisplayNow = millis() - 1001;
            }
        }
    }

    if(tcdIsInP0) {
        if(!tcdIsInP0Old || tcdSpeedP0 != tcdSpeedP0Old) {
            if(!tcdIsInP0Old) {
                triggerTTonThrottle = 0;
                tcdIsInP0Old = tcdIsInP0;
                tcdInP0now = millis();
                tcdClickNow = 0;
                #ifdef REMOTE_DBG
                Serial.printf("Switching to P0\n");
                #endif
            }
            if(FPBUnitIsOn) {
                #ifdef REMOTE_HAVEAUDIO
                if(!tcdSpeedP0) {
                    if(haveThUp) {
                        play_file(throttleUpSnd, PA_THRUP|PA_INTRMUS|PA_ALLOWSD, 1.0);
                    } else {
                        play_file("/throttleup.wav", PA_THRUP|PA_INTRMUS|PA_WAV|PA_ALLOWSD, 1.0);
                    }
                } else if(tcdSpeedP0 > 0 && (!tcdClickNow || millis() - tcdClickNow > 25)) {
                    tcdClickNow = millis();
                    play_click();
                }
                #endif
                remdisplay.on();
                remdisplay.setSpeed(tcdSpeedP0 * 10);
                remdisplay.show();
                tcdSpeedP0Old = tcdSpeedP0;
                tcdSpdChgNow = millis();
                tcdSpdFake100 = 0;
            }
        } else if(FPBUnitIsOn) {
            // Fake .1s
            if(millis() - tcdSpdChgNow > accelDelays[4]) {
                tcdSpdFake100++;
                if(tcdSpdFake100 > 9) tcdSpdFake100 = 1;
                tcdSpdChgNow = millis();
                remdisplay.on();
                remdisplay.setSpeed((tcdSpeedP0 * 10) + tcdSpdFake100);
                remdisplay.show();
            }
        }
        if(millis() - tcdInP0now > 45*1000) {
            tcdIsInP0 = 0;
            #ifdef REMOTE_DBG
            Serial.printf("Expiring P0\n");
            #endif
            doForceDispUpd = true;
        }
    } else if(tcdIsInP0Old) {
        tcdSpeedP0Old = 2000;
        tcdIsInP0Old = 0;
        doForceDispUpd = true;
        triggerTTonThrottle = 0;
        #ifdef REMOTE_DBG
        Serial.printf("P0 is off\n");
        #endif
    }
    
    if(FPBUnitIsOn && doForceDispUpd && !TTrunning && !tcdIsInP0) {
        doForceDispUpd = false;
        remdisplay.setSpeed(currSpeedF);
        remdisplay.show();
    }
    
    if(millis() - lastCommandSent > 10*1000) {
        bttfn_remote_send_combined(powerState, brakeState, currSpeed);
    }

    if(justBootedNow && (millis() - justBootedNow > 10*1000)) {
        justBootedNow = 0;
    }

    if(!FPBUnitIsOn && offDisplayTimer && millis() - offDisplayNow > 1000) {
        offDisplayTimer = false;
        remdisplay.off();
        currSpeedOldGPS = -2;   // force GPS speed display update
    }

    // Execute remote commands
    // No commands in calibMode, during a TT (or P0), and when acceleration is running
    // FPBUnitIsOn checked for each individually
    if(!TTrunning && !calibMode && !tcdIsInP0 && !throttlePos && !keepCounting) {
        execute_remote_command();
    }

    if(FPBUnitIsOn) {
      
        // TT triggered?
        if(!TTrunning) {
            if(networkTimeTravel) {
                networkTimeTravel = false;
                timeTravel(networkLead, networkP1);
            }
        }

    }

    now = millis();
    
    // TT sequence logic: TT is mutually exclusive with stuff below
    
    if(TTrunning) {

        if(TTP0) {   // Acceleration - runs for P0duration ms

            if(!networkAbort && (now - TTstart < P0duration)) {

                // We assume that we are in tcdIsInP0 mode.
                
            } else {

                TTP0 = false;
                TTP1 = true;

                TTFlag = false;

                TTstart = now;

                remdisplay.setText("88.0");
                remdisplay.show();
                remdisplay.on();

                tcdIsInP0 = 0;

            }
        }
        if(TTP1) {   // Peak/"time tunnel" - ends with "REENTRY" or "ABORT"

            if(!networkReentry && !networkAbort) {

                // Display "." at half of P1
            
                if(!TTFlag && (millis() - TTstart > P1duration / 2)) {
                    remdisplay.setText("  .");
                    remdisplay.show();
                    remdisplay.on();

                    // Reset speed to 0, so TCD counts down in its P2
                    currSpeedF = 0;
                    currSpeed = 0;
                    bttfn_remote_send_combined(powerState, brakeState, currSpeed);
                  
                    TTFlag = true;
                }
                                    
            } else {

                TTP1 = false;
                TTP2 = true; 

                TTstart = now;
                TTFlag = false;
                
            }
        }
        if(TTP2) {   // Reentry - up to us

              #ifdef REMOTE_HAVEAUDIO
              if(TTFlag || networkAbort) {
              #endif
              
                  // Lock accel until lever is in zero-pos
                  lockThrottle = true;

                  doForceDispUpd = true;

                  keepCounting = false;
                  triggerTTonThrottle = 0;
    
                  TTP2 = false;
                  TTrunning = false;

              #ifdef REMOTE_HAVEAUDIO
              } else if(millis() - TTstart > 6400) {
                
                  play_file("/reentry.mp3", PA_INTRMUS|PA_ALLOWSD, 1.0);
                  TTFlag = true;

              }
              #endif
            
        }

    } else {    // No TT currently

        if(networkAlarm && !calibMode && !tcdIsInP0 && !throttlePos && !keepCounting) {

            networkAlarm = false;

            #ifdef REMOTE_HAVEAUDIO
            if(atoi(settings.playALsnd) > 0) {
                play_file("/alarm.mp3", PA_INTRMUS|PA_ALLOWSD|PA_DYNVOL, 1.0);
            }
            #endif
        
        } 
        
    }

    now = millis();

    // If network is interrupted, return to stand-alone
    if(useBTTFN) {
        if( (lastBTTFNpacket && (now - lastBTTFNpacket > 30*1000)) ||
            (!BTTFNBootTO && !lastBTTFNpacket && (now - powerupMillis > 60*1000)) ) {
            lastBTTFNpacket = 0;
            BTTFNBootTO = true;
            tcdCurrSpeed = -1;
            tcdIsInP0 = 0;
        }
    }

    bttfnRemPollInt = (!FPBUnitIsOn && displayGPSMode && (tcdCurrSpeed >= 0)) ? BTTFN_POLL_INT_FAST : BTTFN_POLL_INT;

    // Poll RotEnv for volume. Don't in calibmode, P0 or during
    // acceleration
    #if 0
    if(useRotEncVol && !calibMode && !tcdIsInP0 && !throttlePos && !keepCounting) {
        int oldVol = curSoftVol;
        curSoftVol = rotEncVol->updateVolume(curSoftVol, false);
        if(oldVol != curSoftVol) {
            volchanged = true;
            volchgnow = millis();
        }
    }
    #endif

    if(!TTrunning && !tcdIsInP0 && !calibMode && !throttlePos && !keepCounting) {
        // Save secondary settings 10 seconds after last change
        if(brichanged && (now - brichgnow > 10000)) {
            brichanged = false;
            saveBrightness();
        }
        #ifdef REMOTE_HAVEAUDIO
        if(volchanged && (now - volchgnow > 10000)) {
            volchanged = false;
            saveCurVolume();
        }
        #endif
        if(vischanged && (now - vischgnow > 10000)) {
            vischanged = false;
            saveVis();
        }
    }

    if(bootFlag) {
        bootFlag = false;
        if(sendBootStatus) {
            bttfn_remote_send_combined(powerState, brakeState, currSpeed);
            sendBootStatus = false;
        }
        
    }
}

void flushDelayedSave()
{
    if(brichanged) {
        brichanged = false;
        saveBrightness();
    }
    #ifdef REMOTE_HAVEAUDIO
    if(volchanged) {
        volchanged = false;
        saveCurVolume();
    }
    #endif
    if(vischanged) {
        vischanged = false;
        saveVis();
    }
}

#ifdef REMOTE_HAVEAUDIO
static bool chgVolume(int d)
{
    #ifdef REMOTE_HAVEVOLKNOB
    if(curSoftVol == 255)
        return false;
    #endif

    int nv = curSoftVol;
    nv += d;
    if(nv < 0 || nv > 19)
        nv = curSoftVol;
        
    curSoftVol = nv;

    volchanged = true;
    volchgnow = millis();
    updateConfigPortalVolValues();
    return true;
}

bool increaseVolume()
{
    return chgVolume(1);
}

bool decreaseVolume()
{
    return chgVolume(-1);
}

static void displayVolume()
{
    char buf[8];
    sprintf(buf, "%3d", curSoftVol);
    remdisplay.setText(buf);
    remdisplay.show();
    remdisplay.on();
}
#endif

static void changeBrightness(int d)
{
    int b = (int)remdisplay.getBrightness();
    
    b += d;
    if(b < 0) b = 0;
    else if(b > 15) b = 15;
    
    remdisplay.setBrightness(b);
    brichanged = true;
    brichgnow = millis();
    updateConfigPortalBriValues();
}

static void increaseBrightness()
{
    changeBrightness(1);
}

static void decreaseBrightness()
{
    changeBrightness(-1);
}

static void displayBrightness()
{
    char buf[8];
    sprintf(buf, "%3d", remdisplay.getBrightness());
    remdisplay.setText(buf);
    remdisplay.show();
    remdisplay.on();
}

void updateVisMode()
{
    visMode &= ~0x07;
    if(movieMode) visMode |= 0x01;
    if(displayGPSMode) visMode |= 0x02;
    if(autoThrottle) visMode |= 0x04;
}

static void toggleDisplayGPS()
{
    displayGPSMode = !displayGPSMode;
    updateVisMode();
    vischanged = true;
    vischgnow = millis();
    updateConfigPortalVisValues();

    if(!FPBUnitIsOn && displayGPSMode) {
        currSpeedOldGPS = -2;   // force GPS speed display update
    }
}

static void toggleMovieMode()
{
    movieMode = !movieMode;
    updateVisMode();
    vischanged = true;
    vischgnow = millis();
    updateConfigPortalVisValues();
}

static void toggleAutoThrottle()
{
    autoThrottle = !autoThrottle;
    updateVisMode();
    vischanged = true;
    vischgnow = millis();
    updateConfigPortalVisValues();
}

static void condPLEDaBLvl(bool sLED, bool sLvl)
{
    if(pwrLEDonFP) {
        pwrled.setState(sLED);
        bLvLMeter.setState(sLvl);
    }
}

/*
 * Time travel
 * 
 */

void timeTravel(uint16_t P0Dur, uint16_t P1Dur)
{
    if(TTrunning)
        return;

    flushDelayedSave();
        
    TTrunning = true;
    TTstart = millis();
    TTP0 = true;   // phase 0
   
    P0duration = P0Dur;
    P1duration = P1Dur;

    etmr = false;

    #ifdef REMOTE_DBG
    Serial.printf("P0 duration is %d, P1 %d\n", P0duration, P1duration);
    #endif
}

/*
 * Show special signals
 */

void showWaitSequence()
{
    remdisplay.setText("\78\7");
    remdisplay.show();
}

void endWaitSequence()
{
    remdisplay.clearBuf();
    remdisplay.show();
    doForceDispUpd = true;
}

void showCopyError()
{
    remdisplay.setText("ERR");
    remdisplay.show();
    doForceDispUpd = true;
}

void prepareTT()
{
}

// Wakeup: Sent by TCD upon entering dest date,
// return from tt, triggering delayed tt via ETT
// For audio-visually synchronized behavior
// >>> Also called when RotEnc speed is changed, so
//     could ignore if we are not Speed master
void wakeup()
{
}

static void execute_remote_command()
{
    uint32_t command = commandQueue[oCmdIdx];

    // No command execution during timed sequences

    if(!command)
        return;

    commandQueue[oCmdIdx] = 0;
    oCmdIdx++;
    oCmdIdx &= 0x0f;

    if(command < 10) {                                // 700x

        // All here only when we're on
        if(!FPBUnitIsOn)
            return;

        switch(command) {
        case 1:
            #ifdef REMOTE_HAVEAUDIO                   // 7001: play "key1.mp3"
            play_key(1);
            #endif
            break;
        case 2:
            #ifdef REMOTE_HAVEAUDIO
            if(haveMusic) {
                mp_prev(mpActive);                    // 7002: Prev song
            }
            #endif
            break;
        case 3:
            #ifdef REMOTE_HAVEAUDIO                   // 7003: play "key3.mp3"
            play_key(3);
            #endif
            break;
        case 4:
            #ifdef REMOTE_HAVEAUDIO                   // 7004: play "key4.mp3"
            play_key(4);
            #endif
            break;
        case 5:                                       // 7005: Play/stop
            #ifdef REMOTE_HAVEAUDIO
            if(haveMusic) {
                if(mpActive) {
                    mp_stop();
                } else {
                    mp_play();
                }
            }
            #endif
            break;
        case 6:                                       // 7006: Play "key6.mp3"
            #ifdef REMOTE_HAVEAUDIO
            play_key(6);
            #endif
            break;
        case 7:
            #ifdef REMOTE_HAVEAUDIO                   // 7007: play "key7.mp3"
            play_key(7);
            #endif
            break;
        case 8:                                       // 7008: Next song
            #ifdef REMOTE_HAVEAUDIO
            if(haveMusic) {
                mp_next(mpActive);
            }
            #endif
            break;
        case 9:
            #ifdef REMOTE_HAVEAUDIO                   // 7009: play "key9.mp3"
            play_key(9);
            #endif
            break;
        }
      
    } else if(command < 100) {                        // 70xx

        // All here allowed when we're off

        switch(command) {
        case 60:                              // 7060  enable/disable Movie mode
            toggleMovieMode();
            break;
        case 61:                              // 7061  enable/disable "display TCD speed while fake-off"
            toggleDisplayGPS();
            break;
        case 62:                              // 7062  enable/disable autoThrottle
            toggleAutoThrottle();
            break;
        case 90:                              // 7090: Display IP address
            flushDelayedSave();
            display_ip();
            if(FPBUnitIsOn) {
                doForceDispUpd = true;
            } else {
                remdisplay.off();
                // force GPS speed display update
                currSpeedOldGPS = -2;   
            }
            break;
        default:
            if(command >= 50 && command <= 59) {   // 7050-7059: Set music folder number
                #ifdef REMOTE_HAVEAUDIO
                if(haveSD) {
                    switchMusicFolder((uint8_t)command - 50);
                }
                #endif
            }
            
        }
        
    } else if (command < 1000) {                      // 7xxx

        if(command >= 300 && command <= 399) {

            #ifdef REMOTE_HAVEAUDIO
            command -= 300;                           // 7300-7319/7399: Set fixed volume level / enable knob
            if(command == 99) {
                #ifdef REMOTE_HAVEVOLKNOB
                curSoftVol = 255;
                volchanged = true;
                volchgnow = millis();
                updateConfigPortalVolValues();
                re_vol_reset();
                #endif
            } else if(command <= 19) {
                curSoftVol = command;
                volchanged = true;
                volchgnow = millis();
                updateConfigPortalVolValues();
                re_vol_reset();
            } else if(command == 50 || command == 51) { // 7350/7351: Enable/disable acceleration click sound
                playClicks = (command == 50);
            }
            #endif

        } else if(command >= 400 && command <= 415) {

            command -= 400;                           // 7400-7415: Set brightness
            remdisplay.setBrightness(command);
            brichanged = true;
            brichgnow = millis();
            updateConfigPortalBriValues();

        } else {

            // All here only when we're on
            if(!FPBUnitIsOn)
                return;

            switch(command) {
            case 222:                                 // 7222/7555 Disable/enable shuffle
            case 555:
                #ifdef REMOTE_HAVEAUDIO
                if(haveMusic) {
                    mp_makeShuffle((command == 555));
                }
                #endif
                break;
            case 888:                                 // 7888 go to song #0
                #ifdef REMOTE_HAVEAUDIO
                if(haveMusic) {
                    mp_gotonum(0, mpActive);
                }
                #endif
                break;
            }
        }
        
    } else {
      
        switch(command) {
        case 64738:                               // 7064738: reboot
            bttfn_remote_unregister();
            remdisplay.off();
            remledStop.setState(false);
            mp_stop();
            stopAudio();
            flushDelayedSave();
            unmount_fs();
            delay(500);
            esp_restart();
            break;
        case 123456:
            flushDelayedSave();
            deleteIpSettings();                   // 7123456: deletes IP settings
            if(settings.appw[0]) {
                settings.appw[0] = 0;             //          and clears AP mode WiFi password
                write_settings();
            }
            break;
        default:                                  // 7888xxx: goto song #xxx
            #ifdef REMOTE_HAVEAUDIO
            if((command / 1000) == 888) {
                if(FPBUnitIsOn) {
                    uint16_t num = command - 888000;
                    num = mp_gotonum(num, mpActive);
                }
            }
            #endif
            break;
        }

    }
}

void display_ip()
{
    uint8_t a[4];
    char buf[8];

    remdisplay.setText("IP");
    remdisplay.show();

    mydelay(500, true);

    wifi_getIP(a[0], a[1], a[2], a[3]);

    for(int i = 0; i < 4; i++) {
        sprintf(buf, "%3d%s", a[i], (i < 3) ? "." : "");
        remdisplay.setText(buf);
        remdisplay.show();
        mydelay(1000, true);
    }
    
    remdisplay.clearBuf();
    remdisplay.show();
    mydelay(500, true);
}

static void play_startup()
{    
    remdisplay.setSpeed(0);
    remdisplay.show();
    remdisplay.on();

    mydelay(200, true);
    
    for(int i = 10; i <= 110; i+=10) {
        remdisplay.setSpeed(i);
        remdisplay.show();
        mydelay(80, true);
    }

    remdisplay.setSpeed(0);
    remdisplay.show();
}

#ifdef REMOTE_HAVEAUDIO
void switchMusicFolder(uint8_t nmf)
{
    bool waitShown = false;

    if(nmf > 9) return;
    
    if(musFolderNum != nmf) {
        musFolderNum = nmf;
        // Initializing the MP can take a while;
        // need to stop all audio before calling
        // mp_init()
        if(haveMusic && mpActive) {
            mp_stop();
        }
        stopAudio();
        if(mp_checkForFolder(musFolderNum) == -1) {
            flushDelayedSave();
            showWaitSequence();
            waitShown = true;
            play_file("/renaming.mp3", PA_INTRMUS|PA_ALLOWSD);
            waitAudioDone(true);
        }
        saveMusFoldNum();
        updateConfigPortalMFValues();
        mp_init(false);
        if(waitShown) {
            endWaitSequence();
        }
    }
}

void waitAudioDone(bool withBTTFN)
{
    int timeout = 400;

    while(!checkAudioDone() && timeout--) {
        mydelay(10, withBTTFN);
    }
}
#endif

static void powKeyPressed()
{
    isFPBKeyPressed = true;
    isFPBKeyChange = true;
}
static void powKeyLongPressStop()
{
    isFPBKeyPressed = false;
    isFPBKeyChange = true;
}

static void brakeKeyPressed()
{
    isBrakeKeyPressed = true;
    isBrakeKeyChange = true;
}
static void brakeKeyLongPressStop()
{
    isBrakeKeyPressed = false;
    isBrakeKeyChange = true;
}  

static void calibKeyPressed()
{
    isCalibKeyPressed = false;
    isCalibKeyLongPressed = false;
}

static void calibKeyPressStop()
{
    isCalibKeyPressed = true;
    isCalibKeyLongPressed = false;
    isCalibKeyChange = true;
}

static void calibKeyLongPressed()
{
    isCalibKeyPressed = false;
    isCalibKeyLongPressed = true;
    isCalibKeyChange = true;
}

static void buttonAKeyPressed()
{
    isbuttonAKeyPressed = false;
    isbuttonAKeyLongPressed = false;
}

static void buttonAKeyPressStop()
{
    isbuttonAKeyPressed = true;
    isbuttonAKeyLongPressed = false;
    isbuttonAKeyChange = true;
}

static void buttonAKeyLongPressed()
{
    isbuttonAKeyPressed = false;
    isbuttonAKeyLongPressed = true;
    isbuttonAKeyChange = true;
}

static void buttonBKeyPressed()
{
    isbuttonBKeyPressed = false;
    isbuttonBKeyLongPressed = false;
}

static void buttonBKeyPressStop()
{
    isbuttonBKeyPressed = true;
    isbuttonBKeyLongPressed = false;
    isbuttonBKeyChange = true;
}

static void buttonBKeyLongPressed()
{
    isbuttonBKeyPressed = false;
    isbuttonBKeyLongPressed = true;
    isbuttonBKeyChange = true;
}

static void butPackKeyPressed(int i)
{
    if(buttonPackMomentary[i]) {
        isbutPackKeyPressed[i] = false;
        isbutPackKeyLongPressed[i] = false;

        #ifdef REMOTE_HAVEMQTT
        mqtt_send_button_on(i);
        #endif
    }
}

static void butPackKeyPressStop(int i)
{
    if(buttonPackMomentary[i]) {
        isbutPackKeyPressed[i] = true;
        isbutPackKeyLongPressed[i] = false;
        isbutPackKeyChange[i] = true;
        
        #ifdef REMOTE_HAVEMQTT
        mqtt_send_button_off(i);
        #endif
    }
}

static void butPackKeyLongPressed(int i)
{
    if(buttonPackMomentary[i]) {
        isbutPackKeyPressed[i] = false;
        isbutPackKeyLongPressed[i] = true;
    } else {
        isbutPackKeyPressed[i] = true;
        isbutPackKeyLongPressed[i] = false;
        #ifdef REMOTE_HAVEMQTT
        mqtt_send_button_on(i);
        #endif
    }
    isbutPackKeyChange[i] = true;
}

static void butPackKeyLongPressStop(int i)
{
    isbutPackKeyPressed[i] = false;
    isbutPackKeyLongPressed[i] = false;
    isbutPackKeyChange[i] = true;
    #ifdef REMOTE_HAVEMQTT
    mqtt_send_button_off(i);
    #endif
}

static void buttonPackActionPress(int i, bool stopOnly)
{
    switch(i) {
    case 0:
        #ifdef REMOTE_HAVEAUDIO
        play_key(1, false, stopOnly);
        #endif
        break;
    case 1:
        #ifdef REMOTE_HAVEAUDIO
        play_key(2, false, stopOnly);
        #endif
        break;
    case 2:
        #ifdef REMOTE_HAVEAUDIO
        play_key(3, false, stopOnly);
        #endif
        break;
    case 3:
        #ifdef REMOTE_HAVEAUDIO
        play_key(4, false, stopOnly);
        #endif
        break;
    case 4:
        #ifdef REMOTE_HAVEAUDIO
        play_key(5, false, stopOnly);
        #endif
        break;
    case 5:
        #ifdef REMOTE_HAVEAUDIO
        play_key(6, false, stopOnly);
        #endif
        break;
    case 6:
        #ifdef REMOTE_HAVEAUDIO
        play_key(7, false, stopOnly);
        #endif
        break;
    case 7:
        #ifdef REMOTE_HAVEAUDIO
        play_key(9, false, stopOnly);    // yes, 9
        #endif
        break;
    }
}

static void buttonPackActionLongPress(int i)
{
    switch(i) {
    case 0:
        #ifdef REMOTE_HAVEAUDIO
        play_key(1, true);
        #endif
        break;
    case 1:
        #ifdef REMOTE_HAVEAUDIO
        play_key(2, true);
        #endif
        break;
    case 2:
        #ifdef REMOTE_HAVEAUDIO
        play_key(3, true);
        #endif
        break;
    case 3:
        #ifdef REMOTE_HAVEAUDIO
        play_key(4, true);
        #endif
        break;
    case 4:
        #ifdef REMOTE_HAVEAUDIO
        play_key(5, true);
        #endif
        break;
    case 5:
        #ifdef REMOTE_HAVEAUDIO
        play_key(6, true);
        #endif
        break;
    case 6:
        #ifdef REMOTE_HAVEAUDIO
        play_key(7, true);
        #endif
        break;
    case 7:
        #ifdef REMOTE_HAVEAUDIO
        play_key(9, true);      // yes, 9
        #endif
        break;
    }
}

#ifdef REMOTE_HAVEMQTT
static void mqtt_send_button_on(int i)
{
    if(!MQTTbuttonOnLen[i] || !FPBUnitIsOn)
        return;

    mqttPublish(settings.mqttbt[i], settings.mqttbo[i], MQTTbuttonOnLen[i] + 1);
}

static void mqtt_send_button_off(int i)
{
    if(!MQTTbuttonOffLen[i] || !FPBUnitIsOn)
        return;

    mqttPublish(settings.mqttbt[i], settings.mqttbf[i], MQTTbuttonOffLen[i] + 1);
}
#endif

#ifdef REMOTE_HAVEAUDIO
static void re_vol_reset()
{
    if(useRotEncVol) {
        #ifdef REMOTE_HAVEVOLKNOB
        rotEncVol->zeroPos((curSoftVol == 255) ? 0 : curSoftVol);
        #else
        rotEncVol->zeroPos(curSoftVol);
        #endif
    }
}
#endif

/*
 *  Do this whenever we are caught in a while() loop
 *  This allows other stuff to proceed
 */
static void myloop(bool withBTTFN)
{
    wifi_loop();
    audio_loop();
    if(withBTTFN) bttfn_loop_quick();
}

/*
 * MyDelay:
 * Calls myloop() periodically
 */
void mydelay(unsigned long mydel, bool withBTTFN)
{
    unsigned long startNow = millis();
    myloop(withBTTFN);
    while(millis() - startNow < mydel) {
        delay(10);
        myloop(withBTTFN);
    }
}

/*
 * Basic Telematics Transmission Framework (BTTFN)
 */

static void addCmdQueue(uint32_t command)
{
    if(!command) return;

    commandQueue[iCmdIdx] = command;
    iCmdIdx++;
    iCmdIdx &= 0x0f;
}

static void bttfn_setup()
{
    useBTTFN = false;

    // string empty? Disable BTTFN.
    if(!settings.tcdIP[0])
        return;

    haveTCDIP = isIp(settings.tcdIP);
    
    if(!haveTCDIP) {
        #ifdef BTTFN_MC
        tcdHostNameHash = 0;
        unsigned char *s = (unsigned char *)settings.tcdIP;
        for ( ; *s; ++s) tcdHostNameHash = 37 * tcdHostNameHash + tolower(*s);
        #else
        return;
        #endif
    } else {
        bttfnTcdIP.fromString(settings.tcdIP);
    }
    
    remUDP = &bttfUDP;
    remUDP->begin(BTTF_DEFAULT_LOCAL_PORT);

    #ifdef BTTFN_MC
    remMcUDP = &bttfMcUDP;
    remMcUDP->beginMulticast(bttfnMcIP, BTTF_DEFAULT_LOCAL_PORT + 2);
    #endif
    
    BTTFNfailCount = 0;
    useBTTFN = true;
}

void bttfn_loop()
{
    #ifdef BTTFN_MC
    int t = 10;
    #endif
    
    if(!useBTTFN)
        return;

    #ifdef BTTFN_MC
    while(bttfn_checkmc() && t--) {}
    #endif
        
    BTTFNCheckPacket();
    
    if(!BTTFNPacketDue) {
        // If WiFi status changed, trigger immediately
        if(!BTTFNWiFiUp && (WiFi.status() == WL_CONNECTED)) {
            BTTFNUpdateNow = 0;
        }
        if((!BTTFNUpdateNow) || (millis() - BTTFNUpdateNow > bttfnRemPollInt)) {
            BTTFNTriggerUpdate();
        }
    }
}

static void bttfn_loop_quick()
{
    #ifdef BTTFN_MC
    int t = 10;
    #endif
    
    if(!useBTTFN)
        return;

    #ifdef BTTFN_MC
    while(bttfn_checkmc() && t--) {}
    #endif
}

static bool check_packet(uint8_t *buf)
{
    // Basic validity check
    if(memcmp(BTTFUDPBuf, BTTFUDPHD, 4))
        return false;

    uint8_t a = 0;
    for(int i = 4; i < BTTF_PACKET_SIZE - 1; i++) {
        a += BTTFUDPBuf[i] ^ 0x55;
    }
    if(BTTFUDPBuf[BTTF_PACKET_SIZE - 1] != a)
        return false;

    return true;
}

static void handle_tcd_notification(uint8_t *buf)
{
    uint32_t seqCnt;

    switch(buf[5]) {
    case BTTFN_NOT_PREPARE:
        // Prepare for TT. Comes at some undefined point,
        // an undefined time before the actual tt, and
        // may not come at all.
        if(FPBUnitIsOn && !TTrunning) {
            prepareTT();
        }
        break;
    case BTTFN_NOT_TT:
        // Trigger Time Travel (if not running already)
        if(FPBUnitIsOn && !TTrunning) {
            networkTimeTravel = true;
            networkReentry = false;
            networkAbort = false;
            networkLead = buf[6] | (buf[7] << 8);
            networkP1   = buf[8] | (buf[9] << 8);
        }
        break;
    case BTTFN_NOT_REENTRY:
        // Start re-entry (if TT currently running)
        // Ignore command if TCD is connected by wire
        if(TTrunning) {
            networkReentry = true;
        }
        break;
    case BTTFN_NOT_ABORT_TT:
        // Abort TT (if TT currently running)
        // Ignore command if TCD is connected by wire
        if(TTrunning) {
            networkAbort = true;
        }
        tcdIsInP0 = 0;
        break;
    case BTTFN_NOT_ALARM:
        networkAlarm = true;
        // Eval this at our convenience
        break;
    case BTTFN_NOT_REM_CMD:
        addCmdQueue(GET32(buf, 6));
        break;
    case BTTFN_NOT_WAKEUP:
        if(FPBUnitIsOn && !TTrunning) {
            wakeup();
        }
        break;
    case BTTFN_NOT_REM_SPD:     // TCD fw < 10/26/2024
        seqCnt = GET32(buf, 12);
        if(seqCnt == 1 || seqCnt > bttfnTCDSeqCnt) {
            tcdIsInP0  = buf[8] | (buf[9] << 8);
            tcdSpeedP0 = buf[6] | (buf[7] << 8);
            #ifdef REMOTE_DBG
            Serial.printf("TCD sent REM_SPD: %d %d\n", tcdIsInP0, tcdSpeedP0);
            #endif
        } else {
            #ifdef REMOTE_DBG
            Serial.printf("Out-of-sequence packet received from TCD %d %d\n", seqCnt, bttfnTCDSeqCnt);
            #endif
        }
        bttfnTCDSeqCnt = seqCnt;
        break;
    case BTTFN_NOT_SPD:       // TCD fw >= 10/26/2024
        seqCnt = GET32(buf, 12);
        if(seqCnt == 1 || seqCnt > bttfnTCDSeqCnt) {
            int t = buf[8] | (buf[9] << 8);
            tcdCurrSpeed = buf[6] | (buf[7] << 8);
            if(tcdCurrSpeed > 88) tcdCurrSpeed = 88;
            switch(t) {
            case BTTFN_SSRC_P0:
                tcdSpeedP0 = (uint16_t)tcdCurrSpeed;
                tcdIsInP0 = FPBUnitIsOn ? 1 : 0;
                tcdSpdIsRotEnc = tcdSpdIsRemote = false;
                break;
            case BTTFN_SSRC_REM:
                tcdIsInP0 = 0;
                tcdSpdIsRotEnc = false;
                tcdSpdIsRemote = true;
                break;
            case BTTFN_SSRC_ROTENC:
                tcdIsInP0 = 0;
                tcdSpdIsRotEnc = true;
                tcdSpdIsRemote = false;
                break;
            default:
                tcdIsInP0 = 0;
                tcdSpdIsRotEnc = tcdSpdIsRemote = false;
            }
            #ifdef REMOTE_DBG
            Serial.printf("TCD sent NOT_SPD: %d src %d (IsP0:%d)\n", tcdCurrSpeed, t, tcdIsInP0);
            #endif
        } else {
            #ifdef REMOTE_DBG
            Serial.printf("Out-of-sequence packet received from TCD %d %d\n", seqCnt, bttfnTCDSeqCnt);
            #endif
        }
        bttfnTCDSeqCnt = seqCnt;
        break;
    }
}

#ifdef BTTFN_MC
static bool bttfn_checkmc()
{
    int psize = remMcUDP->parsePacket();

    if(!psize) {
        return false;
    }
    
    remMcUDP->read(BTTFMCBuf, BTTF_PACKET_SIZE);

    if(haveTCDIP) {
        if(bttfnTcdIP != remMcUDP->remoteIP())
            return false;
    } else {
        // Do not use tcdHostNameHash; let DISCOVER do its work
        // and wait for a result.
        return false;
    }

    if(!check_packet(BTTFMCBuf))
        return false;

    if((BTTFMCBuf[4] & 0x4f) == (BTTFN_VERSION | 0x40)) {

        // A notification from the TCD
        handle_tcd_notification(BTTFMCBuf);
    
    } else {
      
        return false;

    }

    return true;
}
#endif

// Check for pending packet and parse it
static void BTTFNCheckPacket()
{
    unsigned long mymillis = millis();
    
    int psize = remUDP->parsePacket();
    if(!psize) {
        if(BTTFNPacketDue) {
            if((mymillis - BTTFNTSRQAge) > 700) {
                // Packet timed out
                BTTFNPacketDue = false;
                // Immediately trigger new request for
                // the first 10 timeouts, after that
                // the new request is only triggered
                // in greater intervals via bttfn_loop().
                if(haveTCDIP && BTTFNfailCount < 10) {
                    BTTFNfailCount++;
                    BTTFNUpdateNow = 0;
                }
                bttfnCurrLatency = 0;
            }
        }
        return;
    }
    
    remUDP->read(BTTFUDPBuf, BTTF_PACKET_SIZE);

    if(!check_packet(BTTFUDPBuf))
        return;

    if((BTTFUDPBuf[4] & 0x4f) == (BTTFN_VERSION | 0x40)) {

        // A notification from the TCD
        handle_tcd_notification(BTTFUDPBuf);
        
    } else {

        // (Possibly) a response packet
    
        if(GET32(BTTFUDPBuf, 6) != BTTFUDPID)
            return;
    
        // Response marker missing or wrong version, bail
        if((BTTFUDPBuf[4] & 0x8f) != (BTTFN_VERSION | 0x80))
            return;

        bttfnCurrLatency = (mymillis - bttfnPacketSentNow) / 2;

        BTTFNfailCount = 0;
    
        // If it's our expected packet, no other is due for now
        BTTFNPacketDue = false;

        #ifdef BTTFN_MC
        if(BTTFUDPBuf[5] & 0x80) {
            if(!haveTCDIP) {
                bttfnTcdIP = remUDP->remoteIP();
                haveTCDIP = true;
                #ifdef REMOTE_DBG
                Serial.printf("Discovered TCD IP %d.%d.%d.%d\n", bttfnTcdIP[0], bttfnTcdIP[1], bttfnTcdIP[2], bttfnTcdIP[3]);
                #endif
            } else {
                #ifdef REMOTE_DBG
                Serial.println("Internal error - received unexpected DISCOVER response");
                #endif
            }
        }
        #endif

        if(BTTFUDPBuf[5] & 0x10) {
            remoteAllowed = (BTTFUDPBuf[26] & 0x04) ? true : false;
            tcdHasSpeedo  = (BTTFUDPBuf[26] & 0x08) ? true : false;
        } else {
            remoteAllowed = false;
        }

        if(BTTFUDPBuf[5] & 0x02) {
            tcdCurrSpeed = (int16_t)(BTTFUDPBuf[18] | (BTTFUDPBuf[19] << 8));
            if(tcdCurrSpeed > 88) tcdCurrSpeed = 88;
            tcdSpdIsRotEnc = (BTTFUDPBuf[26] & 0x80) ? true : false; 
            tcdSpdIsRemote = (BTTFUDPBuf[26] & 0x20) ? true : false;
        }

        if(BTTFUDPBuf[5] & 0x40) {
            bttfnReqStatus &= ~0x40;     // Do no longer poll capabilities
            #ifdef BTTFN_MC
            if(BTTFUDPBuf[31] & 0x01) {
                bttfnMcMarker = BTTFN_SUP_MC;
                bttfnReqStatus &= ~0x02; // Do no longer poll speed, comes over multicast
            }
            #endif
        }

        lastBTTFNpacket = mymillis;
    }
}

// Send a new data request
static bool BTTFNTriggerUpdate()
{
    BTTFNPacketDue = false;

    BTTFNUpdateNow = millis();

    if(WiFi.status() != WL_CONNECTED) {
        BTTFNWiFiUp = false;
        return false;
    }

    BTTFNWiFiUp = true;

    // Send new packet
    BTTFNSendPacket();
    BTTFNTSRQAge = millis();
    
    BTTFNPacketDue = true;
    
    return true;
}

static void BTTFNSendPacket()
{
    memset(BTTFUDPBuf, 0, BTTF_PACKET_SIZE);

    // ID
    memcpy(BTTFUDPBuf, BTTFUDPHD, 4);

    // Serial
    BTTFUDPID = (uint32_t)millis();
    SET32(BTTFUDPBuf, 6, BTTFUDPID);

    // Tell the TCD about our hostname (0-term., 13 bytes total)
    strncpy((char *)BTTFUDPBuf + 10, settings.hostName, 12);
    BTTFUDPBuf[10+12] = 0;

    BTTFUDPBuf[10+13] = BTTFN_TYPE_REMOTE;

    #ifdef BTTFN_MC
    BTTFUDPBuf[4] = BTTFN_VERSION | bttfnMcMarker; // Version, MC-marker
    #else
    BTTFUDPBuf[4] = BTTFN_VERSION;
    #endif
    BTTFUDPBuf[5] = bttfnReqStatus;        // Request status & TCD speed

    #ifdef BTTFN_MC
    if(!haveTCDIP) {
        BTTFUDPBuf[5] |= 0x80;
        SET32(BTTFUDPBuf, 31, tcdHostNameHash);
    }
    #endif

    SET32(BTTFUDPBuf, 35, myRemID);

    uint8_t a = 0;
    for(int i = 4; i < BTTF_PACKET_SIZE - 1; i++) {
        a += BTTFUDPBuf[i] ^ 0x55;
    }
    BTTFUDPBuf[BTTF_PACKET_SIZE - 1] = a;

    #ifdef BTTFN_MC
    if(haveTCDIP) {
    #endif
        remUDP->beginPacket(bttfnTcdIP, BTTF_DEFAULT_LOCAL_PORT);
    #ifdef BTTFN_MC
    } else {
        #ifdef REMOTE_DBG
        //Serial.printf("Sending multicast (hostname hash %x)\n", tcdHostNameHash);
        #endif
        remUDP->beginPacket(bttfnMcIP, BTTF_DEFAULT_LOCAL_PORT + 1);
    }
    #endif
    remUDP->write(BTTFUDPBuf, BTTF_PACKET_SIZE);
    remUDP->endPacket();

    bttfnPacketSentNow = millis();
}

static bool BTTFNTriggerTT(bool probe)
{
    if(!useBTTFN)
        return false;

    #ifdef BTTFN_MC
    if(!haveTCDIP)
        return false;
    #endif

    if(WiFi.status() != WL_CONNECTED)
        return false;

    if(!lastBTTFNpacket)
        return false;

    if(TTrunning)
        return false;

    if(probe)
        return true;

    memset(BTTFUDPBuf, 0, BTTF_PACKET_SIZE);

    // ID
    memcpy(BTTFUDPBuf, BTTFUDPHD, 4);

    // Tell the TCD about our hostname (0-term., 13 bytes total)
    strncpy((char *)BTTFUDPBuf + 10, settings.hostName, 12);
    BTTFUDPBuf[10+12] = 0;

    BTTFUDPBuf[10+13] = BTTFN_TYPE_REMOTE;

    #ifdef BTTFN_MC
    BTTFUDPBuf[4] = BTTFN_VERSION | bttfnMcMarker; // Version, MC-marker
    #else
    BTTFUDPBuf[4] = BTTFN_VERSION;
    #endif
    BTTFUDPBuf[5] = 0x80;                          // Trigger BTTFN-wide TT

    SET32(BTTFUDPBuf, 35, myRemID);

    uint8_t a = 0;
    for(int i = 4; i < BTTF_PACKET_SIZE - 1; i++) {
        a += BTTFUDPBuf[i] ^ 0x55;
    }
    BTTFUDPBuf[BTTF_PACKET_SIZE - 1] = a;
        
    remUDP->beginPacket(bttfnTcdIP, BTTF_DEFAULT_LOCAL_PORT);
    remUDP->write(BTTFUDPBuf, BTTF_PACKET_SIZE);
    remUDP->endPacket();

    #ifdef REMOTE_DBG
    Serial.println("Triggered BTTFN-wide TT");
    #endif

    return true;
}

static bool bttfn_send_command(uint8_t cmd, uint8_t p1, uint8_t p2)
{
    if(!useBTTFN)
        return false;

    #ifdef BTTFN_MC
    if(!haveTCDIP)
        return false;
    #endif

    if(WiFi.status() != WL_CONNECTED)
        return false;

    if(!lastBTTFNpacket)
        return false;

    if(!remoteAllowed)
        return false;

    memset(BTTFUDPBuf, 0, BTTF_PACKET_SIZE);

    // ID
    memcpy(BTTFUDPBuf, BTTFUDPHD, 4);

    // Tell the TCD about our hostname (0-term., 13 bytes total)
    strncpy((char *)BTTFUDPBuf + 10, settings.hostName, 12);
    BTTFUDPBuf[10+12] = 0;

    BTTFUDPBuf[10+13] = BTTFN_TYPE_REMOTE;

    BTTFUDPBuf[4] = BTTFN_VERSION | bttfnMcMarker;  // Version
    BTTFUDPBuf[5] = 0x00;

    SET32(BTTFUDPBuf, 6, bttfnSeqCnt[cmd]);
    bttfnSeqCnt[cmd]++;
    if(!bttfnSeqCnt[cmd]) bttfnSeqCnt[cmd]++;

    BTTFUDPBuf[25] = cmd;
    BTTFUDPBuf[26] = p1;
    BTTFUDPBuf[27] = p2;

    SET32(BTTFUDPBuf, 35, myRemID);
    
    uint8_t a = 0;
    for(int i = 4; i < BTTF_PACKET_SIZE - 1; i++) {
        a += BTTFUDPBuf[i] ^ 0x55;
    }
    BTTFUDPBuf[BTTF_PACKET_SIZE - 1] = a;
        
    remUDP->beginPacket(bttfnTcdIP, BTTF_DEFAULT_LOCAL_PORT);
    remUDP->write(BTTFUDPBuf, BTTF_PACKET_SIZE);
    remUDP->endPacket();

    #ifdef REMOTE_DBG
    Serial.printf("Sent command %d: %d %d\n", cmd, p1, p2);
    #endif

    lastCommandSent = millis();

    return true;
}

static void bttfn_remote_keepalive()
{
    if(!bttfn_send_command(BTTFN_REMCMD_PING, 0, 0)) {
        triggerCompleteUpdate = true;
    }
}

void bttfn_remote_unregister()
{
    if(!bttfn_send_command(BTTFN_REMCMD_BYE, 0, 0)) {
        triggerCompleteUpdate = true;
    }
}

static void bttfn_remote_send_combined(bool powerstate, bool brakestate, uint8_t speed)
{
    if(!triggerCompleteUpdate) {
        uint8_t p1 = 0;
        if(powerstate) p1 |= 0x01;
        if(brakestate) p1 |= 0x02;
        if(!bttfn_send_command(BTTFN_REMCMD_COMBINED, p1, speed)) {
            triggerCompleteUpdate = true;
        }
    }
}
