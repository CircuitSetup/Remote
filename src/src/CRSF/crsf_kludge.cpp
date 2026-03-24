/*
 * -------------------------------------------------------------------
 * Remote Control
 * (C) 2026 Thomas Winischhofer (A10001986)
 * https://github.com/realA10001986/Remote
 * https://remote.out-a-ti.me
 *
 * CRSF kludge
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

#include <Arduino.h>

#include "elrs_crsf_shared.h"
#include "elrs_crsf.h"
#include "crsf_kludge.h"

static const uint16_t packetRates[4] = {
    ELRS_PACKET_RATE_50HZ,
    ELRS_PACKET_RATE_100HZ,
    ELRS_PACKET_RATE_150HZ,
    ELRS_PACKET_RATE_250HZ
};

static const uint8_t speedUnits[2] = {
    ELRS_SPEED_UNITS_KMH,
    ELRS_SPEED_UNITS_MPH
};

uint16_t crsf_getPacketRate(int idx)
{
    if(idx < 0 || idx > 3) idx = 3;
    return packetRates[idx];
}

uint8_t crsf_getSpeedUnits(int idx)
{
    if(idx < 0 || idx > 1) idx = 0;
    return speedUnits[idx];
}

bool crsf_begin(
            uint16_t packetRateHz,
            uint8_t speedDisplayUnits,
            ButtonPack *buttonPack,
            bool haveButtonPack,
            remDisplay *remdisplay,
            remLED *pwrled,
            remLED *bLvLMeter,
            remLED *remledStop,
            bool usePowerLed,
            bool useLevelMeter,
            bool powerLedOnFakePower,
            bool levelMeterOnFakePower)
{
    return elrsMode.begin(
            packetRateHz,
            speedDisplayUnits,
            buttonPack,
            haveButtonPack,
            remdisplay,
            pwrled,
            bLvLMeter,
            remledStop,
            usePowerLed,
            useLevelMeter,
            powerLedOnFakePower,
            levelMeterOnFakePower
        );
}

void crsf_loop(int battWarn)
{
    elrsMode.loop(battWarn);
}

void csrf_query_status(bool &FPBUnitIsOn)
{
    ELRSCrsfStatus elrsStatus = elrsMode.getStatus();
    FPBUnitIsOn = elrsStatus.fakePowerOn;
    //calibMode = elrsStatus.calibrating;
}


#endif
