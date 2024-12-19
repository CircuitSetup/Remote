# Firmware for Remote Control (Delorean Time Machine)

This [repository](https://remote.out-a-ti.me) holds the most current firmware for a Futaba remote control kit for use in combination with a CircuitSetup [Time Circuits Display](https://tcd.out-a-ti.me). This kit, which is not available for purchase yet, might consist of the grey box (Unibox), a control board, light and switch for "Stop" and the LED segment display, or a subset of the items listed. 

See [here](FUTABA.md) for information on the hardware. 

A video of an early proof-of-concept is [here](https://www.facebook.com/61553801874212/videos/1047035620236271/) (note that the hardware shown isn't even in prototype stage).

Firmware features:
- [Wireless communication](#connecting-a-time-circuits-display) with [Time Circuits Display](https://tcd.out-a-ti.me); when (fake) powered up by "ON/OFF" switch, the Remote will take over speed control on the TCD. 
- Elevator stick on actual Futaba remote control used for throttle control, like in the movie. The throttle can increase or decrease speed, in five steps. When the speed on the TCD reaches 88mph, a time travel is triggered.
- Optional [coasting](#-coasting-when-throttle-in-neutral), optional [auto-throttle](#-auto-throttle)
- Supports controlling the Futaba's power LED and battery level meter (static display only, no actual battery level display)
- Movie-accurate "Stop" light and stop switch behavior
- Movie-accurate sound effects
- Eight optional "[User Buttons](#user-buttons)" for playback of user-provided sound effects and/or sending user-configurable [Home Assistant/MQTT](#home-assistant--mqtt) messages
- [SD card](#sd-card) support for custom audio files for effects, and music for the Music Player
- [Music player](#the-music-player): Play mp3 files located on an SD card, controlled by the "O.O"/"RESET" buttons or [Time Circuits Display](https://tcd.out-a-ti.me) keypad via BTTFN
- Advanced network-accessible [Config Portal](#the-config-portal) for setup (http://dtmremote.local, hostname configurable)
- [Home Assistant](#home-assistant--mqtt) (MQTT 3.1.1) support
- Smart battery monitoring (M-boards, non-M-boards with battery monitor add-on) for LiPo batteries

## Firmware Installation

If a previous version of the Remote firmware is installed on your device, you can update easily using the pre-compiled binary. Enter the [Config Portal](#the-config-portal), click on "Update" and select the pre-compiled binary file provided in this repository ([install/remote-A10001986.ino.nodemcu-32s.bin](https://github.com/realA10001986/Remote/blob/main/install/remote-A10001986.ino.nodemcu-32s.bin)). 

If you are using a fresh ESP32 board, please see [remote-A10001986.ino](https://github.com/realA10001986/Remote/blob/main/remote-A10001986/remote-A10001986.ino) for detailed build and upload information, or, if you don't want to deal with source code, compilers and all that nerd stuff, go [here](https://install.out-a-ti.me) and follow the instructions.

### Audio data installation

The firmware comes with some audio data ("sound-pack") which needs to be installed separately. The audio data is not updated as often as the firmware itself. If you have previously installed the latest version of the sound-pack, you normally don't have to re-install the audio data when you update the firmware. Only if the Remote displays "AUD" briefly during boot, an update of the audio data is needed.

The first step is to download "install/sound-pack-xxxxxxxx.zip" and extract it. It contains one file named "REMA.bin".

Then there are two alternative ways to proceed. Note that both methods *require an SD card*.

1) Through the [Config Portal](#the-config-portal). Click on *Update*, select the "REMA.bin" file in the bottom file selector and click on *Upload*. Note that an SD card must be in the slot during this operation.

2) Via SD card:
- Copy "REMA.bin" to the root directory of of a FAT32 formatted SD card;
- power down the Remote,
- insert this SD card into the slot and 
- power up the Remote; the audio data will be installed automatically.

After installation, the SD card can be re-used for [other purposes](#sd-card).

## Initial Configuration

>The following instructions only need to be followed once, on fresh Remotes. They do not need to be repeated after a firmware update.

The first step is to establish access to the Remote's configuration web site ("Config Portal") in order to configure your device:

- Power up the Remote and wait until it has finished booting.
- Connect your computer or handheld device to the WiFi network "REM-AP".
- Navigate your browser to http://drmremote.local or http://192.168.4.1 to enter the Config Portal.

#### Connecting to a WiFi network

As long as the device is unconfigured, it creates a WiFi network of its own named "REM-AP". This is called "Access point mode", or "AP-mode". In this mode, other WiFi devices can connect to the Remote.

It is ok to leave the Remote in this mode if it run stand-alone. Typically, you'll use the Remote together with a [Time Circuits Display](https://tcd.out-a-ti.me). This requires the TCD and the Remote to be in the same WiFi network. If you have your TCD mounted in a car, you might want to connect the Remote to the TCD's very own WiFi network "TCD-AP"; see [here](#car-setup).

In order to connect your Remote to a WiFi network, click on "Configure WiFi". The bare minimum is to select an SSID (WiFi network name) and a WiFi password.

>Note that the device requests an IP address via DHCP, unless you entered valid data in the fields for static IP addresses (IP, gateway, netmask, DNS). If the device is inaccessible as a result of incorrect static IPs, 
>- power-down the device,
>- hold the Calibration button,
>- power-up the device (while still holding the Calibration button)
>- wait until the displays shows a circle animation,
>- press Button "O.O" twice within 10 seconds,
>- wait until the display shows "RST",
>- then release the Calibration button.
>
>This procedure causes static IP data to be deleted; the device will return to DHCP after a reboot.
After saving the WiFi network settings, the Remote reboots and tries to connect to your configured WiFi network. If that fails, it will again start in access point mode.

After completing this step, your Remote is basically ready for use; you can also continue configuring it to your personal preferences through the Config Portal.

## The Config Portal

The "Config Portal" is the Remote's configuration web site. 

| ![The Config Portal](img/cpm.png) |
|:--:| 
| *The Config Portal's main page* |

It can be accessed as follows:

#### If Remote is in AP mode

- Connect your computer or handheld device to the WiFi network "REM-AP".
- Navigate your browser to http://dtmremote.local or http://192.168.4.1 to enter the Config Portal.

#### If Remote is connected to WiFi network

- Connect your hand-held/computer to the same WiFi network to which the Remote is connected, and
- navigate your browser to http://dtmremote.local

  Accessing the Config Portal through this address requires the operating system of your hand-held/computer to support Bonjour/mDNS: Windows 10 version TH2     (1511) [other sources say 1703] and later, Android 13 and later; MacOS and iOS since the dawn of time.

  If connecting to http://dtmremote.local fails due to a name resolution error, you need to find out the Remote's IP address: Power up and fake-power-up the Remote and hold the Calibration button for 2 seconds. The Remote will display its current IP address (a. - b. - c. - d). Then, on your handheld or computer, navigate to http://a.b.c.d (a.b.c.d being the IP address as displayed on the Remote) in order to enter the Config Portal.

In the main menu, click on "Setup" to configure your Remote. 

| [<img src="img/cps-frag.png">](img/cp_setup.png) |
|:--:| 
| *Click for full screenshot* |

A full reference of the Config Portal is [here](#appendix-a-the-config-portal).

## Basic Operation

After [calibration](#calibration), the Remote is ready for use. After power-on and fake-power-on, the Remote's throttle controls the TCD's speed (ie the speed displayed on the Speedo). 

For acceleration, there are two modes: Linear mode and "movie mode". In linear mode, acceleration is even over the entire range of 0 to 88mph. In "movie mode", the Remote (mostly) accelerates in the same pace as shown in the movie. In this mode, acceleration becomes slower at higher speeds.

When the remote hits 88.0mph, the TCD triggers a time travel.

Auto-throttle: If this option is checked in the Config Portal (or the Auto-Throttle is enabled through the TCD keypad), acceleration will continue to run after briefliy pushing up the throttle stick and releasing it into neutral. Acceleration is stopped when pulling down the throttle stick, or when 88mph is reached.

Buttons and switches:

### "ON/OFF": Fake power

The "ON/OFF" switch turns the Remote on and off in a sense that it takes over speed control on the TCD when fake-powered on, and hands back speed control when fake-powered off.

"ON/OFF" must be a maintained contact.

### "Stop"

The "Stop" switch activates the brakes on your virtual Delorean; if the brakes are on, speed changes on the Remote are not followed by the TCD, instead the TCD will count speed down to 0. Upon releasing the brake, the virtual car accelerates up to the speed shown on the Remote, and will then follow its speed changes.

"Stop" must be a maintained contact.

### Calibration

<table>
  <tr><td></td><td>Short press</td><td>Long press</td></tr>
  <tr><td>Fake-power off</td><td>Calibrate, see below</td><td>Calibration, see below</td></tr>
  <tr><td>Fake-power on</td><td>Reset speed to 0</td><td>Display IP address, battery charge percentage(*), battery time-to-empty(*), battery voltage(*)</td></tr>
</table>

(* M-board, or non-M-board with BatMon Add-on required; if LiPo battery is properly connected to battery monitor)

The throttle of the Remote needs calibration:

- Put the "ON/OFF" switch in "OFF" position.
- Put the throttle lever in neutral (center) position, press the Calibration button. The display will briefly show "CAL" to show acceptance.
- Hold the Calibration button for >= 2 seconds, "UP" will be displayed. Now push the throttle to the up-most position, and press the Calibration button. "DN" will be displayed.
- Pull the throttle lever to the bottom-most position, and press the Calibration button. The display will clear, calibration is finished.

If you change power-source (ie a new battery, or power via USB to the ESP32), re-calibration is required.
 
The Calibration button needs to be a momentary contact.

### Buttons "O.O" and "RESET"

When fake power is on:
<table>
  <tr><td></td><td>Short press</td><td>Long press</td></tr>
  <tr><td>Button "O.O"</td><td>Prepare BTTFN-wide TT<br>or<br><a href="#the-music-player">Music Player</a>: Previous Song<br>(See <a href="#-oo-throttle-up-trigger-bttfn-wide-time-travel">here</a>)</td><td><a href="#the-music-player">Music Player</a>: Play/Stop</td></tr>
  <tr><td>Button "RESET"</td><td><a href="#the-music-player">Music Player</a>: Next Song</td><td><a href="#the-music-player">Music Player</a>: Toggle Shuffle</td></tr>
</table>

When fake power is off, the buttons are used to set up audio volume and brightness:
<table>
  <tr><td></td><td>Short press</td><td>Long press</td></tr>
  <tr><td>Button "O.O"</td><td>Volume up</td><td>Brightness up</td></tr>
  <tr><td>Button "RESET"</td><td>Volume down</td><td>Brightness down</td></tr>
</table>

### User Buttons

These buttons are entirely optional. You can install any number of buttons, they only serve the purpose of playing back user-provided sound effects and/or send user-configurable messages to an [MQTT](#home-assistant--mqtt) broker.

Sound playback is mapped as follows:

<table>
  <tr><td></td><td>Short press</td><td>Long press</td></tr>
  <tr><td>Button 1</td><td>Play "<a href="#additional-custom-sounds">key1.mp3</a>"</td><td>Play "<a href="#additional-custom-sounds">key1l.mp3</a>"</td></tr>
  <tr><td>Button 2</td><td>Play "<a href="#additional-custom-sounds">key2.mp3</a>"</td><td>Play "<a href="#additional-custom-sounds">key2l.mp3</a>"</td></tr>
  <tr><td>Button 3</td><td>Play "<a href="#additional-custom-sounds">key3.mp3</a>"</td><td>Play "<a href="#additional-custom-sounds">key3l.mp3</a>"</td></tr>
  <tr><td>Button 4</td><td>Play "<a href="#additional-custom-sounds">key4.mp3</a>"</td><td>Play "<a href="#additional-custom-sounds">key4l.mp3</a>"</td></tr>
  <tr><td>Button 5</td><td>Play "<a href="#additional-custom-sounds">key5.mp3</a>"</td><td>Play "<a href="#additional-custom-sounds">key5l.mp3</a>"</td></tr>
  <tr><td>Button 6</td><td>Play "<a href="#additional-custom-sounds">key6.mp3</a>"</td><td>Play "<a href="#additional-custom-sounds">key6l.mp3</a>"</td></tr>
  <tr><td>Button 7</td><td>Play "<a href="#additional-custom-sounds">key7.mp3</a>"</td><td>Play "<a href="#additional-custom-sounds">key7l.mp3</a>"</td></tr>
  <tr><td>Button 8</td><td>Play "<a href="#additional-custom-sounds">key9.mp3</a>"</td><td>Play "<a href="#additional-custom-sounds">key9l.mp3</a>"</td></tr>
</table>

> 'key9' instead of 'key8' is no typo. The seemingly odd numbering is for synchronicity with other props, where keys 2, 5, 8 control the music player. Since the Remote has more buttons free for keyX play-back than other props, 'key2' and 'key5' are supported and only 'key8' is skipped. Note that 'key2' and 'key5' cannot be played back through a TCD keypad command.

If a "button" is configured as a maintained switch in the Config Portal, keyX will be played on every flip (ON->OFF, OFF->ON) by default. If the option **_Maintained: Audio on ON only_** is checked for a switch, keyX will only be played when the switch is brought into ON position. This is especially useful for three-position switches where each of the "ON" positions is wired to a different "Button" on the Control Board. Note that maintained switches cannot trigger play-back of keyXl.

If the button/switch is pressed/flipped while audio from a previous press/flip of the same button/switch is still playing, play-back will be stopped.

### WiFi connection:

If the WiFi network the Remote is supposed to connect to wasn't reachable when the Remote was powered up, it will run in AP mode. You can trigger a re-connection attempt by fake-powering it down and up.

### TCD remote command reference

<table>
   <tr><td>Function</td><td>Code on TCD</td></tr>
    <tr>
     <td align="left">Toggle "<a href="#-movie-mode-acceleration">movie mode</a>"</td>
     <td align="left"<td>7060&#9166;</td>
    </tr>
   <tr>
     <td align="left">Toggle <a href="#-display-tcd-speed-when-off">display of TCD speed while off</a></td>
     <td align="left"<td>7061&#9166;</td>
    </tr>
   <tr>
     <td align="left">Toggle <a href="#-auto-throttle">auto-throttle</a></td>
     <td align="left"<td>7062&#9166;</td>
    </tr>
    <tr>
     <td align="left">Set volume level (00-19)</td>
     <td align="left">7300&#9166; - 7319&#9166;</td>
    </tr>
  <tr>
     <td align="left">Enable / disable click sound</td>
     <td align="left">7350&#9166; / 7351&#9166;</td>
    </tr>
    <tr>
     <td align="left">Set brightness level (00-15)</td>
     <td align="left"<td>7400&#9166; - 7415&#9166;</td>
    </tr>
    <tr>
     <td align="left"><a href="#the-music-player">Music Player</a>: Play/Stop</td>
     <td align="left">7005&#9166;</td>
    </tr>
    <tr>
     <td align="left"><a href="#the-music-player">Music Player</a>: Previous song</td>
     <td align="left">7002&#9166;</td>
    </tr>
    <tr>
     <td align="left"><a href="#the-music-player">Music Player</a>: Next song</td>
     <td align="left">7008&#9166;</td>
    </tr>
    <tr>
     <td align="left"><a href="#the-music-player">Music Player</a>: Select music folder (0-9)</td>
     <td align="left">7050&#9166; - 7059&#9166;</td>
    </tr>
    <tr>
     <td align="left"><a href="#the-music-player">Music Player</a>: Shuffle off</td>
     <td align="left">7222&#9166;</td>
    </tr>
    <tr>
     <td align="left"><a href="#the-music-player">Music Player</a>: Shuffle on</td>
     <td align="left">7555&#9166;</td>
    </tr> 
    <tr>
     <td align="left"><a href="#the-music-player">Music Player</a>: Go to song 0</td>
     <td align="left">7888&#9166;</td>
    </tr>
    <tr>
     <td align="left"><a href="#the-music-player">Music Player</a>: Go to song xxx</td>
     <td align="left">7888xxx&#9166;</td>
    </tr>
    <tr>
     <td align="left">Play "<a href="#additional-custom-sounds">key1.mp3</a>"</td>
     <td align="left">7001&#9166;</td>
    </tr>
    <tr>
     <td align="left">Play "<a href="#additional-custom-sounds">key3.mp3</a>"</td>
     <td align="left">7003&#9166;</td>
    </tr>
    <tr>
     <td align="left">Play "<a href="#additional-custom-sounds">key4.mp3</a>"</td>
     <td align="left">7004&#9166;</td>
    </tr>
    <tr>
     <td align="left">Play "<a href="#additional-custom-sounds">key6.mp3</a>"</td>
     <td align="left">7006&#9166;</td>
    </tr>
    <tr>
     <td align="left">Play "<a href="#additional-custom-sounds">key7.mp3</a>"</td>
     <td align="left">7007&#9166;</td>
    </tr>
    <tr>
     <td align="left">Play "<a href="#additional-custom-sounds">key9.mp3</a>"</td>
     <td align="left">7009&#9166;</td>
    </tr>
    <tr>
     <td align="left">Display current IP address</td>
     <td align="left">7090&#9166;</td>
    </tr>   
  <tr>
     <td align="left">Display battery charge percentage(*)</td>
     <td align="left">7091&#9166;</td>
    </tr>   
  <tr>
     <td align="left">Display battery "Time to empty"(*)</td>
     <td align="left">7092&#9166;</td>
    </tr>   
  <tr>
     <td align="left">Display battery voltage(*)</td>
     <td align="left">7093&#9166;</td>
    </tr>   
    <tr>
     <td align="left">Reboot the device</td>
     <td align="left">7064738&#9166;</td>
    </tr>
     <tr>
     <td align="left">Delete static IP address<br>and WiFi-AP password</td>
     <td align="left">7123456&#9166;</td>
    </tr>
</table>

* M-board, or non-M-board with BatMon Add-on required; only if LiPo battery is properly connected to battery monitor.

## SD card

Preface note on SD cards: For unknown reasons, some SD cards simply do not work with this device. For instance, I had no luck with Sandisk Ultra 32GB and  "Intenso" cards. If your SD card is not recognized, check if it is formatted in FAT32 format (not exFAT!). Also, the size must not exceed 32GB (as larger cards cannot be formatted with FAT32). Transcend SDHC cards and those work fine in my experience.

The SD card, apart from being required for [installing](#audio-data-installation) of the built-in audio data, can be used for substituting built-in sound effects and for music played back by the [Music player](#the-music-player). Also, it is _strongly recommended_ to store [secondary settings](#-save-secondary-settings-on-sd) on the SD card to minimize [Flash Wear](#flash-wear).

Note that the SD card must be inserted before powering up the device. It is not recognized if inserted while the Remote is running. Furthermore, do not remove the SD card while the device is powered.

### Sound substitution

The Remote's built-in sound effects can be substituted by your own sound files on a FAT32-formatted SD card. These files will be played back directly from the SD card during operation, so the SD card has to remain in the slot.

Your replacements need to be put in the root (top-most) directory of the SD card, be in mp3 format (128kbps max) and named as follows:
- "poweron.mp3": Played when the Remote is (fake)-powered on.
- "brakeon.mp3": Played when the brake is switched on (= when the "Stop" light is switched on).
- "alarm.mp3". Played when the alarm sounds (triggered by a Time Circuits Display via BTTFN or MQTT);

### Additional Custom Sounds

The firmware supports some additional user-provided sound effects, which it will load from the SD card. If the respective file is present, it will be used. If that file is absent, no sound will be played.

- "poweroff.mp3": Played when the Remote is (fake)-powered off.
- "brakeoff.mp3": Played when the brake is switched off.
- "key1.mp3", "key2.mp3", ""key3.mp3", "key4.mp3", "key5.mp3", "key6.mp3", "key7.mp3", "key9.mp3": Will be played upon pressing the respective [button](#user-buttons), and by typing 700x on the TCD's keypad (connected through BTTFN).
- "key1l.mp3", "key2l.mp3", "key3l.mp3", "key4l.mp3", "key5l.mp3", "key6l.mp3", "key7l.mp3", "key9l.mp3": Will be played upon long-pressing the respective [button](#user-buttons).

> The seemingly odd numbering for keyX files is because of synchronicity with other props, especially the TCD and its keymap where the MusicPlayer occupies keys 2, 5, 8. Since there are more buttons for sound than keys, 2 and 5 are used but 8 is skipped.

Those files are not provided here. You can use any mp3, with a bitrate of 128kpbs or less.

## The Music Player

The firmware contains a simple music player to play mp3 files located on the SD card. 

In order to be recognized, your mp3 files need to be organized in music folders named *music0* through *music9*. The folder number is 0 by default, ie the player starts searching for music in folder *music0*. This folder number can be changed in the Config Portal or through the TCD keypad (705x).

The names of the audio files must only consist of three-digit numbers, starting at 000.mp3, in consecutive order. No numbers should be left out. Each folder can hold up to 1000 files (000.mp3-999.mp3). *The maximum bitrate is 128kpbs.*

Since manually renaming mp3 files is somewhat cumbersome, the firmware can do this for you - provided you can live with the files being sorted in alphabetical order: Just copy your files with their original filenames to the music folder; upon boot or upon selecting a folder containing such files, they will be renamed following the 3-digit name scheme (as mentioned: in alphabetic order). You can also add files to a music folder later, they will be renamed properly; when you do so, delete the file "TCD_DONE.TXT" from the music folder on the SD card so that the firmware knows that something has changed. The renaming process can take a while (10 minutes for 1000 files in bad cases). Mac users are advised to delete the ._ files from the SD before putting it back into the control board as this speeds up the process.

To start and stop music playback, hold "O.O" for 2 seconds. Briefly pressing "O.O" jumps to the previous song, pressing "RESET" to the next one. (The same can be achieved by entering codes on the TCD's keypad: 7002 = previous song, 7005 = play/stop, 7008 = next song).

By default, the songs are played in order, starting at 000.mp3, followed by 001.mp3 and so on. Holding "RESET" toggles Shuffle mode. The power-up Shuffle mode can be set up in the Config Portal.

See [here](#buttons-oo-and-reset) and [here](#tcd-remote-command-reference) for a list of controls of the music player.

While the music player is playing music, other sound effects might be disabled/muted. The TCD-triggered alarm will, if so configured, sound and stop the music player.

## Connecting a Time Circuits Display

The TCD communicates with the Remote wirelessly, via the built-in "**B**asic-**T**elematics-**T**ransmission-**F**ramework" (BTTFN) over WiFi. Note that a wired connection is not supported.

The actual remote controlling is done wirelessly, and the TCD's keypad can be used to remote-control the Remote (to control the MusicPlayer, for instance).

| [![Watch the video](https://img.youtube.com/vi/u9oTVXUIOXA/0.jpg)](https://youtu.be/u9oTVXUIOXA) |
|:--:|
| Click to watch the video |

Note that the TCD's firmware must be up to date for BTTFN. You can update from [here](http://tcd.out-a-ti.me), or install CircuitSetup's release 3.2 or later.

![BTTFN connection](img/family-wifi-bttfn.png)

In order to connect your Remote to the TCD, just enter the TCD's IP address or hostname in the **_IP address or hostname of TCD_** field in the Remote's Config Portal. On the TCD, no special configuration is required apart from enabling remote controlling through the command 993 followed by ENTER.
  
Afterwards, the Remote and the TCD can communicate wirelessly and 
- the TCD's speed control is done by the Remote,
- both play an alarm-sequence when the TCD's alarm occurs (if so configures),
- the Remote can be remote controlled through the TCD's keypad (command codes 7xxx),
- the Remote can - while fake powered off - display the TCD's speed (eg from GPS).

You can use BTTF-Network and MQTT at the same time, see immediately below.

## Home Assistant / MQTT

The Remote supports the MQTT protocol version 3.1.1 for the following features:

### Send messages through User Buttons

In the Config Portal, you can configure MQTT topics and messages for "on" and/or "off" for each of the eight User buttons/switches. This allows for arbitrary functions in your Home Assistant/MQTT realm. You could switch HA-integrated lights on or off, or send TIMETRAVEL to your [Time Circuits Display](https://tcd.out-a-ti.me) (bttf/tcd/cmd), or REFILL to your [Dash Gauges](https://dg.out-a-ti.me) (bttf/dg/cmd).

The ON message will be sent when a button has been pressed, the OFF message when it has been released. In case of a button being configured as a maintained switch in the Config Portal, the ON message will be sent when the switch is closed, the OFF message when it is opened.

### Control the Remote via MQTT

The Remote can - to some extent - be controlled through messages sent to topic **bttf/remote/cmd**. Support commands are
- MP_PLAY: Starts the [Music Player](#the-music-player)
- MP_STOP: Stops the [Music Player](#the-music-player)
- MP_NEXT: Jump to next song
- MP_PREV: Jump to previous song
- MP_SHUFFLE_ON: Enables shuffle mode in [Music Player](#the-music-player)
- MP_SHUFFLE_OFF: Disables shuffle mode in [Music Player](#the-music-player)
- MP_FOLDER_x: x being 0-9, set Music Folder number for [Music Player](#the-music-player)

### Receive commands from Time Circuits Display

If both TCD and Remote are connected to the same broker, and the option **_Send event notifications_** is checked on the TCD's side, the Remote will receive information on time travel and alarm and play their sequences in sync with the TCD. Unlike BTTFN, however, no other communication takes place. The actual remote controlling requires a BTTFN connection.

![MQTT connection](img/family-wifi-mqtt.png)

MQTT and BTTFN can co-exist. However, the TCD only sends out time travel and alarm notifications through either MQTT or BTTFN, never both. If you have other MQTT-aware devices listening to the TCD's public topic (bttf/tcd/pub) in order to react to time travel or alarm messages, use MQTT (ie check **_Send event notifications_**). If only BTTFN-aware devices are to be used, uncheck this option to use BTTFN as it has less latency.

### Setup

In order to connect to a MQTT network, a "broker" (such as [mosquitto](https://mosquitto.org/), [EMQ X](https://www.emqx.io/), [Cassandana](https://github.com/mtsoleimani/cassandana), [RabbitMQ](https://www.rabbitmq.com/), [Ejjaberd](https://www.ejabberd.im/), [HiveMQ](https://www.hivemq.com/) to name a few) must be present in your network, and its address needs to be configured in the Config Portal. The broker can be specified either by domain or IP (IP preferred, spares us a DNS call). The default port is 1883. If a different port is to be used, append a ":" followed by the port number to the domain/IP, such as "192.168.1.5:1884". 

If your broker does not allow anonymous logins, a username and password can be specified.

Limitations: MQTT Protocol version 3.1.1; TLS/SSL not supported; ".local" domains (MDNS) not supported; server/broker must respond to PING (ICMP) echo requests. For proper operation with low latency, it is recommended that the broker is on your local network. 

## Car setup

If your [Time Circuits Display](https://tcd.out-a-ti.me/) is mounted in a car, the following network configuration is recommended:

#### TCD

- Run your TCD in [*car mode*](https://tcd.out-a-ti.me/#car-mode);
- disable WiFi power-saving on the TCD by setting **_WiFi power save timer (AP-mode)_** to 0 (zero).

#### Remote

Enter the Config Portal on the Remote, click on *Setup* and
  - enter *192.168.4.1* into the field **_IP address or hostname of TCD_**
  - click on *Save*.

After the Remote has restarted, re-enter the Remote's Config Portal (while the TCD is powered and in *car mode*) and
  - click on *Configure WiFi*,
  - select the TCD's access point name in the list at the top or enter *TCD-AP* into the *SSID* field; if you password-protected your TCD's AP, enter this password in the *password* field. Leave all other fields empty,
  - click on *Save*.

Using this setup enables the Remote to communicate with your TCD wirelessly, and to query the TCD for data. Also, the TCD keypad can be used to remote-control the Remote.

In order to access the Remote's Config Portal in this setup, connect your hand held or computer to the TCD's WiFi access point ("TCD-AP"), and direct your browser to http://dtmremote.local ; if that does not work, hold the Calibration button for 2 seconds while the Remote is fake-powered on, it will display its IP address. Then direct your browser to that IP by using the URL http://a.b.c.d (a-d being the IP address displayed on the Remote display).

## Flash Wear

Flash memory has a somewhat limited life-time. It can be written to only between 10.000 and 100.000 times before becoming unreliable. The firmware writes to the internal flash memory when saving settings and other data. Every time you change settings, data is written to flash memory.

In order to reduce the number of write operations and thereby prolong the life of your Remote, it is recommended to use a good-quality SD card and to check **_[Save secondary settings on SD](#-save-secondary-settings-on-sd)_** in the Config Portal; secondary settings are then stored on the SD card (which also suffers from wear but is easy to replace). See [here](#-save-secondary-settings-on-sd) for more information.

## Appendix A: The Config Portal

### Main page

##### &#9654; Configure WiFi

Clicking this leads to the WiFi configuration page. On that page, you can connect your Remote to your WiFi network by selecting/entering the SSID (WiFi network name) as well as a password (WPA2). By default, the Remote requests an IP address via DHCP. However, you can also configure a static IP for the Remote by entering the IP, netmask, gateway and DNS server. All four fields must be filled for a valid static IP configuration. If you want to stick to DHCP, leave those four fields empty.

Note that this page has nothing to do with Access Point mode; it is strictly for connecting your Remote to an existing WiFi network as a client.

##### &#9654; Setup

This leads to the [Setup page](#setup-page).

##### &#9654; Update

This leads to the firmware and audio update page. 

In order to upload a new firmware binary (such as the ones published here in the install/ folder), select that image file in the top file selector and click "Update".

You can also install the Remote's audio data on this page; download the current sound-pack, extract it and select the resulting REMA.bin file in the bottom file selector. Finally, click "Upload". Note that an SD card is required for this operation.

Note that either a firmware or audio data can be uploaded at once, not both at the same time.

##### &#9654; Erase WiFi Config

Clicking this (and saying "yes" in the confirmation dialog) erases the WiFi configuration (WiFi network and password) and reboots the device; it will restart in "access point" mode. See [here](#short-summary-of-first-steps).

---

### Setup page

#### Basic settings

##### &#9654; Coasting when throttle in neutral

Normally, when this is unchecked, keeping the throttle in neutral (center) position hold the current speed. If this option is checked, speed will slowly decrease in neutral, just like a car when the kludge is held down or the gear is in neutral.

##### &#9654; Auto throttle

If this is checked, acceleration is, after being started by pushing the throttle stick up, continued even if the stick is released into neutral. Acceleration is stopped when pulling down the throttle stick, or when 88mph is reached.

##### &#9654; O.O, throttle-up trigger BTTFN-wide Time Travel

This option selects the function of the O.O button:

If checked, briefly pressing O.O prepares a BTTFN-wide Time Travel, which is then triggered when pushing the throttle stick upward.

If unchecked, O.O is part of Music Player control and jumps to the previous song.

##### &#9654; Brightness level

This selects brightness level for the LED display. This can also be done through buttons "O.O" and "RESET", as well as the TCD (74xx).

#### Hardware configuration settings

##### Volume level (0-19)

Enter a value between 0 (mute) or 19 (very loud) here. This is your starting point; you can change the volume using Buttons "O.O" and "RESET", and via TCD (73xx) and that new volume will also be saved (and appear in this field when the page is reloaded in your browser).

#### Network settings

##### &#9654; Hostname

The device's hostname in the WiFi network. Defaults to 'dtmremote'. This also is the domain name at which the Config Portal is accessible from a browser in the same local network. The URL of the Config Portal then is http://<i>hostname</i>.local (the default is http://dtmremote.local)

If you have several Remotes in your local network, please give them unique hostnames. Needless to say, only one Remote can be used with a TCD at a time.

##### &#9654; AP Mode: Network name appendix

By default, if the Remote creates a WiFi network of its own ("AP-mode"), this network is named "REM-AP". In case you have multiple Remotes in your vicinity, you can have a string appended to create a unique network name. If you, for instance, enter "-ABC" here, the WiFi network name will be "REM-AP-ABC". Characters A-Z, a-z, 0-9 and - are allowed.

##### &#9654; AP Mode: WiFi password

By default, and if this field is empty, the Remote's own WiFi network ("AP-mode") will be unprotected. If you want to protect your access point, enter your password here. It needs to be 8 characters in length and only characters A-Z, a-z, 0-9 and - are allowed.

If you forget this password and are thereby locked out of your Remote, 
- power-down the device,
- hold the Calibration button,
- power-up the device (while still holding the Calibration button)
- wait until the display shows a counter-clockwise circle animation,
- press Button "O.O" twice within 10 seconds,
- wait until the display shows "RST",
- then release the Calibration button.

This procedure temporarily (until a reboot) clears the WiFi password, allowing unprotected access to the Config Portal. (Note that this procedure also deletes static IP addres data; the device will return to using DHCP after a reboot.)

##### &#9654; WiFi connection attempts

Number of times the firmware tries to reconnect to a WiFi network, before falling back to AP-mode. See [here](#short-summary-of-first-steps)

##### &#9654; WiFi connection timeout

Number of seconds before a timeout occurs when connecting to a WiFi network. When a timeout happens, another attempt is made (see immediately above), and if all attempts fail, the device falls back to AP-mode. See [here](#short-summary-of-first-steps)

#### Settings for prop communication/synchronization

##### &#9654; IP address or hostname of TCD

In order to connect your Remote to a Time Circuits Display wirelessly ("BTTF-Network"), enter the IP address of the TCD here. You can also enter the TCD's hostname here instead (eg. 'timecircuits').

If you connect your Remote to the TCD's access point ("TCD-AP"), the TCD's IP address is 192.168.4.1.

#### Audio-visual options

##### &#9654; Movie-mode acceleration

The Remote knows to modes of acceleration: "Movie mode" and "linear".

In movie mode, acceleration changes with speed. At lower speeds, it is faster, and will gradually become slower as speed increases. The pace matches the movie mostly; unfortunately the remote is only shown for a very few seconds and timing is inconsistent (to say the least), so some interpolations were required.

In linear mode, the acceleration curve is a straight line, ie the time between each mph is the same.

##### &#9654; Play acceleration 'click' sound

Check this to play a click sound for each "mph" while accelerating. Uncheck to stay mute. Note that the click is only played when accelerating, not with reducing speed.

##### &#9654; Play TCD-alarm sounds

If a TCD is connected via BTTFN or MQTT, the Dash Gauges visually signals when the TCD's alarm sounds. If you want to play an alarm sound, check this option.

##### &#9654; Display TCD speed when off

When this is checked, the Remote (when fake-powered off) shows whatever the TCD displays on its speedo. For instance, if your TCD is in a car along with a GPS-equipped speedo, the Remote can show the GPS speed. In a home setup with a Rotary Encoder for speed, the Remote will show the speed displayed on the TCD's speedo.

##### &#9654; Use Power LED

If unchecked, the power LED stays dark, which is the default. If checked, the power LED lights up on either real power or fake power, as per the **_Power LED/meter on fake power_** option, see below.

##### &#9654; Use Battery Level Meter

If unchecked, the level meter stays at zero, which is the default. If checked, the level meter shows a fictious battery level of around 75% on either real power or fake power, as per the **_Power LED/meter on fake power_** option, see below. Please note that the meter does not show actual battery level.

##### &#9654; Power LED/meter on fake power

If unchecked, the power LED and the battery level meter come to life on real power. If checked, they act on fake power.

#### Home Assistant / MQTT settings

##### &#9654; Use Home Assistant (MQTT 3.1.1)

If checked, the Remote will connect to the broker (if configured) and send and receive messages via [MQTT](#home-assistant--mqtt)

##### &#9654; Broker IP[:port] or domain[:port]

The broker server address. Can be a domain (eg. "myhome.me") or an IP address (eg "192.168.1.5"). The default port is 1883. If different port is to be used, it can be specified after the domain/IP and a colon ":", for example: "192.168.1.5:1884". Specifying the IP address is preferred over a domain since the DNS call adds to the network overhead. Note that ".local" (MDNS) domains are not supported.

##### &#9654; User[:Password]

The username (and optionally the password) to be used when connecting to the broker. Can be left empty if the broker accepts anonymous logins.

##### &#9654; Button x topic

The MQTT topic for on/off messages. Nothing is published/sent if the topic is empty.

##### &#9654; Button x message on ON/OFF

The MQTT message to publish to the button's topic when a button is pressed/released (or in case of a maintained switch: when the switch is put in "on"/"off" position). If a field is empty, nothing is published/sent.

#### Music Player settings

##### &#9654; Music folder

Selects the current music folder, can be 0 through 9. This can also be set/changed through a TCD keypad via BTTFN (705x).

##### &#9654; Shuffle at startup

When checked, songs are shuffled when the device is booted. When unchecked, songs will be played in order.

#### Other settings

##### &#9654; Save secondary settings on SD

If this is checked, some settings (volume, etc) are stored on the SD card (if one is present). This helps to minimize write operations to the internal flash memory and to prolong the lifetime of your Remote. See [Flash Wear](#flash-wear).

Apart from flash Wear, there is another reason for using an SD card for settings: Writing data to internal flash memory can cause delays of up to 1.5 seconds, which interrupt sound playback and have other undesired effects. The Remote needs to save data from time to time, so in order for a smooth experience without unexpected and unwanted delays, please use an SD card and check this option.

It is safe to have this option checked even with no SD card present.

If you want copy settings from one SD card to another, do as follows:
- With the old SD card still in the slot, enter the Config Portal, turn off _Save secondary settings on SD_, and click "SAVE".
- After the Remote has rebooted, power down, and swap the SD card for your new one.
- Power-up the Remote, enter the Config Portal, re-enable _Save secondary settings on SD_, and click "SAVE".

This procedure ensures that all your settings are copied from the old to the new SD card.

#### Hardware settings

##### &#9654; Button x is maintained

You might want use one or more switches of the Futaba remote for sound effects and/or MQTT messages. If that switch is a maintained contact, check this option for the respective "button" number. Leave unchecked when using a momentary button.

##### &#9654; Maintained: Play audio on ON only

If this is unchecked, audio is played on every flip (OFF->ON, ON->OFF) of the maintained switch. If checked, keyX is only played when the switch is brought into "ON" position. Check this if using three-position switches where both ON positions are wired to different "Buttons" on the Control Board.


_Text & images: (C) Thomas Winischhofer ("A10001986"). See LICENSE._ https://remote.out-a-ti.me

