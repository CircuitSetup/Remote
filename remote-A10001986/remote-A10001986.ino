/*
 * -------------------------------------------------------------------
 * Remote Control
 * (C) 2024 Thomas Winischhofer (A10001986)
 * https://github.com/realA10001986/Remote
 * https://remote.out-a-ti.me
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

/*
 * Build instructions (for Arduino IDE)
 * 
 * - Install the Arduino IDE
 *   https://www.arduino.cc/en/software
 *    
 * - This firmware requires the "ESP32-Arduino" framework. To install this framework, 
 *   in the Arduino IDE, go to "File" > "Preferences" and add the URL   
 *   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
 *   - or (if the URL above does not work) -
 *   https://espressif.github.io/arduino-esp32/package_esp32_index.json
 *   to "Additional Boards Manager URLs". The list is comma-separated.
 *   
 * - Go to "Tools" > "Board" > "Boards Manager", then search for "esp32", and install 
 *   the latest 2.x version by Espressif Systems. Versions >=3.x are not supported.
 *   Detailed instructions for this step:
 *   https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html
 *   
 * - Go to "Tools" > "Board: ..." -> "ESP32 Arduino" and select your board model (the
 *   CircuitSetup original boards are "NodeMCU-32S")
 *   
 * - Connect your ESP32 board using a suitable USB cable.
 *   Note that NodeMCU ESP32 boards come in two flavors that differ in which serial 
 *   communications chip is used: Either SLAB CP210x USB-to-UART or CH340. Installing
 *   a driver might be required.
 *   Mac: 
 *   For the SLAB CP210x (which is used by NodeMCU-boards distributed by CircuitSetup)
 *   installing a driver is required:
 *   https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers?tab=downloads
 *   The port ("Tools -> "Port") is named /dev/cu.SLAB_USBtoUART, and the maximum
 *   upload speed ("Tools" -> "Upload Speed") can be used.
 *   The CH340 is supported out-of-the-box since Mojave. The port is named 
 *   /dev/cu.usbserial-XXXX (XXXX being some random number), and the maximum upload 
 *   speed is 460800.
 *   Windows:
 *   For the SLAB CP210x (which is used by NodeMCU-boards distributed by CircuitSetup)
 *   installing a driver is required:
 *   https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers?tab=downloads
 *   After installing this driver, connect your ESP32, start the Device Manager, 
 *   expand the "Ports (COM & LPT)" list and look for the port with the ESP32 name.
 *   Choose this port under "Tools" -> "Port" in Arduino IDE.
 *   For the CH340, another driver is needed. Try connecting the ESP32 and have
 *   Windows install a driver automatically; otherwise search google for a suitable
 *   driver. Note that the maximum upload speed is either 115200, or perhaps 460800.
 *
 * - Install required libraries. In the Arduino IDE, go to "Tools" -> "Manage Libraries" 
 *   and install the following libraries:
 *   - ArduinoJSON (>= 6.19): https://arduinojson.org/v6/doc/installation/
 *
 * - Download the complete firmware source code:
 *   https://github.com/realA10001986/Remote/archive/refs/heads/main.zip
 *   Extract this file somewhere. Enter the "remote-A10001986" folder and 
 *   double-click on "renote-A10001986.ino". This opens the firmware in the
 *   Arduino IDE.
 *
 * - Go to "Sketch" -> "Upload" to compile and upload the firmware to your ESP32 board.
 *
 * - Install the audio data (only applicable if hardware has audio support): 
 *   Method 1:
 *   - Go to Config Portal, click "Update" and upload the audio data (REMA.bin, extracted
 *     from install/sound-pack-xxxxxxxx.zip) through the bottom file selector.
 *     A FAT32 (not ExFAT!) formatted SD card must be present in the slot during this 
 *     operation.
 *   Method 2:
 *   - Copy REMA.bin to the top folder of a FAT32 (not ExFAT!) formatted SD card (max 
 *     32GB) and put this card into the slot while the remote is powered down. 
 *   - Now power-up. The audio data will now be installed. When finished, the remote will 
 *     reboot.
 *     
 *  Calibration:
 *  
 *  The throttle of the Remote needs calibration. Calibration is only possible when the
 *  "fake power" of the Remote is in OFF position.
 *  - Put the throttle stick in neutral (center) position, press the Calibration button.
 *    The display will briefly show "CAL" to show acceptance.
 *  - Hold the Calibration button for >= 2 seconds, "UP" will be displayed. Now push
 *    the throttle to the up-most position, and press the Calibration button.
 *    "DN" will be displayed.
 *  - Pull the throttle stick to the bottom-most position, and press the Calibration
 *    button. The display will clear, calibration is finished.
 *  
 *  When (really, not fake) powering up the Remote, the throttle stick should be in 
 *  Center (neutral) position when using a rotary encoder. If it was not, press 
 *  Calibration after booting, while fake power is off.
 *  
 */

/*  Changelog
 *  
 *  2024/10/26 (A10001986) [0.90]
 *    - Add support for TCD multicast notifications: This brings more immediate speed 
 *      updates (no more polling; TCD sends out speed info when appropriate), and 
 *      less network traffic in time travel sequences.
 *  2024/10/24 (A10001986)
 *    - Allow triggering BTTFN-wide TT via O.O followed by throttle-up; button function
 *      controlled by option (TT vs. MP/prev.song)
 *  2024/10/23 (A10001986)
 *    - Add sound played upon volume change (new sound pack)
 *  2024/10/10 (A10001986)
 *    - CB 1.5: Support for Futaba Battery Level Meter connector.
 *    - Add options for power LED and batt level meter usage
 *    - Add option to enable power LED and level meter on either fake or real power
 *    - Add "auto throttle": If enabled, an acceleration will continue when the throttle
 *      is released into neutral, until either 88mps is reached, or throttle is pulled
 *      below neutral.
 *  2024/10/05 (A10001986)
 *    - Buttons 1-8 can now be configured as maintained AND audio-on-ON-only. This
 *      supports use of three-position ON-OFF-(ON) switches, without audio restarted
 *      on OFF when flipping the switch from ON to (ON).
 *  2024/10/02 (A10001986)
 *    - New logic for button/switch 1-8 triggered sound playback
 *  2024/10/01 (A10001986)
 *    - Fix display.setText for "packed" display types
 *  2024/09/30 (A10001986)
 *    - Do not trigger events on initial position of maintained switches 1-8
 *  2024/09/28 (A10001986)
 *    - Switch i2c speed from 100kHz to 400kHz
 *    - Properly truncate UTF8 strings (MQTT user/topics/messages) if beyond buffer
 *      size. (Browsers' 'maxlength' is in characters, buffers are in bytes.
 *      MQTT is generally UTF8, and WiFiManager treats maxlength=buffer size,
 *      something to deal with at some point.)
 *  2024/09/27 (A10001986)
 *    - MQTT: Send user-configured messages to user-configured topics on
 *      buttons' 1-8 press/unpress events.
 *  2024/09/24 (A10001986)
 *    - Increase number of user buttons from 6 to 8 (CB 1.3)
 *  2024/09/23 (A10001986)
 *    - Re-add power LED (for CB 1.3, and people who want to use the Remote's
 *      native power LED, for instance)
 *    - Error check for calib procedure; store calib data as int, not float
 *  2024/09/21 (A10001986)
 *    - Minor modifications to display code
 *    - Remove redundancies, remove display of "." at boot
 *  2024/09/17 (A10001986)
 *    - Add "startup sequence" (counting from 0.0 to 11.0) and a power-up sound
 *    - Put "click" sound to lowest prio so that when another sound is played
 *      the click is suppressed.
 *  2024/09/16 (A10001986)
 *    - Fix PCA9554(A) reading
 *    - Switch to Dev Board 1.0 (still with prototype display)
 *  2024/09/14 (A10001986)
 *    - Rewrite the throttle control/acceleration logic
 *    - Speed up display of speed
 *  2024/09/13 (A10001986)
 *    - Put click sound into progmem
 *    - Set min accelDelay to 200ms/mph for linear acceleration
 *    - Add "movie-mode" setting to adapt accel pace (mostly) to movie.
 *      Movie times:
 *       0- 7:  90ms/mph
 *      20-24: 197ms/mph
 *      32-39: 200ms/mph
 *      55-59: 220ms/mph
 *      77-81: 300ms/mph
 *    - Add "display TCD speed" to CP and save it
 *  2024/09/12 (A10001986)
 *    - Add accleration "click" sound
 *    - Add default sounds for brake-on and throttle-up (from 0)
 *    - Some cleanup
 *  2024/09/11 (A10001986)
 *    - Fix C99-compliance
 *    - Fake .10ths while in TCD-controlled P0 mode
 *  2024/09/10 (A10001986)
 *    - Add coasting feature (off by default)
 *    - TCD overrules Remote during P0. So if a button-triggered TT occurs, the 
 *      Remote shows the speed the TCD displays on the speedo. For logical reasons,
 *      the brake is ignored in that scenario.
 *  2024/09/09 (A10001986)
 *    - Play sounds on fake-power on/off (poweron.mp3, poweroff.mp3), and on brake 
 *      hold/release (brakeon.mp3, brakeoff.mp3).
 *  2024/09/08 (A10001986)
 *    - Add feature to display GPS speed from TCD when Remote is fake-powered off
 *    - ButtonA = "O.O", button B = "RESET"
 *    - New procedure to reset WiFi AP password and fixed IP
 *  2024/09/07 (A10001986)
 *    - Save throttle Zero position when using an ADC
 *    - Fix throttle direction if sign of reading inversed
 *    - Add first version of TT sequence
 *    - Scale throttle when using an ADC
 *    - Buttons 1-6 can also be a maintained switch; in this case, no "hold" action 
 *      is possible.
 *    - Cleanup
 *  2024/09/04 (A10001986)
 *    - Hardware Spec changes:
 *      - Brake: Switch and LED hardwired (daisy chained), active high
 *      - Buttons A and B: Changed from act-high to act-low. Now all buttons are 
 *        active-low and can be wired with a "common" GND.
 *      - Removed LEDs for fake power and calibration
 *      - Use PCA9554 instead of PCA8574
 *    - Add support for PCA9554(A). If 8574 is connected, it needs to have i2c-A0 set in 
 *      order to be correctly recognized. PCA9554 all Ax -> GND, default i2c address used.
 *  2024/09/01 (A10001986)
 *    - Add support for PCA8574(A)-based button pack: Up to 8 add'l buttons for audio.
 *  2024/08/31 (A10001986)
 *    - Reduce command set (use only COMBINED)
 *    - Added WiFi-reconnect on fake-power-on if connection to configured WiFi network 
 *      failed
 *    - Fix latency calculation
 *    - Add buttons A and B, used for setup stuff when fake-powered down, for audio
 *      when fake-powered-up.
 *  2024/08/30 (A10001986)
 *    - Added preliminary ADS1015 ADC support (untested). i2c ADCs are handled in the
 *      RotEnc class, since their handling is identical.
 *  2024/08/29 (A10001986)
 *    - Add calibration mechanism
 *  2024/08/26 (A10001986)
 *    - Initial version (with RotEnc support for throttle)
 *
 */

#include "remote_global.h"

#include <Arduino.h>
#include <Wire.h>

#include "display.h"
#include "remote_audio.h"
#include "remote_settings.h"
#include "remote_main.h"
#include "remote_wifi.h"

void setup()
{
    powerupMillis = millis();

    Serial.begin(115200);
    Serial.println();

    // I2C init
    Wire.begin(-1, -1, 400000);

    main_boot();
    settings_setup();
    main_boot2();
    wifi_setup();
    audio_setup();
    main_setup();
}

void loop()
{
    audio_loop();
    main_loop();
    audio_loop();
    wifi_loop();
    audio_loop();
    bttfn_loop();
}
