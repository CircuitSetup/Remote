/*
 * -------------------------------------------------------------------
 * Remote Control
 * (C) 2024 Thomas Winischhofer (A10001986)
 * https://github.com/realA10001986/Remote
 * https://remote.out-a-ti.me
 *
 * Global definitions
 */

#ifndef _REMOTE_GLOBAL_H
#define _REMOTE_GLOBAL_H

/*************************************************************************
 ***                           Miscellaneous                           ***
 *************************************************************************/


/*************************************************************************
 ***                          Version Strings                          ***
 *************************************************************************/

#define REMOTE_VERSION "V0.23"
#define REMOTE_VERSION_EXTRA "OCT052024"

#define REMOTE_DBG              // debug output on Serial

/*************************************************************************
 ***                     mDNS (Bonjour) support                        ***
 *************************************************************************/

// Supply mDNS service
// Allows accessing the Config Portal via http://hostname.local
// <hostname> is configurable in the Config Portal
// This needs to be commented if WiFiManager provides mDNS
#define REMOTE_MDNS
// Uncomment this if WiFiManager has mDNS enabled
//#define REMOTE_WM_HAS_MDNS

/*************************************************************************
 ***             Configuration for hardware/peripherals                ***
 *************************************************************************/

// Uncomment for audio support
#define REMOTE_HAVEAUDIO

/*************************************************************************
 ***                           Miscellaneous                           ***
 *************************************************************************/

// Uncomment for HomeAssistant MQTT protocol support
#define REMOTE_HAVEMQTT

// Use SPIFFS (if defined) or LittleFS (if undefined; esp32-arduino >= 2.x)
//#define USE_SPIFFS

// Uncomment to include BTTFN discover support (multicast)
#define BTTFN_MC

// Uncomment if hardware has a volume knob
//#define REMOTE_HAVEVOLKNOB
// Uncomment when using the prototype display
//#define PROTO_DISPLAY

/*************************************************************************
 ***                  esp32-arduino version detection                  ***
 *************************************************************************/

#if defined __has_include && __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#ifdef ESP_ARDUINO_VERSION_MAJOR
    #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(2,0,8)
    #define HAVE_GETNEXTFILENAME
    #endif
#endif
#endif

/*************************************************************************
 ***                             GPIO pins                             ***
 *************************************************************************/

// I2S audio pins
#define I2S_BCLK_PIN      26
#define I2S_LRCLK_PIN     25
#define I2S_DIN_PIN       33

// SD Card pins
#define SD_CS_PIN          5
#define SPI_MOSI_PIN      23
#define SPI_MISO_PIN      19
#define SPI_SCK_PIN       18

                          // -------- Buttons/Switches and LEDs

#define BUTA_IO_PIN       13      // Button A "O.O" (active low)                  (has internal PU/PD) (PU on CB)
#define BUTB_IO_PIN       14      // Button B "RESET" (active low)                (has internal PU/PD) (PU on CB)

#define FPOWER_IO_PIN     15      // Fake power Switch GPIO pin (act. low)        (has internal PU)

#define STOPS_IO_PIN      27      // Stop switch (act high)                       (has internal PU) (PD on CB)

#define CALIBB_IO_PIN     32      // Calibration button (act low)                 (has no internal PU?; PU on CB)

#define STOPOUT_PIN       16      // Stop output

#define PWRLED_PIN        17      // (Fake) power LED (CB 1.3)

#define VOLUME_PIN        39      // (unused)

#endif