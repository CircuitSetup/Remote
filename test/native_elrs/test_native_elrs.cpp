#include <deque>
#include <cstring>
#include <string>
#include <vector>

#include <unity.h>

#include "src/CRSF/elrs_crsf_core.h"
#include "src/CRSF/elrs_input_model.h"

namespace {

constexpr uint8_t AXIS_AILERON = 0;
constexpr uint8_t AXIS_ELEVATOR = 1;
constexpr uint8_t AXIS_RUDDER = 2;
constexpr uint8_t AXIS_THROTTLE = 3;

enum DisplayMode {
    DISPLAY_NONE = 0,
    DISPLAY_TEXT,
    DISPLAY_SPEED
};

class FakeHost : public ELRSCrsfHost {
    public:
        FakeHost()
        {
            resetCalibrationDefaults();
        }

        void resetCalibrationDefaults()
        {
            for(int i = 0; i < ELRS_GIMBAL_AXIS_COUNT; i++) {
                calibration[i].minimum = 0;
                calibration[i].center = 1024;
                calibration[i].maximum = 2047;
            }
        }

        void logMessage(const char *message) override
        {
            logs.push_back(message ? message : "");
        }

        void startSerial(uint32_t baud, bool invert) override
        {
            bauds.push_back(baud);
            inversions.push_back(invert);
            driverEnabled = false;
        }

        void stopSerial() override
        {
            stopSerialCount++;
        }

        int serialAvailable() override
        {
            return (int)rx.size();
        }

        int serialRead() override
        {
            if(rx.empty()) {
                return -1;
            }

            uint8_t value = rx.front();
            rx.pop_front();
            serialReadCount++;
            return value;
        }

        size_t serialWrite(const uint8_t *data, size_t len) override
        {
            writes.push_back(std::vector<uint8_t>(data, data + len));
            driverStatesDuringWrite.push_back(driverEnabled);
            if(loopbackWriteToRx) {
                for(size_t i = 0; i < len; i++) {
                    rx.push_back(data[i]);
                }
                if(!rxAppendAfterWrite.empty()) {
                    for(size_t i = 0; i < rxAppendAfterWrite.size(); i++) {
                        rx.push_back(rxAppendAfterWrite[i]);
                    }
                    rxAppendAfterWrite.clear();
                }
            }
            return len;
        }

        void serialFlush() override
        {
            flushCount++;
        }

        void setDriverEnabled(bool enabled) override
        {
            if(driverTransitions.empty() || driverTransitions.back() != enabled) {
                driverTransitions.push_back(enabled);
            }
            driverEnabled = enabled;
        }

        void discardSerialInput() override
        {
            rx.clear();
            discardSerialCount++;
        }

        unsigned long microsNow() override
        {
            return fakeMicros;
        }

        bool sampleAxes(int16_t axesOut[ELRS_GIMBAL_AXIS_COUNT]) override
        {
            if(!axesAvailable) {
                return false;
            }

            for(int i = 0; i < ELRS_GIMBAL_AXIS_COUNT; i++) {
                axesOut[i] = axes[i];
            }

            return true;
        }

        bool readFakePowerSwitch() override
        {
            return fakePower;
        }

        bool readStopSwitch() override
        {
            return stop;
        }

        bool readButtonA() override
        {
            return buttonA;
        }

        bool readButtonB() override
        {
            return buttonB;
        }

        bool readCalibrationButton() override
        {
            return calibrationButton;
        }

        bool samplePackStates(uint8_t &states) override
        {
            if(!packAvailable) {
                return false;
            }

            states = packStates;
            return true;
        }

        void displayOn() override
        {
            displayOnCalled = true;
        }

        void displaySetText(const char *text) override
        {
            displayMode = DISPLAY_TEXT;
            displayText = text ? text : "";
        }

        void displaySetSpeed(int speed) override
        {
            displayMode = DISPLAY_SPEED;
            displaySpeed = speed;
        }

        void displayShow() override
        {
            displayShows++;
        }

        void setPowerLed(bool state) override
        {
            powerLed = state;
        }

        bool getPowerLed() const override
        {
            return powerLed;
        }

        void setLevelMeter(bool state) override
        {
            levelMeter = state;
        }

        bool getLevelMeter() const override
        {
            return levelMeter;
        }

        void setStopLed(bool state) override
        {
            stopLed = state;
        }

        void loadCalibration(ELRSAxisCalibrationData *cal, int count) override
        {
            for(int i = 0; i < count && i < ELRS_GIMBAL_AXIS_COUNT; i++) {
                cal[i] = calibration[i];
            }
        }

        void saveCalibration(const ELRSAxisCalibrationData *cal, int count) override
        {
            savedCalibrationCount = count;
            for(int i = 0; i < count && i < ELRS_GIMBAL_AXIS_COUNT; i++) {
                calibration[i] = cal[i];
            }
        }

        void queueFrame(const std::vector<uint8_t> &frame)
        {
            for(size_t i = 0; i < frame.size(); i++) {
                rx.push_back(frame[i]);
            }
        }

        int16_t axes[ELRS_GIMBAL_AXIS_COUNT] = { 1024, 1024, 1024, 1024 };
        ELRSAxisCalibrationData calibration[ELRS_GIMBAL_AXIS_COUNT];
        bool axesAvailable = true;
        bool fakePower = false;
        bool stop = false;
        bool buttonA = false;
        bool buttonB = false;
        bool calibrationButton = false;
        bool packAvailable = true;
        uint8_t packStates = 0;

        bool driverEnabled = false;
        unsigned long fakeMicros = 0;
        int serialReadCount = 0;
        bool displayOnCalled = false;
        bool powerLed = false;
        bool levelMeter = false;
        bool stopLed = false;
        int displaySpeed = -1;
        int displayShows = 0;
        int savedCalibrationCount = 0;
        int discardSerialCount = 0;
        int stopSerialCount = 0;
        int flushCount = 0;
        DisplayMode displayMode = DISPLAY_NONE;
        std::string displayText;
        std::deque<uint8_t> rx;
        bool loopbackWriteToRx = false;
        std::vector<uint8_t> rxAppendAfterWrite;
        std::vector<std::string> logs;
        std::vector<uint32_t> bauds;
        std::vector<bool> inversions;
        std::vector<bool> driverTransitions;
        std::vector<bool> driverStatesDuringWrite;
        std::vector<std::vector<uint8_t> > writes;
};

static ELRSCrsfCoreConfig defaultConfig()
{
    ELRSCrsfCoreConfig config;
    config.haveButtonPack = true;
    config.usePowerLed = false;
    config.useLevelMeter = false;
    config.powerLedOnFakePower = true;
    config.levelMeterOnFakePower = true;
    return config;
}

static std::vector<uint8_t> makeFrame(uint8_t type, const std::vector<uint8_t> &payload)
{
    std::vector<uint8_t> frame;
    frame.push_back(0xC8);
    frame.push_back((uint8_t)(payload.size() + 2));
    frame.push_back(type);
    frame.insert(frame.end(), payload.begin(), payload.end());
    frame.push_back(ELRSCrsfCore::crc8D5(&frame[2], payload.size() + 1));
    return frame;
}

static std::vector<uint8_t> makeFrameWithSync(uint8_t syncByte, uint8_t type, const std::vector<uint8_t> &payload)
{
    std::vector<uint8_t> frame = makeFrame(type, payload);

    frame[0] = syncByte;
    return frame;
}

static std::vector<uint8_t> makeExtendedFrame(uint8_t syncByte, uint8_t type, uint8_t dest, uint8_t orig, const std::vector<uint8_t> &payload)
{
    std::vector<uint8_t> frame;

    frame.push_back(syncByte);
    frame.push_back((uint8_t)(payload.size() + 4));
    frame.push_back(type);
    frame.push_back(dest);
    frame.push_back(orig);
    frame.insert(frame.end(), payload.begin(), payload.end());
    frame.push_back(ELRSCrsfCore::crc8D5(&frame[2], payload.size() + 3));

    return frame;
}

static std::vector<uint8_t> makeDeviceInfoFrame(const char *name, uint8_t fieldCount)
{
    std::vector<uint8_t> payload;
    const char *deviceName = name ? name : "ExpressLRS TX";

    payload.insert(payload.end(), deviceName, deviceName + strlen(deviceName) + 1);
    payload.push_back(0x45);
    payload.push_back(0x4C);
    payload.push_back(0x52);
    payload.push_back(0x53);
    payload.push_back(0x00);
    payload.push_back(0x00);
    payload.push_back(0x00);
    payload.push_back(0x00);
    payload.push_back(0x00);
    payload.push_back(0x00);
    payload.push_back(0x00);
    payload.push_back(0x00);
    payload.push_back(fieldCount);
    payload.push_back(0x00);

    return makeExtendedFrame(0xEE, 0x29, 0xEA, 0xEE, payload);
}

static std::vector<uint8_t> makeTextSelectionEntryFrame(uint8_t fieldId, const char *name, const char *options, uint8_t value, uint8_t maxValue)
{
    std::vector<uint8_t> payload;

    payload.push_back(fieldId);
    payload.push_back(0x00);
    payload.push_back(0x00);
    payload.push_back(0x09);
    payload.insert(payload.end(), name, name + strlen(name) + 1);
    payload.insert(payload.end(), options, options + strlen(options) + 1);
    payload.push_back(value);
    payload.push_back(0x00);
    payload.push_back(maxValue);
    payload.push_back(0x00);
    payload.push_back(0x00);

    return makeExtendedFrame(0xEE, 0x2B, 0xEA, 0xEE, payload);
}

static std::vector<uint8_t> makeTextSelectionEntryData(const char *name, const char *options, uint8_t value, uint8_t maxValue)
{
    std::vector<uint8_t> data;

    data.push_back(0x00);
    data.push_back(0x09);
    data.insert(data.end(), name, name + strlen(name) + 1);
    data.insert(data.end(), options, options + strlen(options) + 1);
    data.push_back(value);
    data.push_back(0x00);
    data.push_back(maxValue);
    data.push_back(0x00);
    data.push_back(0x00);

    return data;
}

static std::vector<uint8_t> makeParameterEntryFrame(uint8_t fieldId, const char *name, uint8_t type)
{
    std::vector<uint8_t> payload;

    payload.push_back(fieldId);
    payload.push_back(0x00);
    payload.push_back(0x00);
    payload.push_back(type);
    payload.insert(payload.end(), name, name + strlen(name) + 1);

    return makeExtendedFrame(0xEE, 0x2B, 0xEA, 0xEE, payload);
}

static std::vector<uint8_t> makeParameterChunkFrame(uint8_t fieldId, uint8_t chunksRemain, const std::vector<uint8_t> &chunkData)
{
    std::vector<uint8_t> payload;

    payload.push_back(fieldId);
    payload.push_back(chunksRemain);
    payload.insert(payload.end(), chunkData.begin(), chunkData.end());

    return makeExtendedFrame(0xEE, 0x2B, 0xEA, 0xEE, payload);
}

static int countWrittenFrameType(const FakeHost &host, uint8_t type)
{
    int count = 0;

    for(size_t i = 0; i < host.writes.size(); i++) {
        if(host.writes[i].size() >= 3 && host.writes[i][2] == type) {
            count++;
        }
    }

    return count;
}

static const std::vector<uint8_t> *findWrittenFrameType(const FakeHost &host, uint8_t type, int occurrence)
{
    int seen = 0;

    for(size_t i = 0; i < host.writes.size(); i++) {
        if(host.writes[i].size() >= 3 && host.writes[i][2] == type) {
            if(seen == occurrence) {
                return &host.writes[i];
            }
            seen++;
        }
    }

    return NULL;
}

static std::string writtenFrameTypes(const FakeHost &host)
{
    std::string result;

    for(size_t i = 0; i < host.writes.size(); i++) {
        char buf[8];

        if(i) {
            result += ' ';
        }
        if(host.writes[i].size() >= 3) {
            snprintf(buf, sizeof(buf), "%02X", host.writes[i][2]);
        } else {
            snprintf(buf, sizeof(buf), "--");
        }
        result += buf;
    }

    return result;
}

static bool logsContain(const FakeHost &host, const char *needle)
{
    if(!needle || !*needle) {
        return false;
    }

    for(size_t i = 0; i < host.logs.size(); i++) {
        if(host.logs[i].find(needle) != std::string::npos) {
            return true;
        }
    }

    return false;
}

static std::vector<uint8_t> makeGarbage()
{
    return std::vector<uint8_t>{ 0x00, 0x7F, 0x81, 0x42, 0x18, 0xFF, 0x10 };
}

static void queueBytes(FakeHost &host, const std::vector<uint8_t> &bytes)
{
    for(size_t i = 0; i < bytes.size(); i++) {
        host.rx.push_back(bytes[i]);
    }
}

static ELRSCrsfStatus statusOf(ELRSCrsfCore &core)
{
    return core.getStatus();
}

static void loopAt(ELRSCrsfCore &core, FakeHost &host, unsigned long nowMs, unsigned long nowUs, int battWarn = 0)
{
    core.loop(host, nowMs, nowUs, battWarn);
}

static void test_rc_frame_packing_and_driver_enable()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    static const uint8_t expectedFrame[26] = {
        0xC8, 0x18, 0x16, 0x13, 0x07, 0x1F, 0x2B, 0x26, 0x3E, 0x71, 0x56, 0x4C, 0x9C,
        0x15, 0xAC, 0x98, 0x38, 0x2B, 0x26, 0xCE, 0x0A, 0x56, 0x4C, 0x7C, 0xE2, 0xB8
    };

    host.axes[AXIS_AILERON] = 2047;
    host.axes[AXIS_ELEVATOR] = 1024;
    host.axes[AXIS_THROTTLE] = 0;
    host.axes[AXIS_RUDDER] = 2047;
    host.stop = true;
    host.buttonA = true;
    host.packStates = 0b11001010;

    TEST_ASSERT_TRUE(core.begin(host, config, 0));
    core.loop(host, 10, 0);

    TEST_ASSERT_EQUAL_UINT32(400000, host.bauds[0]);
    TEST_ASSERT_FALSE(host.inversions[0]);
    TEST_ASSERT_EQUAL_INT(1, host.stopSerialCount);
    TEST_ASSERT_EQUAL_INT(1, host.discardSerialCount);
    TEST_ASSERT_EQUAL_INT(1, host.flushCount);
    TEST_ASSERT_EQUAL_INT(1, (int)host.writes.size());
    TEST_ASSERT_TRUE(host.driverStatesDuringWrite[0]);
    TEST_ASSERT_FALSE(host.driverEnabled);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(2, (int)host.driverTransitions.size());
    TEST_ASSERT_TRUE(host.driverTransitions[host.driverTransitions.size() - 2]);
    TEST_ASSERT_FALSE(host.driverTransitions[host.driverTransitions.size() - 1]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedFrame, host.writes[0].data(), 26);
}

static void test_transport_inversion_setting_is_passed_to_hal()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    config.transport.invertLine = true;

    TEST_ASSERT_TRUE(core.begin(host, config, 0));
    TEST_ASSERT_EQUAL_UINT32(400000, host.bauds[0]);
    TEST_ASSERT_TRUE(host.inversions[0]);
    TEST_ASSERT_TRUE(statusOf(core).invertLine);
}

static void test_transport_debug_suppresses_raw_frame_dumps_by_default()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    config.transport.debugEnabled = true;
    config.transport.rawFrameDebugEnabled = false;

    TEST_ASSERT_TRUE(core.begin(host, config, 0));
    core.loop(host, 10, 0);

    TEST_ASSERT_TRUE(logsContain(host, "ELRS/CRSF transport: UART"));
    TEST_ASSERT_FALSE(logsContain(host, "ELRS/CRSF TX len="));
    TEST_ASSERT_FALSE(logsContain(host, "ELRS/CRSF RX len="));
    TEST_ASSERT_TRUE(statusOf(core).debugEnabled);
    TEST_ASSERT_FALSE(statusOf(core).rawFrameDebugEnabled);
}

static void test_transport_raw_frame_dump_requires_explicit_opt_in()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    config.transport.debugEnabled = true;
    config.transport.rawFrameDebugEnabled = true;

    TEST_ASSERT_TRUE(core.begin(host, config, 0, 0));
    host.queueFrame(makeDeviceInfoFrame("RM Ranger Micro", 33));
    loopAt(core, host, 100, 100000);

    TEST_ASSERT_TRUE(logsContain(host, "ELRS/CRSF RX len=36"));
    TEST_ASSERT_TRUE(statusOf(core).rawFrameDebugEnabled);
}

static void test_transport_raw_frame_dump_logs_non_rc_replies_only()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    config.transport.debugEnabled = true;
    config.transport.rawFrameDebugEnabled = true;

    TEST_ASSERT_TRUE(core.begin(host, config, 0, 0));
    loopAt(core, host, 0, 0);

    host.queueFrame(makeDeviceInfoFrame("RM Ranger Micro", 33));
    loopAt(core, host, 100, 100000);

    TEST_ASSERT_TRUE(logsContain(host, "ELRS/CRSF RX len=36"));
    TEST_ASSERT_FALSE(logsContain(host, "ELRS/CRSF TX len="));
}

static void test_ads1015_single_ended_config_uses_4v096_range()
{
    TEST_ASSERT_EQUAL_HEX8(0xC3, elrsAds1015SingleEndedConfigHighByte(0));
    TEST_ASSERT_EQUAL_HEX8(0xD3, elrsAds1015SingleEndedConfigHighByte(1));
    TEST_ASSERT_EQUAL_HEX8(0xE3, elrsAds1015SingleEndedConfigHighByte(2));
    TEST_ASSERT_EQUAL_HEX8(0xF3, elrsAds1015SingleEndedConfigHighByte(3));
}

static void test_adc_debug_log_only_emits_on_axis_change()
{
    int16_t previous[ELRS_GIMBAL_AXIS_COUNT] = { 357, 334, 341, 2047 };
    int16_t same[ELRS_GIMBAL_AXIS_COUNT] = { 357, 334, 341, 2047 };
    int16_t smallJitter[ELRS_GIMBAL_AXIS_COUNT] = { 357, 334, 356, 2047 };
    int16_t changed[ELRS_GIMBAL_AXIS_COUNT] = { 357, 334, 362, 2047 };

    TEST_ASSERT_FALSE(elrsAxesChanged(same, previous, ELRS_GIMBAL_AXIS_COUNT));
    TEST_ASSERT_FALSE(elrsAxesChanged(smallJitter, previous, ELRS_GIMBAL_AXIS_COUNT, 20));
    TEST_ASSERT_TRUE(elrsAxesChanged(changed, previous, ELRS_GIMBAL_AXIS_COUNT, 20));
}

static void test_light_iir_filter_moves_quarter_step_toward_sample()
{
    TEST_ASSERT_EQUAL_INT16(1100, elrsIirFilterStep(1000, 1400, 2));
    TEST_ASSERT_EQUAL_INT16(1300, elrsIirFilterStep(1400, 1000, 2));
}

static void test_light_iir_filter_leaves_small_jitter_unchanged()
{
    TEST_ASSERT_EQUAL_INT16(1000, elrsIirFilterStep(1000, 1001, 2));
    TEST_ASSERT_EQUAL_INT16(1000, elrsIirFilterStep(1000, 1003, 2));
    TEST_ASSERT_EQUAL_INT16(1000, elrsIirFilterStep(1000, 999, 2));
}

static void test_input_model_center_maps_to_1500_us()
{
    const ELRSInputAxisProfile profile = elrsDefaultInputAxisProfile();

    TEST_ASSERT_EQUAL_INT16(1500, elrsInputModelAxisToUs(profile, 1024));
}

static void test_input_model_min_max_map_to_1000_and_2000_us()
{
    const ELRSInputAxisProfile profile = elrsDefaultInputAxisProfile();

    TEST_ASSERT_EQUAL_INT16(1000, elrsInputModelAxisToUs(profile, 0));
    TEST_ASSERT_EQUAL_INT16(2000, elrsInputModelAxisToUs(profile, 2047));
}

static void test_input_model_reverse_flips_output()
{
    ELRSInputAxisProfile profile = elrsDefaultInputAxisProfile();

    profile.reverse = 1;

    TEST_ASSERT_EQUAL_INT16(2000, elrsInputModelAxisToUs(profile, 0));
    TEST_ASSERT_EQUAL_INT16(1000, elrsInputModelAxisToUs(profile, 2047));
}

static void test_input_model_1500_us_maps_to_crsf_mid_ticks()
{
    TEST_ASSERT_EQUAL_UINT16(992, elrsInputUsToCrsfTicks(1500));
}

static void test_input_model_deadband_holds_output_at_1500_us_near_center()
{
    ELRSInputAxisProfile profile = elrsDefaultInputAxisProfile();

    profile.deadband = 20;

    TEST_ASSERT_EQUAL_INT16(1500, elrsInputModelAxisToUs(profile, 1004));
    TEST_ASSERT_EQUAL_INT16(1500, elrsInputModelAxisToUs(profile, 1024));
    TEST_ASSERT_EQUAL_INT16(1500, elrsInputModelAxisToUs(profile, 1044));
}

static void test_input_model_deadband_only_affects_center_band()
{
    ELRSInputAxisProfile profile = elrsDefaultInputAxisProfile();
    int16_t belowMid;
    int16_t aboveMid;

    profile.deadband = 20;

    belowMid = elrsInputModelAxisToUs(profile, 1003);
    aboveMid = elrsInputModelAxisToUs(profile, 1045);

    TEST_ASSERT_TRUE(belowMid < 1500);
    TEST_ASSERT_TRUE(aboveMid > 1500);
    TEST_ASSERT_EQUAL_INT16(1000, elrsInputModelAxisToUs(profile, 0));
    TEST_ASSERT_EQUAL_INT16(2000, elrsInputModelAxisToUs(profile, 2047));
}

static void test_input_model_default_profile_matches_raw_adc_defaults()
{
    const ELRSInputAxisProfile profile = elrsDefaultInputAxisProfile();

    TEST_ASSERT_EQUAL_INT16(0, profile.minimum);
    TEST_ASSERT_EQUAL_INT16(1024, profile.center);
    TEST_ASSERT_EQUAL_INT16(2047, profile.maximum);
    TEST_ASSERT_EQUAL_UINT16(0, profile.reverse);
    TEST_ASSERT_EQUAL_UINT16(0, profile.deadband);
    TEST_ASSERT_EQUAL_UINT8(0, profile.expo);
}

static void test_input_model_raw_three_point_profile_maps_exact_endpoints_and_center()
{
    ELRSInputAxisProfile profile = elrsDefaultInputAxisProfile();

    profile.minimum = 350;
    profile.center = 900;
    profile.maximum = 1500;

    TEST_ASSERT_EQUAL_INT16(1000, elrsInputModelAxisToUs(profile, 350));
    TEST_ASSERT_EQUAL_INT16(1500, elrsInputModelAxisToUs(profile, 900));
    TEST_ASSERT_EQUAL_INT16(2000, elrsInputModelAxisToUs(profile, 1500));
}

static void test_input_model_descending_raw_profile_maps_exact_endpoints_and_center()
{
    ELRSInputAxisProfile profile = elrsDefaultInputAxisProfile();

    profile.minimum = 1800;
    profile.center = 1000;
    profile.maximum = 300;

    TEST_ASSERT_TRUE(elrsIsValidInputAxisProfile(profile));
    TEST_ASSERT_EQUAL_INT16(1000, elrsInputModelAxisToUs(profile, 1800));
    TEST_ASSERT_EQUAL_INT16(1500, elrsInputModelAxisToUs(profile, 1000));
    TEST_ASSERT_EQUAL_INT16(2000, elrsInputModelAxisToUs(profile, 300));
}

static void test_input_model_descending_raw_profile_can_be_reversed()
{
    ELRSInputAxisProfile profile = elrsDefaultInputAxisProfile();

    profile.minimum = 1800;
    profile.center = 1000;
    profile.maximum = 300;
    profile.reverse = 1;

    TEST_ASSERT_TRUE(elrsIsValidInputAxisProfile(profile));
    TEST_ASSERT_EQUAL_INT16(2000, elrsInputModelAxisToUs(profile, 1800));
    TEST_ASSERT_EQUAL_INT16(1500, elrsInputModelAxisToUs(profile, 1000));
    TEST_ASSERT_EQUAL_INT16(1000, elrsInputModelAxisToUs(profile, 300));
}

static void test_input_model_default_gimbal_routing_matches_current_banner_order()
{
    const ELRSGimbalRouting routing = elrsDefaultGimbalRouting();

    TEST_ASSERT_EQUAL_UINT8(0, ELRS_GIMBAL_INPUT_AILERON);
    TEST_ASSERT_EQUAL_UINT8(1, ELRS_GIMBAL_INPUT_ELEVATOR);
    TEST_ASSERT_EQUAL_UINT8(2, ELRS_GIMBAL_INPUT_RUDDER);
    TEST_ASSERT_EQUAL_UINT8(3, ELRS_GIMBAL_INPUT_THROTTLE);
    TEST_ASSERT_EQUAL_UINT8(1, routing.aileronChannel);
    TEST_ASSERT_EQUAL_UINT8(2, routing.elevatorChannel);
    TEST_ASSERT_EQUAL_UINT8(3, routing.throttleChannel);
    TEST_ASSERT_EQUAL_UINT8(4, routing.rudderChannel);
}

static void test_input_model_invalid_profile_normalizes_to_default()
{
    ELRSInputAxisProfile profile = elrsDefaultInputAxisProfile();
    ELRSInputAxisProfile normalized;

    profile.minimum = -1024;
    profile.center = 0;
    profile.maximum = 1023;
    profile.reverse = 1;
    profile.deadband = 42;
    profile.expo = 7;

    normalized = elrsSanitizeInputAxisProfile(profile);

    TEST_ASSERT_EQUAL_INT16(0, normalized.minimum);
    TEST_ASSERT_EQUAL_INT16(1024, normalized.center);
    TEST_ASSERT_EQUAL_INT16(2047, normalized.maximum);
    TEST_ASSERT_EQUAL_UINT16(0, normalized.reverse);
    TEST_ASSERT_EQUAL_UINT16(0, normalized.deadband);
    TEST_ASSERT_EQUAL_UINT8(0, normalized.expo);
}

static void test_input_model_invalid_routing_normalizes_to_default()
{
    ELRSGimbalRouting routing = elrsDefaultGimbalRouting();
    ELRSGimbalRouting normalized;

    routing.aileronChannel = 0;
    routing.elevatorChannel = 17;

    normalized = elrsSanitizeGimbalRouting(routing);

    TEST_ASSERT_EQUAL_UINT8(1, normalized.aileronChannel);
    TEST_ASSERT_EQUAL_UINT8(2, normalized.elevatorChannel);
    TEST_ASSERT_EQUAL_UINT8(3, normalized.throttleChannel);
    TEST_ASSERT_EQUAL_UINT8(4, normalized.rudderChannel);
}

static void test_input_model_duplicate_routing_normalizes_to_default()
{
    ELRSGimbalRouting routing = elrsDefaultGimbalRouting();
    ELRSGimbalRouting normalized;

    routing.aileronChannel = 6;
    routing.elevatorChannel = 6;
    routing.throttleChannel = 7;
    routing.rudderChannel = 8;

    normalized = elrsSanitizeGimbalRouting(routing);

    TEST_ASSERT_EQUAL_UINT8(1, normalized.aileronChannel);
    TEST_ASSERT_EQUAL_UINT8(2, normalized.elevatorChannel);
    TEST_ASSERT_EQUAL_UINT8(3, normalized.throttleChannel);
    TEST_ASSERT_EQUAL_UINT8(4, normalized.rudderChannel);
}

static void test_echoed_tx_frame_is_ignored_as_reply()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    core.loop(host, 10, 0);

    TEST_ASSERT_EQUAL_INT(1, (int)host.writes.size());
    host.queueFrame(host.writes[0]);
    core.loop(host, 11, 0);

    ELRSCrsfStatus status = statusOf(core);
    TEST_ASSERT_FALSE(status.replyActive);
    TEST_ASSERT_FALSE(status.synced);
    TEST_ASSERT_FALSE(status.everReplied);
    TEST_ASSERT_EQUAL_UINT32(0, status.lastReplyAt);
    TEST_ASSERT_EQUAL_UINT32(0, status.lastRxAt);
}

static void test_delayed_echoed_tx_frame_is_ignored_as_reply()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    core.loop(host, 10, 0);

    TEST_ASSERT_EQUAL_INT(1, (int)host.writes.size());
    host.queueFrame(host.writes[0]);
    core.loop(host, 16, 0);

    ELRSCrsfStatus status = statusOf(core);
    TEST_ASSERT_FALSE(status.replyActive);
    TEST_ASSERT_FALSE(status.synced);
    TEST_ASSERT_FALSE(status.everReplied);
    TEST_ASSERT_EQUAL_UINT32(0, status.lastReplyAt);
    TEST_ASSERT_EQUAL_UINT32(0, status.lastRxAt);
}

static void test_rc_frames_do_not_arm_reply_timeouts()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    config.transport.replyTimeoutMs = 20;
    config.transport.packetRateHz = 50;

    TEST_ASSERT_TRUE(core.begin(host, config, 0));
    core.loop(host, 10, 0);
    TEST_ASSERT_EQUAL_UINT32(0, statusOf(core).lastReplyTimeoutAt);

    core.loop(host, 35, 0);
    TEST_ASSERT_EQUAL_UINT32(0, statusOf(core).lastReplyTimeoutAt);
    TEST_ASSERT_EQUAL_INT(2, (int)host.writes.size());
}

static void test_service_frame_reply_timeout_is_reported()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    config.transport.replyTimeoutMs = 20;
    config.transport.packetRateHz = 500;

    TEST_ASSERT_TRUE(core.begin(host, config, 0, 0));

    loopAt(core, host, 0, 0);
    TEST_ASSERT_EQUAL_UINT32(0, statusOf(core).lastReplyTimeoutAt);

    loopAt(core, host, 1000, 1000000);
    loopAt(core, host, 1002, 1002000);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x28));
    TEST_ASSERT_EQUAL_UINT32(0, statusOf(core).lastReplyTimeoutAt);

    loopAt(core, host, 1023, 1023000);
    TEST_ASSERT_EQUAL_UINT32(0, statusOf(core).lastReplyTimeoutAt);
    loopAt(core, host, 1253, 1253000);
    TEST_ASSERT_EQUAL_UINT32(1253, statusOf(core).lastReplyTimeoutAt);
}

static void test_service_reply_without_telemetry_does_not_report_replies_lost()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    config.transport.replyTimeoutMs = 20;
    config.transport.packetRateHz = 500;

    TEST_ASSERT_TRUE(core.begin(host, config, 0, 0));

    loopAt(core, host, 0, 0);
    loopAt(core, host, 1000, 1000000);
    loopAt(core, host, 1002, 1002000);

    host.queueFrame(makeDeviceInfoFrame("RM Ranger Micro", 33));
    loopAt(core, host, 1003, 1003000);

    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);
    loopAt(core, host, 4005, 4005000);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);
    TEST_ASSERT_FALSE(logsContain(host, "replies lost"));
}

static void test_unknown_frame_updates_raw_frame_status()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x28, std::vector<uint8_t>{ 0x01, 0x02 }));

    core.loop(host, 100, 0);

    ELRSCrsfStatus status = statusOf(core);
    TEST_ASSERT_TRUE(status.replyActive);
    TEST_ASSERT_TRUE(status.synced);
    TEST_ASSERT_FALSE(status.telemetryActive);
    TEST_ASSERT_TRUE(status.everReplied);
    TEST_ASSERT_TRUE(status.everSynced);
    TEST_ASSERT_EQUAL_UINT8(0xC8, status.lastRawFrameSyncByte);
    TEST_ASSERT_EQUAL_UINT8(0x28, status.lastRawFrameType);
    TEST_ASSERT_EQUAL_UINT8(6, status.lastRawFrameLength);
    TEST_ASSERT_TRUE(status.lastRawFrameCrcValid);
    TEST_ASSERT_EQUAL_UINT32(100, status.lastReplyAt);
    TEST_ASSERT_EQUAL_UINT32(100, status.lastRxAt);
}

static void test_packet_rate_scheduler_50_100_150_250hz()
{
    FakeHost host50;
    FakeHost host100;
    FakeHost host150;
    FakeHost host250;
    FakeHost host500;
    ELRSCrsfCore core50;
    ELRSCrsfCore core100;
    ELRSCrsfCore core150;
    ELRSCrsfCore core250;
    ELRSCrsfCore core500;
    ELRSCrsfCoreConfig config50 = defaultConfig();
    ELRSCrsfCoreConfig config100 = defaultConfig();
    ELRSCrsfCoreConfig config150 = defaultConfig();
    ELRSCrsfCoreConfig config250 = defaultConfig();
    ELRSCrsfCoreConfig config500 = defaultConfig();

    config50.transport.packetRateHz = 50;
    config100.transport.packetRateHz = 100;
    config150.transport.packetRateHz = 150;
    config250.transport.packetRateHz = 250;
    config500.transport.packetRateHz = 500;

    TEST_ASSERT_TRUE(core50.begin(host50, config50, 0, 0));
    TEST_ASSERT_TRUE(core100.begin(host100, config100, 0, 0));
    TEST_ASSERT_TRUE(core150.begin(host150, config150, 0, 0));
    TEST_ASSERT_TRUE(core250.begin(host250, config250, 0, 0));
    TEST_ASSERT_TRUE(core500.begin(host500, config500, 0, 0));

    loopAt(core50, host50, 0, 0);
    loopAt(core100, host100, 0, 0);
    loopAt(core150, host150, 0, 0);
    loopAt(core250, host250, 0, 0);
    loopAt(core500, host500, 0, 0);

    TEST_ASSERT_EQUAL_INT(1, (int)host50.writes.size());
    TEST_ASSERT_EQUAL_INT(1, (int)host100.writes.size());
    TEST_ASSERT_EQUAL_INT(1, (int)host150.writes.size());
    TEST_ASSERT_EQUAL_INT(1, (int)host250.writes.size());
    TEST_ASSERT_EQUAL_INT(1, (int)host500.writes.size());

    loopAt(core50, host50, 19, 19999);
    loopAt(core100, host100, 9, 9999);
    loopAt(core150, host150, 6, 6665);
    loopAt(core250, host250, 3, 3999);
    loopAt(core500, host500, 1, 1999);

    TEST_ASSERT_EQUAL_INT(1, (int)host50.writes.size());
    TEST_ASSERT_EQUAL_INT(1, (int)host100.writes.size());
    TEST_ASSERT_EQUAL_INT(1, (int)host150.writes.size());
    TEST_ASSERT_EQUAL_INT(1, (int)host250.writes.size());
    TEST_ASSERT_EQUAL_INT(1, (int)host500.writes.size());

    loopAt(core50, host50, 20, 20000);
    loopAt(core100, host100, 10, 10000);
    loopAt(core150, host150, 6, 6666);
    loopAt(core250, host250, 4, 4000);
    loopAt(core500, host500, 2, 2000);

    TEST_ASSERT_EQUAL_INT(2, (int)host50.writes.size());
    TEST_ASSERT_EQUAL_INT(2, (int)host100.writes.size());
    TEST_ASSERT_EQUAL_INT(2, (int)host150.writes.size());
    TEST_ASSERT_EQUAL_INT(2, (int)host250.writes.size());
    TEST_ASSERT_EQUAL_INT(2, (int)host500.writes.size());

    loopAt(core150, host150, 13, 13332);
    TEST_ASSERT_EQUAL_INT(2, (int)host150.writes.size());
    loopAt(core150, host150, 13, 13333);
    TEST_ASSERT_EQUAL_INT(3, (int)host150.writes.size());
    loopAt(core150, host150, 19, 19999);
    TEST_ASSERT_EQUAL_INT(3, (int)host150.writes.size());
    loopAt(core150, host150, 20, 20000);
    TEST_ASSERT_EQUAL_INT(4, (int)host150.writes.size());

    TEST_ASSERT_EQUAL_UINT16(50, statusOf(core50).packetRateHz);
    TEST_ASSERT_EQUAL_UINT16(100, statusOf(core100).packetRateHz);
    TEST_ASSERT_EQUAL_UINT16(150, statusOf(core150).packetRateHz);
    TEST_ASSERT_EQUAL_UINT16(250, statusOf(core250).packetRateHz);
    TEST_ASSERT_EQUAL_UINT16(500, statusOf(core500).packetRateHz);
}

static void test_elrs_crsf_baud_matches_expresslrs_external_module_rate_requirements()
{
    TEST_ASSERT_EQUAL_UINT32(400000UL, elrsCrsfRecommendedBaudRate(ELRS_PACKET_RATE_50HZ));
    TEST_ASSERT_EQUAL_UINT32(400000UL, elrsCrsfRecommendedBaudRate(ELRS_PACKET_RATE_100HZ));
    TEST_ASSERT_EQUAL_UINT32(400000UL, elrsCrsfRecommendedBaudRate(ELRS_PACKET_RATE_150HZ));
    TEST_ASSERT_EQUAL_UINT32(400000UL, elrsCrsfRecommendedBaudRate(ELRS_PACKET_RATE_250HZ));
    TEST_ASSERT_EQUAL_UINT32(921600UL, elrsCrsfRecommendedBaudRate(ELRS_PACKET_RATE_500HZ));
}

static void test_invalid_packet_rate_uses_default_baud()
{
    TEST_ASSERT_EQUAL_UINT32(400000UL, elrsCrsfRecommendedBaudRate(0));
    TEST_ASSERT_EQUAL_UINT32(400000UL, elrsCrsfRecommendedBaudRate(999));
}

static void test_500hz_shared_bus_uses_longer_reply_window()
{
    TEST_ASSERT_EQUAL_UINT16(20, elrsCrsfModuleReplyTimeoutMs(ELRS_PACKET_RATE_50HZ));
    TEST_ASSERT_EQUAL_UINT16(20, elrsCrsfModuleReplyTimeoutMs(ELRS_PACKET_RATE_250HZ));
    TEST_ASSERT_EQUAL_UINT16(50, elrsCrsfModuleReplyTimeoutMs(ELRS_PACKET_RATE_500HZ));
}

static void test_shared_bus_driver_turnaround_guards_are_nonzero()
{
    TEST_ASSERT_EQUAL_UINT16(40, elrsCrsfDriverEnableSetupUs());
    TEST_ASSERT_EQUAL_UINT16(40, elrsCrsfDriverDisableHoldUs());
    TEST_ASSERT_EQUAL_UINT16(150, elrsCrsfDriverReleaseGuardUs());
}

static void test_self_test_emits_known_frame()
{
    FakeHost host;
    ELRSCrsfCore core;
    uint16_t channels[16];
    uint8_t expected[26];

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    core.startSelfTest(0);
    core.loop(host, 10, 0);

    channels[0] = 992;
    channels[1] = 992;
    channels[2] = 172;
    channels[3] = 992;
    channels[4] = 1811;
    for(int i = 5; i < 16; i++) {
        channels[i] = 172;
    }

    TEST_ASSERT_EQUAL_UINT32(26, ELRSCrsfCore::packRcChannelsFrame(channels, expected, sizeof(expected)));
    TEST_ASSERT_TRUE(statusOf(core).selfTestActive);
    TEST_ASSERT_EQUAL_INT(1, (int)host.writes.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, host.writes[0].data(), 26);
}

static void test_adc_missing_at_boot_sets_fault_and_safe_channels()
{
    FakeHost host;
    ELRSCrsfCore core;

    host.axesAvailable = false;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));

    ELRSCrsfStatus status = statusOf(core);
    TEST_ASSERT_TRUE(status.faultFlags & ELRS_FAULT_ADC_MISSING);
    TEST_ASSERT_FALSE(status.faultFlags & ELRS_FAULT_ADC_STALE);
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(0));
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(1));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(2));
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(3));
}

static void test_adc_stale_after_valid_samples_uses_safe_fallback()
{
    FakeHost host;
    ELRSCrsfCore core;

    host.axes[AXIS_AILERON] = 1800;
    host.axes[AXIS_ELEVATOR] = 900;
    host.axes[AXIS_THROTTLE] = 1500;
    host.axes[AXIS_RUDDER] = 1100;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    core.loop(host, 20, 0);

    TEST_ASSERT_FALSE(statusOf(core).faultFlags & ELRS_FAULT_ADC_STALE);

    host.axesAvailable = false;
    core.loop(host, 150, 0);

    ELRSCrsfStatus status = statusOf(core);
    TEST_ASSERT_TRUE(status.faultFlags & ELRS_FAULT_ADC_STALE);
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(0));
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(1));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(2));
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(3));
}

static void test_button_pack_stale_holds_last_valid_states()
{
    FakeHost host;
    ELRSCrsfCore core;

    host.packStates = 0b10101010;
    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    core.loop(host, 20, 0);

    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(9));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(8));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(11));

    host.packAvailable = false;
    core.loop(host, 160, 0);

    ELRSCrsfStatus status = statusOf(core);
    TEST_ASSERT_TRUE(status.faultFlags & ELRS_FAULT_BUTTONPACK_STALE);
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(9));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(8));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(11));
}

static void test_button_pack_missing_at_boot_defaults_low()
{
    FakeHost host;
    ELRSCrsfCore core;

    host.packAvailable = false;
    host.packStates = 0xFF;
    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    core.loop(host, 20, 0);

    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(8));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(9));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(10));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(11));
}

static void test_status_fault_transitions_clear_on_recovery()
{
    FakeHost host;
    ELRSCrsfCore core;

    host.axesAvailable = false;
    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    TEST_ASSERT_TRUE(statusOf(core).faultFlags & ELRS_FAULT_ADC_MISSING);

    host.axesAvailable = true;
    host.axes[AXIS_AILERON] = 1200;
    host.axes[AXIS_ELEVATOR] = 1300;
    host.axes[AXIS_THROTTLE] = 1400;
    host.axes[AXIS_RUDDER] = 1500;
    core.loop(host, 30, 0);
    TEST_ASSERT_FALSE(statusOf(core).faultFlags & ELRS_FAULT_ADC_MISSING);

    host.packStates = 0b00001111;
    core.loop(host, 40, 0);
    TEST_ASSERT_FALSE(statusOf(core).faultFlags & ELRS_FAULT_BUTTONPACK_STALE);

    host.packAvailable = false;
    core.loop(host, 160, 0);
    TEST_ASSERT_TRUE(statusOf(core).faultFlags & ELRS_FAULT_BUTTONPACK_STALE);

    host.packAvailable = true;
    host.packStates = 0b11110000;
    core.loop(host, 170, 0);
    TEST_ASSERT_FALSE(statusOf(core).faultFlags & ELRS_FAULT_BUTTONPACK_STALE);
}

static void test_control_mapping_and_reversed_axis_calibration()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    host.axes[AXIS_AILERON] = 2047;
    host.axes[AXIS_ELEVATOR] = 1024;
    host.axes[AXIS_THROTTLE] = 0;
    host.axes[AXIS_RUDDER] = 2047;
    host.stop = true;
    host.fakePower = true;
    host.buttonA = true;
    host.buttonB = true;
    host.packStates = 0b10100101;
    config.axisProfiles[AXIS_RUDDER] = elrsDefaultInputAxisProfile();
    config.axisProfiles[AXIS_RUDDER].reverse = 1;

    TEST_ASSERT_TRUE(core.begin(host, config, 0));
    core.loop(host, 10, 0);

    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(0));
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(1));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(2));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(3));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(4));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(5));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(6));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(7));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(8));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(9));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(10));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(11));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(12));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(13));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(14));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(15));

    host.axes[AXIS_RUDDER] = 0;
    core.loop(host, 20, 0);
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(3));
}

static void test_axis_order_aileron_elevator_throttle_rudder_matches_runtime_banner()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_EQUAL_UINT8(0, AXIS_AILERON);
    TEST_ASSERT_EQUAL_UINT8(1, AXIS_ELEVATOR);
    TEST_ASSERT_EQUAL_UINT8(2, AXIS_RUDDER);
    TEST_ASSERT_EQUAL_UINT8(3, AXIS_THROTTLE);

    host.axes[AXIS_THROTTLE] = 0;
    host.axes[AXIS_RUDDER] = 512;
    host.axes[AXIS_ELEVATOR] = 1536;
    host.axes[AXIS_AILERON] = 2047;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    loopAt(core, host, 20, 20000);

    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(0));
    TEST_ASSERT_EQUAL_UINT16(1401, core.channelAt(1));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(2));
    TEST_ASSERT_EQUAL_UINT16(582, core.channelAt(3));
}

static void test_nondefault_axis_profile_changes_runtime_output_scaling()
{
    FakeHost defaultHost;
    FakeHost profiledHost;
    ELRSCrsfCore defaultCore;
    ELRSCrsfCore profiledCore;
    ELRSCrsfCoreConfig defaultCfg = defaultConfig();
    ELRSCrsfCoreConfig profiledCfg = defaultConfig();

    defaultHost.axes[AXIS_AILERON] = 900;
    profiledHost.axes[AXIS_AILERON] = 900;
    profiledCfg.axisProfiles[AXIS_AILERON].minimum = 350;
    profiledCfg.axisProfiles[AXIS_AILERON].center = 900;
    profiledCfg.axisProfiles[AXIS_AILERON].maximum = 1500;

    TEST_ASSERT_TRUE(defaultCore.begin(defaultHost, defaultCfg, 0));
    TEST_ASSERT_TRUE(profiledCore.begin(profiledHost, profiledCfg, 0));

    loopAt(defaultCore, defaultHost, 20, 20000);
    loopAt(profiledCore, profiledHost, 20, 20000);

    TEST_ASSERT_NOT_EQUAL(defaultCore.channelAt(0), profiledCore.channelAt(0));
    TEST_ASSERT_EQUAL_UINT16(992, profiledCore.channelAt(0));
}

static void test_legacy_normalized_axis_profile_is_sanitized_before_runtime_mapping()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    host.axes[AXIS_AILERON] = 1024;
    host.calibration[AXIS_AILERON].minimum = 700;
    host.calibration[AXIS_AILERON].center = 900;
    host.calibration[AXIS_AILERON].maximum = 1100;

    config.axisProfiles[AXIS_AILERON].minimum = -1024;
    config.axisProfiles[AXIS_AILERON].center = 0;
    config.axisProfiles[AXIS_AILERON].maximum = 1023;

    TEST_ASSERT_TRUE(core.begin(host, config, 0));
    loopAt(core, host, 20, 20000);

    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(0));
}

static void test_gimbal_routing_can_claim_fixed_function_channels()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    config.inputRouting.aileronChannel = 6;
    config.inputRouting.elevatorChannel = 8;
    config.inputRouting.throttleChannel = 5;
    config.inputRouting.rudderChannel = 9;

    host.axes[AXIS_AILERON] = 1024;
    host.axes[AXIS_ELEVATOR] = 0;
    host.axes[AXIS_THROTTLE] = 1024;
    host.axes[AXIS_RUDDER] = 512;
    host.stop = true;
    host.fakePower = true;
    host.buttonA = true;
    host.buttonB = true;
    host.packStates = 0b00000001;

    TEST_ASSERT_TRUE(core.begin(host, config, 0));
    loopAt(core, host, 20, 20000);

    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(4));
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(5));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(6));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(7));
    TEST_ASSERT_EQUAL_UINT16(582, core.channelAt(8));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(9));
}

static void test_telemetry_parsing_and_bad_crc_rejection()
{
    FakeHost host;
    ELRSCrsfCore core;
    std::vector<uint8_t> badLink;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));

    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 88, 0, 0, 0, 0, 0, 0, 0 }));
    host.queueFrame(makeFrame(0x08, std::vector<uint8_t>{ 0x30, 0x39, 0, 0, 0, 0, 0, 77 }));
    host.queueFrame(makeFrame(0x02, std::vector<uint8_t>{ 0, 0, 0, 0, 0, 0, 0, 0, 0x04, 0xCE, 0, 0, 0, 0, 0 }));
    host.queueFrame(makeFrame(0x0A, std::vector<uint8_t>{ 0x00, 0x4D }));

    core.loop(host, 100, 0);

    TEST_ASSERT_TRUE(core.synced());
    TEST_ASSERT_TRUE(core.telemetryActive());
    TEST_ASSERT_EQUAL_UINT8(88, core.linkQuality());
    TEST_ASSERT_EQUAL_UINT8(77, core.remoteBatteryPercent());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.12345f, core.remoteBatteryVoltage());
    TEST_ASSERT_EQUAL_UINT16(123, core.gpsSpeed10());
    TEST_ASSERT_EQUAL_UINT16(77, core.airspeed10());

    badLink = makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 5, 0, 0, 0, 0, 0, 0, 0 });
    badLink[badLink.size() - 1] ^= 0xFF;
    host.queueFrame(badLink);
    core.loop(host, 200, 0);

    TEST_ASSERT_EQUAL_UINT8(88, core.linkQuality());
}

static void test_non_c8_sync_frame_is_accepted()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    host.queueFrame(makeFrameWithSync(0x00, 0x14, std::vector<uint8_t>{ 0, 0, 68, 0, 0, 0, 0, 0, 0, 0 }));

    core.loop(host, 100, 0);

    TEST_ASSERT_TRUE(statusOf(core).replyActive);
    TEST_ASSERT_TRUE(core.synced());
    TEST_ASSERT_TRUE(core.telemetryActive());
    TEST_ASSERT_EQUAL_UINT8(0x00, statusOf(core).lastRawFrameSyncByte);
    TEST_ASSERT_EQUAL_UINT8(68, core.linkQuality());
}

static void test_parser_recovers_after_garbage_before_valid_frame()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    queueBytes(host, makeGarbage());
    queueBytes(host, makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 67, 0, 0, 0, 0, 0, 0, 0 }));

    core.loop(host, 100, 0);

    TEST_ASSERT_TRUE(core.synced());
    TEST_ASSERT_EQUAL_UINT8(67, core.linkQuality());
}

static void test_parser_recovers_after_bad_crc_followed_by_valid_frame()
{
    FakeHost host;
    ELRSCrsfCore core;

    std::vector<uint8_t> bad = makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 12, 0, 0, 0, 0, 0, 0, 0 });
    bad.back() ^= 0xFF;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    queueBytes(host, bad);
    queueBytes(host, makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 91, 0, 0, 0, 0, 0, 0, 0 }));

    core.loop(host, 100, 0);

    TEST_ASSERT_TRUE(core.synced());
    TEST_ASSERT_EQUAL_UINT8(91, core.linkQuality());
}

static void test_comm_codes_show_no_sync_until_valid_frame()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));

    core.loop(host, 2000, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NRY, statusOf(core).commCode);
    TEST_ASSERT_FALSE(statusOf(core).everSynced);
    TEST_ASSERT_FALSE(statusOf(core).replyActive);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("NRY", host.displayText.c_str());
    TEST_ASSERT_EQUAL_INT(1, (int)host.writes.size());

    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 73, 0, 0, 0, 0, 0, 0, 0 }));
    core.loop(host, 2100, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);
    TEST_ASSERT_TRUE(statusOf(core).everSynced);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("NRY", host.displayText.c_str());

    core.loop(host, 3800, 0);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING(" 73", host.displayText.c_str());
}

static void test_lost_telemetry_sets_los_until_valid_frame()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 44, 0, 0, 0, 0, 0, 0, 0 }));
    core.loop(host, 100, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);
    TEST_ASSERT_TRUE(statusOf(core).everSynced);

    core.loop(host, 2100, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_RLS, statusOf(core).commCode);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("RLS", host.displayText.c_str());
    TEST_ASSERT_EQUAL_INT(2, (int)host.writes.size());

    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 45, 0, 0, 0, 0, 0, 0, 0 }));
    core.loop(host, 2200, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);
    TEST_ASSERT_TRUE(statusOf(core).everSynced);
}

static void test_crc_burst_sets_crc_comm_code()
{
    FakeHost host;
    ELRSCrsfCore core;
    std::vector<uint8_t> bad = makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 12, 0, 0, 0, 0, 0, 0, 0 });

    bad.back() ^= 0xFF;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 55, 0, 0, 0, 0, 0, 0, 0 }));
    core.loop(host, 100, 0);

    host.queueFrame(bad);
    core.loop(host, 1200, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);

    host.queueFrame(bad);
    core.loop(host, 1300, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);

    host.queueFrame(bad);
    core.loop(host, 1400, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_CRC, statusOf(core).commCode);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("CRC", host.displayText.c_str());

    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 56, 0, 0, 0, 0, 0, 0, 0 }));
    core.loop(host, 1500, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);
}

static void test_frame_burst_sets_frm_comm_code()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 61, 0, 0, 0, 0, 0, 0, 0 }));
    core.loop(host, 100, 0);

    queueBytes(host, std::vector<uint8_t>{ 0xC8, 0x01 });
    core.loop(host, 1200, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);

    queueBytes(host, std::vector<uint8_t>{ 0xC8, 0x01 });
    core.loop(host, 1300, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);

    queueBytes(host, std::vector<uint8_t>{ 0xC8, 0x01 });
    core.loop(host, 1400, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_FRM, statusOf(core).commCode);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("FRM", host.displayText.c_str());

    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 62, 0, 0, 0, 0, 0, 0, 0 }));
    core.loop(host, 1500, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);
}

static void test_display_policy_prefers_gps_then_airspeed_then_link_quality()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));

    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 88, 0, 0, 0, 0, 0, 0, 0 }));
    host.queueFrame(makeFrame(0x02, std::vector<uint8_t>{ 0, 0, 0, 0, 0, 0, 0, 0, 0x04, 0xCE, 0, 0, 0, 0, 0 }));
    core.loop(host, 100, 0);

    host.queueFrame(makeFrame(0x0A, std::vector<uint8_t>{ 0x00, 0x4D }));
    core.loop(host, 1500, 0);
    TEST_ASSERT_EQUAL(DISPLAY_SPEED, host.displayMode);
    TEST_ASSERT_EQUAL_INT(123, host.displaySpeed);
    TEST_ASSERT_EQUAL(ELRSCrsfCore::SPEED_SOURCE_GPS, core.activeSpeedSource());

    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 88, 0, 0, 0, 0, 0, 0, 0 }));
    core.loop(host, 2301, 0);
    TEST_ASSERT_EQUAL(DISPLAY_SPEED, host.displayMode);
    TEST_ASSERT_EQUAL_INT(77, host.displaySpeed);
    TEST_ASSERT_EQUAL(ELRSCrsfCore::SPEED_SOURCE_AIRSPEED, core.activeSpeedSource());

    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 88, 0, 0, 0, 0, 0, 0, 0 }));
    core.loop(host, 3600, 0);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING(" 88", host.displayText.c_str());
    TEST_ASSERT_EQUAL(ELRSCrsfCore::SPEED_SOURCE_NONE, core.activeSpeedSource());
}

static void test_speed_units_default_to_kmh()
{
    TEST_ASSERT_EQUAL_UINT8(ELRS_SPEED_UNITS_KMH, elrsSpeedUnitsOrDefault(ELRS_SPEED_UNITS_KMH));
    TEST_ASSERT_EQUAL_UINT8(ELRS_SPEED_UNITS_MPH, elrsSpeedUnitsOrDefault(ELRS_SPEED_UNITS_MPH));
    TEST_ASSERT_EQUAL_UINT8(ELRS_SPEED_UNITS_KMH, elrsSpeedUnitsOrDefault(99));
}

static void test_speed_display_can_convert_kmh_to_mph()
{
    FakeHost hostKmh;
    FakeHost hostMph;
    ELRSCrsfCore coreKmh;
    ELRSCrsfCore coreMph;
    ELRSCrsfCoreConfig configKmh = defaultConfig();
    ELRSCrsfCoreConfig configMph = defaultConfig();

    configMph.speedDisplayUnits = ELRS_SPEED_UNITS_MPH;

    TEST_ASSERT_TRUE(coreKmh.begin(hostKmh, configKmh, 0));
    TEST_ASSERT_TRUE(coreMph.begin(hostMph, configMph, 0));

    hostKmh.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 88, 0, 0, 0, 0, 0, 0, 0 }));
    hostKmh.queueFrame(makeFrame(0x02, std::vector<uint8_t>{ 0, 0, 0, 0, 0, 0, 0, 0, 0x04, 0xCE, 0, 0, 0, 0, 0 }));
    hostMph.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 88, 0, 0, 0, 0, 0, 0, 0 }));
    hostMph.queueFrame(makeFrame(0x02, std::vector<uint8_t>{ 0, 0, 0, 0, 0, 0, 0, 0, 0x04, 0xCE, 0, 0, 0, 0, 0 }));

    coreKmh.loop(hostKmh, 100, 0);
    coreMph.loop(hostMph, 100, 0);
    hostKmh.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 88, 0, 0, 0, 0, 0, 0, 0 }));
    hostMph.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 88, 0, 0, 0, 0, 0, 0, 0 }));
    coreKmh.loop(hostKmh, 1500, 0);
    coreMph.loop(hostMph, 1500, 0);

    TEST_ASSERT_EQUAL(DISPLAY_SPEED, hostKmh.displayMode);
    TEST_ASSERT_EQUAL_INT(123, hostKmh.displaySpeed);
    TEST_ASSERT_EQUAL(DISPLAY_SPEED, hostMph.displayMode);
    TEST_ASSERT_EQUAL_INT(76, hostMph.displaySpeed);
}

static void test_battery_overlay_beats_comm_overlay()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 42, 0, 0, 0, 0, 0, 0, 0 }));
    core.loop(host, 100, 0);

    core.loop(host, 30000, 1);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_RLS, statusOf(core).commCode);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("BAT", host.displayText.c_str());
}

static void test_calibration_prompt_beats_comm_overlay()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 42, 0, 0, 0, 0, 0, 0, 0 }));
    core.loop(host, 100, 0);

    host.calibrationButton = true;
    core.loop(host, 200, 0);
    core.loop(host, 300, 0);
    core.loop(host, 2301, 0);

    TEST_ASSERT_TRUE(core.isCalibrating());
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_RLS, statusOf(core).commCode);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("CEN", host.displayText.c_str());
}

static void test_adc_overlay_beats_comm_overlay()
{
    FakeHost host;
    ELRSCrsfCore core;
    std::vector<uint8_t> bad = makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 12, 0, 0, 0, 0, 0, 0, 0 });

    bad.back() ^= 0xFF;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 52, 0, 0, 0, 0, 0, 0, 0 }));
    core.loop(host, 100, 0);

    host.axesAvailable = false;
    host.queueFrame(bad);
    core.loop(host, 250, 0);
    host.queueFrame(bad);
    core.loop(host, 350, 0);
    host.queueFrame(bad);
    core.loop(host, 450, 0);

    TEST_ASSERT_TRUE(statusOf(core).faultFlags & ELRS_FAULT_ADC_STALE);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_CRC, statusOf(core).commCode);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("ADC", host.displayText.c_str());
}

static void test_button_pack_overlay_beats_comm_overlay()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 57, 0, 0, 0, 0, 0, 0, 0 }));
    core.loop(host, 100, 0);
    core.loop(host, 120, 0);

    host.packAvailable = false;
    queueBytes(host, std::vector<uint8_t>{ 0xC8, 0x01 });
    core.loop(host, 1300, 0);
    queueBytes(host, std::vector<uint8_t>{ 0xC8, 0x01 });
    core.loop(host, 1400, 0);
    queueBytes(host, std::vector<uint8_t>{ 0xC8, 0x01 });
    core.loop(host, 1500, 0);

    TEST_ASSERT_TRUE(statusOf(core).faultFlags & ELRS_FAULT_BUTTONPACK_STALE);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_FRM, statusOf(core).commCode);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("BPK", host.displayText.c_str());
}

static void test_battery_overlay_and_calibration_prompt_still_override_normal_display()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 42, 0, 0, 0, 0, 0, 0, 0 }));
    core.loop(host, 100, 0);
    core.loop(host, 1200, 0);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING(" 42", host.displayText.c_str());

    core.loop(host, 60000, 1);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("BAT", host.displayText.c_str());

    host.calibrationButton = true;
    core.loop(host, 61100, 0);
    core.loop(host, 61200, 0);
    core.loop(host, 63301, 0);
    TEST_ASSERT_TRUE(core.isCalibrating());
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("CEN", host.displayText.c_str());
}

static void test_module_settings_are_discovered_and_written()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    const std::vector<uint8_t> *frame = NULL;

    config.transport.packetRateHz = 50;
    config.telemetryRatio = ELRS_TLM_RATIO_1_4;
    config.maxPower = ELRS_MAX_POWER_500MW;
    config.dynamicPower = ELRS_DYNAMIC_POWER_DYNAMIC;

    TEST_ASSERT_TRUE(core.begin(host, config, 0, 0));
    loopAt(core, host, 0, 0);
    loopAt(core, host, 1000, 1000000);
    loopAt(core, host, 1020, 1020000);

    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x28));
    frame = findWrittenFrameType(host, 0x28, 0);
    TEST_ASSERT_NOT_NULL(frame);
    TEST_ASSERT_EQUAL_UINT8(0xC8, (*frame)[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, (*frame)[3]);
    TEST_ASSERT_EQUAL_UINT8(0xEA, (*frame)[4]);

    host.queueFrame(makeDeviceInfoFrame("ExpressLRS TX", 3));
    loopAt(core, host, 1030, 1030000);

    loopAt(core, host, 1120, 1120000);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x2C));
    frame = findWrittenFrameType(host, 0x2C, 0);
    TEST_ASSERT_NOT_NULL(frame);
    TEST_ASSERT_EQUAL_UINT8(0xC8, (*frame)[0]);
    TEST_ASSERT_EQUAL_UINT8(0xEE, (*frame)[3]);
    TEST_ASSERT_EQUAL_UINT8(0xEF, (*frame)[4]);
    host.queueFrame(makeTextSelectionEntryFrame(1, "Telem Ratio", "Std;1:2;1:4;1:8;Off", 0, 4));
    loopAt(core, host, 1130, 1130000);

    loopAt(core, host, 1230, 1230000);
    loopAt(core, host, 1240, 1240000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, countWrittenFrameType(host, 0x2C), writtenFrameTypes(host).c_str());
    host.queueFrame(makeTextSelectionEntryFrame(2, "Max Power", "10;25;100;250;500;1000", 3, 5));
    loopAt(core, host, 1250, 1250000);

    loopAt(core, host, 1350, 1350000);
    loopAt(core, host, 1360, 1360000);
    TEST_ASSERT_EQUAL_INT(3, countWrittenFrameType(host, 0x2C));
    host.queueFrame(makeTextSelectionEntryFrame(3, "Dynamic", "Off;Dyn;AUX9", 0, 2));
    loopAt(core, host, 1370, 1370000);

    loopAt(core, host, 1470, 1470000);
    loopAt(core, host, 1480, 1480000);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x2D));
    frame = findWrittenFrameType(host, 0x2D, 0);
    TEST_ASSERT_NOT_NULL(frame);
    TEST_ASSERT_EQUAL_UINT8(0xEF, (*frame)[4]);
    TEST_ASSERT_EQUAL_UINT8(1, (*frame)[5]);
    TEST_ASSERT_EQUAL_UINT8(2, (*frame)[6]);

    loopAt(core, host, 1770, 1770000);
    loopAt(core, host, 1780, 1780000);
    loopAt(core, host, 1880, 1880000);
    TEST_ASSERT_EQUAL_INT(2, countWrittenFrameType(host, 0x2D));
    frame = findWrittenFrameType(host, 0x2D, 1);
    TEST_ASSERT_NOT_NULL(frame);
    TEST_ASSERT_EQUAL_UINT8(0xEF, (*frame)[4]);
    TEST_ASSERT_EQUAL_UINT8(2, (*frame)[5]);
    TEST_ASSERT_EQUAL_UINT8(4, (*frame)[6]);

    loopAt(core, host, 2170, 2170000);
    loopAt(core, host, 2180, 2180000);
    loopAt(core, host, 2280, 2280000);
    TEST_ASSERT_EQUAL_INT(3, countWrittenFrameType(host, 0x2D));
    frame = findWrittenFrameType(host, 0x2D, 2);
    TEST_ASSERT_NOT_NULL(frame);
    TEST_ASSERT_EQUAL_UINT8(0xEF, (*frame)[4]);
    TEST_ASSERT_EQUAL_UINT8(3, (*frame)[5]);
    TEST_ASSERT_EQUAL_UINT8(1, (*frame)[6]);
}

static void test_module_settings_retry_without_blocking_rc_output()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    config.transport.packetRateHz = 50;

    TEST_ASSERT_TRUE(core.begin(host, config, 0, 0));
    loopAt(core, host, 0, 0);
    loopAt(core, host, 1000, 1000000);
    loopAt(core, host, 1020, 1020000);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x28));

    loopAt(core, host, 1600, 1600000);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x28));

    loopAt(core, host, 11600, 11600000);
    loopAt(core, host, 11620, 11620000);
    loopAt(core, host, 11640, 11640000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, countWrittenFrameType(host, 0x28), writtenFrameTypes(host).c_str());
    TEST_ASSERT_GREATER_THAN_INT(2, countWrittenFrameType(host, 0x16));
}

static void test_module_settings_request_remaining_chunks_before_advancing_field()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    std::vector<uint8_t> field1;
    std::vector<uint8_t> chunk0;
    std::vector<uint8_t> chunk1;
    const std::vector<uint8_t> *frame = NULL;

    config.transport.packetRateHz = 50;

    TEST_ASSERT_TRUE(core.begin(host, config, 0, 0));
    loopAt(core, host, 0, 0);
    loopAt(core, host, 1000, 1000000);
    loopAt(core, host, 1020, 1020000);

    host.queueFrame(makeDeviceInfoFrame("ExpressLRS TX", 2));
    loopAt(core, host, 1030, 1030000);

    loopAt(core, host, 1120, 1120000);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x2C));
    frame = findWrittenFrameType(host, 0x2C, 0);
    TEST_ASSERT_NOT_NULL(frame);
    TEST_ASSERT_EQUAL_UINT8(1, (*frame)[5]);
    TEST_ASSERT_EQUAL_UINT8(0, (*frame)[6]);

    field1 = makeTextSelectionEntryData("Telem Ratio", "Std;1:2;1:4;1:8;Off", 0, 4);
    chunk0.assign(field1.begin(), field1.begin() + (field1.size() / 2));
    chunk1.assign(field1.begin() + (field1.size() / 2), field1.end());
    host.queueFrame(makeParameterChunkFrame(1, 1, chunk0));
    loopAt(core, host, 1130, 1130000);

    loopAt(core, host, 1230, 1230000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, countWrittenFrameType(host, 0x2C), writtenFrameTypes(host).c_str());
    frame = findWrittenFrameType(host, 0x2C, 1);
    TEST_ASSERT_NOT_NULL(frame);
    TEST_ASSERT_EQUAL_UINT8(1, (*frame)[5]);
    TEST_ASSERT_EQUAL_UINT8(1, (*frame)[6]);

    host.queueFrame(makeParameterChunkFrame(1, 0, chunk1));
    loopAt(core, host, 1240, 1240000);

    loopAt(core, host, 1340, 1340000);
    TEST_ASSERT_EQUAL_INT(3, countWrittenFrameType(host, 0x2C));
    frame = findWrittenFrameType(host, 0x2C, 2);
    TEST_ASSERT_NOT_NULL(frame);
    TEST_ASSERT_EQUAL_UINT8(2, (*frame)[5]);
    TEST_ASSERT_EQUAL_UINT8(0, (*frame)[6]);
}

static void test_module_settings_retry_timed_out_chunk_before_scan_backoff()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    std::vector<uint8_t> field1;
    std::vector<uint8_t> chunk0;
    std::vector<uint8_t> chunk1;
    std::vector<uint8_t> chunk2;
    const std::vector<uint8_t> *frame = NULL;
    const size_t splitA = 18;
    const size_t splitB = 36;

    config.transport.packetRateHz = 50;

    TEST_ASSERT_TRUE(core.begin(host, config, 0, 0));
    loopAt(core, host, 0, 0);
    loopAt(core, host, 1000, 1000000);
    loopAt(core, host, 1020, 1020000);

    host.queueFrame(makeDeviceInfoFrame("ExpressLRS TX", 1));
    loopAt(core, host, 1030, 1030000);

    loopAt(core, host, 1120, 1120000);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x2C));

    field1 = makeTextSelectionEntryData("Telem Ratio", "Std;1:2;1:4;1:8;Off", 0, 4);
    chunk0.assign(field1.begin(), field1.begin() + splitA);
    chunk1.assign(field1.begin() + splitA, field1.begin() + splitB);
    chunk2.assign(field1.begin() + splitB, field1.end());

    host.queueFrame(makeParameterChunkFrame(1, 2, chunk0));
    loopAt(core, host, 1130, 1130000);

    loopAt(core, host, 1230, 1230000);
    TEST_ASSERT_EQUAL_INT(2, countWrittenFrameType(host, 0x2C));
    frame = findWrittenFrameType(host, 0x2C, 1);
    TEST_ASSERT_NOT_NULL(frame);
    TEST_ASSERT_EQUAL_UINT8(1, (*frame)[5]);
    TEST_ASSERT_EQUAL_UINT8(1, (*frame)[6]);

    loopAt(core, host, 1740, 1740000);
    loopAt(core, host, 1760, 1760000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, countWrittenFrameType(host, 0x2C), writtenFrameTypes(host).c_str());
    frame = findWrittenFrameType(host, 0x2C, 2);
    TEST_ASSERT_NOT_NULL(frame);
    TEST_ASSERT_EQUAL_UINT8(1, (*frame)[5]);
    TEST_ASSERT_EQUAL_UINT8(1, (*frame)[6]);

    host.queueFrame(makeParameterChunkFrame(1, 1, chunk1));
    loopAt(core, host, 1770, 1770000);

    loopAt(core, host, 1880, 1880000);
    TEST_ASSERT_EQUAL_INT(4, countWrittenFrameType(host, 0x2C));
    frame = findWrittenFrameType(host, 0x2C, 3);
    TEST_ASSERT_NOT_NULL(frame);
    TEST_ASSERT_EQUAL_UINT8(1, (*frame)[5]);
    TEST_ASSERT_EQUAL_UINT8(2, (*frame)[6]);

    host.queueFrame(makeParameterChunkFrame(1, 0, chunk2));
    loopAt(core, host, 1890, 1890000);
    loopAt(core, host, 1990, 1990000);

    TEST_ASSERT_FALSE(logsContain(host, "parameter scan timed out"));
}

static void test_module_settings_retry_probe_before_long_backoff()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    config.transport.packetRateHz = 50;

    TEST_ASSERT_TRUE(core.begin(host, config, 0, 0));
    loopAt(core, host, 0, 0);
    loopAt(core, host, 1000, 1000000);
    loopAt(core, host, 1020, 1020000);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x28));

    loopAt(core, host, 1700, 1700000);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x28));

    loopAt(core, host, 2800, 2800000);
    loopAt(core, host, 2820, 2820000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, countWrittenFrameType(host, 0x28), writtenFrameTypes(host).c_str());
    TEST_ASSERT_FALSE(logsContain(host, "module settings probe timed out"));
}

static void test_module_probe_does_not_lower_configured_500hz_runtime_rate()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    config.transport.packetRateHz = 500;
    config.transport.replyTimeoutMs = 50;

    TEST_ASSERT_TRUE(core.begin(host, config, 0, 0));
    loopAt(core, host, 0, 0);
    loopAt(core, host, 1000, 1000000);
    loopAt(core, host, 1020, 1020000);
    TEST_ASSERT_EQUAL_UINT16(500, statusOf(core).packetRateHz);

    loopAt(core, host, 1700, 1700000);
    loopAt(core, host, 2800, 2800000);
    loopAt(core, host, 2820, 2820000);
    loopAt(core, host, 3900, 3900000);
    loopAt(core, host, 3920, 3920000);
    loopAt(core, host, 5000, 5000000);
    loopAt(core, host, 5020, 5020000);
    loopAt(core, host, 5600, 5600000);

    TEST_ASSERT_EQUAL_UINT16(500, statusOf(core).packetRateHz);
    TEST_ASSERT_FALSE(logsContain(host, "switching module probe packet rate"));
    TEST_ASSERT_TRUE(logsContain(host, "ELRS/CRSF: module settings probe timed out"));
}

static void test_bootstrap_probe_waits_long_enough_for_late_first_module_reply()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    config.transport.packetRateHz = 500;
    config.transport.replyTimeoutMs = 50;

    TEST_ASSERT_TRUE(core.begin(host, config, 0, 0));

    loopAt(core, host, 0, 0);
    loopAt(core, host, 1000, 1000000);
    loopAt(core, host, 1002, 1002000);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x28));

    loopAt(core, host, 1055, 1055000);
    TEST_ASSERT_EQUAL_UINT32(0, statusOf(core).lastReplyTimeoutAt);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, countWrittenFrameType(host, 0x16), writtenFrameTypes(host).c_str());

    host.queueFrame(makeFrame(0x3A, std::vector<uint8_t>{ 0xEA, 0xEE, 0x10, 0x00, 0x00, 0x9C, 0x40, 0xFF, 0xFF, 0xFC, 0x18 }));
    loopAt(core, host, 1060, 1060000);

    TEST_ASSERT_TRUE(statusOf(core).everReplied);
    TEST_ASSERT_EQUAL_UINT32(0, statusOf(core).lastReplyTimeoutAt);
}

static void test_module_settings_apply_after_targets_are_found_without_full_scan()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    config.transport.packetRateHz = 50;
    config.telemetryRatio = ELRS_TLM_RATIO_STD;
    config.maxPower = ELRS_MAX_POWER_100MW;
    config.dynamicPower = ELRS_DYNAMIC_POWER_OFF;

    TEST_ASSERT_TRUE(core.begin(host, config, 0, 0));
    loopAt(core, host, 0, 0);
    loopAt(core, host, 1000, 1000000);
    loopAt(core, host, 1020, 1020000);

    host.queueFrame(makeDeviceInfoFrame("RM Ranger Micro", 33));
    loopAt(core, host, 1030, 1030000);

    loopAt(core, host, 1120, 1120000);
    host.queueFrame(makeTextSelectionEntryFrame(1, "Packet Rate", "50Hz;250Hz;500Hz", 0, 2));
    loopAt(core, host, 1130, 1130000);

    loopAt(core, host, 1230, 1230000);
    host.queueFrame(makeTextSelectionEntryFrame(2, "Telem Ratio", "Std;1:2;1:4;1:8;Off", 0, 4));
    loopAt(core, host, 1240, 1240000);

    loopAt(core, host, 1340, 1340000);
    host.queueFrame(makeTextSelectionEntryFrame(3, "Switch Mode", "Wide;Hybrid", 0, 1));
    loopAt(core, host, 1350, 1350000);

    loopAt(core, host, 1450, 1450000);
    host.queueFrame(makeTextSelectionEntryFrame(4, "Link Mode", "Normal", 0, 0));
    loopAt(core, host, 1460, 1460000);

    loopAt(core, host, 1560, 1560000);
    host.queueFrame(makeTextSelectionEntryFrame(5, "Model Match", "Off;On", 0, 1));
    loopAt(core, host, 1570, 1570000);

    loopAt(core, host, 1670, 1670000);
    host.queueFrame(makeParameterEntryFrame(6, "TX Power (100mW)", 0x0B));
    loopAt(core, host, 1680, 1680000);

    loopAt(core, host, 1780, 1780000);
    host.queueFrame(makeTextSelectionEntryFrame(7, "Max Power", "25;50;100;250;500;1000", 2, 5));
    loopAt(core, host, 1790, 1790000);

    loopAt(core, host, 1890, 1890000);
    host.queueFrame(makeTextSelectionEntryFrame(8, "Dynamic", "Off;On", 0, 1));
    loopAt(core, host, 1900, 1900000);

    loopAt(core, host, 2100, 2100000);
    loopAt(core, host, 2110, 2110000);
    loopAt(core, host, 2120, 2120000);
    loopAt(core, host, 2130, 2130000);

    TEST_ASSERT_TRUE(logsContain(host, "module settings apply complete"));
    TEST_ASSERT_FALSE(logsContain(host, "parameter scan timed out"));
}

static void test_module_settings_do_not_write_packet_rate_target_or_change_transport_rate()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    const std::vector<uint8_t> *frame = NULL;

    config.transport.packetRateHz = 500;
    config.telemetryRatio = ELRS_TLM_RATIO_STD;
    config.maxPower = ELRS_MAX_POWER_100MW;
    config.dynamicPower = ELRS_DYNAMIC_POWER_OFF;

    TEST_ASSERT_TRUE(core.begin(host, config, 0, 0));
    loopAt(core, host, 0, 0);
    loopAt(core, host, 1000, 1000000);
    loopAt(core, host, 1020, 1020000);

    host.queueFrame(makeDeviceInfoFrame("RM Ranger Micro", 33));
    loopAt(core, host, 1030, 1030000);

    loopAt(core, host, 1130, 1130000);
    host.queueFrame(makeTextSelectionEntryFrame(1, "Packet Rate", "50Hz;250Hz;500Hz", 1, 2));
    loopAt(core, host, 1140, 1140000);

    loopAt(core, host, 1240, 1240000);
    host.queueFrame(makeTextSelectionEntryFrame(2, "Telem Ratio", "Std;1:2;1:4;1:8;Off", 0, 4));
    loopAt(core, host, 1250, 1250000);

    loopAt(core, host, 1350, 1350000);
    host.queueFrame(makeTextSelectionEntryFrame(3, "Switch Mode", "Wide;Hybrid", 0, 1));
    loopAt(core, host, 1360, 1360000);

    loopAt(core, host, 1460, 1460000);
    host.queueFrame(makeTextSelectionEntryFrame(4, "Link Mode", "Normal", 0, 0));
    loopAt(core, host, 1470, 1470000);

    loopAt(core, host, 1570, 1570000);
    host.queueFrame(makeTextSelectionEntryFrame(5, "Model Match", "Off;On", 0, 1));
    loopAt(core, host, 1580, 1580000);

    loopAt(core, host, 1680, 1680000);
    host.queueFrame(makeParameterEntryFrame(6, "TX Power (100mW)", 0x0B));
    loopAt(core, host, 1690, 1690000);

    loopAt(core, host, 1790, 1790000);
    host.queueFrame(makeTextSelectionEntryFrame(7, "Max Power", "25;50;100;250;500;1000", 2, 5));
    loopAt(core, host, 1800, 1800000);

    loopAt(core, host, 1900, 1900000);
    host.queueFrame(makeTextSelectionEntryFrame(8, "Dynamic", "Off;On", 0, 1));
    loopAt(core, host, 1910, 1910000);

    loopAt(core, host, 2110, 2110000);
    frame = findWrittenFrameType(host, 0x2D, 0);
    TEST_ASSERT_NULL_MESSAGE(frame, "Packet Rate must not be written through the module config menu");
    TEST_ASSERT_FALSE(logsContain(host, "applying module setting 'Packet Rate'"));

    loopAt(core, host, 2120, 2120000);
    loopAt(core, host, 2130, 2130000);
    loopAt(core, host, 2140, 2140000);
    loopAt(core, host, 2150, 2150000);
    TEST_ASSERT_TRUE(logsContain(host, "module settings apply complete"));
    TEST_ASSERT_EQUAL_UINT16(500, statusOf(core).packetRateHz);
}

static void test_module_ping_holds_rc_until_reply_timeout_on_half_duplex_link()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    config.transport.packetRateHz = 500;
    config.transport.replyTimeoutMs = 20;

    TEST_ASSERT_TRUE(core.begin(host, config, 0, 0));

    loopAt(core, host, 0, 0);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x16));

    loopAt(core, host, 1000, 1000000);
    TEST_ASSERT_EQUAL_INT(2, countWrittenFrameType(host, 0x16));

    loopAt(core, host, 1002, 1002000);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x28));
    TEST_ASSERT_EQUAL_INT(2, countWrittenFrameType(host, 0x16));

    loopAt(core, host, 1010, 1010000);
    loopAt(core, host, 1018, 1018000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, countWrittenFrameType(host, 0x16), writtenFrameTypes(host).c_str());

    loopAt(core, host, 1021, 1021000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, countWrittenFrameType(host, 0x16), writtenFrameTypes(host).c_str());

    loopAt(core, host, 1253, 1253000);
    TEST_ASSERT_TRUE(countWrittenFrameType(host, 0x16) >= 3);
}

static void test_service_probe_drains_synchronous_loopback_echo_from_uart_buffer()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    config.transport.packetRateHz = 500;
    config.transport.replyTimeoutMs = 50;

    TEST_ASSERT_TRUE(core.begin(host, config, 0, 0));

    loopAt(core, host, 0, 0);
    loopAt(core, host, 1000, 1000000);

    host.loopbackWriteToRx = true;
    loopAt(core, host, 1020, 1020000);

    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x28));
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, host.serialReadCount, "transport never attempted to read echoed probe bytes");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)host.rx.size(), "loopback echo should be drained immediately after service TX");
}

static void test_service_probe_drain_preserves_following_module_reply_bytes()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    config.transport.packetRateHz = 500;
    config.transport.replyTimeoutMs = 50;

    TEST_ASSERT_TRUE(core.begin(host, config, 0, 0));

    loopAt(core, host, 0, 0);
    loopAt(core, host, 1000, 1000000);

    host.loopbackWriteToRx = true;
    host.rxAppendAfterWrite = makeDeviceInfoFrame("RM Ranger Micro", 33);
    loopAt(core, host, 1020, 1020000);

    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x28));
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, host.serialReadCount, "transport never attempted to read echoed probe bytes");
    TEST_ASSERT_FALSE(host.rx.empty());
    TEST_ASSERT_EQUAL_HEX8(0xEE, host.rx.front());
}

}

void setUp(void)
{
}

void tearDown(void)
{
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_rc_frame_packing_and_driver_enable);
    RUN_TEST(test_transport_inversion_setting_is_passed_to_hal);
    RUN_TEST(test_transport_debug_suppresses_raw_frame_dumps_by_default);
    RUN_TEST(test_transport_raw_frame_dump_requires_explicit_opt_in);
    RUN_TEST(test_transport_raw_frame_dump_logs_non_rc_replies_only);
    RUN_TEST(test_ads1015_single_ended_config_uses_4v096_range);
    RUN_TEST(test_adc_debug_log_only_emits_on_axis_change);
    RUN_TEST(test_light_iir_filter_moves_quarter_step_toward_sample);
    RUN_TEST(test_light_iir_filter_leaves_small_jitter_unchanged);
    RUN_TEST(test_input_model_center_maps_to_1500_us);
    RUN_TEST(test_input_model_min_max_map_to_1000_and_2000_us);
    RUN_TEST(test_input_model_reverse_flips_output);
    RUN_TEST(test_input_model_1500_us_maps_to_crsf_mid_ticks);
    RUN_TEST(test_input_model_deadband_holds_output_at_1500_us_near_center);
    RUN_TEST(test_input_model_deadband_only_affects_center_band);
    RUN_TEST(test_input_model_default_profile_matches_raw_adc_defaults);
    RUN_TEST(test_input_model_raw_three_point_profile_maps_exact_endpoints_and_center);
    RUN_TEST(test_input_model_descending_raw_profile_maps_exact_endpoints_and_center);
    RUN_TEST(test_input_model_descending_raw_profile_can_be_reversed);
    RUN_TEST(test_input_model_default_gimbal_routing_matches_current_banner_order);
    RUN_TEST(test_input_model_invalid_profile_normalizes_to_default);
    RUN_TEST(test_input_model_invalid_routing_normalizes_to_default);
    RUN_TEST(test_input_model_duplicate_routing_normalizes_to_default);
    RUN_TEST(test_echoed_tx_frame_is_ignored_as_reply);
    RUN_TEST(test_delayed_echoed_tx_frame_is_ignored_as_reply);
    RUN_TEST(test_rc_frames_do_not_arm_reply_timeouts);
    RUN_TEST(test_service_frame_reply_timeout_is_reported);
    RUN_TEST(test_service_reply_without_telemetry_does_not_report_replies_lost);
    RUN_TEST(test_unknown_frame_updates_raw_frame_status);
    RUN_TEST(test_packet_rate_scheduler_50_100_150_250hz);
    RUN_TEST(test_elrs_crsf_baud_matches_expresslrs_external_module_rate_requirements);
    RUN_TEST(test_invalid_packet_rate_uses_default_baud);
    RUN_TEST(test_500hz_shared_bus_uses_longer_reply_window);
    RUN_TEST(test_shared_bus_driver_turnaround_guards_are_nonzero);
    RUN_TEST(test_self_test_emits_known_frame);
    RUN_TEST(test_adc_missing_at_boot_sets_fault_and_safe_channels);
    RUN_TEST(test_adc_stale_after_valid_samples_uses_safe_fallback);
    RUN_TEST(test_button_pack_stale_holds_last_valid_states);
    RUN_TEST(test_button_pack_missing_at_boot_defaults_low);
    RUN_TEST(test_status_fault_transitions_clear_on_recovery);
    RUN_TEST(test_control_mapping_and_reversed_axis_calibration);
    RUN_TEST(test_axis_order_aileron_elevator_throttle_rudder_matches_runtime_banner);
    RUN_TEST(test_nondefault_axis_profile_changes_runtime_output_scaling);
    RUN_TEST(test_legacy_normalized_axis_profile_is_sanitized_before_runtime_mapping);
    RUN_TEST(test_gimbal_routing_can_claim_fixed_function_channels);
    RUN_TEST(test_telemetry_parsing_and_bad_crc_rejection);
    RUN_TEST(test_non_c8_sync_frame_is_accepted);
    RUN_TEST(test_parser_recovers_after_garbage_before_valid_frame);
    RUN_TEST(test_parser_recovers_after_bad_crc_followed_by_valid_frame);
    RUN_TEST(test_comm_codes_show_no_sync_until_valid_frame);
    RUN_TEST(test_lost_telemetry_sets_los_until_valid_frame);
    RUN_TEST(test_crc_burst_sets_crc_comm_code);
    RUN_TEST(test_frame_burst_sets_frm_comm_code);
    RUN_TEST(test_display_policy_prefers_gps_then_airspeed_then_link_quality);
    RUN_TEST(test_speed_units_default_to_kmh);
    RUN_TEST(test_speed_display_can_convert_kmh_to_mph);
    RUN_TEST(test_battery_overlay_beats_comm_overlay);
    RUN_TEST(test_calibration_prompt_beats_comm_overlay);
    RUN_TEST(test_adc_overlay_beats_comm_overlay);
    RUN_TEST(test_button_pack_overlay_beats_comm_overlay);
    RUN_TEST(test_battery_overlay_and_calibration_prompt_still_override_normal_display);
    RUN_TEST(test_module_settings_are_discovered_and_written);
    RUN_TEST(test_module_settings_retry_without_blocking_rc_output);
    RUN_TEST(test_module_settings_request_remaining_chunks_before_advancing_field);
    RUN_TEST(test_module_settings_retry_timed_out_chunk_before_scan_backoff);
    RUN_TEST(test_module_settings_retry_probe_before_long_backoff);
    RUN_TEST(test_module_probe_does_not_lower_configured_500hz_runtime_rate);
    RUN_TEST(test_bootstrap_probe_waits_long_enough_for_late_first_module_reply);
    RUN_TEST(test_module_settings_apply_after_targets_are_found_without_full_scan);
    RUN_TEST(test_module_settings_do_not_write_packet_rate_target_or_change_transport_rate);
    RUN_TEST(test_module_ping_holds_rc_until_reply_timeout_on_half_duplex_link);
    RUN_TEST(test_service_probe_drains_synchronous_loopback_echo_from_uart_buffer);
    RUN_TEST(test_service_probe_drain_preserves_following_module_reply_bytes);
    return UNITY_END();
}
