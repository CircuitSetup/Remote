/*
 * -------------------------------------------------------------------
 * Remote Control
 * (C) 2024-2025 Thomas Winischhofer (A10001986)
 * https://github.com/realA10001986/Remote
 * https://remote.out-a-ti.me
 *
 * Settings handling
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

#ifndef _REMOTE_SETTINGS_H
#define _REMOTE_SETTINGS_H

extern bool haveFS;
extern bool haveSD;
extern bool FlashROMode;

extern bool haveAudioFiles;

extern uint8_t musFolderNum;

#define MS(s) XMS(s)
#define XMS(s) #s

// Default settings

#define DEF_HOSTNAME        "dtmremote"
#define DEF_WIFI_RETRY      3     // 1-10; Default: 3 retries
#define DEF_WIFI_TIMEOUT    7     // 7-25; Default: 7 seconds
#define DEF_AP_CHANNEL      1     // 1-13; 0 = random(1-13)
#define DEF_WIFI_APOFFDELAY 0

#define DEF_COAST           0     // Engine braking / coasting
#define DEF_AT              0     // Auto-throttle: Default off
#define DEF_OO_TT           1     // O.O: 1 = trigger BTTFN-wide TT; 0 = musicplayer prev song
#define DEF_MOV_MD          1     // 1: (mostly) movie-accurate accel pace, 0: linear and faster
#define DEF_PLAY_CLK        1     // 1: Play accel-clicks, 0: Do not
#define DEF_PLAY_ALM_SND    0     // 1: Play TCD-alarm sound, 0: do not
#define DEF_DISP_GPS        0     // 1: Display TCD speed (GPS, RotEnc) when fake-off, 0: Do not
#define DEF_BRI             15    // Default display brightness

#define DEF_SHUFFLE         0     // Music Player: Do not shuffle by default

#define DEF_TCD_IP          ""    // TCD ip address or hostname for networked polling

#define DEF_CFG_ON_SD       1     // Save secondary settings on SD card. Default: Yes (1)
#define DEF_SD_FREQ         0     // SD/SPI frequency: Default 16MHz

#define DEF_DIS_BPACK       0     // 1: Disable ButtonPack (Buttons 1-8), 0: Use buttons 1-8 if detected
#define DEF_BPMAINT         0     // ButtonPack button is momentary (0) or maintained (1)
#define DEF_BPMTOO          0     // Maintained switch: Play audio on ON only (1), or on each flip (0)

#define DEF_USE_PLED        0     // 1: Use power LED, 0: do not, leave dark (like mostly in movie)
#define DEF_USE_LVLMTR      0     // 1: Use Batt. Level Meter, 0: do not, leave at zero (like in movie)
#define DEF_PLEDFP          1     // 1: Power LED/Meter on fake power, 0: on real power

#define DEF_USE_PWRMON      1     // 1: Use Power Monitor (if present), 0: do not
#define DEF_BAT_TYPE        0     // 0=3.7/4.2V
#define DEF_BAT_CAP         2000  // battery capacity per cell


struct Settings {
    char ssid[34]           = "";
    char pass[66]           = "";

    char hostName[32]       = DEF_HOSTNAME;
    char wifiConRetries[4]  = MS(DEF_WIFI_RETRY);
    char wifiConTimeout[4]  = MS(DEF_WIFI_TIMEOUT);
    char systemID[8]        = "";
    char appw[10]           = "";
    char apChnl[4]          = MS(DEF_AP_CHANNEL);
    char wifiAPOffDelay[4]  = MS(DEF_WIFI_APOFFDELAY);

    char coast[4]           = MS(DEF_COAST);
    char autoThrottle[4]    = MS(DEF_AT);
    char ooTT[4]            = MS(DEF_OO_TT);
    char movieMode[4]       = MS(DEF_MOV_MD);       // saved, but overruled by vis config file
    char playClick[4]       = MS(DEF_PLAY_CLK);
    char playALsnd[4]       = MS(DEF_PLAY_ALM_SND);
    char dgps[4]            = MS(DEF_DISP_GPS);     // saved, but overruled by vis config file

    char shuffle[4]         = MS(DEF_SHUFFLE);

    char tcdIP[32]          = DEF_TCD_IP;

#ifdef REMOTE_HAVEMQTT  
    char useMQTT[4]         = "0";
    char mqttServer[80]     = "";  // ip or domain [:port]  
    char mqttUser[128]      = "";  // user[:pass] (UTF8)
    char mqttbt[8][256]     = { 0 };  // buttons topics (UTF8)
    char mqttbo[8][128]     = { 0 };  // buttons on message (UTF8)
    char mqttbf[8][128]     = { 0 };  // buttons off message (UTF8)
#endif

    char CfgOnSD[4]         = MS(DEF_CFG_ON_SD);
    char sdFreq[4]          = MS(DEF_SD_FREQ);

#ifdef ALLOW_DIS_UB
    char disBPack[4]        = MS(DEF_DIS_BPACK);
#endif    
    char bPb0Maint[4]       = MS(DEF_BPMAINT);
    char bPb1Maint[4]       = MS(DEF_BPMAINT);
    char bPb2Maint[4]       = MS(DEF_BPMAINT);
    char bPb3Maint[4]       = MS(DEF_BPMAINT);
    char bPb4Maint[4]       = MS(DEF_BPMAINT);
    char bPb5Maint[4]       = MS(DEF_BPMAINT);
    char bPb6Maint[4]       = MS(DEF_BPMAINT);
    char bPb7Maint[4]       = MS(DEF_BPMAINT);

    char bPb0MtO[4]         = MS(DEF_BPMTOO);
    char bPb1MtO[4]         = MS(DEF_BPMTOO);
    char bPb2MtO[4]         = MS(DEF_BPMTOO);
    char bPb3MtO[4]         = MS(DEF_BPMTOO);
    char bPb4MtO[4]         = MS(DEF_BPMTOO);
    char bPb5MtO[4]         = MS(DEF_BPMTOO);
    char bPb6MtO[4]         = MS(DEF_BPMTOO);
    char bPb7MtO[4]         = MS(DEF_BPMTOO);

    char usePwrLED[4]       = MS(DEF_USE_PLED);
    char useLvlMtr[4]       = MS(DEF_USE_LVLMTR);
    char pwrLEDonFP[4]      = MS(DEF_PLEDFP);
#ifdef HAVE_PM
    char usePwrMon[4]       = MS(DEF_USE_PWRMON);
    char batType[4]         = MS(DEF_BAT_TYPE);
    char batCap[8]          = MS(DEF_BAT_CAP);
#endif    
   
    char Vol[6];
    char musicFolder[6];
    char Bri[6];
};

struct IPSettings {
    char ip[20]       = "";
    char gateway[20]  = "";
    char netmask[20]  = "";
    char dns[20]      = "";
};

extern struct Settings settings;
extern struct IPSettings ipsettings;

void settings_setup();

void unmount_fs();

void write_settings();
bool checkConfigExists();

bool loadCalib();
void saveCalib();

bool loadVis();
void saveVis(bool useCache = true);

bool loadBrightness();
void saveBrightness(bool useCache = true);

bool loadCurVolume();
void saveCurVolume(bool useCache = true);

bool loadMusFoldNum();
void saveMusFoldNum();

bool loadVis();
void saveVis(bool useCache);

bool loadIpSettings();
void writeIpSettings();
void deleteIpSettings();

void copySettings();

bool check_if_default_audio_present();
bool prepareCopyAudioFiles();
void doCopyAudioFiles();

bool check_allow_CPA();
void delete_ID_file();

#define MAX_SIM_UPLOADS 16
#define UPL_OPENERR 1
#define UPL_NOSDERR 2
#define UPL_WRERR   3
#define UPL_BADERR  4
#define UPL_MEMERR  5
#define UPL_UNKNOWN 6
#define UPL_DPLBIN  7
#include <FS.h>
bool   openUploadFile(String& fn, File& file, int idx, bool haveAC, int& opType, int& errNo);
size_t writeACFile(File& file, uint8_t *buf, size_t len);
void   closeACFile(File& file);
void   removeACFile(int idx);
void   renameUploadFile(int idx);
char   *getUploadFileName(int idx);
int    getUploadFileNameLen(int idx);
void   freeUploadFileNames();

#endif
