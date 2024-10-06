/*
 * -------------------------------------------------------------------
 * Remote Control
 * (C) 2024 Thomas Winischhofer (A10001986)
 * https://github.com/realA10001986/Remote
 * https://remote.out-a-ti.me
 *
 * WiFi and Config Portal handling
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

#ifdef REMOTE_MDNS
#include <ESPmDNS.h>
#endif

#include "src/WiFiManager/WiFiManager.h"

#include "display.h"
#include "remote_audio.h"
#include "remote_settings.h"
#include "remote_wifi.h"
#include "remote_main.h"
#ifdef REMOTE_HAVEMQTT
#include "mqtt.h"
#endif

// If undefined, use the checkbox/dropdown-hacks.
// If defined, go back to standard text boxes
//#define TC_NOCHECKBOXES

#define STRLEN(x) (sizeof(x)-1)

Settings settings;

IPSettings ipsettings;

WiFiManager wm;
bool wifiSetupDone = false;

#ifdef REMOTE_HAVEMQTT
WiFiClient mqttWClient;
PubSubClient mqttClient(mqttWClient);
#endif

static const char R_updateacdone[] = "/uac";

static const char acul_part1[]  = "<!DOCTYPE html><html lang='en'><head><meta name='format-detection' content='telephone=no'><meta charset='UTF-8'><meta  name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'/><title>";
static const char acul_part2[]  = "</title><style>.c,body{text-align:center;font-family:verdana}div{padding:5px;font-size:1em;margin:5px 0;box-sizing:border-box}.msg{border-radius:.3rem;width: 100%}.wrap {text-align:left;display:inline-block;min-width:260px;max-width:500px}.msg{padding:20px;margin:20px 0;border:1px solid #eee;border-left-width:5px;border-left-color:#777}.msg h4{margin-top:0;margin-bottom:5px}.msg.P{border-left-color:#1fa3ec}.msg.P h4{color:#1fa3ec}.msg.D{border-left-color:#dc3630}.msg.D h4{color:#dc3630}.msg.S{border-left-color: #5cb85c}.msg.S h4{color: #5cb85c}dt{font-weight:bold}dd{margin:0;padding:0 0 0.5em 0;min-height:12px}td{vertical-align: top;}</style>";
static const char acul_part3[]  = "</head><body class='{c}'>";
static const char acul_part4[]  = "<div class='wrap'><h1>";
static const char acul_part5[]  = "</h1><h3>";
static const char acul_part6[]  = "</h3><div class='msg";
static const char acul_part7[]  = " S'><strong>Upload successful.</strong><br>Installation will proceed...";
static const char acul_part71[] = " D'><strong>Upload failed.</strong><br>";
static const char *acul_errs[]  = { "Can't open file on SD", "No SD card found", "Write error", "Aborted", "Bad file" };
static const char acul_part8[]  = "</div></div></body></html>";

static const char *aco = "autocomplete='off'";

#ifdef REMOTE_HAVEAUDIO
WiFiManagerParameter custom_aood("<div class='msg P'>Please <a href='/update'>install/update</a> audio data</div>");
#endif

#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_coast("cst", "Coasting when throttle in neutral (0=no, 1=yes)", settings.coast, 1, "autocomplete='off' title='Enable to enable coasting then trottle is in neutral position'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_coast("cst", "Coasting when throttle in neutral", settings.coast, 1, "autocomplete='off' title='Check to enable coasting then trottle is in neutral position' type='checkbox' style='margin-top:5px;margin-bottom:10px;'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------

WiFiManagerParameter custom_Bri("Bri", "<br>Brightness level (0-15)", settings.Bri, 2, "type='number' min='0' max='15' autocomplete='off'", WFM_LABEL_BEFORE);

#ifdef REMOTE_HAVEAUDIO
#ifdef REMOTE_HAVEVOLKNOB
#ifdef DG_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_FixV("FixV", "Disable volume knob (0=no, 1=yes)", settings.FixV, 1, "autocomplete='off' title='Enable this if the audio volume should be set by software; if disabled, the volume knob is used' style='margin-top:5px;'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_FixV("FixV", "Disable volume knob", settings.FixV, 1, "autocomplete='off' title='Check this if the audio volume should be set by software; if unchecked, the volume knob is used' type='checkbox' class='mt5'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
WiFiManagerParameter custom_Vol("Vol", "<br>Volume level (0-19)", settings.Vol, 2, "type='number' min='0' max='19' autocomplete='off'", WFM_LABEL_BEFORE);
#else
WiFiManagerParameter custom_Vol("Vol", "Volume level (0-19)", settings.Vol, 2, "type='number' min='0' max='19' autocomplete='off'", WFM_LABEL_BEFORE);
#endif
#endif

#if defined(REMOTE_MDNS) || defined(REMOTE_WM_HAS_MDNS)
#define HNTEXT "Hostname<br><span style='font-size:80%'>The Config Portal is accessible at http://<i>hostname</i>.local<br>(Valid characters: a-z/0-9/-)</span>"
#else
#define HNTEXT "Hostname<br><span style='font-size:80%'>(Valid characters: a-z/0-9/-)</span>"
#endif
WiFiManagerParameter custom_hostName("hostname", HNTEXT, settings.hostName, 31, "pattern='[A-Za-z0-9\\-]+' placeholder='Example: dtmremote'");
WiFiManagerParameter custom_sysID("sysID", "AP Mode: Network name appendix<br><span style='font-size:80%'>Will be appended to \"REM-AP\" to create a unique name if multiple remotes in range. [a-z/0-9/-]</span>", settings.systemID, 7, "pattern='[A-Za-z0-9\\-]+'");
WiFiManagerParameter custom_appw("appw", "AP Mode: WiFi password<br><span style='font-size:80%'>Password to protect REM-AP. Empty or 8 characters [a-z/0-9/-]<br><b>Write this down, you might lock yourself out!</b></span>", settings.appw, 8, "minlength='8' pattern='[A-Za-z0-9\\-]+'");
WiFiManagerParameter custom_wifiConRetries("wifiret", "WiFi connection attempts (1-10)", settings.wifiConRetries, 2, "type='number' min='1' max='10' autocomplete='off'", WFM_LABEL_BEFORE);
WiFiManagerParameter custom_wifiConTimeout("wificon", "WiFi connection timeout (7-25[seconds])", settings.wifiConTimeout, 2, "type='number' min='7' max='25'");

#ifdef BTTFN_MC
WiFiManagerParameter custom_tcdIP("tcdIP", "IP address or hostname of TCD", settings.tcdIP, 63, "pattern='(^((25[0-5]|(2[0-4]|1\\d|[1-9]|)\\d)\\.?\\b){4}$)|([A-Za-z0-9\\-]+)' placeholder='Example: timecircuits'");
#else
WiFiManagerParameter custom_tcdIP("tcdIP", "IP address of TCD", settings.tcdIP, 63, "pattern='^((25[0-5]|(2[0-4]|1\\d|[1-9]|)\\d)\\.?\\b){4}$' placeholder='Example: 192.168.4.1'");
#endif

#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_sStrict("sStrict", "Movie-mode acceleration (0=no, 1=yes)<br><span style='font-size:80%'>Check to set the acceleration pace to what is shown in the movie. This slows down acceleration at higher speeds.</span>", settings.movieMode, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_sStrict("sStrict", "Movie-mode acceleration<br><span style='font-size:80%'>Check to set the acceleration pace to what is shown in the movie. This slows down acceleration at higher speeds.</span>", settings.movieMode, 1, "autocomplete='off' type='checkbox' class='mt5'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#ifdef REMOTE_HAVEAUDIO
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_playclick("plyCLK", "Play acceleration 'click' sound (0=no, 1=yes)", settings.playClick, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_playclick("plyCLK", "Play acceleration 'click' sound", settings.playClick, 1, "autocomplete='off' type='checkbox'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_playALSnd("plyALS", "Play TCD-alarm sound (0=no, 1=yes)", settings.playALsnd, 1, "autocomplete='off' title='Enable to have the device play a sound then the TCD alarm sounds.'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_playALSnd("plyALS", "Play TCD-alarm sound", settings.playALsnd, 1, "autocomplete='off' title='Check to have the device play a sound then the TCD alarm sounds.' type='checkbox'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#endif // HAVEAUDIO
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_dGPS("dGPS", "Display TCD speed when off (0=no, 1=yes)", settings.movieMode, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_dGPS("dGPS", "Display TCD speed when off", settings.movieMode, 1, "autocomplete='off' type='checkbox'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------

#ifdef REMOTE_HAVEMQTT
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_useMQTT("uMQTT", "Use Home Assistant (0=no, 1=yes)", settings.useMQTT, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_useMQTT("uMQTT", "Use Home Assistant (MQTT 3.1.1)", settings.useMQTT, 1, "type='checkbox' class='mt5'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
WiFiManagerParameter custom_mqttServer("ha_server", "<br>Broker IP[:port] or domain[:port]", settings.mqttServer, 79, "pattern='[a-zA-Z0-9\\.:\\-]+' placeholder='Example: 192.168.1.5'");
WiFiManagerParameter custom_mqttUser("ha_usr", "User[:Password]", settings.mqttUser, 63, "placeholder='Example: ronald:mySecret' class='mb15'");
WiFiManagerParameter custom_mqttb1t("ha_b1t", "Button 1 topic", settings.mqttbt[0], 127, "placeholder='Example: home/lights/1/'");
WiFiManagerParameter custom_mqttb1o("ha_b1o", "Button 1 message on ON", settings.mqttbo[0], 63, "placeholder='Example: ON'");
WiFiManagerParameter custom_mqttb1f("ha_b1f", "Button 1 message on OFF", settings.mqttbf[0], 63, "placeholder='Example: OFF' class='mb15'");
WiFiManagerParameter custom_mqttb2t("ha_b2t", "Button 2 topic", settings.mqttbt[1],127);
WiFiManagerParameter custom_mqttb2o("ha_b2o", "Button 2 message on ON", settings.mqttbo[1], 63);
WiFiManagerParameter custom_mqttb2f("ha_b2f", "Button 2 message on OFF", settings.mqttbf[1], 63, "class='mb15'");
WiFiManagerParameter custom_mqttb3t("ha_b3t", "Button 3 topic", settings.mqttbt[2],127);
WiFiManagerParameter custom_mqttb3o("ha_b3o", "Button 3 message on ON", settings.mqttbo[2], 63);
WiFiManagerParameter custom_mqttb3f("ha_b3f", "Button 3 message on OFF", settings.mqttbf[2], 63, "class='mb15'");
WiFiManagerParameter custom_mqttb4t("ha_b4t", "Button 4 topic", settings.mqttbt[3], 127);
WiFiManagerParameter custom_mqttb4o("ha_b4o", "Button 4 message on ON", settings.mqttbo[3], 63);
WiFiManagerParameter custom_mqttb4f("ha_b4f", "Button 4 message on OFF", settings.mqttbf[3], 63, "class='mb15'");
WiFiManagerParameter custom_mqttb5t("ha_b5t", "Button 5 topic", settings.mqttbt[4], 127);
WiFiManagerParameter custom_mqttb5o("ha_b5o", "Button 5 message on ON", settings.mqttbo[4], 63);
WiFiManagerParameter custom_mqttb5f("ha_b5f", "Button 5 message on OFF", settings.mqttbf[4], 63, "class='mb15'");
WiFiManagerParameter custom_mqttb6t("ha_b6t", "Button 6 topic", settings.mqttbt[5], 127);
WiFiManagerParameter custom_mqttb6o("ha_b6o", "Button 6 message on ON", settings.mqttbo[5], 63);
WiFiManagerParameter custom_mqttb6f("ha_b6f", "Button 6 message on OFF", settings.mqttbf[5], 63, "class='mb15'");
WiFiManagerParameter custom_mqttb7t("ha_b7t", "Button 7 topic", settings.mqttbt[6], 127);
WiFiManagerParameter custom_mqttb7o("ha_b7o", "Button 7 message on ON", settings.mqttbo[6], 63);
WiFiManagerParameter custom_mqttb7f("ha_b7f", "Button 7 message on OFF", settings.mqttbf[6], 63, "class='mb15'");
WiFiManagerParameter custom_mqttb8t("ha_b8t", "Button 8 topic", settings.mqttbt[7], 127);
WiFiManagerParameter custom_mqttb8o("ha_b8o", "Button 8 message on ON", settings.mqttbo[7], 63);
WiFiManagerParameter custom_mqttb8f("ha_b9f", "Button 8 message on OFF", settings.mqttbf[7], 63);
#endif // HAVEMQTT

#ifdef REMOTE_HAVEAUDIO
WiFiManagerParameter custom_musicFolder("mfol", "Music folder (0-9)", settings.musicFolder, 2, "type='number' min='0' max='9'");
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_shuffle("musShu", "Shuffle mode enabled at startup (0=no, 1=yes)", settings.shuffle, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_shuffle("musShu", "Shuffle mode enabled at startup", settings.shuffle, 1, "type='checkbox' style='margin-top:8px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#endif

#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_CfgOnSD("CfgOnSD", "Save secondary settings on SD (0=no, 1=yes)<br><span style='font-size:80%'>Enable this to avoid flash wear</span>", settings.CfgOnSD, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_CfgOnSD("CfgOnSD", "Save secondary settings on SD<br><span style='font-size:80%'>Check this to avoid flash wear</span>", settings.CfgOnSD, 1, "autocomplete='off' type='checkbox' class='mt5'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
//#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
//WiFiManagerParameter custom_sdFrq("sdFrq", "SD clock speed (0=16Mhz, 1=4Mhz)<br><span style='font-size:80%'>Slower access might help in case of problems with SD cards</span>", settings.sdFreq, 1, "autocomplete='off'");
//#else // -------------------- Checkbox hack: --------------
//WiFiManagerParameter custom_sdFrq("sdFrq", "4MHz SD clock speed<br><span style='font-size:80%'>Checking this might help in case of SD card problems</span>", settings.sdFreq, 1, "autocomplete='off' type='checkbox' style='margin-top:12px'", WFM_LABEL_AFTER);
//#endif // -------------------------------------------------

#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_dBP("dBP", "Disable User Buttons (0=no, 1=yes)", settings.disBPack, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_dBP("dBP", "Disable User Buttons", settings.disBPack, 1, "autocomplete='off' type='checkbox' class='mt5'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_b0mt("b0mt", "Button 1 is maintained (0=no, 1=yes)", settings.bPb0Maint, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_b0mt("b0mt", "Button 1 is maintained switch", settings.bPb0Maint, 1, "autocomplete='off' type='checkbox'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_b1mt("b1mt", "Button 2 is maintained (0=no, 1=yes)", settings.bPb1Maint, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_b1mt("b1mt", "Button 2 is maintained switch", settings.bPb1Maint, 1, "autocomplete='off' type='checkbox'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_b2mt("b2mt", "Button 3 is maintained (0=no, 1=yes)", settings.bPb2Maint, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_b2mt("b2mt", "Button 3 is maintained switch", settings.bPb2Maint, 1, "autocomplete='off' type='checkbox'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_b3mt("b3mt", "Button 4 is maintained (0=no, 1=yes)", settings.bPb3Maint, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_b3mt("b3mt", "Button 4 is maintained switch", settings.bPb3Maint, 1, "autocomplete='off' type='checkbox'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_b4mt("b4mt", "Button 5 is maintained (0=no, 1=yes)", settings.bPb4Maint, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_b4mt("b4mt", "Button 5 is maintained switch", settings.bPb4Maint, 1, "autocomplete='off' type='checkbox'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_b5mt("b5mt", "Button 6 is maintained (0=no, 1=yes)", settings.bPb5Maint, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_b5mt("b5mt", "Button 6 is maintained switch", settings.bPb5Maint, 1, "autocomplete='off' type='checkbox'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_b6mt("b6mt", "Button 7 is maintained (0=no, 1=yes)", settings.bPb6Maint, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_b6mt("b6mt", "Button 7 is maintained switch", settings.bPb6Maint, 1, "autocomplete='off' type='checkbox'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_b7mt("b7mt", "Button 8 is maintained (0=no, 1=yes)", settings.bPb7Maint, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_b7mt("b7mt", "Button 8 is maintained switch", settings.bPb7Maint, 1, "autocomplete='off' type='checkbox'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------

#ifdef REMOTE_HAVEAUDIO
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_b0mtoo("b0mto", "Play audio on ON only (0=no, 1=yes)", settings.bPb0MtO, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_b0mtoo("b0mto", "Maintained: Play audio on ON only", settings.bPb0MtO, 1, "autocomplete='off' title='Check to play audio when switch is in ON position only. If unchecked, audio is played on each flip.' type='checkbox' class='mt5' style='margin-left:20px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_b1mtoo("b1mto", "Play audio on ON only (0=no, 1=yes)", settings.bPb1MtO, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_b1mtoo("b1mto", "Maintained: Play audio on ON only", settings.bPb1MtO, 1, "autocomplete='off' title='Check to play audio when switch is in ON position only. If unchecked, audio is played on each flip.' type='checkbox' class='mt5' style='margin-left:20px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_b2mtoo("b2mto", "Play audio on ON only (0=no, 1=yes)", settings.bPb2MtO, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_b2mtoo("b2mto", "Maintained: Play audio on ON only", settings.bPb2MtO, 1, "autocomplete='off' title='Check to play audio when switch is in ON position only. If unchecked, audio is played on each flip.' type='checkbox' class='mt5' style='margin-left:20px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_b3mtoo("b3mto", "Play audio on ON only (0=no, 1=yes)", settings.bPb3MtO, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_b3mtoo("b3mto", "Maintained: Play audio on ON only", settings.bPb3MtO, 1, "autocomplete='off' title='Check to play audio when switch is in ON position only. If unchecked, audio is played on each flip.' type='checkbox' class='mt5' style='margin-left:20px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_b4mtoo("b4mto", "Play audio on ON only (0=no, 1=yes)", settings.bPb4MtO, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_b4mtoo("b4mto", "Maintained: Play audio on ON only", settings.bPb4MtO, 1, "autocomplete='off' title='Check to play audio when switch is in ON position only. If unchecked, audio is played on each flip.' type='checkbox' class='mt5' style='margin-left:20px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_b5mtoo("b5mto", "Play audio on ON only (0=no, 1=yes)", settings.bPb5MtO, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_b5mtoo("b5mto", "Maintained: Play audio on ON only", settings.bPb5MtO, 1, "autocomplete='off' title='Check to play audio when switch is in ON position only. If unchecked, audio is played on each flip.' type='checkbox' class='mt5' style='margin-left:20px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_b6mtoo("b6mto", "Play audio on ON only (0=no, 1=yes)", settings.bPb6MtO, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_b6mtoo("b6mto", "Maintained: Play audio on ON only", settings.bPb6MtO, 1, "autocomplete='off' title='Check to play audio when switch is in ON position only. If unchecked, audio is played on each flip.' type='checkbox' class='mt5' style='margin-left:20px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_b7mtoo("b7mto", "Play audio on ON only (0=no, 1=yes)", settings.bPb7MtO, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_b7mtoo("b7mto", "Maintained: Play audio on ON only", settings.bPb7MtO, 1, "autocomplete='off' title='Check to play audio when switch is in ON position only. If unchecked, audio is played on each flip.' type='checkbox' class='mt5' style='margin-left:20px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#endif

WiFiManagerParameter custom_sectstart_head("<div class='sects'>");
WiFiManagerParameter custom_sectstart("</div><div class='sects'>");
WiFiManagerParameter custom_sectend("</div>");

WiFiManagerParameter custom_sectstart_nw("</div><div class='sects'><div class='headl'>Wireless communication (BTTF-Network)</div>");

#ifdef REMOTE_HAVEAUDIO
WiFiManagerParameter custom_sectstart_mp("</div><div class='sects'><div class='headl'>MusicPlayer</div>");
#endif

WiFiManagerParameter custom_sectstart_hw("</div><div class='sects'><div class='headl'>Buttons 1-8 setup</div>");

WiFiManagerParameter custom_sectend_foot("</div><p></p>");

#define TC_MENUSIZE 6
static const char* wifiMenu[TC_MENUSIZE] = {"wifi", "param", "sep", "update", "sep", "custom" };

static const char apName[]  = "REM-AP";
static const char myTitle[] = "DTM Remote";
static const char myHead[]  = "<link rel='shortcut icon' type='image/png' href=' data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAMAAAAoLQ9TAAAAGXRFWHRTb2Z0d2FyZQBBZG9iZSBJbWFnZVJlYWR5ccllPAAAABVQTFRFSUpKzMvHtr+9AAAA2uXj8iU99tM09Eq9GAAAAEFJREFUeNp8z8sKACAIRFFf+f+f3CzMcIguuDkokSglKqOGiB8YmpA5wbsDVhUsBmw4DHM3JsjjhIGfpc9RW4ABAKA0AUfA4/H/AAAAAElFTkSuQmCC'><script>function wlp(){return window.location.pathname;}function getn(x){return document.getElementsByTagName(x)}function ge(x){return document.getElementById(x)}function c(l){ge('s').value=l.getAttribute('data-ssid')||l.innerText||l.textContent;p=l.nextElementSibling.classList.contains('l');ge('p').disabled=!p;if(p){ge('p').placeholder='';ge('p').focus();}}uacs1=\"(function(el){document.getElementById('uacb').style.display = el.value=='' ? 'none' : 'initial';})(this)\";uacstr=\"Upload audio data (REMA.bin)<br><form method='POST' action='uac' enctype='multipart/form-data' onchange=\\\"\"+uacs1+\"\\\"><input type='file' name='upac' accept='.bin,application/octet-stream'><button id='uacb' type='submit' class='h D'>Upload</button></form>\";window.onload=function(){xx=false;document.title='DTM Remote';if(ge('s')&&ge('dns')){xx=true;xxx=document.title;yyy='Configure WiFi';aa=ge('s').parentElement;bb=aa.innerHTML;dd=bb.search('<hr>');ee=bb.search('<button');cc='<div class=\"sects\">'+bb.substring(0,dd)+'</div><div class=\"sects\">'+bb.substring(dd+4,ee)+'</div>'+bb.substring(ee);aa.innerHTML=cc;document.querySelectorAll('a[href=\"#p\"]').forEach((userItem)=>{userItem.onclick=function(){c(this);return false;}});if(aa=ge('s')){aa.oninput=function(){if(this.placeholder.length>0&&this.value.length==0){ge('p').placeholder='********';}}}}if(ge('uploadbin')){aa=document.getElementsByClassName('wrap');if(aa.length>0){aa[0].insertAdjacentHTML('beforeend',uacstr);}}if(ge('uploadbin')||wlp()=='/u'||wlp()=='/uac'||wlp()=='/wifisave'||wlp()=='/paramsave'){xx=true;xxx=document.title;yyy=(wlp()=='/wifisave')?'Configure WiFi':(wlp()=='/paramsave'?'Setup':'Firmware update');aa=document.getElementsByClassName('wrap');if(aa.length>0){if((bb=ge('uploadbin'))){aa[0].style.textAlign='center';bb.parentElement.onsubmit=function(){aa=ge('uploadbin');if(aa){aa.disabled=true;aa.innerHTML='Please wait'}aa=ge('uacb');if(aa){aa.disabled=true}};if((bb=ge('uacb'))){aa[0].style.textAlign='center';bb.parentElement.onsubmit=function(){aa=ge('uacb');if(aa){aa.disabled=true;aa.innerHTML='Please wait'}aa=ge('uploadbin');if(aa){aa.disabled=true}}}}aa=getn('H3');if(aa.length>0){aa[0].remove()}aa=getn('H1');if(aa.length>0){aa[0].remove()}}}if(ge('ttrp')||window.location.pathname=='/param'){xx=true;xxx=document.title;yyy='Setup';}if(ge('ebnew')){xx=true;bb=getn('H3');aa=getn('H1');xxx=aa[0].innerHTML;yyy=bb[0].innerHTML;ff=aa[0].parentNode;ff.style.position='relative';}if(xx){zz=(Math.random()>0.8);dd=document.createElement('div');dd.classList.add('tpm0');dd.innerHTML='<div class=\"tpm\" onClick=\"window.location=\\'/\\'\"><div class=\"tpm2\"><img src=\"data:image/png;base64,'+(zz?'iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAMAAACdt4HsAAAAGXRFWHRTb2Z0d2FyZQBBZG9iZSBJbWFnZVJlYWR5ccllPAAAAAZQTFRFSp1tAAAA635cugAAAAJ0Uk5T/wDltzBKAAAAbUlEQVR42tzXwRGAQAwDMdF/09QQQ24MLkDj77oeTiPA1wFGQiHATOgDGAp1AFOhDWAslAHMhS6AQKgCSIQmgEgoAsiEHoBQqAFIhRaAWCgByIVXAMuAdcA6YBlwALAKePzgd71QAByP71uAAQC+xwvdcFg7UwAAAABJRU5ErkJggg==':'iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAMAAACdt4HsAAAAGXRFWHRTb2Z0d2FyZQBBZG9iZSBJbWFnZVJlYWR5ccllPAAAAAZQTFRFSp1tAAAA635cugAAAAJ0Uk5T/wDltzBKAAAAgElEQVR42tzXQQqDABAEwcr/P50P2BBUdMhee6j7+lw8i4BCD8MiQAjHYRAghAh7ADWMMAcQww5jADHMsAYQwwxrADHMsAYQwwxrADHMsAYQwwxrgLgOPwKeAjgrrACcFkYAzgu3AN4C3AV4D3AP4E3AHcDF+8d/YQB4/Pn+CjAAMaIIJuYVQ04AAAAASUVORK5CYII=')+'\" class=\"tpm3\"></div><H1 class=\"tpmh1\"'+(zz?' style=\"margin-left:1.2em\"':'')+'>'+xxx+'</H1>'+'<H3 class=\"tpmh3\"'+(zz?' style=\"padding-left:4.5em\"':'')+'>'+yyy+'</div></div>';}if(ge('ebnew')){bb[0].remove();aa[0].replaceWith(dd);}if((ge('s')&&ge('dns'))||ge('uploadbin')||wlp()=='/u'||wlp()=='/uac'||wlp()=='/wifisave'||wlp()=='/paramsave'||ge('ttrp')||wlp()=='/param'){aa=document.getElementsByClassName('wrap');if(aa.length>0){aa[0].insertBefore(dd,aa[0].firstChild);aa[0].style.position='relative';}}}</script><style type='text/css'>body{font-family:-apple-system,BlinkMacSystemFont,system-ui,'Segoe UI',Roboto,'Helvetica Neue',Verdana,Helvetica}H1,H2{margin-top:0px;margin-bottom:0px;text-align:center;}H3{margin-top:0px;margin-bottom:5px;text-align:center;}div.msg{border:1px solid #ccc;border-left-width:15px;border-radius:20px;background:linear-gradient(320deg,rgb(255,255,255) 0%,rgb(235,234,233) 100%);}button{transition-delay:250ms;margin-top:10px;margin-bottom:10px;color:#fff;background-color:#225a98;font-variant-caps:all-small-caps;}button.DD{color:#000;border:4px ridge #999;border-radius:2px;background:#e0c942;background-image:url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAMAAABEpIrGAAAAGXRFWHRTb2Z0d2FyZQBBZG9iZSBJbWFnZVJlYWR5ccllPAAAADBQTFRF////AAAAMyks8+AAuJYi3NHJo5aQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAbP19EwAAAAh0Uk5T/////////wDeg71ZAAAA4ElEQVR42qSTyxLDIAhF7yChS/7/bwtoFLRNF2UmRr0H8IF4/TBsY6JnQFvTJ8D0ncChb0QGlDvA+hkw/yC4xED2Z2L35xwDRSdqLZpFIOU3gM2ox6mA3tnDPa8UZf02v3q6gKRH/Eyg6JZBqRUCRW++yFYIvCjNFIt9OSC4hol/ItH1FkKRQgAbi0ty9f/F7LM6FimQacPbAdG5zZVlWdfvg+oEpl0Y+jzqIJZ++6fLqlmmnq7biZ4o67lgjBhA0kvJyTww/VK0hJr/LHvBru8PR7Dpx9MT0f8e72lvAQYALlAX+Kfw0REAAAAASUVORK5CYII=');background-repeat:no-repeat;background-origin:content-box;background-size:contain;}br{display:block;font-size:1px;content:''}input[type='checkbox']{display:inline-block;margin-top:10px}input{border:thin inset}small{display:none}em > small{display:inline}form{margin-block-end:0;}.tpm{cursor:pointer;border:1px solid black;border-radius:5px;padding:0 0 0 0px;min-width:18em;}.tpm2{position:absolute;top:-0.7em;z-index:130;left:0.7em;}.tpm3{width:4em;height:4em;}.tpmh1{font-variant-caps:all-small-caps;font-weight:normal;margin-left:2em;overflow:clip}.tpmh3{background:#000;font-size:0.6em;color:#ffa;padding-left:7em;margin-left:0.5em;margin-right:0.5em;border-radius:5px}.sects{background-color:#eee;border-radius:7px;margin-bottom:20px;padding-bottom:7px;padding-top:7px}.tpm0{position:relative;width:20em;margin:0 auto 0 auto;}.headl{margin:0 0 5px 0;padding:0}.cmp0{margin:0;padding:0;}.sel0{font-size:90%;width:auto;margin-left:10px;vertical-align:baseline;}.mb15{margin-bottom:15px!important}.mt5{margin-top:5px!important}</style>";
static const char *myCustMenu = "<form action='/erase' method='get' onsubmit='return confirm(\"This erases the WiFi config and reboots. The device will restart in access point mode. Are you sure?\");'><button id='ebnew' class='DD'>Erase WiFi Config</button></form><br/><img style='display:block;margin:10px auto 10px auto;' src='data:image/png;base64,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'><div style='font-size:10px;margin-left:auto;margin-right:auto;text-align:center;'>Version " REMOTE_VERSION " (" REMOTE_VERSION_EXTRA ")<br>Powered by <a href='https://remote.out-a-ti.me'>A10001986  [Documentation]</a></div>";

static int  shouldSaveConfig = 0;
static bool shouldSaveIPConfig = false;
static bool shouldDeleteIPConfig = false;

// WiFi power management in AP mode
bool          wifiInAPMode = false;
bool          wifiAPIsOff = false;
unsigned long wifiAPModeNow;
unsigned long wifiAPOffDelay = 0;     // default: never

// WiFi power management in STA mode
bool          wifiIsOff = false;
unsigned long wifiOnNow = 0;
unsigned long wifiOffDelay     = 0;   // default: never
unsigned long origWiFiOffDelay = 0;

static bool   wifiHaveSTAConf = false;

#ifdef REMOTE_HAVEAUDIO
static File acFile;
static bool haveACFile = false;
static int  ACULerr = 0;
#endif

#ifdef REMOTE_HAVEMQTT
#define       MQTT_SHORT_INT  (30*1000)
#define       MQTT_LONG_INT   (5*60*1000)
bool          useMQTT = false;
char          mqttUser[64] = { 0 };
char          mqttPass[64] = { 0 };
char          mqttServer[80] = { 0 };
uint16_t      mqttPort = 1883;
bool          pubMQTT = false;
static unsigned long mqttReconnectNow = 0;
static unsigned long mqttReconnectInt = MQTT_SHORT_INT;
static uint16_t      mqttReconnFails = 0;
static bool          mqttSubAttempted = false;
static bool          mqttOldState = true;
static bool          mqttDoPing = true;
static bool          mqttRestartPing = false;
static bool          mqttPingDone = false;
static unsigned long mqttPingNow = 0;
static unsigned long mqttPingInt = MQTT_SHORT_INT;
static uint16_t      mqttPingsExpired = 0;
#endif

static void wifiConnect(bool deferConfigPortal = false);
static void saveParamsCallback();
static void saveConfigCallback();
static void preUpdateCallback();
static void preSaveConfigCallback();
static void waitConnectCallback();

static void setupStaticIP();
static void ipToString(char *str, IPAddress ip);
static IPAddress stringToIp(char *str);

static void getParam(String name, char *destBuf, size_t length);
static bool myisspace(char mychar);
static char* strcpytrim(char* destination, const char* source, bool doFilter = false);
static void mystrcpy(char *sv, WiFiManagerParameter *el);
#ifndef TC_NOCHECKBOXES
static void strcpyCB(char *sv, WiFiManagerParameter *el);
static void setCBVal(WiFiManagerParameter *el, char *sv);
#endif

#ifdef REMOTE_HAVEAUDIO
static void setupWebServerCallback();
static void handleUploadDone();
static void handleUploading();
static void handleUploadDone();
#endif

#ifdef REMOTE_HAVEMQTT
static void strcpyutf8(char *dst, const char *src, unsigned int len);
static void mqttPing();
static bool mqttReconnect(bool force = false);
static void mqttLooper();
static void mqttCallback(char *topic, byte *payload, unsigned int length);
static void mqttSubscribe();
#endif

/*
 * wifi_setup()
 *
 */
void wifi_setup()
{
    int temp, wifiretry;

    // Explicitly set mode, esp allegedly defaults to STA_AP
    WiFi.mode(WIFI_MODE_STA);

    #ifndef REMOTE_DBG
    wm.setDebugOutput(false);
    #endif

    wm.setParamsPage(true);
    wm.setBreakAfterConfig(true);
    wm.setConfigPortalBlocking(false);
    wm.setPreSaveConfigCallback(preSaveConfigCallback);
    wm.setSaveConfigCallback(saveConfigCallback);
    wm.setSaveParamsCallback(saveParamsCallback);
    wm.setPreOtaUpdateCallback(preUpdateCallback);
    #ifdef REMOTE_HAVEAUDIO
    wm.setWebServerCallback(setupWebServerCallback);
    #endif
    wm.setHostname(settings.hostName);
    wm.setCaptivePortalEnable(false);
    
    // Our style-overrides, the page title
    wm.setCustomHeadElement(myHead);
    wm.setTitle(myTitle);
    wm.setDarkMode(false);

    // Hack version number into WiFiManager main page
    wm.setCustomMenuHTML(myCustMenu);

    // Static IP info is not saved by WiFiManager,
    // have to do this "manually". Hence ipsettings.
    wm.setShowStaticFields(true);
    wm.setShowDnsFields(true);

    temp = atoi(settings.wifiConTimeout);
    if(temp < 7) temp = 7;
    if(temp > 25) temp = 25;
    wm.setConnectTimeout(temp);

    wifiretry = atoi(settings.wifiConRetries);
    if(wifiretry < 1) wifiretry = 1;
    if(wifiretry > 10) wifiretry = 10;
    wm.setConnectRetries(wifiretry);

    wm.setCleanConnect(true);
    //wm.setRemoveDuplicateAPs(false);

    #ifdef WIFIMANAGER_2_0_17
    wm._preloadwifiscan = false;
    wm._asyncScan = true;
    #endif

    wm.setMenu(wifiMenu, TC_MENUSIZE);

    #ifdef REMOTE_HAVEAUDIO
    if(!haveAudioFiles) {
        wm.addParameter(&custom_aood);
    }
    #endif

    wm.addParameter(&custom_sectstart_head);// 3
    wm.addParameter(&custom_coast);
    wm.addParameter(&custom_Bri);

    #ifdef REMOTE_HAVEAUDIO
    wm.addParameter(&custom_sectstart);     // 2 (3)
    #ifdef REMOTE_HAVEVOLKNOB
    wm.addParameter(&custom_FixV);
    #endif
    wm.addParameter(&custom_Vol);
    #endif

    wm.addParameter(&custom_sectstart);     // 6
    wm.addParameter(&custom_hostName);
    wm.addParameter(&custom_sysID);
    wm.addParameter(&custom_appw);
    wm.addParameter(&custom_wifiConRetries);
    wm.addParameter(&custom_wifiConTimeout);

    wm.addParameter(&custom_sectstart_nw);  // 2
    wm.addParameter(&custom_tcdIP);

    wm.addParameter(&custom_sectstart);     // 5 (3)
    wm.addParameter(&custom_sStrict);
    #ifdef REMOTE_HAVEAUDIO
    wm.addParameter(&custom_playclick);
    wm.addParameter(&custom_playALSnd);
    #endif
    wm.addParameter(&custom_dGPS);

    #ifdef REMOTE_HAVEMQTT
    wm.addParameter(&custom_sectstart);     // 28
    wm.addParameter(&custom_useMQTT);
    wm.addParameter(&custom_mqttServer);
    wm.addParameter(&custom_mqttUser);
    wm.addParameter(&custom_mqttb1t);
    wm.addParameter(&custom_mqttb1o);
    wm.addParameter(&custom_mqttb1f);
    wm.addParameter(&custom_mqttb2t);
    wm.addParameter(&custom_mqttb2o);
    wm.addParameter(&custom_mqttb2f);
    wm.addParameter(&custom_mqttb3t);
    wm.addParameter(&custom_mqttb3o);
    wm.addParameter(&custom_mqttb3f);
    wm.addParameter(&custom_mqttb4t);
    wm.addParameter(&custom_mqttb4o);
    wm.addParameter(&custom_mqttb4f);
    wm.addParameter(&custom_mqttb5t);
    wm.addParameter(&custom_mqttb5o);
    wm.addParameter(&custom_mqttb5f);
    wm.addParameter(&custom_mqttb6t);
    wm.addParameter(&custom_mqttb6o);
    wm.addParameter(&custom_mqttb6f);
    wm.addParameter(&custom_mqttb7t);
    wm.addParameter(&custom_mqttb7o);
    wm.addParameter(&custom_mqttb7f);
    wm.addParameter(&custom_mqttb8t);
    wm.addParameter(&custom_mqttb8o);
    wm.addParameter(&custom_mqttb8f);
    #endif

    #ifdef REMOTE_HAVEAUDIO
    wm.addParameter(&custom_sectstart_mp);  // 3
    wm.addParameter(&custom_musicFolder);
    wm.addParameter(&custom_shuffle);
    #endif
    
    wm.addParameter(&custom_sectstart);     // 2 (3)
    wm.addParameter(&custom_CfgOnSD);
    //wm.addParameter(&custom_sdFrq);

    wm.addParameter(&custom_sectstart_hw);  // 10
    wm.addParameter(&custom_dBP);
    wm.addParameter(&custom_b0mt);
    #ifdef REMOTE_HAVEAUDIO
    wm.addParameter(&custom_b0mtoo);
    #endif
    wm.addParameter(&custom_b1mt);
    #ifdef REMOTE_HAVEAUDIO
    wm.addParameter(&custom_b1mtoo);
    #endif
    wm.addParameter(&custom_b2mt);
    #ifdef REMOTE_HAVEAUDIO
    wm.addParameter(&custom_b2mtoo);
    #endif
    wm.addParameter(&custom_b3mt);
    #ifdef REMOTE_HAVEAUDIO
    wm.addParameter(&custom_b3mtoo);
    #endif
    wm.addParameter(&custom_b4mt);
    #ifdef REMOTE_HAVEAUDIO
    wm.addParameter(&custom_b4mtoo);
    #endif
    wm.addParameter(&custom_b5mt);
    #ifdef REMOTE_HAVEAUDIO
    wm.addParameter(&custom_b5mtoo);
    #endif
    wm.addParameter(&custom_b6mt);
    #ifdef REMOTE_HAVEAUDIO
    wm.addParameter(&custom_b6mtoo);
    #endif
    wm.addParameter(&custom_b7mt);
    #ifdef REMOTE_HAVEAUDIO
    wm.addParameter(&custom_b7mtoo);
    #endif
   
    wm.addParameter(&custom_sectend_foot);  // 1

    updateConfigPortalValues();

    #ifdef REMOTE_MDNS
    if(MDNS.begin(settings.hostName)) {
        MDNS.addService("http", "tcp", 80);
    }
    #endif

    // No WiFi powersave features here
    wifiOffDelay = 0;
    wifiAPOffDelay = 0;
    
    // Configure static IP
    if(loadIpSettings()) {
        setupStaticIP();
    }

    // Find out if we have a configured WiFi network to connect to.
    // If we detect "TCD-AP" as the SSID, we make sure that we retry
    // at least 2 times so we have a chance to catch the TCD's AP if 
    // both are powered up at the same time.
    {
        wifi_config_t conf;
        esp_wifi_get_config(WIFI_IF_STA, &conf);
        wifiHaveSTAConf = (conf.sta.ssid[0] != 0);
        #ifdef REMOTE_DBG
        Serial.printf("WiFi network configured: %s (%s)\n", wifiHaveSTAConf ? "YES" : "NO", 
                    wifiHaveSTAConf ? (const char *)conf.sta.ssid : "n/a");
        #endif
        if(wifiHaveSTAConf) {
            if(!strncmp("TCD-AP", (const char *)conf.sta.ssid, 6)) {
                if(wifiretry < 2) {
                    wm.setConnectRetries(2);
                }
            }
        } else {
            // No point in retry when we have no WiFi config'd
            wm.setConnectRetries(1);
        }
    }
    
    wifi_setup2();
}

void wifi_setup2()
{
    // Connect, but defer starting the CP
    wifiConnect(true);

#ifdef REMOTE_HAVEMQTT
    useMQTT = (atoi(settings.useMQTT) > 0);
    
    if((!settings.mqttServer[0]) || // No server -> no MQTT
       (wifiInAPMode))              // WiFi in AP mode -> no MQTT
        useMQTT = false;  
    
    if(useMQTT) {

        bool mqttRes = false;
        char *t;
        int tt;

        // No WiFi power save if we're using MQTT
        origWiFiOffDelay = wifiOffDelay = 0;

        if((t = strchr(settings.mqttServer, ':'))) {
            strncpy(mqttServer, settings.mqttServer, t - settings.mqttServer);
            mqttServer[t - settings.mqttServer + 1] = 0;
            tt = atoi(t+1);
            if(tt > 0 && tt <= 65535) {
                mqttPort = tt;
            }
        } else {
            strcpy(mqttServer, settings.mqttServer);
        }

        if(isIp(mqttServer)) {
            mqttClient.setServer(stringToIp(mqttServer), mqttPort);
        } else {
            IPAddress remote_addr;
            if(WiFi.hostByName(mqttServer, remote_addr)) {
                mqttClient.setServer(remote_addr, mqttPort);
            } else {
                mqttClient.setServer(mqttServer, mqttPort);
                // Disable PING if we can't resolve domain
                mqttDoPing = false;
                Serial.printf("MQTT: Failed to resolve '%s'\n", mqttServer);
            }
        }
        
        mqttClient.setCallback(mqttCallback);
        mqttClient.setLooper(mqttLooper);

        if(settings.mqttUser[0] != 0) {
            if((t = strchr(settings.mqttUser, ':'))) {
                strncpy(mqttUser, settings.mqttUser, t - settings.mqttUser);
                mqttUser[t - settings.mqttUser + 1] = 0;
                strcpy(mqttPass, t + 1);
            } else {
                strcpy(mqttUser, settings.mqttUser);
            }
        }

        #ifdef REMOTE_DBG
        Serial.printf("MQTT: server '%s' port %d user '%s' pass '%s'\n", mqttServer, mqttPort, mqttUser, mqttPass);
        #endif
            
        mqttReconnect(true);
        // Rest done in loop
            
    } else {

        #ifdef REMOTE_DBG
        Serial.println("MQTT: Disabled");
        #endif

    }
#endif

    // Start the Config Portal. A WiFiScan does not
    // disturb anything at this point hopefully.
    if(WiFi.status() == WL_CONNECTED) {
        wifiStartCP();
    }

    wifiSetupDone = true;
}

/*
 * wifi_loop()
 *
 */
void wifi_loop()
{
    char oldCfgOnSD = 0;

#ifdef REMOTE_HAVEMQTT
    if(useMQTT) {
        if(mqttClient.state() != MQTT_CONNECTING) {
            if(!mqttClient.connected()) {
                if(mqttOldState || mqttRestartPing) {
                    // Disconnection first detected:
                    mqttPingDone = mqttDoPing ? false : true;
                    mqttPingNow = mqttRestartPing ? millis() : 0;
                    mqttOldState = false;
                    mqttRestartPing = false;
                    mqttSubAttempted = false;
                }
                if(mqttDoPing && !mqttPingDone) {
                    audio_loop();
                    mqttPing();
                    audio_loop();
                }
                if(mqttPingDone) {
                    audio_loop();
                    mqttReconnect();
                    audio_loop();
                }
            } else {
                // Only call Subscribe() if connected
                mqttSubscribe();
                mqttOldState = true;
            }
        }
        mqttClient.loop();
    }
#endif    
    
    wm.process();
    
    if(shouldSaveIPConfig) {

        #ifdef REMOTE_DBG
        Serial.println(F("WiFi: Saving IP config"));
        #endif

        writeIpSettings();

        shouldSaveIPConfig = false;

    } else if(shouldDeleteIPConfig) {

        #ifdef REMOTE_DBG
        Serial.println(F("WiFi: Deleting IP config"));
        #endif

        deleteIpSettings();

        shouldDeleteIPConfig = false;

    }

    if(shouldSaveConfig) {

        // Save settings and restart esp32

        #ifdef REMOTE_DBG
        Serial.println(F("Config Portal: Saving config"));
        #endif

        // Only read parms if the user actually clicked SAVE on the params page
        if(shouldSaveConfig > 1) {

            int temp;

            // Save "strict" setting
            #ifdef TC_NOCHECKBOXES // --------- Plain text boxes:
            mystrcpy(settings.movieMode, &custom_sStrict);
            #else
            strcpyCB(settings.movieMode, &custom_sStrict);
            #endif
            if(settings.movieMode[0] == '1') {
                movieMode = true;
            } else if(settings.movieMode[0] == '0') {
                movieMode = false;
            }
            // Save "display TCD speed" setting
            #ifdef TC_NOCHECKBOXES // --------- Plain text boxes:
            mystrcpy(settings.dgps, &custom_dGPS);
            #else
            strcpyCB(settings.dgps, &custom_dGPS);
            #endif
            if(settings.dgps[0] == '1') {
                displayGPSMode = true;
            } else if(settings.dgps[0] == '0') {
                displayGPSMode = false;
            }
            updateVisMode();
            saveVis();

            // Save volume setting
            #ifdef REMOTE_HAVEAUDIO
            #ifdef REMOTE_HAVEVOLKNOB
            #ifdef DG_NOCHECKBOXES // --------- Plain text boxes:
            mystrcpy(settings.FixV, &custom_FixV);
            #else
            strcpyCB(settings.FixV, &custom_FixV);
            #endif
            #endif

            mystrcpy(settings.Vol, &custom_Vol);
            
            #ifdef REMOTE_HAVEVOLKNOB
            if(settings.FixV[0] == '0') {
                if(curSoftVol != 255) {
                    curSoftVol = 255;
                    saveCurVolume();
                }
            } else if(settings.FixV[0] == '1') {
            #endif  // HAVEVOLKNOB
                if(strlen(settings.Vol) > 0) {
                    temp = atoi(settings.Vol);
                    if(temp >= 0 && temp <= 19) {
                        curSoftVol = temp;
                        saveCurVolume();
                    }
                }
            #ifdef REMOTE_HAVEVOLKNOB
            }
            #endif

            // Save music folder number
            if(haveSD) {
                mystrcpy(settings.musicFolder, &custom_musicFolder);
                if(strlen(settings.musicFolder) > 0) {
                    temp = atoi(settings.musicFolder);
                    if(temp >= 0 && temp <= 9) {
                        musFolderNum = temp;
                        saveMusFoldNum();
                    }
                }
            }
            #endif
            
            // Save brightness setting
            mystrcpy(settings.Bri, &custom_Bri);
            if(strlen(settings.Bri) > 0) {
                temp = atoi(settings.Bri);
                if(temp >= 0 && temp <= 15) {
                    remdisplay.setBrightness(temp);
                    saveBrightness();
                }
            }
                       
            strcpytrim(settings.hostName, custom_hostName.getValue(), true);
            if(strlen(settings.hostName) == 0) {
                strcpy(settings.hostName, DEF_HOSTNAME);
            } else {
                char *s = settings.hostName;
                for ( ; *s; ++s) *s = tolower(*s);
            }
            strcpytrim(settings.systemID, custom_sysID.getValue(), true);
            strcpytrim(settings.appw, custom_appw.getValue(), true);
            if((temp = strlen(settings.appw)) > 0) {
                if(temp < 8) {
                    settings.appw[0] = 0;
                }
            }
            mystrcpy(settings.wifiConRetries, &custom_wifiConRetries);
            mystrcpy(settings.wifiConTimeout, &custom_wifiConTimeout);

            strcpytrim(settings.tcdIP, custom_tcdIP.getValue());
            if(strlen(settings.tcdIP) > 0) {
                char *s = settings.tcdIP;
                for ( ; *s; ++s) *s = tolower(*s);
            }

            #ifdef REMOTE_HAVEMQTT
            strcpytrim(settings.mqttServer, custom_mqttServer.getValue());
            strcpyutf8(settings.mqttUser, custom_mqttUser.getValue(), sizeof(settings.mqttUser));

            strcpyutf8(settings.mqttbt[0], custom_mqttb1t.getValue(), sizeof(settings.mqttbt[0]));
            strcpyutf8(settings.mqttbo[0], custom_mqttb1o.getValue(), sizeof(settings.mqttbo[0]));
            strcpyutf8(settings.mqttbf[0], custom_mqttb1f.getValue(), sizeof(settings.mqttbf[0]));
            strcpyutf8(settings.mqttbt[1], custom_mqttb2t.getValue(), sizeof(settings.mqttbt[0]));
            strcpyutf8(settings.mqttbo[1], custom_mqttb2o.getValue(), sizeof(settings.mqttbo[0]));
            strcpyutf8(settings.mqttbf[1], custom_mqttb2f.getValue(), sizeof(settings.mqttbf[0]));
            strcpyutf8(settings.mqttbt[2], custom_mqttb3t.getValue(), sizeof(settings.mqttbt[0]));
            strcpyutf8(settings.mqttbo[2], custom_mqttb3o.getValue(), sizeof(settings.mqttbo[0]));
            strcpyutf8(settings.mqttbf[2], custom_mqttb3f.getValue(), sizeof(settings.mqttbf[0]));
            strcpyutf8(settings.mqttbt[3], custom_mqttb4t.getValue(), sizeof(settings.mqttbt[0]));
            strcpyutf8(settings.mqttbo[3], custom_mqttb4o.getValue(), sizeof(settings.mqttbo[0]));
            strcpyutf8(settings.mqttbf[3], custom_mqttb4f.getValue(), sizeof(settings.mqttbf[0]));
            strcpyutf8(settings.mqttbt[4], custom_mqttb5t.getValue(), sizeof(settings.mqttbt[0]));
            strcpyutf8(settings.mqttbo[4], custom_mqttb5o.getValue(), sizeof(settings.mqttbo[0]));
            strcpyutf8(settings.mqttbf[4], custom_mqttb5f.getValue(), sizeof(settings.mqttbf[0]));
            strcpyutf8(settings.mqttbt[5], custom_mqttb6t.getValue(), sizeof(settings.mqttbt[0]));
            strcpyutf8(settings.mqttbo[5], custom_mqttb6o.getValue(), sizeof(settings.mqttbo[0]));
            strcpyutf8(settings.mqttbf[5], custom_mqttb6f.getValue(), sizeof(settings.mqttbf[0]));
            strcpyutf8(settings.mqttbt[6], custom_mqttb7t.getValue(), sizeof(settings.mqttbt[0]));
            strcpyutf8(settings.mqttbo[6], custom_mqttb7o.getValue(), sizeof(settings.mqttbo[0]));
            strcpyutf8(settings.mqttbf[6], custom_mqttb7f.getValue(), sizeof(settings.mqttbf[0]));
            strcpyutf8(settings.mqttbt[7], custom_mqttb8t.getValue(), sizeof(settings.mqttbt[0]));
            strcpyutf8(settings.mqttbo[7], custom_mqttb8o.getValue(), sizeof(settings.mqttbo[0]));
            strcpyutf8(settings.mqttbf[7], custom_mqttb8f.getValue(), sizeof(settings.mqttbf[0]));
            #endif
            
            #ifdef TC_NOCHECKBOXES // --------- Plain text boxes:

            mystrcpy(settings.coast, &custom_coast);

            #ifdef REMOTE_HAVEAUDIO
            mystrcpy(settings.playClick, &custom_playclick);
            mystrcpy(settings.playALsnd, &custom_playALSnd);
            #endif

            #ifdef REMOTE_HAVEMQTT
            mystrcpy(settings.useMQTT, &custom_useMQTT);
            #endif

            #ifdef REMOTE_HAVEAUDIO
            mystrcpy(settings.shuffle, &custom_shuffle);
            #endif

            oldCfgOnSD = settings.CfgOnSD[0];
            mystrcpy(settings.CfgOnSD, &custom_CfgOnSD);
            //mystrcpy(settings.sdFreq, &custom_sdFrq);

            mystrcpy(settings.disBPack, &custom_dBP);

            mystrcpy(settings.bPb0Maint, &custom_b0mt);
            mystrcpy(settings.bPb1Maint, &custom_b1mt);
            mystrcpy(settings.bPb2Maint, &custom_b2mt);
            mystrcpy(settings.bPb3Maint, &custom_b3mt);
            mystrcpy(settings.bPb4Maint, &custom_b4mt);
            mystrcpy(settings.bPb5Maint, &custom_b5mt);
            mystrcpy(settings.bPb6Maint, &custom_b6mt);
            mystrcpy(settings.bPb7Maint, &custom_b7mt);

            #ifdef REMOTE_HAVEAUDIO
            mystrcpy(settings.bPb0MtO, &custom_b0mtoo);
            mystrcpy(settings.bPb1MtO, &custom_b1mtoo);
            mystrcpy(settings.bPb2MtO, &custom_b2mtoo);
            mystrcpy(settings.bPb3MtO, &custom_b3mtoo);
            mystrcpy(settings.bPb4MtO, &custom_b4mtoo);
            mystrcpy(settings.bPb5MtO, &custom_b5mtoo);
            mystrcpy(settings.bPb6MtO, &custom_b6mtoo);
            mystrcpy(settings.bPb7MtO, &custom_b7mtoo);
            #endif
            
            #else // -------------------------- Checkboxes:

            strcpyCB(settings.coast, &custom_coast);
            
            #ifdef REMOTE_HAVEAUDIO
            strcpyCB(settings.playClick, &custom_playclick);
            strcpyCB(settings.playALsnd, &custom_playALSnd);
            #endif

            #ifdef REMOTE_HAVEMQTT
            strcpyCB(settings.useMQTT, &custom_useMQTT);
            #endif

            #ifdef REMOTE_HAVEAUDIO
            strcpyCB(settings.shuffle, &custom_shuffle);
            #endif
            
            oldCfgOnSD = settings.CfgOnSD[0];
            strcpyCB(settings.CfgOnSD, &custom_CfgOnSD);
            //strcpyCB(settings.sdFreq, &custom_sdFrq);

            strcpyCB(settings.disBPack, &custom_dBP);

            strcpyCB(settings.bPb0Maint, &custom_b0mt);
            strcpyCB(settings.bPb1Maint, &custom_b1mt);
            strcpyCB(settings.bPb2Maint, &custom_b2mt);
            strcpyCB(settings.bPb3Maint, &custom_b3mt);
            strcpyCB(settings.bPb4Maint, &custom_b4mt);
            strcpyCB(settings.bPb5Maint, &custom_b5mt);
            strcpyCB(settings.bPb6Maint, &custom_b6mt);
            strcpyCB(settings.bPb7Maint, &custom_b7mt);

            #ifdef REMOTE_HAVEAUDIO
            strcpyCB(settings.bPb0MtO, &custom_b0mtoo);
            strcpyCB(settings.bPb1MtO, &custom_b1mtoo);
            strcpyCB(settings.bPb2MtO, &custom_b2mtoo);
            strcpyCB(settings.bPb3MtO, &custom_b3mtoo);
            strcpyCB(settings.bPb4MtO, &custom_b4mtoo);
            strcpyCB(settings.bPb5MtO, &custom_b5mtoo);
            strcpyCB(settings.bPb6MtO, &custom_b6mtoo);
            strcpyCB(settings.bPb7MtO, &custom_b7mtoo);
            #endif

            #endif  // -------------------------

            // Copy secondary settings to other medium if
            // user changed respective option
            if(oldCfgOnSD != settings.CfgOnSD[0]) {
                copySettings();
            }

        }

        stopAudio();

        // Write settings if requested, or no settings file exists
        if(shouldSaveConfig > 1 || !checkConfigExists()) {
            write_settings();
        }
        
        shouldSaveConfig = 0;

        // Reset esp32 to load new settings
        remdisplay.off();
        remledStop.setState(false);

        // Unregister from TCD
        bttfn_remote_unregister();

        // Flush outstanding saves
        flushDelayedSave();

        unmount_fs();

        #ifdef REMOTE_DBG
        Serial.println(F("Config Portal: Restarting ESP...."));
        #endif

        Serial.flush();

        delay(500);

        esp_restart();
    }

    // WiFi power management
    // If a delay > 0 is configured, WiFi is powered-down after timer has
    // run out. The timer starts when the device is powered-up/boots.
    // There are separate delays for AP mode and STA mode.
    // WiFi can be re-enabled for the configured time by holding '7'
    // on the keypad.
    // NTP requests will re-enable WiFi (in STA mode) for a short
    // while automatically.
    if(wifiInAPMode) {
        // Disable WiFi in AP mode after a configurable delay (if > 0)
        if(wifiAPOffDelay > 0) {
            if(!wifiAPIsOff && (millis() - wifiAPModeNow >= wifiAPOffDelay)) {
                wifiOff();
                wifiAPIsOff = true;
                wifiIsOff = false;
                #ifdef REMOTE_DBG
                Serial.println(F("WiFi (AP-mode) is off. Hold '7' to re-enable."));
                #endif
            }
        }
    } else {
        // Disable WiFi in STA mode after a configurable delay (if > 0)
        if(origWiFiOffDelay > 0) {
            if(!wifiIsOff && (millis() - wifiOnNow >= wifiOffDelay)) {
                wifiOff();
                wifiIsOff = true;
                wifiAPIsOff = false;
                #ifdef REMOTE_DBG
                Serial.println(F("WiFi (STA-mode) is off. Hold '7' to re-enable."));
                #endif
            }
        }
    }

}

static void wifiConnect(bool deferConfigPortal)
{
    char realAPName[16];

    strcpy(realAPName, apName);
    if(settings.systemID[0]) {
        strcat(realAPName, settings.systemID);
    }
    
    // Automatically connect using saved credentials if they exist
    // If connection fails it starts an access point with the specified name
    if(wm.autoConnect(realAPName, settings.appw)) {
        #ifdef REMOTE_DBG
        Serial.println(F("WiFi connected"));
        #endif

        // Since WM 2.0.13beta, starting the CP invokes an async
        // WiFi scan. This interferes with network access for a 
        // few seconds after connecting. So, during boot, we start
        // the CP later, to allow a quick NTP update.
        if(!deferConfigPortal) {
            wm.startWebPortal();
        }

        // Allow modem sleep:
        // WIFI_PS_MIN_MODEM is the default, and activated when calling this
        // with "true". When this is enabled, received WiFi data can be
        // delayed for as long as the DTIM period.
        // Since it is the default setting, so no need to call it here.
        //WiFi.setSleep(true);

        // Disable modem sleep, don't want delays accessing the CP or
        // with BTTFN/MQTT.
        WiFi.setSleep(false);

        // Set transmit power to max; we might be connecting as STA after
        // a previous period in AP mode.
        #ifdef REMOTE_DBG
        {
            wifi_power_t power = WiFi.getTxPower();
            Serial.printf("WiFi: Max TX power in STA mode %d\n", power);
        }
        #endif
        WiFi.setTxPower(WIFI_POWER_19_5dBm);

        wifiInAPMode = false;
        wifiIsOff = false;
        wifiOnNow = millis();
        wifiAPIsOff = false;  // Sic! Allows checks like if(wifiAPIsOff || wifiIsOff)

    } else {
        #ifdef REMOTE_DBG
        Serial.println(F("Config portal running in AP-mode"));
        #endif

        {
            #ifdef REMOTE_DBG
            int8_t power;
            esp_wifi_get_max_tx_power(&power);
            Serial.printf("WiFi: Max TX power %d\n", power);
            #endif

            // Try to avoid "burning" the ESP when the WiFi mode
            // is "AP" and the speed/vol knob is fully up by reducing
            // the max. transmit power.
            // The choices are:
            // WIFI_POWER_19_5dBm    = 19.5dBm
            // WIFI_POWER_19dBm      = 19dBm
            // WIFI_POWER_18_5dBm    = 18.5dBm
            // WIFI_POWER_17dBm      = 17dBm
            // WIFI_POWER_15dBm      = 15dBm
            // WIFI_POWER_13dBm      = 13dBm
            // WIFI_POWER_11dBm      = 11dBm
            // WIFI_POWER_8_5dBm     = 8.5dBm
            // WIFI_POWER_7dBm       = 7dBm     <-- proven to avoid the issues
            // WIFI_POWER_5dBm       = 5dBm
            // WIFI_POWER_2dBm       = 2dBm
            // WIFI_POWER_MINUS_1dBm = -1dBm
            WiFi.setTxPower(WIFI_POWER_7dBm);

            #ifdef REMOTE_DBG
            esp_wifi_get_max_tx_power(&power);
            Serial.printf("WiFi: Max TX power set to %d\n", power);
            #endif
        }

        wifiInAPMode = true;
        wifiAPIsOff = false;
        wifiAPModeNow = millis();
        wifiIsOff = false;    // Sic!
    }
}

void wifiOff(bool force)
{
    if(!force) {
        if( (!wifiInAPMode && wifiIsOff) ||
            (wifiInAPMode && wifiAPIsOff) ) {
            return;
        }
    }

    wm.stopWebPortal();
    wm.disconnect();
    WiFi.mode(WIFI_OFF);
}

void wifiOn(unsigned long newDelay, bool alsoInAPMode, bool deferCP)
{
    unsigned long desiredDelay;
    unsigned long Now = millis();

    if(wifiInAPMode && !alsoInAPMode) return;

    if(wifiInAPMode) {
        if(wifiAPOffDelay == 0) return;   // If no delay set, auto-off is disabled
        wifiAPModeNow = Now;              // Otherwise: Restart timer
        if(!wifiAPIsOff) return;
    } else {
        if(origWiFiOffDelay == 0) return; // If no delay set, auto-off is disabled
        desiredDelay = (newDelay > 0) ? newDelay : origWiFiOffDelay;
        if((Now - wifiOnNow >= wifiOffDelay) ||                    // If delay has run out, or
           (wifiOffDelay - (Now - wifiOnNow))  < desiredDelay) {   // new delay exceeds remaining delay:
            wifiOffDelay = desiredDelay;                           // Set new timer delay, and
            wifiOnNow = Now;                                       // restart timer
            #ifdef REMOTE_DBG
            Serial.printf("Restarting WiFi-off timer; delay %d\n", wifiOffDelay);
            #endif
        }
        if(!wifiIsOff) {
            // If WiFi is not off, check if user wanted
            // to start the CP, and do so, if not running
            if(!deferCP) {
                if(!wm.getWebPortalActive()) {
                    wm.startWebPortal();
                }
            }
            return;
        }
    }

    WiFi.mode(WIFI_MODE_STA);

    wifiConnect(deferCP);
}

void wifiReConnect(bool deferCP)
{
    // We are connected? Bail...
    if(WiFi.status() == WL_CONNECTED)
        return;

    // No network configured? Bail...
    if(!wifiHaveSTAConf)
        return;

    wifiOff(true);
    
    // (Re)connect
    WiFi.mode(WIFI_MODE_STA);
    wifiConnect(deferCP);
}

bool wifiNeedReConnect()
{
    // We are connected? Bail...
    if(WiFi.status() == WL_CONNECTED)
        return false;

    // No network configured? Bail...
    if(!wifiHaveSTAConf)
        return false;

    return true;
}    

// Check if WiFi is on; used to determine if a 
// longer interruption due to a re-connect is to
// be expected.
bool wifiIsOn()
{
    if(wifiInAPMode) {
        if(wifiAPOffDelay == 0) return true;
        if(!wifiAPIsOff) return true;
    } else {
        if(origWiFiOffDelay == 0) return true;
        if(!wifiIsOff) return true;
    }
    return false;
}

void wifiStartCP()
{
    if(wifiInAPMode || wifiIsOff)
        return;

    wm.startWebPortal();
}

// This is called when the WiFi config changes, so it has
// nothing to do with our settings here. Despite that,
// we write out our config file so that when the user initially
// configures WiFi, a default settings file exists upon reboot.
// Also, this triggers a reboot, so if the user entered static
// IP data, it becomes active after this reboot.
static void saveConfigCallback()
{
    shouldSaveConfig = 1;
}

// This is the callback from the actual Params page. In this
// case, we really read out the server parms and save them.
static void saveParamsCallback()
{
    shouldSaveConfig = 2;
}

// This is called before a firmware updated is initiated.
// Disable WiFi-off-timers, switch off audio, show "wait"
static void preUpdateCallback()
{
    wifiAPOffDelay = 0;
    origWiFiOffDelay = 0;

    // Reset esp32 to load new settings
    remdisplay.off();
    remledStop.setState(false);

    // Unregister from TCD
    bttfn_remote_unregister();

    mp_stop();
    stopAudio();

    flushDelayedSave();

    showWaitSequence();
}

// Grab static IP parameters from WiFiManager's server.
// Since there is no public method for this, we steal
// the html form parameters in this callback.
static void preSaveConfigCallback()
{
    char ipBuf[20] = "";
    char gwBuf[20] = "";
    char snBuf[20] = "";
    char dnsBuf[20] = "";
    bool invalConf = false;

    #ifdef REMOTE_DBG
    Serial.println("preSaveConfigCallback");
    #endif

    // clear as strncpy might leave us unterminated
    memset(ipBuf, 0, 20);
    memset(gwBuf, 0, 20);
    memset(snBuf, 0, 20);
    memset(dnsBuf, 0, 20);

    if(wm.server->arg(FPSTR(S_ip)) != "") {
        strncpy(ipBuf, wm.server->arg(FPSTR(S_ip)).c_str(), 19);
    } else invalConf |= true;
    if(wm.server->arg(FPSTR(S_gw)) != "") {
        strncpy(gwBuf, wm.server->arg(FPSTR(S_gw)).c_str(), 19);
    } else invalConf |= true;
    if(wm.server->arg(FPSTR(S_sn)) != "") {
        strncpy(snBuf, wm.server->arg(FPSTR(S_sn)).c_str(), 19);
    } else invalConf |= true;
    if(wm.server->arg(FPSTR(S_dns)) != "") {
        strncpy(dnsBuf, wm.server->arg(FPSTR(S_dns)).c_str(), 19);
    } else invalConf |= true;

    #ifdef REMOTE_DBG
    if(strlen(ipBuf) > 0) {
        Serial.printf("IP:%s / SN:%s / GW:%s / DNS:%s\n", ipBuf, snBuf, gwBuf, dnsBuf);
    } else {
        Serial.println("Static IP unset, using DHCP");
    }
    #endif

    if(!invalConf && isIp(ipBuf) && isIp(gwBuf) && isIp(snBuf) && isIp(dnsBuf)) {

        #ifdef REMOTE_DBG
        Serial.println("All IPs valid");
        #endif

        strcpy(ipsettings.ip, ipBuf);
        strcpy(ipsettings.gateway, gwBuf);
        strcpy(ipsettings.netmask, snBuf);
        strcpy(ipsettings.dns, dnsBuf);

        shouldSaveIPConfig = true;

    } else {

        #ifdef REMOTE_DBG
        if(strlen(ipBuf) > 0) {
            Serial.println("Invalid IP");
        }
        #endif

        shouldDeleteIPConfig = true;

    }
}

static void setupStaticIP()
{
    IPAddress ip;
    IPAddress gw;
    IPAddress sn;
    IPAddress dns;

    if(strlen(ipsettings.ip) > 0 &&
        isIp(ipsettings.ip) &&
        isIp(ipsettings.gateway) &&
        isIp(ipsettings.netmask) &&
        isIp(ipsettings.dns)) {

        ip = stringToIp(ipsettings.ip);
        gw = stringToIp(ipsettings.gateway);
        sn = stringToIp(ipsettings.netmask);
        dns = stringToIp(ipsettings.dns);

        wm.setSTAStaticIPConfig(ip, gw, sn, dns);
    }
}

void updateConfigPortalValues()
{
    // Make sure the settings form has the correct values

    //custom_ssDelay.setValue(settings.ssTimer, 3);

    custom_hostName.setValue(settings.hostName, 31);
    custom_sysID.setValue(settings.systemID, 7);
    custom_appw.setValue(settings.appw, 8);
    custom_wifiConTimeout.setValue(settings.wifiConTimeout, 2);
    custom_wifiConRetries.setValue(settings.wifiConRetries, 2);

    custom_tcdIP.setValue(settings.tcdIP, 63);

    #ifdef REMOTE_HAVEMQTT
    custom_mqttServer.setValue(settings.mqttServer, 79);
    custom_mqttUser.setValue(settings.mqttUser, 63);

    custom_mqttb1t.setValue(settings.mqttbt[0], 127);
    custom_mqttb1o.setValue(settings.mqttbo[0], 63);
    custom_mqttb1f.setValue(settings.mqttbf[0], 63);
    custom_mqttb2t.setValue(settings.mqttbt[1], 127);
    custom_mqttb2o.setValue(settings.mqttbo[1], 63);
    custom_mqttb2f.setValue(settings.mqttbf[1], 63);
    custom_mqttb3t.setValue(settings.mqttbt[2], 127);
    custom_mqttb3o.setValue(settings.mqttbo[2], 63);
    custom_mqttb3f.setValue(settings.mqttbf[2], 63);
    custom_mqttb4t.setValue(settings.mqttbt[3], 127);
    custom_mqttb4o.setValue(settings.mqttbo[3], 63);
    custom_mqttb4f.setValue(settings.mqttbf[3], 63);
    custom_mqttb5t.setValue(settings.mqttbt[4], 127);
    custom_mqttb5o.setValue(settings.mqttbo[4], 63);
    custom_mqttb5f.setValue(settings.mqttbf[4], 63);
    custom_mqttb6t.setValue(settings.mqttbt[5], 127);
    custom_mqttb6o.setValue(settings.mqttbo[5], 63);
    custom_mqttb6f.setValue(settings.mqttbf[5], 63);
    custom_mqttb7t.setValue(settings.mqttbt[6], 127);
    custom_mqttb7o.setValue(settings.mqttbo[6], 63);
    custom_mqttb7f.setValue(settings.mqttbf[6], 63);
    custom_mqttb8t.setValue(settings.mqttbt[7], 127);
    custom_mqttb8o.setValue(settings.mqttbo[7], 63);
    custom_mqttb8f.setValue(settings.mqttbf[7], 63);
    
    #endif

    #ifdef TC_NOCHECKBOXES  // Standard text boxes: -------

    custom_coast.setValue(settings.coast, 1);

    #ifdef REMOTE_HAVEAUDIO
    custom_playclick.setValue(settings.playClick, 1);
    custom_playALSnd.setValue(settings.playALsnd, 1);
    #endif
    
    #ifdef REMOTE_HAVEMQTT
    custom_useMQTT.setValue(settings.useMQTT, 1);
    #endif

    #ifdef REMOTE_HAVEAUDIO
    custom_shuffle.setValue(settings.shuffle, 1);
    #endif
    
    custom_CfgOnSD.setValue(settings.CfgOnSD, 1);
    //custom_sdFrq.setValue(settings.sdFreq, 1);

    custom_dBP.setValue(settings.disBPack, 1);

    custom_b0mt.setValue(settings.bPb0Maint, 1);
    custom_b1mt.setValue(settings.bPb1Maint, 1);
    custom_b2mt.setValue(settings.bPb2Maint, 1);
    custom_b3mt.setValue(settings.bPb3Maint, 1);
    custom_b4mt.setValue(settings.bPb4Maint, 1);
    custom_b5mt.setValue(settings.bPb5Maint, 1);
    custom_b6mt.setValue(settings.bPb6Maint, 1);
    custom_b7mt.setValue(settings.bPb7Maint, 1);

    #ifdef REMOTE_HAVEAUDIO
    custom_b0mtoo.setValue(settings.bPb0MtO, 1);
    custom_b1mtoo.setValue(settings.bPb1MtO, 1);
    custom_b2mtoo.setValue(settings.bPb2MtO, 1);
    custom_b3mtoo.setValue(settings.bPb3MtO, 1);
    custom_b4mtoo.setValue(settings.bPb4MtO, 1);
    custom_b5mtoo.setValue(settings.bPb5MtO, 1);
    custom_b6mtoo.setValue(settings.bPb6MtO, 1);
    custom_b7mtoo.setValue(settings.bPb7MtO, 1);
    #endif
    
    #else   // For checkbox hack --------------------------

    setCBVal(&custom_coast, settings.coast);
    
    #ifdef REMOTE_HAVEAUDIO
    setCBVal(&custom_playclick, settings.playClick);
    setCBVal(&custom_playALSnd, settings.playALsnd);
    #endif

    #ifdef REMOTE_HAVEMQTT
    setCBVal(&custom_useMQTT, settings.useMQTT);
    #endif

    #ifdef REMOTE_HAVEAUDIO
    setCBVal(&custom_shuffle, settings.shuffle);
    #endif
    
    setCBVal(&custom_CfgOnSD, settings.CfgOnSD);
    //setCBVal(&custom_sdFrq, settings.sdFreq);

    setCBVal(&custom_dBP, settings.disBPack);

    setCBVal(&custom_b0mt, settings.bPb0Maint);
    setCBVal(&custom_b1mt, settings.bPb1Maint);
    setCBVal(&custom_b2mt, settings.bPb2Maint);
    setCBVal(&custom_b3mt, settings.bPb3Maint);
    setCBVal(&custom_b4mt, settings.bPb4Maint);
    setCBVal(&custom_b5mt, settings.bPb5Maint);
    setCBVal(&custom_b6mt, settings.bPb6Maint);
    setCBVal(&custom_b7mt, settings.bPb7Maint);

    #ifdef REMOTE_HAVEAUDIO
    setCBVal(&custom_b0mtoo, settings.bPb0MtO);
    setCBVal(&custom_b1mtoo, settings.bPb1MtO);
    setCBVal(&custom_b2mtoo, settings.bPb2MtO);
    setCBVal(&custom_b3mtoo, settings.bPb3MtO);
    setCBVal(&custom_b4mtoo, settings.bPb4MtO);
    setCBVal(&custom_b5mtoo, settings.bPb5MtO);
    setCBVal(&custom_b6mtoo, settings.bPb6MtO);
    setCBVal(&custom_b7mtoo, settings.bPb7MtO);
    #endif

    #endif // ---------------------------------------------
}

#ifdef REMOTE_HAVEAUDIO
void updateConfigPortalVolValues()
{
    #ifdef REMOTE_HAVEVOLKNOB
    if(curSoftVol == 255) {
        strcpy(settings.FixV, "0");
        strcpy(settings.Vol, "6");
    } else {
        strcpy(settings.FixV, "1");
        sprintf(settings.Vol, "%d", curSoftVol);
    }
    #else
    if(curSoftVol > 19) curSoftVol = DEFAULT_VOLUME;
    sprintf(settings.Vol, "%d", curSoftVol);
    #endif
    
    #ifdef REMOTE_HAVEVOLKNOB
    #ifdef DG_NOCHECKBOXES  // Standard text boxes: -------
    custom_FixV.setValue(settings.FixV, 1);
    #else   // For checkbox hack --------------------------
    setCBVal(&custom_FixV, settings.FixV);
    #endif // ---------------------------------------------
    #endif
    
    custom_Vol.setValue(settings.Vol, 2);

    updateConfigPortalVisValues();
}

void updateConfigPortalMFValues()
{
    sprintf(settings.musicFolder, "%d", musFolderNum);
    custom_musicFolder.setValue(settings.musicFolder, 2);
}
#endif

void updateConfigPortalBriValues()
{
    sprintf(settings.Bri, "%d", remdisplay.getBrightness());
    custom_Bri.setValue(settings.Bri, 2);
}

void updateConfigPortalVisValues()
{
    strcpy(settings.movieMode, movieMode ? "1" : "0");
    #ifdef PG_NOCHECKBOXES  // Standard text boxes: -------
    custom_sStrict.setValue(settings.movieMode, 1);
    #else   // For checkbox hack --------------------------
    setCBVal(&custom_sStrict, settings.movieMode);
    #endif // ---------------------------------------------
    strcpy(settings.dgps, displayGPSMode ? "1" : "0");
    #ifdef PG_NOCHECKBOXES  // Standard text boxes: -------
    custom_dGPS.setValue(settings.dgps, 1);
    #else   // For checkbox hack --------------------------
    setCBVal(&custom_dGPS, settings.dgps);
    #endif // ---------------------------------------------
}

/*
 * Audio data uploader
 */
#ifdef REMOTE_HAVEAUDIO 
static void setupWebServerCallback()
{
    wm.server->on(WM_G(R_updateacdone), HTTP_POST, &handleUploadDone, &handleUploading);
}

static void doCloseACFile(bool doRemove)
{
    if(haveACFile) {
        closeACFile(acFile);
        haveACFile = false;
    }
    if(doRemove) removeACFile();
}

static void handleUploading()
{
    HTTPUpload& upload = wm.server->upload();

    if(upload.status == UPLOAD_FILE_START) {

          preUpdateCallback();

          #ifdef REMOTE_DBG
          Serial.printf("Upload: %s\n", upload.filename.c_str());
          #endif
          
          haveACFile = openACFile(acFile); 
          ACULerr = haveACFile ? 0 : (haveSD ? 1 : 2);
          
    } else if(upload.status == UPLOAD_FILE_WRITE) {

          if(haveACFile) {
              if(writeACFile(acFile, upload.buf, upload.currentSize) != upload.currentSize) {
                  doCloseACFile(true);
                  ACULerr = 3;
              }
          }

    } else if(upload.status == UPLOAD_FILE_END) {

        doCloseACFile(false);
      
    } else if(upload.status == UPLOAD_FILE_ABORTED) {

        doCloseACFile(true);
        ACULerr = 4;
        endWaitSequence();

    }

    delay(0);
}

static void handleUploadDone()
{
    const char *ebuf = "ERROR";
    const char *dbuf = "DONE";
    char *buf = NULL;
    bool ownbuf = false;
    int buflen  = STRLEN(acul_part1) +
                  STRLEN(myTitle)    +
                  STRLEN(acul_part2) +
                  STRLEN(myHead)     +
                  STRLEN(acul_part3) +
                  STRLEN(acul_part4) +
                  STRLEN(myTitle)    +
                  STRLEN(acul_part5) +
                  STRLEN(apName)     +
                  STRLEN(acul_part6) +
                  STRLEN(acul_part8) +
                  1;

    if(!ACULerr) {
        if(!check_if_default_audio_present()) {
            ACULerr = 5;
            removeACFile();
        }
    }

    buflen += ACULerr ? (STRLEN(acul_part71) + strlen(acul_errs[ACULerr-1])) : STRLEN(acul_part7);

    if(!(buf = (char *)malloc(buflen))) {
        buf = (char *)(ACULerr ? ebuf : dbuf);
        ownbuf = true;
    } else {
        strcpy(buf, acul_part1);
        strcat(buf, myTitle);
        strcat(buf, acul_part2);
        strcat(buf, myHead);
        strcat(buf, acul_part3);
        strcat(buf, acul_part4);
        strcat(buf, myTitle);
        strcat(buf, acul_part5);
        strcat(buf, apName);
        strcat(buf, acul_part6);
        if(!ACULerr) {
            strcat(buf, acul_part7);
        } else {
            strcat(buf, acul_part71);
            strcat(buf, acul_errs[ACULerr-1]);
        }
        strcat(buf, acul_part8);
    }

    String str(buf);
    wm.server->send(200, F("text/html"), str);
    
    if(!ACULerr) {
        delay(1000);
        ESP.restart();
    }

    // preUpdateCallback() does no real harm, so resume
    if(!ownbuf) free(buf);
    endWaitSequence();
}
#endif

bool wifi_getIP(uint8_t& a, uint8_t& b, uint8_t& c, uint8_t& d)
{
    IPAddress myip;

    switch(WiFi.getMode()) {
      case WIFI_MODE_STA:
          myip = WiFi.localIP();
          break;
      case WIFI_MODE_AP:
      case WIFI_MODE_APSTA:
          myip = WiFi.softAPIP();
          break;
      default:
          a = b = c = d = 0;
          return true;
    }

    a = myip[0];
    b = myip[1];
    c = myip[2];
    d = myip[3];

    return true;
}

// Check if String is a valid IP address
bool isIp(char *str)
{
    int segs = 0;
    int digcnt = 0;
    int num = 0;

    while(*str != '\0') {

        if(*str == '.') {

            if(!digcnt || (++segs == 4))
                return false;

            num = digcnt = 0;
            str++;
            continue;

        } else if((*str < '0') || (*str > '9')) {

            return false;

        }

        if((num = (num * 10) + (*str - '0')) > 255)
            return false;

        digcnt++;
        str++;
    }

    if(segs == 3) 
        return true;

    return false;
}

// IPAddress to string
static void ipToString(char *str, IPAddress ip)
{
    sprintf(str, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

// String to IPAddress
static IPAddress stringToIp(char *str)
{
    int ip1, ip2, ip3, ip4;

    sscanf(str, "%d.%d.%d.%d", &ip1, &ip2, &ip3, &ip4);

    return IPAddress(ip1, ip2, ip3, ip4);
}

/*
 * Read parameter from server, for customhmtl input
 */
static void getParam(String name, char *destBuf, size_t length)
{
    memset(destBuf, 0, length+1);
    if(wm.server->hasArg(name)) {
        strncpy(destBuf, wm.server->arg(name).c_str(), length);
    }
}

static bool myisspace(char mychar)
{
    return (mychar == ' ' || mychar == '\n' || mychar == '\t' || mychar == '\v' || mychar == '\f' || mychar == '\r');
}

static bool myisgoodchar(char mychar)
{
    return ((mychar >= '0' && mychar <= '9') || (mychar >= 'a' && mychar <= 'z') || (mychar >= 'A' && mychar <= 'Z') || mychar == '-');
}

static char* strcpytrim(char* destination, const char* source, bool doFilter)
{
    char *ret = destination;
    
    while(*source) {
        if(!myisspace(*source) && (!doFilter || myisgoodchar(*source))) *destination++ = *source;
        source++;
    }
    
    *destination = 0;
    
    return ret;
}

static void mystrcpy(char *sv, WiFiManagerParameter *el)
{
    strcpy(sv, el->getValue());
}

#ifndef TC_NOCHECKBOXES
static void strcpyCB(char *sv, WiFiManagerParameter *el)
{
    strcpy(sv, (atoi(el->getValue()) > 0) ? "1" : "0");
}

static void setCBVal(WiFiManagerParameter *el, char *sv)
{
    const char makeCheck[] = "1' checked a='";
    
    el->setValue((atoi(sv) > 0) ? makeCheck : "1", 14);
}
#endif

#ifdef REMOTE_HAVEMQTT
static void truncateUTF8(char *src)
{
    int i, j, slen = strlen(src);
    unsigned char c, e;

    for(i = 0; i < slen; i++) {
        c = (unsigned char)src[i];
        e = 0;
        if     (c >= 192 && c < 224)  e = 1;
        else if(c >= 224 && c < 240)  e = 2;
        else if(c >= 240 && c < 248)  e = 3;  // Invalid UTF8 >= 245, but consider 4-byte char anyway
        if(e) {
            if((i + e) < slen) {
                i += e;
            } else {
                src[i] = 0;
                return;
            }
        }
    }
}

static void strcpyutf8(char *dst, const char *src, unsigned int len)
{
    strncpy(dst, src, len - 1);
    dst[len - 1] = 0;
    truncateUTF8(dst);
}

static void mqttLooper()
{
    audio_loop();
}

static void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    int i = 0, j, ml = (length <= 255) ? length : 255;
    char tempBuf[256];
    static const char *cmdList[] = {
      "AAA",              // [Placeholder] 0
      "BBB",              // [Placeholder] 1
      "CCC",              // [Placeholder] 2
      "DDD",              // [Placeholder] 3
      "WWW",              // [Placeholder] 4
      #ifdef REMOTE_HAVEAUDIO
      "MP_SHUFFLE_ON",    // 5 
      "MP_SHUFFLE_OFF",   // 6
      "MP_PLAY",          // 7
      "MP_STOP",          // 8
      "MP_NEXT",          // 9
      "MP_PREV",          // 10
      "MP_FOLDER_",       // 11  MP_FOLDER_0..MP_FOLDER_9
      #endif
      NULL
    };
    static const char *cmdList2[] = {
      "PREPARE",          // 0
      "TIMETRAVEL",       // 1
      "REENTRY",          // 2
      "ABORT_TT",         // 3
      "ALARM",            // 4
      "WAKEUP",           // 5
      NULL
    };

    if(!length) return;

    memcpy(tempBuf, (const char *)payload, ml);
    tempBuf[ml] = 0;
    for(j = 0; j < ml; j++) {
        if(tempBuf[j] >= 'a' && tempBuf[j] <= 'z') tempBuf[j] &= ~0x20;
    }

    if(!strcmp(topic, "bttf/tcd/pub")) {

        // Commands from TCD

        while(cmdList2[i]) {
            j = strlen(cmdList2[i]);
            if((length >= j) && !strncmp((const char *)tempBuf, cmdList2[i], j)) {
                break;
            }
            i++;          
        }

        if(!cmdList2[i]) return;

        switch(i) {
        case 0:
            // Prepare for TT. Comes at some undefined point,
            // an undefined time before the actual tt, and may
            // not come at all.
            if(FPBUnitIsOn && !TTrunning) {
                prepareTT();
            }
            break;
        case 1:
            // Trigger Time Travel (if not running already)
            if(FPBUnitIsOn && !TTrunning) {
                networkTimeTravel = true;
                networkReentry = false;
                networkAbort = false;
                networkLead = P0_DUR;
                networkP1 = P1_DUR;
            }
            break;
        case 2:   // Re-entry
            // Start re-entry (if TT currently running)
            if(TTrunning) {
                networkReentry = true;
            }
            break;
        case 3:   // Abort TT (TCD fake-powered down during TT)
            if(TTrunning) {
                networkAbort = true;
            }
            tcdIsInP0 = 0;
            break;
        case 4:
            networkAlarm = true;
            // Eval this at our convenience
            break;
        case 5: 
            if(FPBUnitIsOn && !TTrunning) {
                wakeup();
            }
            break;
        }
       
    } else if(!strcmp(topic, "bttf/remote/cmd")) {

        // User commands

        // Not taking commands under these circumstances:
        if(TTrunning || !FPBUnitIsOn)
            return;
        
        while(cmdList[i]) {
            j = strlen(cmdList[i]);
            if((length >= j) && !strncmp((const char *)tempBuf, cmdList[i], j)) {
                break;
            }
            i++;          
        }

        if(!cmdList[i]) return;

        switch(i) {
        #ifdef REMOTE_HAVEAUDIO
        case 5:
        case 6:
            if(haveMusic) mp_makeShuffle((i == 5));
            break;
        case 7:    
            if(haveMusic) mp_play();
            break;
        case 8:
            if(haveMusic && mpActive) {
                mp_stop();
            }
            break;
        case 9:
            if(haveMusic) mp_next(mpActive);
            break;
        case 10:
            if(haveMusic) mp_prev(mpActive);
            break;
        case 11:
            if(haveSD) {
                if(strlen(tempBuf) > j && tempBuf[j] >= '0' && tempBuf[j] <= '9') {
                    switchMusicFolder((uint8_t)(tempBuf[j] - '0'));
                }
            }
            break;
        #endif
        }
            
    } 
}

#ifdef REMOTE_DBG
#define MQTT_FAILCOUNT 6
#else
#define MQTT_FAILCOUNT 120
#endif

static void mqttPing()
{
    switch(mqttClient.pstate()) {
    case PING_IDLE:
        if(WiFi.status() == WL_CONNECTED) {
            if(!mqttPingNow || (millis() - mqttPingNow > mqttPingInt)) {
                mqttPingNow = millis();
                if(!mqttClient.sendPing()) {
                    // Mostly fails for internal reasons;
                    // skip ping test in that case
                    mqttDoPing = false;
                    mqttPingDone = true;  // allow mqtt-connect attempt
                }
            }
        }
        break;
    case PING_PINGING:
        if(mqttClient.pollPing()) {
            mqttPingDone = true;          // allow mqtt-connect attempt
            mqttPingNow = 0;
            mqttPingsExpired = 0;
            mqttPingInt = MQTT_SHORT_INT; // Overwritten on fail in reconnect
            // Delay re-connection for 5 seconds after first ping echo
            mqttReconnectNow = millis() - (mqttReconnectInt - 5000);
        } else if(millis() - mqttPingNow > 5000) {
            mqttClient.cancelPing();
            mqttPingNow = millis();
            mqttPingsExpired++;
            mqttPingInt = MQTT_SHORT_INT * (1 << (mqttPingsExpired / MQTT_FAILCOUNT));
            mqttReconnFails = 0;
        }
        break;
    } 
}

static bool mqttReconnect(bool force)
{
    bool success = false;

    if(useMQTT && (WiFi.status() == WL_CONNECTED)) {

        if(!mqttClient.connected()) {
    
            if(force || !mqttReconnectNow || (millis() - mqttReconnectNow > mqttReconnectInt)) {

                #ifdef REMOTE_DBG
                Serial.println("MQTT: Attempting to (re)connect");
                #endif
    
                if(strlen(mqttUser)) {
                    success = mqttClient.connect(settings.hostName, mqttUser, strlen(mqttPass) ? mqttPass : NULL);
                } else {
                    success = mqttClient.connect(settings.hostName);
                }
    
                mqttReconnectNow = millis();
                
                if(!success) {
                    mqttRestartPing = true;  // Force PING check before reconnection attempt
                    mqttReconnFails++;
                    if(mqttDoPing) {
                        mqttPingInt = MQTT_SHORT_INT * (1 << (mqttReconnFails / MQTT_FAILCOUNT));
                    } else {
                        mqttReconnectInt = MQTT_SHORT_INT * (1 << (mqttReconnFails / MQTT_FAILCOUNT));
                    }
                    #ifdef REMOTE_DBG
                    Serial.printf("MQTT: Failed to reconnect (%d)\n", mqttReconnFails);
                    #endif
                } else {
                    mqttReconnFails = 0;
                    mqttReconnectInt = MQTT_SHORT_INT;
                    #ifdef REMOTE_DBG
                    Serial.println("MQTT: Connected to broker, waiting for CONNACK");
                    #endif
                }
    
                return success;
            } 
        }
    }
      
    return true;
}

static void mqttSubscribe()
{
    // Meant only to be called when connected!
    if(!mqttSubAttempted) {
        if(!mqttClient.subscribe("bttf/remote/cmd", "bttf/tcd/pub")) {
            #ifdef REMOTE_DBG
            Serial.println("MQTT: Failed to subscribe to command topics");
            #endif
        }
        mqttSubAttempted = true;
    }
}

bool mqttState()
{
    return (useMQTT && mqttClient.connected());
}

void mqttPublish(const char *topic, const char *pl, unsigned int len)
{
    if(useMQTT) {
        mqttClient.publish(topic, (uint8_t *)pl, len, false);
    }
}           

#endif
