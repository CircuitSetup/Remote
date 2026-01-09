/*
 * -------------------------------------------------------------------
 * Remote Control
 * (C) 2024-2026 Thomas Winischhofer (A10001986)
 * https://github.com/realA10001986/Remote
 * https://remote.out-a-ti.me
 *
 * Power Monitor class (LC709204F)
 * 
 * 5V batteries normally use a common LiPo cell (which delivers somewhere
 * between 3.7V and 4.2V) in connection with a booster IC to shift voltage
 * to 5V, which is what the Control Board needs. 
 * Battery monitoring requires connecting the BATT+ header on the Control 
 * Board or the "BATT" connector on the Add-On board to the output of 
 * the LiPo cell(s), NOT the power output of the booster.
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
 * -------------------------------------------------------------------
 */
#include "remote_global.h"

#ifdef HAVE_PM

#include <Arduino.h>
#include <math.h>
#include "power.h"
#include <Wire.h>

// SoC limits for "Low battery"
// < _LOW = trigger warning
// > _HIGH = end warning
#define S_LIMIT_LOW   8
#define S_LIMIT_HIGH  10

// Scan intervals
#define SCANA_INT      60000
#define SCANS_INT_BOOT 75000
#define SCANS_INT      60000
#define SCANT_INT_BOOT 90000
#define SCANT_INT      60000

// Assumed battery cell temperature (in Celsius)
#define CELLTEMP 25

static const uint8_t battProfile[5] = {
    0x00, 0x01, 0x02, 0x03, 0x04
};

#define CAPTABSIZE 6
static const uint16_t apa[CAPTABSIZE][6] = {
    { 1000, 0x2d, 0x10, 0x06, 0x1e, 0x13 },   // 1000mAh
    { 2000, 0x3a, 0x10, 0x06, 0x28, 0x19 },   // 2000mAh
    { 3000, 0x3f, 0x10, 0x06, 0x30, 0x1c },   // 3000mAh
    { 4000, 0x42, 0x10, 0x06, 0x34, 0x1c },   // 4000mAh
    { 5000, 0x44, 0x10, 0x06, 0x36, 0x1c },   // 5000mAh
    { 6000, 0x45, 0x10, 0x06, 0x37, 0x1c }    // 6000mAh
};

#define REG_TTE                 0x03
#define REG_BEF_RSOC            0x04
#define REG_TTF                 0x05
#define REG_INIT_RSOC           0x07
#define REG_CELL_TEMP           0x08
#define REG_CELL_VOLTAGE        0x09
#define REG_CURR_DIR            0x0a
#define REG_APA                 0x0b
#define REG_APT                 0x0c
#define REG_RSOC                0x0d
#define REG_ITE                 0x0f
#define REG_IC_VERSION          0x11
#define REG_SET_BATT_PROFILE    0x12
#define REG_IC_POWER_MODE       0x15
#define REG_STATUS_BIT          0x16
#define REG_CYCLE_COUNT         0x17
#define REG_BATTERY_STATUS      0x19
#define REG_GET_BATT_PROFILE    0x1a
#define REG_TERM_CURRENT_RATE   0x1b
#define REG_EMPTY_CELL_VOLTAGE  0x1d
#define REG_ITE_OFFSET          0x1e
#define REG_TOTAL_RUN_TIME_LOW  0x24
#define REG_TOTAL_RUN_TIME_HIGH 0x25
#define REG_ACC_RSOC_LOW        0x28
#define REG_ACC_RSOC_HIGH       0x29
#define REG_MAX_CELL_VOLTAGE    0x2a
#define REG_MIN_CELL_VOLTAGE    0x2b
#define REG_SOH                 0x32

#define REG_ALM_SOC             0x13
#define REG_ALM_VOLT            0x14
#define REG_ALM_HVOLT           0x1f
#define REG_ALM_LTEMP           0x20
#define REG_ALM_HTEMP           0x21

static uint8_t crc8_atm(uint8_t len, uint8_t *buf)
{
    uint8_t crc = 0;
 
    while(len--) {
        crc ^= *buf++;
 
        for(uint8_t i = 0; i < 8; i++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
        }
    }
 
    return crc;
}

// Store i2c address
remPowMon::remPowMon(uint8_t address)
{
    _address = address;
}

// Start the display
bool remPowMon::begin(bool doUse, int battProf, uint16_t battCap)
{
    uint16_t temp = 0, apaval = 0;
    bool doReset = false;
    
    _usePwrMon = _havePwrMon = false;

    if(battProf < 0 || battProf > sizeof(battProfile) - 1)
        return false;

    // Battery monitor alarm pin setup
    pinMode(BALM_PIN, INPUT);

    // Check for IC on i2c bus
    Wire.beginTransmission(_address);
    if(!Wire.endTransmission(true)) {

        // Shortcuts for CRC calculation
        _crcAW = _address << 1;
        _crcAR = _crcAW + 1;

        if(read16(REG_TTE, temp)) {
          
            _havePwrMon = true;

            //Wire.setTimeOut(80); //(uint16_t timeOutMillis); default: 50

            // Calculate APA
            if(battProf == 1 || battProf == 2) {
                apaval = apa[0][battProf+1];
            } else {
                for(int i = 0; i < CAPTABSIZE; i++) {
                    if(battCap == apa[i][0]) {
                        apaval = apa[i][battProf+1];
                        break;
                    } else if(battCap < apa[i][0]) {
                        if(i > 0) {
                            apaval = apa[i-1][battProf+1] + 
                                      ((apa[i][battProf+1] - apa[i-1][battProf+1]) *
                                       (battCap - apa[i-1][0]) /
                                       (apa[i][0] - apa[i-1][0]));
                        } else {
                            apaval = apa[0][battProf+1];
                        }
                        break;
                    }
                }
                if(!apaval) {
                    apaval = apa[CAPTABSIZE-1][battProf+1];
                }
            }
            #ifdef REMOTE_DBG
            Serial.printf("BatMon: Calculated APA %x\n", apaval);
            #endif

            if(read16(REG_BATTERY_STATUS, temp)) {
                if(temp & 0x80) doReset |= true;
            } else doReset |= true;
            if(read16(REG_APA, temp)) {
                if(temp != (apaval | (apaval << 8))) doReset |= true;
            } else doReset |= true;
            if(read16(REG_SET_BATT_PROFILE, temp)) {
                if(temp != battProfile[battProf]) doReset |= true;
            } else doReset |= true;
            
            if(doReset) {
                #ifdef REMOTE_DBG
                Serial.println("BatMon: Reset due to new battery or changed parameters");
                #endif
                // Write APA
                write16(REG_APA, apaval | (apaval << 8));
                // Write battery type
                write16(REG_SET_BATT_PROFILE, battProfile[battProf]);
            }
            // We have no thermistor
            write16(REG_STATUS_BIT, 0x0000);
            // Write cell temp
            write16(REG_CELL_TEMP, 0x980 + ((30 + CELLTEMP) * 10));
            // auto mode
            write16(REG_CURR_DIR, 0x0000);
            // Alarm thresholds
            write16(REG_ALM_SOC, S_LIMIT_LOW);
            write16(REG_ALM_VOLT, 0x0000);
            write16(REG_ALM_HVOLT, 0x0000);
            write16(REG_ALM_LTEMP, 0x0000);
            write16(REG_ALM_HTEMP, 0x0000);
            // Switch to "operational mode"
            write16(REG_IC_POWER_MODE, 0x0001);
            // Reset batt status
            write16(REG_BATTERY_STATUS, 0x0000);
            
            _SSScanInt = SCANS_INT_BOOT;
            _TTScanInt = SCANT_INT_BOOT;

            _usePwrMon = doUse;
        } else {
            #ifdef REMOTE_DBG
            Serial.println("Reading from BatMon IC failed");
            #endif
        }
    } else {
        #ifdef REMOTE_DBG
        Serial.println("No response from BatMon IC");
        #endif
    }

    return _usePwrMon;
}

bool remPowMon::havePM()
{
    return _havePwrMon;
}

int remPowMon::loop()
{
    if(_usePwrMon) {
        unsigned long now = millis();
        if(_useAlarm) {
            if((now - _lastAScan > SCANA_INT)) {
                _lastAScan = now;
                _battWarn = digitalRead(BALM_PIN) ? 0 : 1;
            }
            return _battWarn;
        } else if(now - _lastSScan > _SSScanInt) {
            _lastSScan = now;
            _SSScanInt = SCANS_INT;
            if(readSOC()) {
                _haveSOC = true;
                if(!_lowSCond) {
                    if(_soc < S_LIMIT_LOW) {
                        _lowSCond = 1;
                    }
                } else if(_soc > S_LIMIT_HIGH) {
                    _lowSCond = 0;
                }
            } else {
                _haveSOC = false;
                _lowSCond = -1;
            }
        } else if(now - _lastTScan > _TTScanInt) {
            _lastTScan = now;
            _TTScanInt = SCANT_INT;
            _haveTTE = readTimeToEmpty();
        }
        _battWarn = 0;
        if(_lowSCond > 0) _battWarn |= 1;
        return _battWarn;
    } else {
        return 0;
    }
}

bool remPowMon::readSOC()
{
    if(_havePwrMon) {
        uint16_t temp;
        if(read16(REG_RSOC, temp)) {
            _soc = temp;
            return true;
        }
    }
    return false;
}

// Read Time-to-Empty when discharging, Time-to-Full when charging
bool remPowMon::readTimeToEmpty()
{
    if(_havePwrMon) {
        uint16_t temp, reg;
        if(!read16(REG_BATTERY_STATUS, temp)) {
            _haveCharging = false;
            return false;
        }
        _charging = !!!(temp & 0x0040);
        #ifdef REMOTE_DBG
        Serial.printf("BatMon: Charging is %d (0x%x)\n",  _charging, temp);
        #endif
        _haveCharging = true;
        reg = (temp & 0x0040) ? REG_TTE : REG_TTF;
        if(read16(reg, temp)) {
            _tte = temp;
            #ifdef REMOTE_DBG
            Serial.printf("BatMon: TTx %x %x\n",  reg, _tte);
            #endif
            return true;
        }
    }
    return false;
}

bool remPowMon::readVoltage()
{
    if(_havePwrMon) {
        uint16_t temp;
        if(read16(REG_CELL_VOLTAGE, temp)) {
            _voltage = (float)temp  / 1000.0f;
            return true;
        }
    }
    return false;
}

bool remPowMon::read16(uint16_t regno, uint16_t& val)
{
    size_t i2clen = 0;
    uint8_t buf[6];

    buf[0] = _crcAW;
    buf[1] = (uint8_t)regno;
    buf[2] = _crcAR;

    Wire.beginTransmission(_address);
    Wire.write(buf[1]);
    Wire.endTransmission(false);

    i2clen = Wire.requestFrom(_address, (uint8_t)3);

    if(i2clen == 3) {
        buf[3] = Wire.read();
        buf[4] = Wire.read();
        buf[5] = Wire.read();
        if(crc8_atm(5, buf) == buf[5]) {
            val = buf[3] | (buf[4] << 8);
            return true;
        }
    }

    return false;
}

void remPowMon::write16(uint16_t regno, uint16_t value)
{
    uint8_t buf[6];
    buf[0] = _crcAW;
    buf[1] = (uint8_t)regno;
    buf[2] = value & 0xff;
    buf[3] = value >> 8;
    buf[4] = crc8_atm(4, buf);
    Wire.beginTransmission(_address);
    Wire.write(buf[1]);
    Wire.write(buf[2]);
    Wire.write(buf[3]);
    Wire.write(buf[4]);
    Wire.endTransmission();
}

#endif
