/*
 * -------------------------------------------------------------------
 * Remote Control
 * (C) 2024-2025 Thomas Winischhofer (A10001986)
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

static const char acul_part3[]  = "</head><body><div class='wrap'><h1>";
static const char acul_part5[]  = "</h1><h3>";
static const char acul_part6[]  = "</h3><div class='msg";
static const char acul_part7[]  = " S'><strong>Upload successful.</strong>";
static const char acul_part7a[] = "<br>Installation will proceed after reboot.";
static const char acul_part7b[] = " S'><strong>File deleted.</strong>";
static const char acul_part71[] = " D'><strong>Upload failed.</strong><br>";
static const char acul_part8[]  = "</div></div></body></html>";
static const char *acul_errs[]  = { 
    "Can't open file on SD",
    "No SD card found",
    "Write error",
    "Bad file",
    "Not enough memory",
    "Unrecognized type",
    "Extraneous .bin file"
};

static const char *apChannelCustHTMLSrc[16] = {
    "<div class='cmp0'><label for='apchnl'>WiFi channel</label><select class='sel0' value='",
    "apchnl",
    ">Random%s1'",
    ">1%s2'",
    ">2%s3'",
    ">3%s4'",
    ">4%s5'",
    ">5%s6'",
    ">6%s7'",
    ">7%s8'",
    ">8%s9'",
    ">9%s10'",
    ">10%s11'",
    ">11%s12'",
    ">12%s13'",
    ">13%s"
};

#ifdef HAVE_PM
static const char *batTypeHTMLSrc[7] = {
    "<div class='cmp0'><label for='bty'>Battery type</label><select class='sel0' value='",
    "bty",
    ">3.7V/4.2V LiPo%s3'",
    ">3.8V/4.35V LiPo%s4'",
    ">3.85V/4.4V LiPo%s1'",
    ">UR18650ZY%s2'",
    ">ICR18650-26H%s"
};
static const char *wmBuildBatType(const char *dest);
#endif

static const char *wmBuildApChnl(const char *dest);

static const char custHTMLSel[] = " selected";
static const char *osde = "</option></select></div>";
static const char *ooe  = "</option><option value='";

static const char *aco = "autocomplete='off'";

// WiFi Configuration

#if defined(REMOTE_MDNS) || defined(REMOTE_WM_HAS_MDNS)
#define HNTEXT "Hostname<br><span style='font-size:80%'>The Config Portal is accessible at http://<i>hostname</i>.local<br>(Valid characters: a-z/0-9/-)</span>"
#else
#define HNTEXT "Hostname<br><span style='font-size:80%'>(Valid characters: a-z/0-9/-)</span>"
#endif
WiFiManagerParameter custom_hostName("hostname", HNTEXT, settings.hostName, 31, "pattern='[A-Za-z0-9\\-]+' placeholder='Example: dtmremote'");
WiFiManagerParameter custom_wifiConRetries("wifiret", "Connection attempts (1-10)", settings.wifiConRetries, 2, "type='number' min='1' max='10' autocomplete='off'", WFM_LABEL_BEFORE);
WiFiManagerParameter custom_wifiConTimeout("wificon", "Connection timeout (7-25[seconds])", settings.wifiConTimeout, 2, "type='number' min='7' max='25'");

WiFiManagerParameter custom_sysID("sysID", "Network name (SSID) appendix<br><span style='font-size:80%'>Will be appended to \"REM-AP\" to create a unique SSID if multiple remotes are in range. [a-z/0-9/-]</span>", settings.systemID, 7, "pattern='[A-Za-z0-9\\-]+'");
WiFiManagerParameter custom_appw("appw", "Password<br><span style='font-size:80%'>Password to protect REM-AP. Empty or 8 characters [a-z/0-9/-]<br><b>Write this down, you might lock yourself out!</b></span>", settings.appw, 8, "minlength='8' pattern='[A-Za-z0-9\\-]+'");
WiFiManagerParameter custom_apch(wmBuildApChnl);

// Settings

WiFiManagerParameter custom_aood("<div class='msg P'>Please <a href='/update'>install/update</a> sound pack</div>");

WiFiManagerParameter custom_at("at", "Auto throttle", settings.autoThrottle, 1, "autocomplete='off' title='When checked, accleration will continue when trottle in neutral. Has precedence over Coasting.' type='checkbox' class='mt5' style='margin-bottom:5px;'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_coast("cst", "Coasting when throttle in neutral", settings.coast, 1, "autocomplete='off' title='Check to enable coasting then trottle is in neutral position. Mutually exclusive to Auto Throttle.' type='checkbox'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_oott("oott", "O.O, throttle-up trigger BTTFN-wide Time Travel", settings.ooTT, 1, "autocomplete='off' title='When unchecked, pressing O.O will play previous song' type='checkbox'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_sStrict("sStrict", "Movie-like acceleration<br><span style='font-size:80%'>Check to set the acceleration pace to what is shown in the movie. This slows down acceleration at higher speeds.</span>", settings.movieMode, 1, "autocomplete='off' type='checkbox' class='mb0'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_playclick("plyCLK", "Play acceleration 'click' sound", settings.playClick, 1, "autocomplete='off' type='checkbox'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_playALSnd("plyALS", "Play TCD-alarm sound", settings.playALsnd, 1, "autocomplete='off' title='Check to have the device play a sound then the TCD alarm sounds.' type='checkbox'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_dGPS("dGPS", "Display TCD speed when fake-off", settings.dgps, 1, "autocomplete='off' type='checkbox' class='mb10'", WFM_LABEL_AFTER);

WiFiManagerParameter custom_Bri("Bri", "Brightness level (0-15)", settings.Bri, 2, "type='number' min='0' max='15' autocomplete='off'", WFM_LABEL_BEFORE);

#ifdef REMOTE_HAVEVOLKNOB
WiFiManagerParameter custom_FixV("FixV", "Disable volume knob", settings.FixV, 1, "autocomplete='off' title='Check this if the audio volume should be set by software; if unchecked, the volume knob is used' type='checkbox' class='mt5'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_Vol("Vol", "Volume level (0-19)", settings.Vol, 2, "type='number' min='0' max='19' autocomplete='off'", WFM_LABEL_BEFORE);
#else
WiFiManagerParameter custom_Vol("Vol", "Volume level (0-19)", settings.Vol, 2, "type='number' min='0' max='19' autocomplete='off'", WFM_LABEL_BEFORE);
#endif

WiFiManagerParameter custom_musicFolder("mfol", "Music folder (0-9)", settings.musicFolder, 2, "type='number' min='0' max='9'");
WiFiManagerParameter custom_shuffle("musShu", "Shuffle mode enabled at startup", settings.shuffle, 1, "type='checkbox' class='mt5'", WFM_LABEL_AFTER);

#ifdef BTTFN_MC
WiFiManagerParameter custom_tcdIP("tcdIP", "IP address or hostname of TCD", settings.tcdIP, 31, "pattern='(^((25[0-5]|(2[0-4]|1\\d|[1-9]|)\\d)\\.?\\b){4}$)|([A-Za-z0-9\\-]+)' placeholder='Example: timecircuits'");
#else
WiFiManagerParameter custom_tcdIP("tcdIP", "IP address of TCD", settings.tcdIP, 31, "pattern='^((25[0-5]|(2[0-4]|1\\d|[1-9]|)\\d)\\.?\\b){4}$' placeholder='Example: 192.168.4.1'");
#endif

#ifdef REMOTE_HAVEMQTT
WiFiManagerParameter custom_useMQTT("uMQTT", "Use Home Assistant (MQTT 3.1.1)", settings.useMQTT, 1, "type='checkbox' class='mt5 mb10'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_mqttServer("ha_server", "Broker IP[:port] or domain[:port]", settings.mqttServer, 79, "pattern='[a-zA-Z0-9\\.:\\-]+' placeholder='Example: 192.168.1.5'");
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

WiFiManagerParameter custom_CfgOnSD("CfgOnSD", "Save secondary settings on SD<br><span style='font-size:80%'>Check this to avoid flash wear</span>", settings.CfgOnSD, 1, "autocomplete='off' type='checkbox' class='mt5 mb0'", WFM_LABEL_AFTER);
//WiFiManagerParameter custom_sdFrq("sdFrq", "4MHz SD clock speed<br><span style='font-size:80%'>Checking this might help in case of SD card problems</span>", settings.sdFreq, 1, "autocomplete='off' type='checkbox' style='margin-top:12px'", WFM_LABEL_AFTER);

#ifdef ALLOW_DIS_UB
WiFiManagerParameter custom_dBP("dBP", "Disable User Buttons", settings.disBPack, 1, "autocomplete='off' type='checkbox' class='mt5'", WFM_LABEL_AFTER);
#endif
WiFiManagerParameter custom_b0mt("b0mt", "Button 1 is maintained switch", settings.bPb0Maint, 1, "autocomplete='off' type='checkbox' class='mt5'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_b1mt("b1mt", "Button 2 is maintained switch", settings.bPb1Maint, 1, "autocomplete='off' type='checkbox'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_b2mt("b2mt", "Button 3 is maintained switch", settings.bPb2Maint, 1, "autocomplete='off' type='checkbox'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_b3mt("b3mt", "Button 4 is maintained switch", settings.bPb3Maint, 1, "autocomplete='off' type='checkbox'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_b4mt("b4mt", "Button 5 is maintained switch", settings.bPb4Maint, 1, "autocomplete='off' type='checkbox'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_b5mt("b5mt", "Button 6 is maintained switch", settings.bPb5Maint, 1, "autocomplete='off' type='checkbox'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_b6mt("b6mt", "Button 7 is maintained switch", settings.bPb6Maint, 1, "autocomplete='off' type='checkbox'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_b7mt("b7mt", "Button 8 is maintained switch", settings.bPb7Maint, 1, "autocomplete='off' type='checkbox'", WFM_LABEL_AFTER);

WiFiManagerParameter custom_b0mtoo("b0mto", "Maintained: Play audio on ON only", settings.bPb0MtO, 1, "autocomplete='off' title='Check to play audio when switch is in ON position only. If unchecked, audio is played on each flip.' type='checkbox' class='mt5' style='margin-left:20px'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_b1mtoo("b1mto", "Maintained: Play audio on ON only", settings.bPb1MtO, 1, "autocomplete='off' title='Check to play audio when switch is in ON position only. If unchecked, audio is played on each flip.' type='checkbox' class='mt5' style='margin-left:20px'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_b2mtoo("b2mto", "Maintained: Play audio on ON only", settings.bPb2MtO, 1, "autocomplete='off' title='Check to play audio when switch is in ON position only. If unchecked, audio is played on each flip.' type='checkbox' class='mt5' style='margin-left:20px'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_b3mtoo("b3mto", "Maintained: Play audio on ON only", settings.bPb3MtO, 1, "autocomplete='off' title='Check to play audio when switch is in ON position only. If unchecked, audio is played on each flip.' type='checkbox' class='mt5' style='margin-left:20px'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_b4mtoo("b4mto", "Maintained: Play audio on ON only", settings.bPb4MtO, 1, "autocomplete='off' title='Check to play audio when switch is in ON position only. If unchecked, audio is played on each flip.' type='checkbox' class='mt5' style='margin-left:20px'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_b5mtoo("b5mto", "Maintained: Play audio on ON only", settings.bPb5MtO, 1, "autocomplete='off' title='Check to play audio when switch is in ON position only. If unchecked, audio is played on each flip.' type='checkbox' class='mt5' style='margin-left:20px'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_b6mtoo("b6mto", "Maintained: Play audio on ON only", settings.bPb6MtO, 1, "autocomplete='off' title='Check to play audio when switch is in ON position only. If unchecked, audio is played on each flip.' type='checkbox' class='mt5' style='margin-left:20px'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_b7mtoo("b7mto", "Maintained: Play audio on ON only", settings.bPb7MtO, 1, "autocomplete='off' title='Check to play audio when switch is in ON position only. If unchecked, audio is played on each flip.' type='checkbox' class='mt5' style='margin-left:20px'", WFM_LABEL_AFTER);

WiFiManagerParameter custom_uPL("uPL", "Use Futaba power LED", settings.usePwrLED, 1, "autocomplete='off' type='checkbox' class='mt5'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_uLM("uMt", "Use Futaba battery level meter", settings.useLvlMtr, 1, "autocomplete='off' title='If unchecked, LED and meter follow real power' type='checkbox'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_PLD("PLD", "Power LED/level meter on fake power", settings.pwrLEDonFP, 1, "autocomplete='off' title='If unchecked, LED and meter follow real power' type='checkbox'", WFM_LABEL_AFTER);

#ifdef HAVE_PM
WiFiManagerParameter custom_UPM("UPM", "Battery monitoring/warnings", settings.usePwrMon, 1, "autocomplete='off' title='If unchecked, no battery-low warnings will be given' type='checkbox' class='mt5 mb10'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_bty(wmBuildBatType);  // batt type
WiFiManagerParameter custom_bca("bCa", "Capacity per cell (1000-6000)", settings.batCap, 5, "type='number' min='1000' max='6000' autocomplete='off'", WFM_LABEL_BEFORE);
#endif

WiFiManagerParameter custom_sectstart_head("<div class='sects'>");
WiFiManagerParameter custom_sectstart("</div><div class='sects'>");
WiFiManagerParameter custom_sectend("</div>");

WiFiManagerParameter custom_sectstart_wifi("</div><div class='sects'><div class='headl'>WiFi connection: Other settings</div>");

WiFiManagerParameter custom_sectstart_mp("</div><div class='sects'><div class='headl'>MusicPlayer</div>");
WiFiManagerParameter custom_sectstart_ap("</div><div class='sects'><div class='headl'>Access point (AP) mode</div>");
WiFiManagerParameter custom_sectstart_nw("</div><div class='sects'><div class='headl'>Wireless communication (BTTF-Network)</div>");
WiFiManagerParameter custom_sectstart_hw("</div><div class='sects'><div class='headl'>User Buttons</div>");

WiFiManagerParameter custom_sectend_foot("</div><p></p>");

#define TC_MENUSIZE 6
static const int8_t wifiMenu[TC_MENUSIZE] = { 
    WM_MENU_WIFI,
    WM_MENU_PARAM,
    WM_MENU_SEP,
    WM_MENU_UPDATE,
    WM_MENU_SEP,
    WM_MENU_CUSTOM
};

#define AA_TITLE "DTM Remote"
#define AA_ICON "iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAMAAAAoLQ9TAAAAGXRFWHRTb2Z0d2FyZQBBZG9iZSBJbWFnZVJlYWR5ccllPAAAABVQTFRFSUpKzMvHtr+9AAAA8iU92uXj9tM0iFgDpgAAAEFJREFUeNp8z0sKACAIRVF/uf8ll30QX9EdODgoETFETKWE9gMZVXCvoNkGOU0wA4gNDRtjgT3gOkHAZ+FzUBdgAJ/RAVPNkrSUAAAAAElFTkSuQmCC"
#define AA_CONTAINER "REMA"
#define UNI_VERSION REMOTE_VERSION 
#define UNI_VERSION_EXTRA REMOTE_VERSION_EXTRA
#define WEBHOME "remote"

static const char myTitle[] = AA_TITLE;
static const char apName[]  = "REM-AP";
static const char myHead[]  = "<link rel='shortcut icon' type='image/png' href='data:image/png;base64," AA_ICON "'><script>window.onload=function(){xx=false;document.title=xxx='" AA_TITLE "';id=-1;ar=['/u','/uac','/wifisave','/paramsave','/erase'];ti=['Firmware upload','','WiFi Configuration','Settings','Erase WiFi Config'];if(ge('s')&&ge('dns')){xx=true;yyy=ti[2]}if(ge('uploadbin')||(id=ar.indexOf(wlp()))>=0){xx=true;if(id>=2){yyy=ti[id]}else{yyy=ti[0]};aa=gecl('wrap');if(aa.length>0){if(ge('uploadbin')){aa[0].style.textAlign='center';}aa=getn('H3');if(aa.length>0){aa[0].remove()}aa=getn('H1');if(aa.length>0){aa[0].remove()}}}if(ge('ttrp')||wlp()=='/param'){xx=true;yyy=ti[3];}if(ge('ebnew')){xx=true;bb=getn('H3');aa=getn('H1');yyy=bb[0].innerHTML;ff=aa[0].parentNode;ff.style.position='relative';}if(xx){zz=(Math.random()>0.8);dd=document.createElement('div');dd.classList.add('tpm0');dd.innerHTML='<div class=\"tpm\" onClick=\"window.location=\\'/\\'\"><div class=\"tpm2\"><img src=\"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAMAAACdt4HsAAAAGXRFWHRTb2Z0d2FyZQBBZG9iZSBJbWFnZVJlYWR5ccllPAAAAAZQTFRFSp1tAAAA635cugAAAAJ0Uk5T/wDltzBKAAAA'+(zz?'bUlEQVR42tzXwRGAQAwDMdF/09QQQ24MLkDj77oeTiPA1wFGQiHATOgDGAp1AFOhDWAslAHMhS6AQKgCSIQmgEgoAsiEHoBQqAFIhRaAWCgByIVXAMuAdcA6YBlwALAKePzgd71QAByP71uAAQC+xwvdcFg7UwAAAABJRU5ErkJggg==':'gElEQVR42tzXQQqDABAEwcr/P50P2BBUdMhee6j7+lw8i4BCD8MiQAjHYRAghAh7ADWMMAcQww5jADHMsAYQwwxrADHMsAYQwwxrADHMsAYQwwxrgLgOPwKeAjgrrACcFkYAzgu3AN4C3AV4D3AP4E3AHcDF+8d/YQB4/Pn+CjAAMaIIJuYVQ04AAAAASUVORK5CYII=')+'\" class=\"tpm3\"></div><H1 class=\"tpmh1\"'+(zz?' style=\"margin-left:1.4em\"':'')+'>'+xxx+'</H1>'+'<H3 class=\"tpmh3\"'+(zz?' style=\"padding-left:5em\"':'')+'>'+yyy+'</div></div>';}if(ge('ebnew')){bb[0].remove();aa[0].replaceWith(dd);}else if(xx){aa=gecl('wrap');if(aa.length>0){aa[0].insertBefore(dd,aa[0].firstChild);aa[0].style.position='relative';}}}</script><style type='text/css'>H1,H2{margin-top:0px;margin-bottom:0px;text-align:center;}H3{margin-top:0px;margin-bottom:5px;text-align:center;}button{transition-delay:250ms;margin-top:10px;margin-bottom:10px;font-variant-caps:all-small-caps;border-bottom:0.2em solid #225a98}br{display:block;font-size:1px;content:''}input[type='checkbox']{display:inline-block;margin-top:10px}input{border:thin inset}small{display:none}em > small{display:inline}form{margin-block-end:0;}.tpm{cursor:pointer;border:1px solid black;border-radius:5px;padding:0 0 0 0px;min-width:18em;}.tpm2{position:absolute;top:-0.7em;z-index:130;left:0.7em;}.tpm3{width:4em;height:4em;}.tpmh1{font-variant-caps:all-small-caps;font-weight:normal;margin-left:2.2em;overflow:clip;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI Semibold',Roboto,'Helvetica Neue',Verdana,Helvetica}.tpmh3{background:#000;font-size:0.6em;color:#ffa;padding-left:7.2em;margin-left:0.5em;margin-right:0.5em;border-radius:5px}.tpm0{position:relative;width:20em;padding:5px 0px 5px 0px;margin:0 auto 0 auto;}.cmp0{margin:0;padding:0;}.sel0{font-size:90%;width:auto;margin-left:10px;vertical-align:baseline;}.mt5{margin-top:5px!important}.mb10{margin-bottom:10px!important}.mb0{margin-bottom:0px!important}.mb15{margin-bottom:15px!important}</style>";
static const char* myCustMenu = "<img id='ebnew' style='display:block;margin:10px auto 10px auto;' src='data:image/png;base64,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'><div style='font-size:11px;margin-left:auto;margin-right:auto;text-align:center;'>Version " UNI_VERSION " (" UNI_VERSION_EXTRA ")<br>Powered by A10001986 <a href='https://" WEBHOME ".out-a-ti.me' target=_blank>[Home]</a></div>";

const char menu_myNoSP[] = "<hr><div style='margin-left:auto;margin-right:auto;text-align:center;'>Please <a href='/update'>install</a> sound pack</div><hr>";

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

static File acFile;
static bool haveACFile = false;
static bool haveAC = false;
static int  numUploads = 0;
static int  *ACULerr = NULL;
static int  *opType = NULL;

#ifdef REMOTE_HAVEMQTT
#define       MQTT_SHORT_INT  (30*1000)
#define       MQTT_LONG_INT   (5*60*1000)
static const char emptyStr[1] = { 0 };
bool          useMQTT = false;
char          *mqttUser = (char *)emptyStr;
char          *mqttPass = (char *)emptyStr;
char          *mqttServer = (char *)emptyStr;
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
static void preSaveWiFiCallback();
static void saveWiFiCallback(const char *ssid, const char *pass);
static void preUpdateCallback();
static void postUpdateCallback(bool);
static void eraseCallback(bool);
static int  menuOutLenCallback();
static void menuOutCallback(String& page);
static void wifiDelayReplacement(unsigned int mydel);
static void gpCallback(int);
static bool preWiFiScanCallback();

static void setupStaticIP();
static void ipToString(char *str, IPAddress ip);
static IPAddress stringToIp(char *str);

static void getParam(String name, char *destBuf, size_t length, int defaultVal);
static bool myisspace(char mychar);
static char* strcpytrim(char* destination, const char* source, bool doFilter = false);
static void mystrcpy(char *sv, WiFiManagerParameter *el);
static void strcpyCB(char *sv, WiFiManagerParameter *el);
static void setCBVal(WiFiManagerParameter *el, char *sv);

#ifdef HAVE_PM
static void buildSelectMenu(char *target, const char **theHTML, int cnt, char *setting);
#endif

static void setupWebServerCallback();
static void handleUploadDone();
static void handleUploading();
static void handleUploadDone();

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
    int temp;

    WiFiManagerParameter *wifiParmArray[] = {

      &custom_sectstart_head, 
      &custom_hostName,

      &custom_sectstart_wifi,
      &custom_wifiConRetries, 
      &custom_wifiConTimeout, 

      &custom_sectstart_ap,
      &custom_sysID,
      &custom_appw,
      &custom_apch,

      &custom_sectend_foot,

      NULL
      
    };

    WiFiManagerParameter *parmArray[] = {

      &custom_aood,

      &custom_sectstart_head,// 3
      &custom_at,
      &custom_coast,
      &custom_oott,
      &custom_sStrict,
      &custom_playclick,
      &custom_playALSnd,
      &custom_dGPS,
      &custom_Bri,
  
      &custom_sectstart,     // 2 (3)
      #ifdef REMOTE_HAVEVOLKNOB
      &custom_FixV,
      #endif
      &custom_Vol,
  
      &custom_sectstart_mp,  // 3
      &custom_musicFolder,
      &custom_shuffle,
  
      &custom_sectstart_nw,  // 2
      &custom_tcdIP,    
  
      #ifdef REMOTE_HAVEMQTT
      &custom_sectstart,     // 28
      &custom_useMQTT,
      &custom_mqttServer,
      &custom_mqttUser,
      &custom_mqttb1t,
      &custom_mqttb1o,
      &custom_mqttb1f,
      &custom_mqttb2t,
      &custom_mqttb2o,
      &custom_mqttb2f,
      &custom_mqttb3t,
      &custom_mqttb3o,
      &custom_mqttb3f,
      &custom_mqttb4t,
      &custom_mqttb4o,
      &custom_mqttb4f,
      &custom_mqttb5t,
      &custom_mqttb5o,
      &custom_mqttb5f,
      &custom_mqttb6t,
      &custom_mqttb6o,
      &custom_mqttb6f,
      &custom_mqttb7t,
      &custom_mqttb7o,
      &custom_mqttb7f,
      &custom_mqttb8t,
      &custom_mqttb8o,
      &custom_mqttb8f,
      #endif
  
      &custom_sectstart,     // 2 (3)
      &custom_CfgOnSD,
      //&custom_sdFrq,
  
      &custom_sectstart_hw,  // 10
      #ifdef ALLOW_DIS_UB
      &custom_dBP,
      #endif
      &custom_b0mt,
      &custom_b0mtoo,
      &custom_b1mt,
      &custom_b1mtoo,
      &custom_b2mt,
      &custom_b2mtoo,
      &custom_b3mt,
      &custom_b3mtoo,
      &custom_b4mt,
      &custom_b4mtoo,
      &custom_b5mt,
      &custom_b5mtoo,
      &custom_b6mt,
      &custom_b6mtoo,
      &custom_b7mt,
      &custom_b7mtoo,
  
      &custom_sectstart,
      &custom_uPL,
      &custom_uLM,
      &custom_PLD,

      NULL
    };

    #ifndef REMOTE_DBG
    wm.setDebugOutput(false);
    #endif

    // Transition from NVS-saved data to own management:
    if(!settings.ssid[0] && settings.ssid[1] == 'X') {
        
        // Read NVS-stored WiFi data
        wm.getStoredCredentials(settings.ssid, sizeof(settings.ssid) - 1, settings.pass, sizeof(settings.pass) - 1);

        #ifdef REMOTE_DBG
        Serial.printf("WiFi Transition: ssid '%s' pass '%s'\n", settings.ssid, settings.pass);
        #endif

        write_settings();
    }

    wm.setHostname(settings.hostName);

    wm.showUploadContainer(true, AA_CONTAINER);
    
    wm.setPreSaveWiFiCallback(preSaveWiFiCallback);
    wm.setSaveWiFiCallback(saveWiFiCallback);
    wm.setSaveParamsCallback(saveParamsCallback);
    wm.setPreOtaUpdateCallback(preUpdateCallback);
    wm.setPostOtaUpdateCallback(postUpdateCallback);
    wm.setWebServerCallback(setupWebServerCallback);
    wm.setEraseCallback(eraseCallback);
    wm.setMenuOutLenCallback(menuOutLenCallback);
    wm.setMenuOutCallback(menuOutCallback);
    wm.setDelayReplacement(wifiDelayReplacement);
    wm.setGPCallback(gpCallback);
    wm.setPreWiFiScanCallback(preWiFiScanCallback);

    // Our style-overrides, the page title
    wm.setCustomHeadElement(myHead);
    wm.setTitle(myTitle);

    // Hack version number into WiFiManager main page
    wm.setCustomMenuHTML(myCustMenu);

    // Static IP info is not saved by WiFiManager,
    // have to do this "manually". Hence ipsettings.
    wm.setShowStaticFields(true);
    wm.setShowDnsFields(true);

    temp = atoi(settings.apChnl);
    if(temp < 0) temp = 0;
    if(temp > 13) temp = 13;
    if(!temp) temp = random(1, 13);
    wm.setWiFiAPChannel(temp);

    temp = atoi(settings.wifiConTimeout);
    if(temp < 7) temp = 7;
    if(temp > 25) temp = 25;
    wm.setConnectTimeout(temp);

    temp = atoi(settings.wifiConRetries);
    if(temp < 1) temp = 1;
    if(temp > 10) temp = 10;
    wm.setConnectRetries(temp);

    wm.setCleanConnect(true);
    //wm.setRemoveDuplicateAPs(false);

    wm.setMenu(wifiMenu, TC_MENUSIZE);

    temp = (sizeof(parmArray) / sizeof(WiFiManagerParameter *)) - 1;
    #ifdef HAVE_PM
    if(havePwrMon) {
        temp += 4;
    }
    #endif
    temp++;
    wm.allocParms(temp);

    temp = haveAudioFiles ? 1 : 0;
    while(parmArray[temp]) {
        wm.addParameter(parmArray[temp]);
        temp++;
    }
    
    #ifdef HAVE_PM
    if(havePwrMon) {
        wm.addParameter(&custom_sectstart);
        wm.addParameter(&custom_UPM);
        wm.addParameter(&custom_bty);
        wm.addParameter(&custom_bca);
    }
    #endif
   
    wm.addParameter(&custom_sectend_foot);  // 1

    wm.allocWiFiParms((sizeof(wifiParmArray) / sizeof(WiFiManagerParameter *)) - 1);

    temp = 0;
    while(wifiParmArray[temp]) {
        wm.addWiFiParameter(wifiParmArray[temp]);
        temp++;
    }

    updateConfigPortalValues();

    #ifdef REMOTE_HAVEMQTT
    useMQTT = (atoi(settings.useMQTT) > 0);
    #endif

    // See if we have a configured WiFi network to connect to.
    // If we detect "TCD-AP" as the SSID, we make sure that we retry
    // at least 2 times so we have a chance to catch the TCD's AP if 
    // both are powered up at the same time.
    if(settings.ssid[0] != 0) {
        wifiHaveSTAConf = true;
        if(!strncmp("TCD-AP", settings.ssid, 6)) {
            if(wm.getConnectRetries() < 2) {
                wm.setConnectRetries(2);
            }
            #ifdef REMOTE_HAVEMQTT
            useMQTT = false;
            #endif
        }      
    } else {
        // No point in retry when we have no WiFi config'd
        wm.setConnectRetries(1);
    }

    // No WiFi powersave features here
    wifiOffDelay = 0;
    wifiAPOffDelay = 0;
    
    // Configure static IP
    if(loadIpSettings()) {
        setupStaticIP();
    }
    
    wifi_setup2();
}

void wifi_setup2()
{
    // Connect, but defer starting the CP
    wifiConnect(true);

    #ifdef REMOTE_MDNS
    if(MDNS.begin(settings.hostName)) {
        MDNS.addService("http", "tcp", 80);
    }
    #endif

#ifdef REMOTE_HAVEMQTT
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
            size_t ts = (t - settings.mqttServer) + 1;
            mqttServer = (char *)malloc(ts);
            memset(mqttServer, 0, ts);
            strncpy(mqttServer, settings.mqttServer, t - settings.mqttServer);
            tt = atoi(t + 1);
            if(tt > 0 && tt <= 65535) {
                mqttPort = tt;
            }
        } else {
            mqttServer = settings.mqttServer;
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
                size_t ts = strlen(settings.mqttUser) + 1;
                mqttUser = (char *)malloc(ts);
                strcpy(mqttUser, settings.mqttUser);
                mqttUser[t - settings.mqttUser] = 0;
                mqttPass = mqttUser + (t - settings.mqttUser + 1);
            } else {
                mqttUser = settings.mqttUser;
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

    // Start the Config Portal
    if(WiFi.status() == WL_CONNECTED) {
        wifiStartCP();
    }

    wifiSetupDone = true;
}

static void setBool(char c, bool& b)
{
    if(c == '1') {
        b = true;
    } else if(c == '0') {
        b = false;
    }
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
    
    if(shouldSaveIPConfig) {

        #ifdef REMOTE_DBG
        Serial.println("WiFi: Saving IP config");
        #endif

        mp_stop();
        stopAudio();

        writeIpSettings();

        shouldSaveIPConfig = false;

    } else if(shouldDeleteIPConfig) {

        #ifdef REMOTE_DBG
        Serial.println("WiFi: Deleting IP config");
        #endif

        mp_stop();
        stopAudio();

        deleteIpSettings();

        shouldDeleteIPConfig = false;

    }

    if(shouldSaveConfig) {

        int temp;

        // Save settings and restart esp32

        mp_stop();
        stopAudio();

        #ifdef REMOTE_DBG
        Serial.println("Config Portal: Saving config");
        #endif

        // Only read parms if the user actually clicked SAVE on the wifi config or params pages
        if(shouldSaveConfig == 1) {

            // Parameters on WiFi Config page

            // Note: Parameters that need to grabbed from the server directly
            // through getParam() must be handled in preSaveConfigCallback().

            strcpytrim(settings.hostName, custom_hostName.getValue(), true);
            if(strlen(settings.hostName) == 0) {
                strcpy(settings.hostName, DEF_HOSTNAME);
            } else {
                char *s = settings.hostName;
                for ( ; *s; ++s) *s = tolower(*s);
            }
            mystrcpy(settings.wifiConRetries, &custom_wifiConRetries);
            mystrcpy(settings.wifiConTimeout, &custom_wifiConTimeout);
            
            strcpytrim(settings.systemID, custom_sysID.getValue(), true);
            strcpytrim(settings.appw, custom_appw.getValue(), true);
            if((temp = strlen(settings.appw)) > 0) {
                if(temp < 8) {
                    settings.appw[0] = 0;
                }
            }

        } else { 

            // Parameters on Settings page

            // Save "movieMode" setting
            strcpyCB(settings.movieMode, &custom_sStrict);
            setBool(settings.movieMode[0], movieMode);
            
            // Save "display TCD speed" setting
            strcpyCB(settings.dgps, &custom_dGPS);
            setBool(settings.dgps[0], displayGPSMode);
            
            // Save "AutoThrottle" setting
            strcpyCB(settings.autoThrottle, &custom_at);
            setBool(settings.autoThrottle[0], autoThrottle);
            
            updateVisMode();
            saveVis();

            // Save volume setting
            #ifdef REMOTE_HAVEVOLKNOB
            strcpyCB(settings.FixV, &custom_FixV);
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
            
            // Save brightness setting
            mystrcpy(settings.Bri, &custom_Bri);
            if(strlen(settings.Bri) > 0) {
                temp = atoi(settings.Bri);
                if(temp >= 0 && temp <= 15) {
                    remdisplay.setBrightness(temp);
                    saveBrightness();
                }
            }

            strcpyCB(settings.coast, &custom_coast);
            strcpyCB(settings.ooTT, &custom_oott);
            strcpyCB(settings.playClick, &custom_playclick);
            strcpyCB(settings.playALsnd, &custom_playALSnd);

            strcpyCB(settings.shuffle, &custom_shuffle);

            strcpytrim(settings.tcdIP, custom_tcdIP.getValue());
            if(strlen(settings.tcdIP) > 0) {
                char *s = settings.tcdIP;
                for ( ; *s; ++s) *s = tolower(*s);
            }

            #ifdef REMOTE_HAVEMQTT
            strcpyCB(settings.useMQTT, &custom_useMQTT);
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

            oldCfgOnSD = settings.CfgOnSD[0];
            strcpyCB(settings.CfgOnSD, &custom_CfgOnSD);
            //strcpyCB(settings.sdFreq, &custom_sdFrq);

            #ifdef ALLOW_DIS_UB
            strcpyCB(settings.disBPack, &custom_dBP);
            #endif
            strcpyCB(settings.bPb0Maint, &custom_b0mt);
            strcpyCB(settings.bPb1Maint, &custom_b1mt);
            strcpyCB(settings.bPb2Maint, &custom_b2mt);
            strcpyCB(settings.bPb3Maint, &custom_b3mt);
            strcpyCB(settings.bPb4Maint, &custom_b4mt);
            strcpyCB(settings.bPb5Maint, &custom_b5mt);
            strcpyCB(settings.bPb6Maint, &custom_b6mt);
            strcpyCB(settings.bPb7Maint, &custom_b7mt);

            strcpyCB(settings.bPb0MtO, &custom_b0mtoo);
            strcpyCB(settings.bPb1MtO, &custom_b1mtoo);
            strcpyCB(settings.bPb2MtO, &custom_b2mtoo);
            strcpyCB(settings.bPb3MtO, &custom_b3mtoo);
            strcpyCB(settings.bPb4MtO, &custom_b4mtoo);
            strcpyCB(settings.bPb5MtO, &custom_b5mtoo);
            strcpyCB(settings.bPb6MtO, &custom_b6mtoo);
            strcpyCB(settings.bPb7MtO, &custom_b7mtoo);

            strcpyCB(settings.usePwrLED, &custom_uPL);
            strcpyCB(settings.useLvlMtr, &custom_uLM);
            strcpyCB(settings.pwrLEDonFP, &custom_PLD);
            
            #ifdef HAVE_PM
            if(havePwrMon) {
                strcpyCB(settings.usePwrMon, &custom_UPM);
                getParam("bty", settings.batType, 1, DEF_BAT_TYPE);
                mystrcpy(settings.batCap, &custom_bca);
            }
            #endif

            // Copy secondary settings to other medium if
            // user changed respective option
            if(oldCfgOnSD != settings.CfgOnSD[0]) {
                copySettings();
            }

        }

        // Write settings if requested, or no settings file exists
        if(shouldSaveConfig >= 1 || !checkConfigExists()) {
            write_settings();
        }
        
        shouldSaveConfig = 0;

        // Reset esp32 to load new settings
        
        // Unregister from TCD
        bttfn_remote_unregister();
       
        #ifdef REMOTE_DBG
        Serial.println("Config Portal: Restarting ESP....");
        #endif
        Serial.flush();
        
        prepareReboot();
        delay(500);
        esp_restart();
    }

    // For some reason (probably memory allocation doing some
    // garbage collection), the first HTTPSend (triggered in
    // wm.process()) after initiating mp3 playback takes
    // unusally long, in bad cases 100s of ms. It's worse 
    // closer to playback start.
    // This hack skips Webserver-handling inside wm.process() 
    // if an mp3 playback started within the last 3 seconds.
    wm.process(!checkAudioStarted());

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
                Serial.println("WiFi (AP-mode) is off.");
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
                Serial.println("WiFi (STA-mode) is off.");
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
    if(wm.autoConnect(settings.ssid, settings.pass, realAPName, settings.appw)) {
        #ifdef REMOTE_DBG
        Serial.println("WiFi connected");
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
        Serial.println("Config portal running in AP-mode");
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
    delay(100);   // never called while sound is playing, so ok
    
    // (Re)connect
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

// This is called when the WiFi config is to be saved. We set
// a flag for the loop to read out and save the new WiFi config.
// SSID and password are copied to settings here.
static void saveWiFiCallback(const char *ssid, const char *pass)
{
    // ssid is the (new?) ssid to connect to, pass the password.
    // (We don't need to compare to the old ones since the
    // settings are saved in any case)
    memset(settings.ssid, 0, sizeof(settings.ssid));
    strncpy(settings.ssid, ssid, sizeof(settings.ssid) - 1);
    #ifdef FC_DBG
    Serial.printf("saveWiFiCallback: New ssid '%s'\n", settings.ssid);
    #endif
    
    memset(settings.pass, 0, sizeof(settings.pass));
    strncpy(settings.pass, pass, sizeof(settings.pass) - 1);
    #ifdef FC_DBG
    Serial.printf("saveWiFiCallback: New pass '%s'\n", settings.pass);
    #endif
    
    shouldSaveConfig = 1;
}

// This is the callback from the actual Params page. We read out
// thew WM "Settings" parameters and save them.
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

    remdisplay.blink(false);
    remledStop.setState(false);

    // Unregister from TCD
    bttfn_remote_unregister();

    mp_stop();
    stopAudio();

    flushDelayedSave();

    showWaitSequence();
    remdisplay.on();
}

// This is called after a firmware updated has finished.
// parm = true of ok, false if error. WM reboots only 
// if the update worked, ie when res is true.
static void postUpdateCallback(bool res)
{
    Serial.flush();
    prepareReboot();

    // WM does not reboot on OTA update errors.
    // However, don't bother for that really
    // rare case to put code here to restore
    // under all possible circumstances, like
    // fake-off, time-travel going on, ss, ....
    if(!res) {
        delay(1000);
        esp_restart();
    }
}

// This is called before right rebooting after erasing WiFi config.
static void eraseCallback(bool isPre)
{
    if(isPre) {

        bttfn_remote_unregister();
        // Pre-Erase: Stop sound, erase() writes to NVS
        mp_stop();
        stopAudio();

    } else {

        // Actual Erase: Delete ssid and pass, as well
        // as static IP config, save, and reboot
        
        if(settings.ssid[0] || settings.pass[0]) {
            memset(settings.ssid, 0, sizeof(settings.ssid));
            memset(settings.pass, 0, sizeof(settings.pass));
            write_settings();
        }

        deleteIpSettings();
        
        Serial.flush();
        prepareReboot();
        delay(1000);
        esp_restart();
    }
}

// Grab static IP and other parameters from WiFiManager's server.
// Since there is no public method for this, we steal the HTML
// form parameters in this callback.
static void preSaveWiFiCallback()
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

    String temp;
    temp.reserve(16);
    if((temp = wm.server->arg(FPSTR(WMS_ip))) != "") {
        strncpy(ipBuf, temp.c_str(), 19);
    } else invalConf |= true;
    if((temp = wm.server->arg(FPSTR(WMS_sn))) != "") {
        strncpy(snBuf, temp.c_str(), 19);
    } else invalConf |= true;
    if((temp = wm.server->arg(FPSTR(WMS_gw))) != "") {
        strncpy(gwBuf, temp.c_str(), 19);
    } else invalConf |= true;
    if((temp = wm.server->arg(FPSTR(WMS_dns))) != "") {
        strncpy(dnsBuf, temp.c_str(), 19);
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

        shouldSaveIPConfig = (strcmp(ipsettings.ip, ipBuf)      ||
                              strcmp(ipsettings.gateway, gwBuf) ||
                              strcmp(ipsettings.netmask, snBuf) ||
                              strcmp(ipsettings.dns, dnsBuf));
          
        if(shouldSaveIPConfig) {
            strcpy(ipsettings.ip, ipBuf);
            strcpy(ipsettings.gateway, gwBuf);
            strcpy(ipsettings.netmask, snBuf);
            strcpy(ipsettings.dns, dnsBuf);
        }

    } else {

        #ifdef REMOTE_DBG
        if(strlen(ipBuf) > 0) {
            Serial.println("Invalid IP");
        }
        #endif

        shouldDeleteIPConfig = true;

    }

    // Other parameters on WiFi Config page that
    // need grabbing directly from the server

    getParam("apchnl", settings.apChnl, 2, DEF_AP_CHANNEL);
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

static int menuOutLenCallback()
{
    int mySize = 0;

    if(!haveAudioFiles) {
        mySize += STRLEN(menu_myNoSP);
    }
    return mySize;
}

static void menuOutCallback(String& page)
{       
    if(!haveAudioFiles) {
        page += menu_myNoSP;
    }
}

static bool preWiFiScanCallback()
{
    // Do not allow a WiFi scan under some circumstances (as
    // it may disrupt sequences)
    
    if(TTrunning || tcdIsInP0 || throttlePos || keepCounting || calibMode)
        return false;

    return true;
}

static void wifiDelayReplacement(unsigned int mydel)
{
    if((mydel > 30) && audioInitDone) {
        unsigned long startNow = millis();
        while(millis() - startNow < mydel) {
            audio_loop();
            delay(20);
        }
    } else {
        delay(mydel);
    }
}

void gpCallback(int reason)
{
    // Called when WM does stuff that might
    // take some time, like before and after
    // HTTPSend().
    // MUST NOT call wifi_loop() !!!
    
    if(audioInitDone) {
        switch(reason) {
        case WM_LP_PREHTTPSEND:
            if(wifiInAPMode) {
                if(checkMP3Running()) {
                    mp_stop();
                    stopAudio();
                    return;
                }
            } // fall through
        case WM_LP_NONE:
        case WM_LP_POSTHTTPSEND:
            audio_loop();
            yield();
            break;
        }
    }
}

void updateConfigPortalValues()
{
    // Make sure the settings form has the correct values

    custom_hostName.setValue(settings.hostName, 31);
    custom_wifiConTimeout.setValue(settings.wifiConTimeout, 2);
    custom_wifiConRetries.setValue(settings.wifiConRetries, 2);

    custom_sysID.setValue(settings.systemID, 7);
    // ap channel done on-the-fly
    custom_appw.setValue(settings.appw, 8);

    setCBVal(&custom_coast, settings.coast);
    setCBVal(&custom_oott, settings.ooTT);
    setCBVal(&custom_playclick, settings.playClick);
    setCBVal(&custom_playALSnd, settings.playALsnd);

    setCBVal(&custom_shuffle, settings.shuffle);
    
    custom_tcdIP.setValue(settings.tcdIP, 31);

    #ifdef REMOTE_HAVEMQTT
    setCBVal(&custom_useMQTT, settings.useMQTT);
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

    setCBVal(&custom_CfgOnSD, settings.CfgOnSD);
    //setCBVal(&custom_sdFrq, settings.sdFreq);

    #ifdef ALLOW_DIS_UB
    setCBVal(&custom_dBP, settings.disBPack);
    #endif
    
    setCBVal(&custom_b0mt, settings.bPb0Maint);
    setCBVal(&custom_b1mt, settings.bPb1Maint);
    setCBVal(&custom_b2mt, settings.bPb2Maint);
    setCBVal(&custom_b3mt, settings.bPb3Maint);
    setCBVal(&custom_b4mt, settings.bPb4Maint);
    setCBVal(&custom_b5mt, settings.bPb5Maint);
    setCBVal(&custom_b6mt, settings.bPb6Maint);
    setCBVal(&custom_b7mt, settings.bPb7Maint);

    setCBVal(&custom_b0mtoo, settings.bPb0MtO);
    setCBVal(&custom_b1mtoo, settings.bPb1MtO);
    setCBVal(&custom_b2mtoo, settings.bPb2MtO);
    setCBVal(&custom_b3mtoo, settings.bPb3MtO);
    setCBVal(&custom_b4mtoo, settings.bPb4MtO);
    setCBVal(&custom_b5mtoo, settings.bPb5MtO);
    setCBVal(&custom_b6mtoo, settings.bPb6MtO);
    setCBVal(&custom_b7mtoo, settings.bPb7MtO);

    setCBVal(&custom_uPL, settings.usePwrLED);
    setCBVal(&custom_uLM, settings.useLvlMtr);
    setCBVal(&custom_PLD, settings.pwrLEDonFP);

    #ifdef HAVE_PM
    if(havePwrMon) {
        setCBVal(&custom_UPM, settings.usePwrMon);
        // Bat type done on-the-fly
        custom_bca.setValue(settings.batCap, 5);
    }
    #endif
}

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
    setCBVal(&custom_FixV, settings.FixV);
    #endif
    
    custom_Vol.setValue(settings.Vol, 2);

    updateConfigPortalVisValues();
}

void updateConfigPortalMFValues()
{
    sprintf(settings.musicFolder, "%d", musFolderNum);
    custom_musicFolder.setValue(settings.musicFolder, 2);
}

void updateConfigPortalBriValues()
{
    sprintf(settings.Bri, "%d", remdisplay.getBrightness());
    custom_Bri.setValue(settings.Bri, 2);
}

void updateConfigPortalVisValues()
{
    strcpy(settings.movieMode, movieMode ? "1" : "0");
    setCBVal(&custom_sStrict, settings.movieMode);
    
    strcpy(settings.dgps, displayGPSMode ? "1" : "0");
    setCBVal(&custom_dGPS, settings.dgps);
    
    strcpy(settings.autoThrottle, autoThrottle ? "1" : "0");
    setCBVal(&custom_at, settings.autoThrottle);
}

static void buildSelectMenu(char *target, const char **theHTML, int cnt, char *setting)
{
    int sr = atoi(setting);
    
    strcpy(target, theHTML[0]);
    strcat(target, setting);
    sprintf(target + strlen(target), "' name='%s' id='%s' autocomplete='off'><option value='0'", theHTML[1], theHTML[1]);
    for(int i = 0; i < cnt - 2; i++) {
        if(sr == i) strcat(target, custHTMLSel);
        sprintf(target + strlen(target), 
            theHTML[i+2], (i == cnt - 3) ? osde : ooe);
    }
}

#ifdef HAVE_PM
static const char *wmBuildBatType(const char *dest)
{
    if(dest) {
      free((void *)dest);
      return NULL;
    }
    
    char *str = (char *)malloc(512);    // actual length ?

    buildSelectMenu(str, batTypeHTMLSrc, 7, settings.batType);
    
    return str;
}
#endif

static const char *wmBuildApChnl(const char *dest)
{
    if(dest) {
      free((void *)dest);
      return NULL;
    }
    
    char *str = (char *)malloc(600);    // actual length 564

    str[0] = 0;
    buildSelectMenu(str, apChannelCustHTMLSrc, 16, settings.apChnl);
    
    return str;
}

/*
 * Audio data uploader
 */

static void doReboot()
{
    delay(1000);
    prepareReboot();
    delay(500);
    esp_restart();
}

static void allocUplArrays()
{
    if(opType) free((void *)opType);
    opType = (int *)malloc(MAX_SIM_UPLOADS * sizeof(int));
    if(ACULerr) free((void *)ACULerr);
    ACULerr = (int *)malloc(MAX_SIM_UPLOADS * sizeof(int));;
    memset(opType, 0, MAX_SIM_UPLOADS * sizeof(int));
    memset(ACULerr, 0, MAX_SIM_UPLOADS * sizeof(int));
}

static void setupWebServerCallback()
{
    wm.server->on(WM_G(R_updateacdone), HTTP_POST, &handleUploadDone, &handleUploading);
}

static void doCloseACFile(int idx, bool doRemove)
{
    if(haveACFile) {
        closeACFile(acFile);
        haveACFile = false;
    }
    if(doRemove) removeACFile(idx);  
}

static void handleUploading()
{
    HTTPUpload& upload = wm.server->upload();

    if(upload.status == UPLOAD_FILE_START) {

          String c = upload.filename;
          const char *illChrs = "|~><:*?\" ";
          int temp;
          char tempc;

          if(numUploads >= MAX_SIM_UPLOADS) {
            
              haveACFile = false;

          } else {

              c.toLowerCase();
    
              // Remove path and some illegal characters
              tempc = '/';
              for(int i = 0; i < 2; i++) {
                  if((temp = c.lastIndexOf(tempc)) >= 0) {
                      if(c.length() - 1 > temp) {
                          c = c.substring(temp);
                      } else {
                          c = "";
                      }
                      break;
                  }
                  tempc = '\\';
              }
              for(int i = 0; i < strlen(illChrs); i++) {
                  c.replace(illChrs[i], '_');
              }
              if(!c.indexOf("..")) {
                  c.replace("..", "");
              }
    
              if(!numUploads) {
                  allocUplArrays();
                  preUpdateCallback();
              }
    
              haveACFile = openUploadFile(c, acFile, numUploads, haveAC, opType[numUploads], ACULerr[numUploads]);

              if(haveACFile && opType[numUploads] == 1) {
                  haveAC = true;
              }

          }

    } else if(upload.status == UPLOAD_FILE_WRITE) {

          if(haveACFile) {
              if(writeACFile(acFile, upload.buf, upload.currentSize) != upload.currentSize) {
                  doCloseACFile(numUploads, true);
                  ACULerr[numUploads] = UPL_WRERR;
              }
          }

    } else if(upload.status == UPLOAD_FILE_END) {

        if(numUploads < MAX_SIM_UPLOADS) {

            doCloseACFile(numUploads, false);
    
            if(opType[numUploads] >= 0) {
                renameUploadFile(numUploads);
            }
    
            numUploads++;

        }
      
    } else if(upload.status == UPLOAD_FILE_ABORTED) {

        if(numUploads < MAX_SIM_UPLOADS) {
            doCloseACFile(numUploads, true);
        }

        doReboot();

    }

    delay(0);
}

static void handleUploadDone()
{
    const char *ebuf = "ERROR";
    const char *dbuf = "DONE";
    char *buf = NULL;
    bool haveErrs = false;
    bool haveAC = false;
    int titStart = -1;
    int buflen  = strlen(wm.getHTTPSTART(titStart)) +
                  STRLEN(myTitle)    +
                  strlen(wm.getHTTPSCRIPT()) +
                  strlen(wm.getHTTPSTYLE()) +
                  STRLEN(myHead)     +
                  STRLEN(acul_part3) +
                  STRLEN(myTitle)    +
                  STRLEN(acul_part5) +
                  STRLEN(apName)     +
                  STRLEN(acul_part6) +
                  STRLEN(acul_part8) +
                  1;

    for(int i = 0; i < numUploads; i++) {
        if(opType[i] > 0) {
            haveAC = true;
            if(!ACULerr[i]) {
                if(!check_if_default_audio_present()) {
                    haveAC = false;
                    ACULerr[i] = UPL_BADERR;
                    removeACFile(i);
                }
            }
            break;
        }
    }    

    if(!haveSD && numUploads) {
      
        buflen += (STRLEN(acul_part71) + strlen(acul_errs[1]));
        
    } else {

        for(int i = 0; i < numUploads; i++) {
            if(ACULerr[i]) haveErrs = true;
        }
        if(haveErrs) {
            buflen += STRLEN(acul_part71);
            for(int i = 0; i < numUploads; i++) {
                if(ACULerr[i]) {
                    buflen += getUploadFileNameLen(i);
                    buflen += 2; // :_
                    buflen += strlen(acul_errs[ACULerr[i]-1]);
                    buflen += 4; // <br>
                }
            }
        } else {
            buflen += STRLEN(acul_part7);
        }
        if(haveAC) {
            buflen += STRLEN(acul_part7a);
        }
    }

    buflen += 8;

    if(!(buf = (char *)malloc(buflen))) {
        buf = (char *)(haveErrs ? ebuf : dbuf);
    } else {
        strcpy(buf, wm.getHTTPSTART(titStart));
        if(titStart >= 0) {
            strcpy(buf + titStart, myTitle);
        }
        strcat(buf, "</title>");
        strcat(buf, wm.getHTTPSCRIPT());
        strcat(buf, wm.getHTTPSTYLE());
        strcat(buf, myHead);
        strcat(buf, acul_part3);
        strcat(buf, myTitle);
        strcat(buf, acul_part5);
        strcat(buf, apName);
        strcat(buf, acul_part6);

        if(!haveSD && numUploads) {

            strcat(buf, acul_part71);
            strcat(buf, acul_errs[1]);
            
        } else {
            
            if(haveErrs) {
                strcat(buf, acul_part71);
                for(int i = 0; i < numUploads; i++) {
                    if(ACULerr[i]) {
                        char *t = getUploadFileName(i);
                        if(t) {
                            strcat(buf, t);
                        }
                        strcat(buf, ": ");
                        strcat(buf, acul_errs[ACULerr[i]-1]);
                        strcat(buf, "<br>");
                    }
                }
            } else {
                strcat(buf, acul_part7);
            }
            if(haveAC) {
                strcat(buf, acul_part7a);
            }
        }

        strcat(buf, acul_part8);
    }

    freeUploadFileNames();
    /* Pointless, we reboot
    numUploads = 0;
    for(int i = 0; i < MAX_SIM_UPLOADS; i++) {
        opType[i] = 0;
        ACULerr[i] = 0;
    }
    */
    
    String str(buf);
    wm.server->send(200, "text/html", str);

    // Reboot required even for mp3 upload, because for most files, we check
    // during boot if they exist (to avoid repeatedly failing open() calls)

    doReboot();
}

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

static void getParam(String name, char *destBuf, size_t length, int defaultVal)
{
    memset(destBuf, 0, length+1);
    if(wm.server->hasArg(name)) {
        strncpy(destBuf, wm.server->arg(name).c_str(), length);
    }
    if(strlen(destBuf) == 0) {
        sprintf(destBuf, "%d", defaultVal);
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

static void strcpyCB(char *sv, WiFiManagerParameter *el)
{
    strcpy(sv, (atoi(el->getValue()) > 0) ? "1" : "0");
}

static void setCBVal(WiFiManagerParameter *el, char *sv)
{
    const char makeCheck[] = "1' checked a='";
    
    el->setValue((atoi(sv) > 0) ? makeCheck : "1", 14);
}

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
      "MP_SHUFFLE_ON",    // 5 
      "MP_SHUFFLE_OFF",   // 6
      "MP_PLAY",          // 7
      "MP_STOP",          // 8
      "MP_NEXT",          // 9
      "MP_PREV",          // 10
      "MP_FOLDER_",       // 11  MP_FOLDER_0..MP_FOLDER_9
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
