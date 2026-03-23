#include <deque>
#include <string>
#include <vector>

#include <unity.h>

#include "elrs_crsf_core.h"

namespace {

constexpr uint8_t AXIS_THROTTLE = 0;
constexpr uint8_t AXIS_YAW = 1;
constexpr uint8_t AXIS_PITCH = 2;
constexpr uint8_t AXIS_ROLL = 3;

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

        void startSerial(uint32_t baud) override
        {
            bauds.push_back(baud);
            driverEnabled = false;
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
            return value;
        }

        size_t serialWrite(const uint8_t *data, size_t len) override
        {
            writes.push_back(std::vector<uint8_t>(data, data + len));
            driverStatesDuringWrite.push_back(driverEnabled);
            return len;
        }

        void serialFlush() override
        {
        }

        void setDriverEnabled(bool enabled) override
        {
            if(driverTransitions.empty() || driverTransitions.back() != enabled) {
                driverTransitions.push_back(enabled);
            }
            driverEnabled = enabled;
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

        void discardSerialInput() override
        {
            rx.clear();
            discardSerialCount++;
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
        bool displayOnCalled = false;
        bool powerLed = false;
        bool levelMeter = false;
        bool stopLed = false;
        int displaySpeed = -1;
        int displayShows = 0;
        int savedCalibrationCount = 0;
        int discardSerialCount = 0;
        DisplayMode displayMode = DISPLAY_NONE;
        std::string displayText;
        std::deque<uint8_t> rx;
        std::vector<std::string> logs;
        std::vector<uint32_t> bauds;
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

static void test_rc_frame_packing_and_driver_enable()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    static const uint8_t expectedFrame[26] = {
        0xC8, 0x18, 0x16, 0x13, 0x07, 0x1F, 0x2B, 0x26, 0x3E, 0x71, 0x56, 0x4C, 0x9C,
        0x15, 0xAC, 0x98, 0x38, 0x2B, 0x26, 0xCE, 0x0A, 0x56, 0x4C, 0x7C, 0xE2, 0xB8
    };

    host.axes[AXIS_ROLL] = 2047;
    host.axes[AXIS_PITCH] = 1024;
    host.axes[AXIS_THROTTLE] = 0;
    host.axes[AXIS_YAW] = 2047;
    host.stop = true;
    host.buttonA = true;
    host.packStates = 0b11001010;

    TEST_ASSERT_TRUE(core.begin(host, config, 0));
    core.loop(host, 10, 0);

    TEST_ASSERT_EQUAL_UINT32(420000, host.bauds[0]);
    TEST_ASSERT_EQUAL_INT(1, (int)host.writes.size());
    TEST_ASSERT_TRUE(host.driverStatesDuringWrite[0]);
    TEST_ASSERT_FALSE(host.driverEnabled);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(2, (int)host.driverTransitions.size());
    TEST_ASSERT_TRUE(host.driverTransitions[host.driverTransitions.size() - 2]);
    TEST_ASSERT_FALSE(host.driverTransitions[host.driverTransitions.size() - 1]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedFrame, host.writes[0].data(), 26);
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

    host.axes[AXIS_ROLL] = 1800;
    host.axes[AXIS_PITCH] = 900;
    host.axes[AXIS_THROTTLE] = 1500;
    host.axes[AXIS_YAW] = 1100;

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
    host.axes[AXIS_ROLL] = 1200;
    host.axes[AXIS_PITCH] = 1300;
    host.axes[AXIS_THROTTLE] = 1400;
    host.axes[AXIS_YAW] = 1500;
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

    host.axes[AXIS_ROLL] = 2047;
    host.axes[AXIS_PITCH] = 1024;
    host.axes[AXIS_THROTTLE] = 0;
    host.axes[AXIS_YAW] = 2047;
    host.stop = true;
    host.fakePower = true;
    host.buttonA = true;
    host.buttonB = true;
    host.packStates = 0b10100101;
    host.calibration[AXIS_YAW].minimum = 2047;
    host.calibration[AXIS_YAW].center = 1024;
    host.calibration[AXIS_YAW].maximum = 0;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
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

    host.axes[AXIS_YAW] = 0;
    core.loop(host, 20, 0);
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(3));
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

static void test_baud_fallback_clears_pending_rx()
{
    FakeHost host;
    ELRSCrsfCore core;

    queueBytes(host, makeGarbage());
    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    core.loop(host, 3001, 0);

    ELRSCrsfStatus status = statusOf(core);
    TEST_ASSERT_TRUE(status.faultFlags & ELRS_FAULT_FALLBACK_BAUD);
    TEST_ASSERT_EQUAL_INT(1, host.discardSerialCount);
    TEST_ASSERT_EQUAL_INT(0, (int)host.rx.size());
    TEST_ASSERT_FALSE(core.synced());

    queueBytes(host, makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 73, 0, 0, 0, 0, 0, 0, 0 }));
    core.loop(host, 3100, 0);
    TEST_ASSERT_EQUAL_UINT8(73, core.linkQuality());
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

static void test_battery_overlay_calibration_overlay_and_baud_fallback()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(core.begin(host, defaultConfig(), 0));
    core.loop(host, 3001, 0);
    TEST_ASSERT_EQUAL_INT(2, (int)host.bauds.size());
    TEST_ASSERT_EQUAL_UINT32(400000, host.bauds[1]);

    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 42, 0, 0, 0, 0, 0, 0, 0 }));
    core.loop(host, 30100, 0);
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
    RUN_TEST(test_adc_missing_at_boot_sets_fault_and_safe_channels);
    RUN_TEST(test_adc_stale_after_valid_samples_uses_safe_fallback);
    RUN_TEST(test_button_pack_stale_holds_last_valid_states);
    RUN_TEST(test_button_pack_missing_at_boot_defaults_low);
    RUN_TEST(test_status_fault_transitions_clear_on_recovery);
    RUN_TEST(test_control_mapping_and_reversed_axis_calibration);
    RUN_TEST(test_telemetry_parsing_and_bad_crc_rejection);
    RUN_TEST(test_parser_recovers_after_garbage_before_valid_frame);
    RUN_TEST(test_parser_recovers_after_bad_crc_followed_by_valid_frame);
    RUN_TEST(test_baud_fallback_clears_pending_rx);
    RUN_TEST(test_display_policy_prefers_gps_then_airspeed_then_link_quality);
    RUN_TEST(test_battery_overlay_calibration_overlay_and_baud_fallback);
    return UNITY_END();
}
