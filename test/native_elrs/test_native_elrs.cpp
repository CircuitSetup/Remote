#include <deque>
#include <cstring>
#include <string>
#include <vector>

#include <unity.h>

#include "src/CRSF/elrs_crsf_core.h"

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

    host.axes[AXIS_ROLL] = 2047;
    host.axes[AXIS_PITCH] = 1024;
    host.axes[AXIS_THROTTLE] = 0;
    host.axes[AXIS_YAW] = 2047;
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

static void test_reply_timeout_is_reported()
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
    TEST_ASSERT_EQUAL_UINT32(35, statusOf(core).lastReplyTimeoutAt);
    TEST_ASSERT_EQUAL_INT(2, (int)host.writes.size());
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
    ELRSCrsfCore core50;
    ELRSCrsfCore core100;
    ELRSCrsfCore core150;
    ELRSCrsfCore core250;
    ELRSCrsfCoreConfig config50 = defaultConfig();
    ELRSCrsfCoreConfig config100 = defaultConfig();
    ELRSCrsfCoreConfig config150 = defaultConfig();
    ELRSCrsfCoreConfig config250 = defaultConfig();

    config50.transport.packetRateHz = 50;
    config100.transport.packetRateHz = 100;
    config150.transport.packetRateHz = 150;
    config250.transport.packetRateHz = 250;

    TEST_ASSERT_TRUE(core50.begin(host50, config50, 0, 0));
    TEST_ASSERT_TRUE(core100.begin(host100, config100, 0, 0));
    TEST_ASSERT_TRUE(core150.begin(host150, config150, 0, 0));
    TEST_ASSERT_TRUE(core250.begin(host250, config250, 0, 0));

    loopAt(core50, host50, 0, 0);
    loopAt(core100, host100, 0, 0);
    loopAt(core150, host150, 0, 0);
    loopAt(core250, host250, 0, 0);

    TEST_ASSERT_EQUAL_INT(1, (int)host50.writes.size());
    TEST_ASSERT_EQUAL_INT(1, (int)host100.writes.size());
    TEST_ASSERT_EQUAL_INT(1, (int)host150.writes.size());
    TEST_ASSERT_EQUAL_INT(1, (int)host250.writes.size());

    loopAt(core50, host50, 19, 19999);
    loopAt(core100, host100, 9, 9999);
    loopAt(core150, host150, 6, 6665);
    loopAt(core250, host250, 3, 3999);

    TEST_ASSERT_EQUAL_INT(1, (int)host50.writes.size());
    TEST_ASSERT_EQUAL_INT(1, (int)host100.writes.size());
    TEST_ASSERT_EQUAL_INT(1, (int)host150.writes.size());
    TEST_ASSERT_EQUAL_INT(1, (int)host250.writes.size());

    loopAt(core50, host50, 20, 20000);
    loopAt(core100, host100, 10, 10000);
    loopAt(core150, host150, 6, 6666);
    loopAt(core250, host250, 4, 4000);

    TEST_ASSERT_EQUAL_INT(2, (int)host50.writes.size());
    TEST_ASSERT_EQUAL_INT(2, (int)host100.writes.size());
    TEST_ASSERT_EQUAL_INT(2, (int)host150.writes.size());
    TEST_ASSERT_EQUAL_INT(2, (int)host250.writes.size());

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
    TEST_ASSERT_EQUAL_UINT8(0xEE, (*frame)[0]);
    TEST_ASSERT_EQUAL_UINT8(0xEE, (*frame)[3]);
    TEST_ASSERT_EQUAL_UINT8(0xEA, (*frame)[4]);

    host.queueFrame(makeDeviceInfoFrame("ExpressLRS TX", 3));
    loopAt(core, host, 1030, 1030000);

    loopAt(core, host, 1120, 1120000);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x2C));
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
    TEST_ASSERT_EQUAL_UINT8(1, (*frame)[5]);
    TEST_ASSERT_EQUAL_UINT8(2, (*frame)[6]);

    loopAt(core, host, 1770, 1770000);
    loopAt(core, host, 1780, 1780000);
    loopAt(core, host, 1880, 1880000);
    TEST_ASSERT_EQUAL_INT(2, countWrittenFrameType(host, 0x2D));
    frame = findWrittenFrameType(host, 0x2D, 1);
    TEST_ASSERT_NOT_NULL(frame);
    TEST_ASSERT_EQUAL_UINT8(2, (*frame)[5]);
    TEST_ASSERT_EQUAL_UINT8(4, (*frame)[6]);

    loopAt(core, host, 2170, 2170000);
    loopAt(core, host, 2180, 2180000);
    loopAt(core, host, 2280, 2280000);
    TEST_ASSERT_EQUAL_INT(3, countWrittenFrameType(host, 0x2D));
    frame = findWrittenFrameType(host, 0x2D, 2);
    TEST_ASSERT_NOT_NULL(frame);
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
    RUN_TEST(test_reply_timeout_is_reported);
    RUN_TEST(test_unknown_frame_updates_raw_frame_status);
    RUN_TEST(test_packet_rate_scheduler_50_100_150_250hz);
    RUN_TEST(test_self_test_emits_known_frame);
    RUN_TEST(test_adc_missing_at_boot_sets_fault_and_safe_channels);
    RUN_TEST(test_adc_stale_after_valid_samples_uses_safe_fallback);
    RUN_TEST(test_button_pack_stale_holds_last_valid_states);
    RUN_TEST(test_button_pack_missing_at_boot_defaults_low);
    RUN_TEST(test_status_fault_transitions_clear_on_recovery);
    RUN_TEST(test_control_mapping_and_reversed_axis_calibration);
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
    return UNITY_END();
}
