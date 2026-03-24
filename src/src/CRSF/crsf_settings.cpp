/*
 * -------------------------------------------------------------------
 * Remote Control
 * (C) 2026 Thomas Winischhofer (A10001986)
 * https://github.com/realA10001986/Remote
 * https://remote.out-a-ti.me
 *
 * CRSF settings kludge
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

#include "../../remote_global.h"

#ifdef HAVE_CRSF

#define ARDUINOJSON_USE_LONG_LONG 0
#define ARDUINOJSON_USE_DOUBLE 0
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 0
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_STD_STREAM 0
#define ARDUINOJSON_ENABLE_STD_STRING 0
#define ARDUINOJSON_ENABLE_NAN 0
#include <ArduinoJson.h>
#include <Arduino.h>
#include <FS.h>
#include <math.h>
#include <SD.h>
#ifdef USE_SPIFFS
#define MYNVS SPIFFS
#include <SPIFFS.h>
#else
#define MYNVS LittleFS
#include <LittleFS.h>
#endif

#include "../../remote_settings.h"
#include "elrs_crsf_shared.h"
#include "crsf_kludge.h"
#include "crsf_settings.h"

// External
extern bool     loadConfigFile(const char *fn, uint8_t *buf, int len, int& validBytes, int forcefs = 0);
extern bool     saveConfigFile(const char *fn, uint8_t *buf, int len, int forcefs = 0);
extern uint32_t calcHash(uint8_t *buf, int len);

// CRSF settings
// Do not change or insert new values, this
// struct is saved as such. Append new stuff.
static struct [[gnu::packed]] {
    ELRSAxisCalibrationData elrsAxis[ELRS_GIMBAL_AXIS_COUNT] = {
        { 0, 1024, 2047 },
        { 0, 1024, 2047 },
        { 0, 1024, 2047 },
        { 0, 1024, 2047 }
    };
} crsfSettings;

static int      crsfSetValidBytes = 0;
static uint32_t crsfSettingsHash  = 0;
static bool     haveCRSFSettings  = false;
static uint32_t crsfPageSettingsHash = 0;

static const char *crsfCfgName      = "/crsfcfg";
static const char *crsfPageCfgName  = "/remcrsfcfg.json";

namespace {
#if ARDUINOJSON_VERSION_MAJOR >= 7
#define CRSF_DECLARE_JSON(name, size) JsonDocument name
#else
#define CRSF_DECLARE_JSON(name, size) DynamicJsonDocument name(size)
#endif

static bool crsfPrimaryStorageUsesSD()
{
    return FlashROMode;
}

static bool crsfStorageHasPrimary()
{
    return crsfPrimaryStorageUsesSD() ? haveSD : haveFS;
}

static bool crsfStorageExists(const char *fn, bool useSD)
{
    if(useSD) {
        return haveSD && SD.exists(fn);
    }
    return haveFS && MYNVS.exists(fn);
}

static bool crsfOpenForRead(File& file, const char *fn, bool useSD)
{
    if(useSD) {
        if(haveSD && SD.exists(fn)) {
            file = SD.open(fn, FILE_READ);
        }
    } else if(haveFS && MYNVS.exists(fn)) {
        file = MYNVS.open(fn, FILE_READ);
    }
    return (bool)file;
}

static bool crsfOpenAnyForRead(File& file, const char *fn)
{
    if(crsfOpenForRead(file, fn, crsfPrimaryStorageUsesSD())) {
        return true;
    }
    return crsfOpenForRead(file, fn, !crsfPrimaryStorageUsesSD());
}

static bool crsfWriteBuffer(const char *fn, const uint8_t *buf, size_t len)
{
    File file;

    if(crsfPrimaryStorageUsesSD()) {
        if(!haveSD) {
            return false;
        }
        file = SD.open(fn, FILE_WRITE);
    } else {
        if(!haveFS) {
            return false;
        }
        file = MYNVS.open(fn, FILE_WRITE);
    }

    if(!file) {
        return false;
    }

    bool ok = (file.write(buf, len) == len);
    file.close();
    return ok;
}

static bool crsfReadJSON(File& file, JsonDocument& json, uint32_t *readHash = NULL)
{
    size_t bufSize = file.size();
    char *buf = (char *)malloc(bufSize + 1);
    bool ok = false;

    if(!buf) {
        file.close();
        return false;
    }

    memset(buf, 0, bufSize + 1);
    if(file.read((uint8_t *)buf, bufSize) == bufSize) {
        if(readHash) {
            *readHash = calcHash((uint8_t *)buf, (int)bufSize);
        }
        ok = !deserializeJson(json, buf);
    }
    file.close();
    free(buf);
    return ok;
}

static bool crsfCopyValidNumParm(const char *json, char *text, int psize, int lowerLim, int upperLim, int setDefault)
{
    int i = setDefault;
    bool ret = false;

    if(json && *json) {
        int len = strlen(json);
        ret = false;
        for(int j = 0; j < len; j++) {
            if(json[j] < '0' || json[j] > '9') {
                ret = true;
                break;
            }
        }
        if(!ret) {
            i = atoi(json);
            if(i < lowerLim) {
                i = lowerLim;
                ret = true;
            } else if(i > upperLim) {
                i = upperLim;
                ret = true;
            }
        }
    } else {
        ret = true;
    }

    snprintf(text, psize, "%d", i);
    return ret;
}

static void crsfSetPageDefaults()
{
    snprintf(settings.opMode, sizeof(settings.opMode), "%d", DEF_OPMODE);
    crsf_normalizeELRSPacketRate(NULL, settings.elrsPktRate);
}

}

void crsf_load_settings()
{
    if(loadConfigFile(crsfCfgName, (uint8_t *)&crsfSettings, sizeof(crsfSettings), crsfSetValidBytes, 0)) {
        crsfSettingsHash = calcHash((uint8_t *)&crsfSettings, sizeof(crsfSettings));
        haveCRSFSettings = true;
    }
}

bool crsf_save_settings(bool useCache)
{
    uint32_t oldHash = crsfSettingsHash;

    crsfSettingsHash = calcHash((uint8_t *)&crsfSettings, sizeof(crsfSettings));

    if(useCache) {
        if(oldHash == crsfSettingsHash) {
            return true;
        }
    }

    return saveConfigFile(crsfCfgName, (uint8_t *)&crsfSettings, sizeof(crsfSettings), 0);
}

bool crsf_settings_exist()
{
    return crsfStorageExists(crsfPageCfgName, crsfPrimaryStorageUsesSD());
}

void crsf_read_page_settings()
{
    File configFile;
    CRSF_DECLARE_JSON(json, 512);
    bool writeDefaults = true;
    bool rewrite = false;

    if(!crsfStorageHasPrimary()) {
        crsfSetPageDefaults();
        return;
    }

    if(crsfOpenAnyForRead(configFile, crsfPageCfgName) && crsfReadJSON(configFile, json, &crsfPageSettingsHash)) {
        writeDefaults = false;
        rewrite |= crsfCopyValidNumParm(json["opMode"], settings.opMode, sizeof(settings.opMode), 0, 1, DEF_OPMODE);
        rewrite |= crsf_normalizeELRSPacketRate(json["ePRHz"], settings.elrsPktRate);
    }

    if(writeDefaults) {
        crsfSetPageDefaults();
        crsfPageSettingsHash = 0;
        crsf_write_page_settings();
    } else if(rewrite) {
        crsf_write_page_settings();
    }
}

void crsf_write_page_settings()
{
    CRSF_DECLARE_JSON(json, 256);
    size_t bufSize = 0;
    char *buf = NULL;
    uint32_t newHash = 0;

    if(!crsfStorageHasPrimary()) {
        return;
    }

    json["opMode"] = (const char *)settings.opMode;
    json["ePRHz"] = (const char *)settings.elrsPktRate;

    bufSize = measureJson(json);
    buf = (char *)malloc(bufSize + 1);
    if(!buf) {
        return;
    }

    memset(buf, 0, bufSize + 1);
    serializeJson(json, buf, bufSize + 1);

    newHash = calcHash((uint8_t *)buf, (int)bufSize);
    if(crsfPageSettingsHash && crsfPageSettingsHash == newHash) {
        free(buf);
        return;
    }

    if(crsfWriteBuffer(crsfPageCfgName, (const uint8_t *)buf, bufSize)) {
        crsfPageSettingsHash = newHash;
    }

    free(buf);
}

void loadELRSCalibration(ELRSAxisCalibrationData *cal, int count)
{
    if(!cal) {
        return;
    }

    count = min(count, ELRS_GIMBAL_AXIS_COUNT);
    for(int i = 0; i < count; i++) {
        cal[i] = crsfSettings.elrsAxis[i];
    }
}

void saveELRSCalibration(const ELRSAxisCalibrationData *cal, int count)
{
    if(!cal) {
        return;
    }

    count = min(count, ELRS_GIMBAL_AXIS_COUNT);
    for(int i = 0; i < count; i++) {
        crsfSettings.elrsAxis[i] = cal[i];
    }
    crsf_save_settings(true);
}

bool crsf_normalizeELRSPacketRate(const char *src, char *dest)
{
    uint16_t packetRateHz = ELRS_PACKET_RATE_DEFAULT;
    bool wd = false;

    if(!src) {
        wd = true;
    } else {
        packetRateHz = (uint16_t)atoi(src);
        if(!elrsPacketRateSupported(packetRateHz)) {
            packetRateHz = ELRS_PACKET_RATE_DEFAULT;
            wd = true;
        }
    }
    sprintf(dest, "%u", (unsigned)packetRateHz);
    return wd;
}

#endif
