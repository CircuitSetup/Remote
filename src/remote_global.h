/*
 * -------------------------------------------------------------------
 * Remote Control
 * (C) 2024-2025 Thomas Winischhofer (A10001986)
 * https://github.com/realA10001986/Remote
 * https://remote.out-a-ti.me
 *
 * Global definitions
 */

#ifndef _REMOTE_GLOBAL_H
#define _REMOTE_GLOBAL_H

/*************************************************************************
 ***                          Version Strings                          ***
 *************************************************************************/

#define REMOTE_VERSION "V1.10"
#define REMOTE_VERSION_EXTRA "OCT072025"

//#define REMOTE_DBG              // debug output on Serial

/*************************************************************************
 ***             Configuration for hardware/peripherals                ***
 *************************************************************************/

// Nix...

/*************************************************************************
 ***                           Miscellaneous                           ***
 *************************************************************************/

// Uncomment for HomeAssistant MQTT protocol support
#define REMOTE_HAVEMQTT

// Uncomment to include BTTFN discover support (multicast)
// This is required for proper operation with TCD firmwares dated Oct 26, 2024,
// or later.
#define BTTFN_MC

// Uncomment if hardware has a volume knob
//#define REMOTE_HAVEVOLKNOB
// Uncomment when using the prototype display
//#define PROTO_DISPLAY

// Battery monitor support
#define HAVE_PM

// Uncomment to allow user to disable User Buttons
// (Was used for prototype)
//#define ALLOW_DIS_UB

// Use SPIFFS (if defined) or LittleFS (if undefined; esp32-arduino >= 2.x)
//#define USE_SPIFFS

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
#define LVLMETER_PIN      12      // Battery level meter on Futaba (CB 1.5)

#define BALM_PIN          4       // Battery monitor alarm (CB 1.6; act. low)     (PU on CB 1.6)

#define VOLUME_PIN        39      // (unused)

#endif
