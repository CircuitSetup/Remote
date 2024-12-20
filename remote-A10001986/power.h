/*
 * -------------------------------------------------------------------
 * Remote Control
 * (C) 2024 Thomas Winischhofer (A10001986)
 * https://github.com/realA10001986/Remote
 * https://remote.out-a-ti.me
 *
 * Power Monitor class (LC709204F)
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

#ifndef REM_PWRMON
#define REM_PWRMON

#ifdef HAVE_PM

// Battery profiles
#define BAT_PROF_01  0    // Type 1: 3.7V-4.2V
#define BAT_PROF_04  1    // Type 4: UR18650ZY(Panasonic) 3.7V-4.2V
#define BAT_PROF_05  2    // Type 5: ICR18650-26H(Samsung) 3.63V-4.2V
#define BAT_PROF_06  3    // Type 6: 3.8V-4.35V
#define BAT_PROF_07  4    // Type 7: 3.85V-4.4V
#define BAT_PROF_MAX BAT_PROF_07

/* remPwrMon Class */

class remPowMon {

    public:

        remPowMon(uint8_t address);
        bool     begin(bool doUse, int battProf=BAT_PROF_01, uint16_t battCap=2000); // Cap per cell

        bool     havePM();

        int      loop();
        bool     readSOC();
        bool     readVoltage();
        bool     readTimeToEmpty();

        bool     _useAlarm = true;    // Use alarm pin instead of reading via i2c

        bool     _haveSOC = false;
        bool     _haveTTE = false;
        bool     _haveVolt = false;
        bool     _haveCharging = false;

        uint16_t _soc = 0;
        uint16_t _tte = 0;
        float    _voltage = 0;
        bool     _charging = false;

        bool     _battWarn = false;

        int      _lowSCond = -1;
        
    private:
        bool     read16(uint16_t regno, uint16_t& val);
        void     write16(uint16_t regno, uint16_t value);

        uint8_t _address;
        uint8_t _crcAR, _crcAW;
        bool    _usePwrMon;
        bool    _havePwrMon;

        unsigned long _lastAScan;
        unsigned long _lastSScan;
        unsigned long _lastTScan;
        unsigned long _SSScanInt;
        unsigned long _TTScanInt;
                
};
#endif

#endif
