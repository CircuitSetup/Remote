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
            txStarts.push_back(fakeMicros);
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
            if(costedIo) advanceTime((uint32_t)((writes.back().size() * 10000000ULL + bauds.back() - 1) / bauds.back()) + extraFlushUs);
            txStops.push_back(fakeMicros);
            flushCount++;
        }

        void setDriverEnabled(bool enabled) override
        {
            if(costedIo && !enabled) advanceTime(40);
            (enabled ? oeOnTimes : oeOffTimes).push_back(fakeMicros);
            if(driverTransitions.empty() || driverTransitions.back() != enabled) {
                driverTransitions.push_back(enabled);
            }
            driverEnabled = enabled;
            if(costedIo) advanceTime(enabled ? 40 : 150);
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

        unsigned long millisNow() { return fakeMillis; }

        void setTime(uint32_t ms, uint32_t us)
        {
            fakeMillis = ms;
            fakeMicros = us;
            subMillisUs = us % 1000;
        }

        void advanceTime(uint32_t us)
        {
            fakeMicros += us;
            fakeMillis += (subMillisUs + us) / 1000;
            subMillisUs = (subMillisUs + us) % 1000;
        }

        ELRSAxesResult sampleAxes(int16_t axesOut[ELRS_GIMBAL_AXIS_COUNT], uint32_t &completedAt,
                                  ELRSAxesRequest request, uint32_t) override
        {
            if(request != ELRS_AXES_POLL) axesAwaiting = true;
            if(!axesAvailable) { axesAwaiting = false; return ELRS_AXES_ERROR; }
            if(!axesAwaiting || axesPending) return ELRS_AXES_PENDING;
            advanceTime(sampleCostUs);
            sampleCompletions.push_back(fakeMicros);
            for(int i = 0; i < ELRS_GIMBAL_AXIS_COUNT; i++) axesOut[i] = axes[i];
            completedAt = fakeMillis;
            axesAwaiting = false;
            return ELRS_AXES_READY;
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

        void scanLocalSwitches(uint16_t states, uint16_t validMask) override
        {
            localScanCount++;
            localStates = states;
            localValidMask = validMask;
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

        bool saveCalibration(const ELRSAxisCalibrationData *cal, int count) override
        {
            savedCalibrationCount = count;
            if(!calibrationWriteOk) return false;
            for(int i = 0; i < count && i < ELRS_GIMBAL_AXIS_COUNT; i++) {
                calibration[i] = cal[i];
            }
            return true;
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
        bool axesPending = false;
        bool axesAwaiting = false;
        bool fakePower = false;
        bool stop = false;
        bool buttonA = false;
        bool buttonB = false;
        bool calibrationButton = false;
        bool packAvailable = true;
        uint8_t packStates = 0;
        uint16_t localStates = 0, localValidMask = 0;
        int localScanCount = 0;

        bool driverEnabled = false;
        uint32_t fakeMicros = 0, fakeMillis = 0, subMillisUs = 0;
        uint32_t sampleCostUs = 0, extraFlushUs = 0;
        bool costedIo = false;
        std::vector<uint32_t> txStarts, txStops, oeOnTimes, oeOffTimes, sampleCompletions;
        int serialReadCount = 0;
        bool displayOnCalled = false;
        bool powerLed = false;
        bool levelMeter = false;
        bool stopLed = false;
        int displaySpeed = -1;
        int displayShows = 0;
        int savedCalibrationCount = 0;
        bool calibrationWriteOk = true;
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
    host.setTime((uint32_t)nowMs, (uint32_t)nowUs);
    core.loop(host, nowMs, nowUs, battWarn);
}

static void loopAtMs(ELRSCrsfCore &core, FakeHost &host, uint32_t nowMs, int battWarn)
{
    loopAt(core, host, nowMs, (uint32_t)(nowMs * 1000U), battWarn);
}

static bool beginAt(ELRSCrsfCore &core, FakeHost &host, const ELRSCrsfCoreConfig &config, uint32_t ms, uint32_t us)
{
    host.setTime(ms, us);
    return core.begin(host, config, ms, us);
}

static bool beginAt(ELRSCrsfCore &core, FakeHost &host, const ELRSCrsfCoreConfig &config, uint32_t ms)
{
    return beginAt(core, host, config, ms, (uint32_t)(ms * 1000U));
}

static void transportAt(ELRSCrsfTransport &transport, FakeHost &host, uint32_t ms, uint32_t us)
{
    host.setTime(ms, us);
    transport.loop(host, ms, us);
}

static void beginAt(ELRSCrsfTransport &transport, FakeHost &host, const ELRSCrsfTransportConfig &config, uint32_t ms, uint32_t us)
{
    host.setTime(ms, us);
    transport.begin(host, config, ms, us);
}

static void test_tx_deadline_advances_past_real_io_completion()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    config.propControls = true;
    config.haveButtonPack = false;
    TEST_ASSERT_TRUE(beginAt(core, host, config, 0, 0));
    host.costedIo = true;
    host.sampleCostUs = 2810;
    loopAt(core, host, 5, 5000);
    TEST_ASSERT_EQUAL_UINT32(7810, host.sampleCompletions.back());
    TEST_ASSERT_EQUAL_UINT32(7850, host.txStarts[0]);
    TEST_ASSERT_EQUAL_UINT32(8500, host.txStops[0]);
    TEST_ASSERT_EQUAL_UINT32(8540, host.oeOffTimes.back());
    loopAt(core, host, host.fakeMillis, host.fakeMicros);
    if(host.txStarts.size() > 1) printf("HP-1 baseline: TX interval=%luus OE off=%luus\n",
        (unsigned long)(host.txStarts[1] - host.txStarts[0]),
        (unsigned long)(host.oeOnTimes.back() - host.oeOffTimes[host.oeOffTimes.size() - 2]));
    TEST_ASSERT_EQUAL_UINT32(1, host.writes.size());
    host.sampleCostUs = 0;
    for(uint32_t us = host.fakeMicros; us <= 16000; us += 10) loopAt(core, host, us / 1000, us);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(4000, host.oeOnTimes[1] - host.oeOnTimes[0]);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(3270, host.oeOnTimes[1] - host.oeOffTimes[1]);
    printf("HP-1 repaired: TX interval=%luus OE off=%luus\n",
        (unsigned long)(host.txStarts[1] - host.txStarts[0]),
        (unsigned long)(host.oeOnTimes[1] - host.oeOffTimes[1]));
}

static void test_late_tx_preserves_receive_opportunity()
{
    const uint16_t rates[] = {50, 100, 150, 250, 500};
    for(uint16_t rate : rates) {
        for(uint32_t frameLen : {6U, 26U, 64U}) {
            for(uint32_t delay : {201U, 1000U, 1990U, 0U, UINT32_MAX}) {
                FakeHost host;
                ELRSCrsfTransport transport;
                ELRSCrsfTransportConfig config;
                config.packetRateHz = rate;
                config.baudRate = elrsCrsfRecommendedBaudRate(rate);
                transport.begin(host, config, 0, 0);
                if(frameLen != 26) {
                    const std::vector<uint8_t> service = makeFrame(0x28, std::vector<uint8_t>(frameLen - 4, 0));
                    TEST_ASSERT_TRUE(transport.queueServiceFrame(service.data(), service.size()));
                }
                host.costedIo = true;
                const uint32_t period = 1000000U / rate;
                const uint32_t wireUs = (frameLen * 10000000U + config.baudRate - 1) / config.baudRate;
                // Completion falls just BEFORE or AFTER the next deadline.
                uint32_t start = delay == 0 ? period - wireUs - 230 - 10 :
                    (delay == UINT32_MAX ? period - wireUs - 230 + 990 : delay);
                transportAt(transport, host, start / 1000, start);
                const uint32_t off = host.oeOffTimes.back();
                const uint32_t on = host.oeOnTimes.back();
                for(uint32_t us = host.fakeMicros; host.txStarts.size() < 2; us += 10)
                    transportAt(transport, host, us / 1000, us);
                TEST_ASSERT_GREATER_OR_EQUAL_UINT32(period, host.oeOnTimes.back() - on);
                TEST_ASSERT_GREATER_OR_EQUAL_UINT32(period - 40 - wireUs - 40, host.oeOnTimes.back() - off);
            }
        }
    }
}

static void test_bounded_polling_jitter_keeps_healthy_tx_cadence()
{
    for(uint16_t rate : {50, 100, 150, 250, 500}) {
        FakeHost host;
        ELRSCrsfTransport transport;
        ELRSCrsfTransportConfig config;
        config.packetRateHz = rate;
        config.baudRate = elrsCrsfRecommendedBaudRate(rate);
        transport.begin(host, config, 0, 0);
        host.costedIo = true;
        for(uint32_t slot = 0; slot < 31; slot++) {
            const uint32_t deadline = (uint32_t)((uint64_t)slot * 1000000 / rate);
            const uint32_t jitter = (slot % 3) * 90; // Independently bounded at 180us.
            transportAt(transport, host, (deadline + jitter) / 1000, deadline + jitter);
            TEST_ASSERT_EQUAL_UINT32(slot + 1, host.txStarts.size());
            TEST_ASSERT_EQUAL_UINT32(deadline + jitter + 40, host.txStarts.back());
        }
    }
}

static void test_tx_pacing_retains_fractional_periods_and_rollover()
{
    for(uint16_t rate : {50, 100, 150, 250, 500}) {
        FakeHost host;
        ELRSCrsfTransport transport;
        ELRSCrsfTransportConfig config;
        config.packetRateHz = rate;
        config.baudRate = elrsCrsfRecommendedBaudRate(rate);
        const uint32_t start = UINT32_MAX - 20000;
        host.setTime(4000000, start); // millis is deliberately in another rollover domain.
        transport.begin(host, config, host.fakeMillis, start);
        host.costedIo = true;
        for(uint32_t slot = 0; slot <= 30; slot++) {
            const uint32_t elapsed = (uint32_t)((uint64_t)slot * 1000000 / rate);
            transportAt(transport, host, 4000000 + elapsed / 1000, start + elapsed);
            TEST_ASSERT_EQUAL_UINT32(slot + 1, host.txStarts.size());
            TEST_ASSERT_EQUAL_UINT32(start + elapsed + 40, host.txStarts.back());
        }
        const uint32_t paused = start + 1000000;
        transportAt(transport, host, 4001000, paused);
        const size_t count = host.txStarts.size();
        transportAt(transport, host, host.fakeMillis, host.fakeMicros);
        TEST_ASSERT_EQUAL_UINT32(count, host.txStarts.size());
        const uint32_t period = (1000000 + rate - 1) / rate;
        transportAt(transport, host, 4001000 + period / 1000, paused + period);
        TEST_ASSERT_EQUAL_UINT32(count + 1, host.txStarts.size());
    }
}

static void test_costed_service_slots_keep_rc_and_real_reply_deadlines()
{
    FakeHost host;
    ELRSCrsfTransport transport;
    ELRSCrsfTransportConfig config;
    config.packetRateHz = 500;
    config.baudRate = 921600;
    transport.begin(host, config, 0, 0);
    host.costedIo = true;
    host.extraFlushUs = 1500; // Real completion crosses a millisecond boundary.
    const std::vector<uint8_t> ping = makeFrame(0x28, {0xEE, 0xEA});
    TEST_ASSERT_TRUE(transport.queueServiceFrame(ping.data(), ping.size()));
    transportAt(transport, host, UINT32_MAX - 249, 0);
    const uint32_t releasedMs = host.fakeMillis;
    TEST_ASSERT_EQUAL_UINT32(releasedMs, transport.status().lastTxAt);
    host.extraFlushUs = 0;
    TEST_ASSERT_TRUE(transport.queueServiceFrame(ping.data(), ping.size()));
    for(uint32_t us = 2000; us < 100000; us += 2000)
        transportAt(transport, host, (uint32_t)(releasedMs + us / 1000), us);
    TEST_ASSERT_GREATER_THAN_INT(40, countWrittenFrameType(host, 0x16));
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x28));
    transportAt(transport, host, releasedMs + 249, 249000);
    TEST_ASSERT_EQUAL_UINT32(0, transport.status().lastReplyTimeoutAt);
    // A later service slot refreshes the transaction deadline; RC never pauses for it.
    TEST_ASSERT_EQUAL_INT(2, countWrittenFrameType(host, 0x28));
}

static void test_pending_axes_keep_filter_and_rc_slots()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    config.propControls = true;
    config.transport.packetRateHz = 500;
    host.axes[0] = 1800;
    TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
    host.axesPending = true;
    int16_t axes[4];
    for(uint32_t ms = 2; ms <= 100; ms += 2) {
        loopAt(core, host, ms, ms * 1000);
        TEST_ASSERT_TRUE(core.readFilteredAxes(axes));
        TEST_ASSERT_EQUAL_INT16(1800, axes[0]);
        TEST_ASSERT_FALSE(statusOf(core).faultFlags & (ELRS_FAULT_ADC_MISSING | ELRS_FAULT_ADC_STALE));
    }
    TEST_ASSERT_EQUAL_UINT32(50, host.txStarts.size());
    loopAt(core, host, 101, 101000);
    TEST_ASSERT_FALSE(core.readFilteredAxes(axes));
    TEST_ASSERT_TRUE(statusOf(core).faultFlags & ELRS_FAULT_ADC_STALE);
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(0));
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(2));
}

static void test_new_identical_samples_and_separate_input_rollover()
{
    for(uint32_t origin : {0U, UINT32_MAX - 50}) {
        FakeHost host;
        ELRSCrsfCore core;
        TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), origin, UINT32_MAX - 1000));
        for(uint32_t elapsed = 20; elapsed <= 300; elapsed += 20) {
            loopAt(core, host, origin + elapsed, (uint32_t)(UINT32_MAX - 1000 + elapsed * 1000));
            TEST_ASSERT_FALSE(statusOf(core).faultFlags & ELRS_FAULT_ADC_STALE);
        }
        host.axesPending = true;
        loopAt(core, host, origin + 400, 1000);
        int16_t axes[4]; TEST_ASSERT_TRUE(core.readFilteredAxes(axes));
        loopAt(core, host, origin + 401, 2000);
        TEST_ASSERT_FALSE(core.readFilteredAxes(axes));
        TEST_ASSERT_TRUE(statusOf(core).faultFlags & ELRS_FAULT_ADC_STALE);
    }
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

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
    loopAtMs(core, host, 10, 0);

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

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
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

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
    loopAtMs(core, host, 10, 0);

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

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0, 0));
    host.queueFrame(makeDeviceInfoFrame("RM Ranger Micro", 33));
    loopAt(core, host, 100, 100000);

#ifdef REMOTE_CRSF_NO_RAW_DUMPS
    TEST_ASSERT_FALSE(logsContain(host, "ELRS/CRSF RX len=36"));
    TEST_ASSERT_FALSE(statusOf(core).rawFrameDebugEnabled);
#else
    TEST_ASSERT_TRUE(logsContain(host, "ELRS/CRSF RX len=36"));
    TEST_ASSERT_TRUE(statusOf(core).rawFrameDebugEnabled);
#endif
    TEST_ASSERT_EQUAL_HEX8(0x29, statusOf(core).lastRawFrameType);
    TEST_ASSERT_EQUAL_UINT8(36, statusOf(core).lastRawFrameLength);
}

static void test_transport_raw_frame_dump_logs_non_rc_replies_only()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    config.transport.debugEnabled = true;
    config.transport.rawFrameDebugEnabled = true;

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0, 0));
    loopAt(core, host, 0, 0);

    host.queueFrame(makeDeviceInfoFrame("RM Ranger Micro", 33));
    loopAt(core, host, 100, 100000);

#ifdef REMOTE_CRSF_NO_RAW_DUMPS
    TEST_ASSERT_FALSE(logsContain(host, "ELRS/CRSF RX len=36"));
#else
    TEST_ASSERT_TRUE(logsContain(host, "ELRS/CRSF RX len=36"));
#endif
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

static void test_light_iir_filter_converges_on_small_stable_changes()
{
    for(int16_t sample : {997, 999, 1000, 1001, 1003}) {
        int16_t filtered = 1000;
        for(int i = 0; i < 5; i++) filtered = elrsIirFilterStep(filtered, sample, 2);
        TEST_ASSERT_EQUAL_INT16(sample, filtered);
    }
}

static void test_output_limits_scale_each_side_of_neutral()
{
    const ELRSOutputLimits limits = {1200, 1800};
    const int16_t inputs[] = {1000, 1250, 1500, 1750, 2000};
    const int16_t expected[] = {1200, 1350, 1500, 1650, 1800};
    for(int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_INT16(expected[i], elrsApplyOutputLimits(limits, inputs[i]));
    }
    const ELRSOutputLimits asymmetric = {1100, 1800};
    TEST_ASSERT_EQUAL_INT16(1300, elrsApplyOutputLimits(asymmetric, 1250));
    TEST_ASSERT_EQUAL_INT16(1650, elrsApplyOutputLimits(asymmetric, 1750));
    const ELRSOutputLimits defaults = elrsDefaultOutputLimits();
    for(int16_t us = 1000; us <= 2000; us++) {
        TEST_ASSERT_EQUAL_INT16(us, elrsApplyOutputLimits(defaults, us));
    }
}

static void test_output_limits_validate_and_clamp()
{
    const ELRSOutputLimits limits = {1200, 1800};
    TEST_ASSERT_TRUE(elrsIsValidOutputLimits(limits));
    TEST_ASSERT_EQUAL_INT16(1200, elrsApplyOutputLimits(limits, -32768));
    TEST_ASSERT_EQUAL_INT16(1800, elrsApplyOutputLimits(limits, 32767));
    const ELRSOutputLimits neutral = {1500, 1500};
    TEST_ASSERT_TRUE(elrsIsValidOutputLimits(neutral));
    TEST_ASSERT_EQUAL_INT16(1500, elrsApplyOutputLimits(neutral, 1000));
    TEST_ASSERT_EQUAL_INT16(1500, elrsApplyOutputLimits(neutral, 2000));
    const ELRSOutputLimits oneSided[] = {{1500, 1800}, {1200, 1500}};
    for(const auto &value : oneSided) TEST_ASSERT_TRUE(elrsIsValidOutputLimits(value));
    const ELRSOutputLimits invalid[] = {{999, 2000}, {1000, 2001}, {1501, 1800}, {1200, 1499}, {0, 0}, {65535, 65535}};
    for(const auto &value : invalid) {
        TEST_ASSERT_FALSE(elrsIsValidOutputLimits(value));
        const ELRSOutputLimits sanitized = elrsSanitizeOutputLimits(value);
        TEST_ASSERT_EQUAL_UINT16(1000, sanitized.minimumUs);
        TEST_ASSERT_EQUAL_UINT16(2000, sanitized.maximumUs);
    }
}

static void test_output_limits_follow_axes_through_reverse_and_routing()
{
    const ELRSOutputLimits limits[] = {{1100,1700}, {1200,1800}, {1300,1900}, {1400,1600}};
    const uint16_t lowTicks[] = {336, 500, 664, 828};
    const uint16_t highTicks[] = {1319, 1483, 1647, 1155};
    const uint8_t channels[] = {4, 3, 1, 2};
    for(int descending = 0; descending < 2; descending++) {
        for(int reverse = 0; reverse < 2; reverse++) {
            FakeHost host;
            ELRSCrsfCore core;
            ELRSCrsfCoreConfig config = defaultConfig();
            config.inputRouting = {4, 3, 2, 1};
            config.throttleIdleDeadband = 0;
            for(int axis = 0; axis < 4; axis++) {
                config.outputLimits[axis] = limits[axis];
                config.axisProfiles[axis] = elrsDefaultInputAxisProfile();
                config.axisProfiles[axis].minimum = descending ? 1800 : 300;
                config.axisProfiles[axis].center = 900;
                config.axisProfiles[axis].maximum = descending ? 300 : 1800;
                config.axisProfiles[axis].reverse = reverse;
                host.axes[axis] = 900;
            }
            TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
            for(int point = 0; point < 3; point++) {
                uint16_t expectedChannels[16];
                for(int i = 0; i < 16; i++) expectedChannels[i] = 172;
                for(int axis = 0; axis < 4; axis++) {
                    const auto &profile = config.axisProfiles[axis];
                    host.axes[axis] = point == 0 ? profile.minimum : point == 1 ? profile.center : profile.maximum;
                    expectedChannels[channels[axis] - 1] = point == 1 ? 992 : (point == reverse * 2 ? lowTicks[axis] : highTicks[axis]);
                }
                host.writes.clear();
                loopAtMs(core, host, 20 * (point + 1), 0);
                for(int axis = 0; axis < 4; axis++) {
                    TEST_ASSERT_EQUAL_UINT16(expectedChannels[channels[axis] - 1], core.channelAt(channels[axis] - 1));
                }
                uint8_t expectedFrame[26];
                ELRSCrsfCore::packRcChannelsFrame(expectedChannels, expectedFrame, sizeof(expectedFrame));
                TEST_ASSERT_FALSE(host.writes.empty());
                TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedFrame, host.writes[0].data(), 26);
            }
        }
    }
}

// Missing centered shaping, shaping before deadband/reversal, or shaping throttle
// twice would break these literal outputs through the existing public APIs.
static void test_gimbal_curve_values_and_bounds()
{
    const int16_t inputs[] = {1000, 1250, 1500, 1750, 2000};
    const uint8_t strengths[] = {0, 40, 100};
    const int16_t centeredValues[][5] = {{1000, 1250, 1500, 1750, 2000}, {1000, 1325, 1500, 1675, 2000}, {1000, 1437, 1500, 1563, 2000}};
    const int16_t throttleValues[][5] = {{1000, 1250, 1500, 1750, 2000}, {1000, 1156, 1350, 1619, 2000}, {1000, 1016, 1125, 1422, 2000}};
    for(int row = 0; row < 3; row++) {
        for(int i = 0; i < 5; i++) {
            TEST_ASSERT_EQUAL_INT16(centeredValues[row][i], elrsInputModelApplyExpo(inputs[i], strengths[row], true));
            TEST_ASSERT_EQUAL_INT16(throttleValues[row][i], elrsInputModelApplyExpo(inputs[i], strengths[row], false));
        }
    }
    for(int strength = 0; strength <= 100; strength++) {
        for(bool centered : {false, true}) {
            int16_t previous = 1000;
            TEST_ASSERT_EQUAL_INT16(1000, elrsInputModelApplyExpo(500, strength, centered));
            TEST_ASSERT_EQUAL_INT16(2000, elrsInputModelApplyExpo(2500, strength, centered));
            TEST_ASSERT_EQUAL_INT16(1000, elrsInputModelApplyExpo(1000, strength, centered));
            TEST_ASSERT_EQUAL_INT16(2000, elrsInputModelApplyExpo(2000, strength, centered));
            for(int16_t input = 1000; input <= 2000; input++) {
                int16_t output = elrsInputModelApplyExpo(input, strength, centered);
                TEST_ASSERT_TRUE(output >= 1000 && output <= 2000);
                TEST_ASSERT_TRUE(output >= previous);
                if(!strength) TEST_ASSERT_EQUAL_INT16(input, output);
                if(centered) {
                    TEST_ASSERT_EQUAL_INT16(3000 - output, elrsInputModelApplyExpo(3000 - input, strength, true));
                    TEST_ASSERT_TRUE(input <= 1500 ? (output >= input && output <= 1500) : (output <= input && output >= 1500));
                } else {
                    TEST_ASSERT_TRUE(output <= input);
                }
                previous = output;
            }
        }
    }
    for(uint8_t strength : {101, 255}) {
        for(bool centered : {false, true}) {
            TEST_ASSERT_EQUAL_INT16(1000, elrsInputModelApplyExpo(500, strength, centered));
            TEST_ASSERT_EQUAL_INT16(2000, elrsInputModelApplyExpo(2500, strength, centered));
            for(int16_t input = 1000; input <= 2000; input++) TEST_ASSERT_EQUAL_INT16(input, elrsInputModelApplyExpo(input, strength, centered));
        }
    }
}

static void test_gimbal_curve_matches_original_integer_rounding()
{
    for(bool centered : {false, true}) {
        for(int strength = 0; strength <= 255; strength++) {
            for(int input = 1000; input <= 2000; input++) {
                const int16_t origin = centered ? 1500 : 1000;
                const int64_t span = centered ? 500 : 1000;
                int64_t magnitude = (int64_t)input - origin;
                const bool negative = magnitude < 0;
                if(negative) magnitude = -magnitude;
                const int64_t denominator = 100 * span * span;
                const int64_t numerator = (100 - strength) * magnitude * span * span +
                                          strength * magnitude * magnitude * magnitude;
                const int16_t shaped = (int16_t)((numerator + denominator / 2) / denominator);
                const int16_t expected = !strength || strength > 100 ? input :
                    origin + (negative ? -shaped : shaped);
                TEST_ASSERT_EQUAL_INT16(expected, elrsInputModelApplyExpo(input, strength, centered));
            }
            for(int16_t input : {-32768, 999, 2001, 32767}) {
                TEST_ASSERT_EQUAL_INT16(input < 1000 ? 1000 : 2000,
                                       elrsInputModelApplyExpo(input, strength, centered));
            }
        }
    }
}

static void test_throttle_curves_preserve_idle_center_and_endpoints()
{
    for(uint8_t strength : {40, 100}) {
        for(int narrow = 0; narrow <= 2; narrow++) {
            for(int descending = 0; descending <= 1; descending++) {
                for(int reverse = 0; reverse <= 1; reverse++) {
                    for(uint16_t band : {0, 5, 32}) {
                        int16_t lo = narrow ? 1000 : 300;
                        int16_t mid = narrow == 2 ? 1001 : (narrow ? 1010 : 900);
                        int16_t hi = narrow == 2 ? 1002 : (narrow ? 1020 : 1500);
                        ELRSInputAxisProfile profile = {descending ? hi : lo, mid, descending ? lo : hi, (uint16_t)reverse, 0, strength};
                        int idle = reverse ? profile.maximum : profile.minimum;
                        int full = reverse ? profile.minimum : profile.maximum;
                        int direction = full > idle ? 1 : -1;
                        TEST_ASSERT_EQUAL_INT16(1000, elrsInputModelThrottleToUs(profile, idle, band));
                        TEST_ASSERT_EQUAL_INT16(2000, elrsInputModelThrottleToUs(profile, full, band));
                        TEST_ASSERT_EQUAL_INT16(strength == 40 ? 1350 : 1125, elrsInputModelThrottleToUs(profile, mid, band));
                        FakeHost host;
                        ELRSCrsfCore core;
                        ELRSCrsfCoreConfig config = defaultConfig();
                        config.axisProfiles[AXIS_THROTTLE] = profile;
                        config.throttleIdleDeadband = band;
                        host.axes[AXIS_THROTTLE] = mid;
                        TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
                        TEST_ASSERT_EQUAL_UINT16(strength == 40 ? 746 : 377, core.channelAt(2));
                        host.axes[AXIS_THROTTLE] = full;
                        loopAtMs(core, host, 20, 0);
                        TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(2));
                        host.axes[AXIS_THROTTLE] = idle;
                        loopAtMs(core, host, 40, 0);
                        TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(2));
                        if(band) {
                            int span = direction * (mid - idle);
                            int amount = band < span ? band : span - 1;
                            host.axes[AXIS_THROTTLE] = idle + direction * amount;
                            loopAtMs(core, host, 60, 0);
                            TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(2));
                        }
                        if(!narrow && !band) {
                            TEST_ASSERT_EQUAL_INT16(strength == 40 ? 1156 : 1016, elrsInputModelThrottleToUs(profile, idle + direction * 300, 0));
                            TEST_ASSERT_EQUAL_INT16(strength == 40 ? 1619 : 1422, elrsInputModelThrottleToUs(profile, mid + direction * 300, 0));
                        }
                        // Center deadband is resolved before the idle curve, even
                        // when a large band leaves only one count at an endpoint.
                        profile.deadband = narrow == 2 ? 0 : (narrow ? 9 : 20);
                        TEST_ASSERT_EQUAL_INT16(1000, elrsInputModelThrottleToUs(profile, idle, band));
                        TEST_ASSERT_EQUAL_INT16(2000, elrsInputModelThrottleToUs(profile, full, band));
                        TEST_ASSERT_EQUAL_INT16(strength == 40 ? 1350 : 1125, elrsInputModelThrottleToUs(profile, mid, band));
                    }
                }
            }
        }
    }
}

static void test_output_limits_preserve_safe_neutral_and_idle()
{
    for(int scenario = 0; scenario < 3; scenario++) {
        FakeHost host;
        ELRSCrsfCore core;
        ELRSCrsfCoreConfig config = defaultConfig();
        config.inputRouting = {4, 3, 2, 1};
        for(auto &limits : config.outputLimits) limits = {1200, 1800};
        for(auto &profile : config.axisProfiles) profile.expo = 100;
        host.axes[AXIS_THROTTLE] = 0;
        host.axesAvailable = scenario != 0;
        TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
        if(scenario) {
            TEST_ASSERT_EQUAL_UINT16(500, core.channelAt(1));
            if(scenario == 1) host.axesAvailable = false;
            else core.startSelfTest(0);
            loopAtMs(core, host, 150, 0);
        }
        for(int channel = 0; channel < 4; channel++) TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(channel));
    }
}

static void test_output_limits_runtime_invalid_pairs_use_defaults()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    config.outputLimits[0] = {1600, 1700};
    host.axes[0] = 0;
    beginAt(core, host, config, 0);
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(0));
    host.axes[0] = 2047;
    loopAtMs(core, host, 20, 0);
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(0));
}

static void test_gimbal_curves_keep_direct_safe_outputs_and_reseed_on_recovery()
{
    for(uint8_t strength : {40, 100}) {
        FakeHost host;
        ELRSCrsfCore core;
        ELRSCrsfCoreConfig config = defaultConfig();
        config.adcHysteresis = 32;
        config.throttleIdleDeadband = 5;
        for(int axis = 0; axis < 4; axis++) config.axisProfiles[axis] = {300, 900, 1500, 0, 0, strength};
        host.axesAvailable = false;
        TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
        TEST_ASSERT_TRUE(statusOf(core).faultFlags & ELRS_FAULT_ADC_MISSING);
        for(int channel : {0, 1, 3}) TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(channel));
        TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(2));
        host.axesAvailable = true;
        for(int axis = 0; axis < 4; axis++) host.axes[axis] = 900;
        loopAtMs(core, host, 20, 0);
        TEST_ASSERT_FALSE(statusOf(core).faultFlags & (ELRS_FAULT_ADC_MISSING | ELRS_FAULT_ADC_STALE));
        TEST_ASSERT_EQUAL_UINT16(strength == 40 ? 746 : 377, core.channelAt(2));
        // Return to idle must bypass even a 32-count hold. At strength 100,
        // nearest-us rounding extends idle beyond the raw five-count band.
        host.axes[AXIS_THROTTLE] = strength == 40 ? 330 : 400;
        loopAtMs(core, host, 40, 0);
        TEST_ASSERT_TRUE(core.channelAt(2) > 172);
        host.axes[AXIS_THROTTLE] = strength == 40 ? 305 : 380;
        loopAtMs(core, host, 60, 0);
        TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(2));
        host.axes[AXIS_THROTTLE] = 600;
        loopAtMs(core, host, 80, 0);
        uint16_t beforeFailure = core.channelAt(2);
        host.axesAvailable = false;
        loopAtMs(core, host, 100, 0);
        // A failed sample clears the raw hold, and fresh recovery reseeds it.
        host.axesAvailable = true;
        host.axes[AXIS_THROTTLE] = 620;
        loopAtMs(core, host, 120, 0);
        TEST_ASSERT_TRUE(core.channelAt(2) > beforeFailure);
        host.axesAvailable = false;
        loopAtMs(core, host, 260, 0);
        TEST_ASSERT_TRUE(statusOf(core).faultFlags & ELRS_FAULT_ADC_STALE);
        for(int channel : {0, 1, 3}) TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(channel));
        TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(2));
        host.axesAvailable = true;
        for(int axis = 0; axis < 4; axis++) host.axes[axis] = 900;
        loopAtMs(core, host, 280, 0);
        TEST_ASSERT_FALSE(statusOf(core).faultFlags & (ELRS_FAULT_ADC_MISSING | ELRS_FAULT_ADC_STALE));
        for(int channel : {0, 1, 3}) TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(channel));
        TEST_ASSERT_EQUAL_UINT16(strength == 40 ? 746 : 377, core.channelAt(2));
        core.startSelfTest(280);
        loopAtMs(core, host, 290, 0);
        TEST_ASSERT_TRUE(statusOf(core).selfTestActive);
        for(int channel : {0, 1, 3}) TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(channel));
        TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(2));
    }
}

static void test_gimbal_curves_preserve_neutral_direction_and_deadbands()
{
    for(uint8_t strength : {40, 100}) {
        for(int axis = AXIS_AILERON; axis <= AXIS_RUDDER; axis++) {
            for(int descending = 0; descending <= 1; descending++) {
                for(int reverse = 0; reverse <= 1; reverse++) {
                    ELRSInputAxisProfile profile = {300, 900, 1500, (uint16_t)reverse, 20, strength};
                    if(descending) { profile.minimum = 1500; profile.maximum = 300; }
                    int direction = descending ? -1 : 1;
                    TEST_ASSERT_EQUAL_INT16(1500, elrsInputModelAxisToUs(profile, 900));
                    TEST_ASSERT_EQUAL_INT16(1500, elrsInputModelAxisToUs(profile, 880));
                    TEST_ASSERT_EQUAL_INT16(1500, elrsInputModelAxisToUs(profile, 920));
                    TEST_ASSERT_EQUAL_INT16(reverse ? 2000 : 1000, elrsInputModelAxisToUs(profile, profile.minimum));
                    TEST_ASSERT_EQUAL_INT16(reverse ? 1000 : 2000, elrsInputModelAxisToUs(profile, profile.maximum));
                    int16_t below = strength == 40 ? 1325 : 1437;
                    int16_t above = strength == 40 ? 1675 : 1563;
                    TEST_ASSERT_EQUAL_INT16(reverse ? above : below, elrsInputModelAxisToUs(profile, 900 - direction * 310));
                    TEST_ASSERT_EQUAL_INT16(reverse ? below : above, elrsInputModelAxisToUs(profile, 900 + direction * 310));
                    // Exercise each physical centered axis through the core, too.
                    FakeHost host;
                    ELRSCrsfCore core;
                    ELRSCrsfCoreConfig config = defaultConfig();
                    config.axisProfiles[axis] = profile;
                    host.axes[axis] = 900 - direction * 310;
                    TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
                    uint8_t channel = axis == AXIS_RUDDER ? 3 : axis;
                    TEST_ASSERT_EQUAL_UINT16(reverse ? (strength == 40 ? 1278 : 1095) : (strength == 40 ? 705 : 888), core.channelAt(channel));
                }
            }
        }
    }
}

static void test_gimbal_curve_sanitization_preserves_profiles()
{
    for(uint8_t strength : {40, 100, 101, 255}) {
        ELRSInputAxisProfile profile = {1800, 1000, 300, 1, 20, strength};
        ELRSInputAxisProfile sanitized = elrsSanitizeInputAxisProfile(profile);
        TEST_ASSERT_EQUAL_INT16(1800, sanitized.minimum);
        TEST_ASSERT_EQUAL_INT16(1000, sanitized.center);
        TEST_ASSERT_EQUAL_INT16(300, sanitized.maximum);
        TEST_ASSERT_EQUAL_UINT16(1, sanitized.reverse);
        TEST_ASSERT_EQUAL_UINT16(20, sanitized.deadband);
        TEST_ASSERT_EQUAL_UINT8(strength <= 100 ? strength : 0, sanitized.expo);
        profile.minimum = profile.center;
        sanitized = elrsSanitizeInputAxisProfile(profile);
        TEST_ASSERT_EQUAL_INT16(0, sanitized.minimum);
        TEST_ASSERT_EQUAL_INT16(1024, sanitized.center);
        TEST_ASSERT_EQUAL_INT16(2047, sanitized.maximum);
        TEST_ASSERT_EQUAL_UINT16(0, sanitized.reverse);
        TEST_ASSERT_EQUAL_UINT16(0, sanitized.deadband);
        TEST_ASSERT_EQUAL_UINT8(0, sanitized.expo);
        TEST_ASSERT_EQUAL_INT16(1500, elrsInputModelAxisToUs(profile, 1000));
        TEST_ASSERT_EQUAL_INT16(1500, elrsInputModelThrottleToUs(profile, 1000, 0));
        TEST_ASSERT_EQUAL_INT16(1500, elrsInputModelThrottleToUs(profile, 1000, 32));
    }
}

static void test_gimbal_curves_are_independent_and_follow_channel_routing()
{
    const uint8_t strengths[] = {40, 0, 100, 40};
    const uint16_t expected[] = {705, 582, 888, 428};
    for(int permuted = 0; permuted <= 1; permuted++) {
        FakeHost host;
        ELRSCrsfCore core;
        ELRSCrsfCoreConfig config = defaultConfig();
        config.throttleIdleDeadband = 0;
        const uint8_t channels[2][4] = {{0, 1, 3, 2}, {2, 3, 1, 0}};
        if(permuted) config.inputRouting = {3, 4, 1, 2};
        for(int axis = 0; axis < 4; axis++) {
            config.axisProfiles[axis] = {300, 900, 1500, 0, 0, strengths[axis]};
            host.axes[axis] = 600;
        }
        TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
        for(int axis = 0; axis < 4; axis++) TEST_ASSERT_EQUAL_UINT16(expected[axis], core.channelAt(channels[permuted][axis]));
        // Changing Elevator cannot change the other three physical outputs.
        config.axisProfiles[AXIS_ELEVATOR].expo = 100;
        TEST_ASSERT_TRUE(beginAt(core, host, config, 20));
        TEST_ASSERT_EQUAL_UINT16(888, core.channelAt(channels[permuted][AXIS_ELEVATOR]));
        for(int axis : {AXIS_AILERON, AXIS_RUDDER, AXIS_THROTTLE}) TEST_ASSERT_EQUAL_UINT16(expected[axis], core.channelAt(channels[permuted][axis]));
        // Curves shape calibrated input before travel limits scale each physical axis.
        config.axisProfiles[AXIS_ELEVATOR].expo = strengths[AXIS_ELEVATOR];
        for(auto &limits : config.outputLimits) limits = {1200, 1800};
        TEST_ASSERT_TRUE(beginAt(core, host, config, 40));
        const uint16_t limited[] = {819, 746, 929, 654};
        for(int axis = 0; axis < 4; axis++) TEST_ASSERT_EQUAL_UINT16(limited[axis], core.channelAt(channels[permuted][axis]));
    }
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

    routing.aileronChannel = 255;
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

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    loopAtMs(core, host, 10, 0);

    TEST_ASSERT_EQUAL_INT(1, (int)host.writes.size());
    host.queueFrame(host.writes[0]);
    loopAtMs(core, host, 11, 0);

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

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    loopAtMs(core, host, 10, 0);

    TEST_ASSERT_EQUAL_INT(1, (int)host.writes.size());
    host.queueFrame(host.writes[0]);
    loopAtMs(core, host, 16, 0);

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

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
    loopAtMs(core, host, 10, 0);
    TEST_ASSERT_EQUAL_UINT32(0, statusOf(core).lastReplyTimeoutAt);

    loopAtMs(core, host, 35, 0);
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

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0, 0));

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

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0, 0));

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

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x28, std::vector<uint8_t>{ 0x01, 0x02 }));

    loopAtMs(core, host, 100, 0);

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

    TEST_ASSERT_TRUE(beginAt(core50, host50, config50, 0, 0));
    TEST_ASSERT_TRUE(beginAt(core100, host100, config100, 0, 0));
    TEST_ASSERT_TRUE(beginAt(core150, host150, config150, 0, 0));
    TEST_ASSERT_TRUE(beginAt(core250, host250, config250, 0, 0));
    TEST_ASSERT_TRUE(beginAt(core500, host500, config500, 0, 0));

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

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    core.startSelfTest(0);
    loopAtMs(core, host, 10, 0);

    channels[0] = 992;
    channels[1] = 992;
    channels[2] = 992;
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

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));

    ELRSCrsfStatus status = statusOf(core);
    TEST_ASSERT_TRUE(status.faultFlags & ELRS_FAULT_ADC_MISSING);
    TEST_ASSERT_FALSE(status.faultFlags & ELRS_FAULT_ADC_STALE);
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(0));
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(1));
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(2));
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

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    loopAtMs(core, host, 20, 0);

    TEST_ASSERT_FALSE(statusOf(core).faultFlags & ELRS_FAULT_ADC_STALE);

    host.axesAvailable = false;
    loopAtMs(core, host, 150, 0);

    ELRSCrsfStatus status = statusOf(core);
    TEST_ASSERT_TRUE(status.faultFlags & ELRS_FAULT_ADC_STALE);
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(0));
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(1));
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(2));
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(3));
}

static void test_adc_fault_and_self_test_keep_remapped_throttle_neutral()
{
    for(int scenario = 0; scenario < 3; scenario++) {
        FakeHost host;
        ELRSCrsfCore core;
        ELRSCrsfCoreConfig config = defaultConfig();
        config.inputRouting = elrsDefaultGimbalRouting();
        config.inputRouting.elevatorChannel = 3;
        config.inputRouting.throttleChannel = 2;
        host.axes[AXIS_THROTTLE] = 0;
        host.axesAvailable = scenario != 0;
        TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
        if(scenario) {
            TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(1));
            if(scenario == 1) host.axesAvailable = false;
            else core.startSelfTest(0);
            loopAtMs(core, host, 150, 0);
        }
        TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(1));
        TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(2));
    }
}

static void test_button_pack_stale_holds_last_valid_states()
{
    FakeHost host;
    ELRSCrsfCore core;

    host.packStates = 0b10101010;
    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    loopAtMs(core, host, 20, 0);

    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(9));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(8));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(11));

    host.packAvailable = false;
    loopAtMs(core, host, 160, 0);

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
    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    loopAtMs(core, host, 20, 0);

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
    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    TEST_ASSERT_TRUE(statusOf(core).faultFlags & ELRS_FAULT_ADC_MISSING);

    host.axesAvailable = true;
    host.axes[AXIS_AILERON] = 1200;
    host.axes[AXIS_ELEVATOR] = 1300;
    host.axes[AXIS_THROTTLE] = 1400;
    host.axes[AXIS_RUDDER] = 1500;
    loopAtMs(core, host, 30, 0);
    TEST_ASSERT_FALSE(statusOf(core).faultFlags & ELRS_FAULT_ADC_MISSING);

    host.packStates = 0b00001111;
    loopAtMs(core, host, 40, 0);
    TEST_ASSERT_FALSE(statusOf(core).faultFlags & ELRS_FAULT_BUTTONPACK_STALE);

    host.packAvailable = false;
    loopAtMs(core, host, 160, 0);
    TEST_ASSERT_TRUE(statusOf(core).faultFlags & ELRS_FAULT_BUTTONPACK_STALE);

    host.packAvailable = true;
    host.packStates = 0b11110000;
    loopAtMs(core, host, 170, 0);
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

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
    loopAtMs(core, host, 10, 0);

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
    loopAtMs(core, host, 20, 0);
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

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
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

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
    loopAt(core, host, 20, 20000);

    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(0));
}

static void test_gimbal_routing_collision_uses_defaults()
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

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
    loopAt(core, host, 20, 20000);

    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(0));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(1));
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(2));
    TEST_ASSERT_EQUAL_UINT16(582, core.channelAt(3));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(4));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(5));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(6));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(7));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(8));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(9));
}

static void test_telemetry_parsing_and_bad_crc_rejection()
{
    FakeHost host;
    ELRSCrsfCore core;
    std::vector<uint8_t> badLink;

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));

    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 88, 0, 0, 0, 0, 0, 0, 0 }));
    host.queueFrame(makeFrame(0x08, std::vector<uint8_t>{ 0, 126, 0, 0, 0, 0, 0, 77 }));
    host.queueFrame(makeFrame(0x02, std::vector<uint8_t>{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 126, 0, 0, 0, 0, 0 }));
    host.queueFrame(makeFrame(0x0A, std::vector<uint8_t>{ 0x00, 0x4D }));

    loopAtMs(core, host, 100, 0);

    TEST_ASSERT_TRUE(core.synced());
    TEST_ASSERT_TRUE(core.telemetryActive());
    TEST_ASSERT_EQUAL_UINT8(88, core.linkQuality());
    TEST_ASSERT_EQUAL_UINT8(77, core.remoteBatteryPercent());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 12.6f, core.remoteBatteryVoltage());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 12.6f, core.getStatus().remoteBatteryVoltage);
    TEST_ASSERT_EQUAL_UINT16(126, core.gpsSpeed10());
    TEST_ASSERT_EQUAL_UINT16(77, core.airspeed10());

    badLink = makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 5, 0, 0, 0, 0, 0, 0, 0 });
    badLink[badLink.size() - 1] ^= 0xFF;
    host.queueFrame(badLink);
    loopAtMs(core, host, 200, 0);

    TEST_ASSERT_EQUAL_UINT8(88, core.linkQuality());
}

static void test_non_c8_sync_frame_is_accepted()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    host.queueFrame(makeFrameWithSync(0x00, 0x14, std::vector<uint8_t>{ 0, 0, 68, 0, 0, 0, 0, 0, 0, 0 }));

    loopAtMs(core, host, 100, 0);

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

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    queueBytes(host, makeGarbage());
    queueBytes(host, makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 67, 0, 0, 0, 0, 0, 0, 0 }));

    loopAtMs(core, host, 100, 0);

    TEST_ASSERT_TRUE(core.synced());
    TEST_ASSERT_EQUAL_UINT8(67, core.linkQuality());
}

static void test_parser_recovers_after_bad_crc_followed_by_valid_frame()
{
    FakeHost host;
    ELRSCrsfCore core;

    std::vector<uint8_t> bad = makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 12, 0, 0, 0, 0, 0, 0, 0 });
    bad.back() ^= 0xFF;

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    queueBytes(host, bad);
    queueBytes(host, makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 91, 0, 0, 0, 0, 0, 0, 0 }));

    loopAtMs(core, host, 100, 0);

    TEST_ASSERT_TRUE(core.synced());
    TEST_ASSERT_EQUAL_UINT8(91, core.linkQuality());
}

static void test_comm_codes_show_no_sync_until_valid_frame()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));

    loopAtMs(core, host, 2000, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NRY, statusOf(core).commCode);
    TEST_ASSERT_FALSE(statusOf(core).everSynced);
    TEST_ASSERT_FALSE(statusOf(core).replyActive);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("NRY", host.displayText.c_str());
    TEST_ASSERT_EQUAL_INT(1, (int)host.writes.size());

    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 73, 0, 0, 0, 0, 0, 0, 0 }));
    loopAtMs(core, host, 2100, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);
    TEST_ASSERT_TRUE(statusOf(core).everSynced);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("NRY", host.displayText.c_str());

    loopAtMs(core, host, 3800, 0);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("73", host.displayText.c_str());
}

static void test_lost_telemetry_sets_los_until_valid_frame()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 44, 0, 0, 0, 0, 0, 0, 0 }));
    loopAtMs(core, host, 100, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);
    TEST_ASSERT_TRUE(statusOf(core).everSynced);

    loopAtMs(core, host, 2100, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_RLS, statusOf(core).commCode);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("RLS", host.displayText.c_str());
    TEST_ASSERT_EQUAL_INT(2, (int)host.writes.size());

    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 45, 0, 0, 0, 0, 0, 0, 0 }));
    loopAtMs(core, host, 2200, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);
    TEST_ASSERT_TRUE(statusOf(core).everSynced);
}

static void test_crc_burst_sets_crc_comm_code()
{
    FakeHost host;
    ELRSCrsfCore core;
    std::vector<uint8_t> bad = makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 12, 0, 0, 0, 0, 0, 0, 0 });

    bad.back() ^= 0xFF;

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 55, 0, 0, 0, 0, 0, 0, 0 }));
    loopAtMs(core, host, 100, 0);

    host.queueFrame(bad);
    loopAtMs(core, host, 1200, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);

    host.queueFrame(bad);
    loopAtMs(core, host, 1300, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);

    host.queueFrame(bad);
    loopAtMs(core, host, 1400, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_CRC, statusOf(core).commCode);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("CRC", host.displayText.c_str());

    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 56, 0, 0, 0, 0, 0, 0, 0 }));
    loopAtMs(core, host, 1500, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);
}

static void test_frame_burst_sets_frm_comm_code()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 61, 0, 0, 0, 0, 0, 0, 0 }));
    loopAtMs(core, host, 100, 0);

    queueBytes(host, std::vector<uint8_t>{ 0xC8, 0x01 });
    loopAtMs(core, host, 1200, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);

    queueBytes(host, std::vector<uint8_t>{ 0xC8, 0x01 });
    loopAtMs(core, host, 1300, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);

    queueBytes(host, std::vector<uint8_t>{ 0xC8, 0x01 });
    loopAtMs(core, host, 1400, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_FRM, statusOf(core).commCode);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("FRM", host.displayText.c_str());

    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 62, 0, 0, 0, 0, 0, 0, 0 }));
    loopAtMs(core, host, 1500, 0);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_NONE, statusOf(core).commCode);
}

static void test_display_policy_prefers_gps_then_airspeed_then_link_quality()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));

    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 88, 0, 0, 0, 0, 0, 0, 0 }));
    host.queueFrame(makeFrame(0x02, std::vector<uint8_t>{ 0, 0, 0, 0, 0, 0, 0, 0, 0x04, 0xCE, 0, 0, 0, 0, 0 }));
    loopAtMs(core, host, 100, 0);

    host.queueFrame(makeFrame(0x0A, std::vector<uint8_t>{ 0x00, 0x4D }));
    loopAtMs(core, host, 1500, 0);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("HI", host.displayText.c_str());
    TEST_ASSERT_EQUAL(ELRSCrsfCore::SPEED_SOURCE_GPS, core.activeSpeedSource());

    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 88, 0, 0, 0, 0, 0, 0, 0 }));
    loopAtMs(core, host, 2301, 0);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("7.7", host.displayText.c_str());
    TEST_ASSERT_EQUAL(ELRSCrsfCore::SPEED_SOURCE_AIRSPEED, core.activeSpeedSource());

    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 88, 0, 0, 0, 0, 0, 0, 0 }));
    loopAtMs(core, host, 3600, 0);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("88", host.displayText.c_str());
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

    TEST_ASSERT_TRUE(beginAt(coreKmh, hostKmh, configKmh, 0));
    TEST_ASSERT_TRUE(beginAt(coreMph, hostMph, configMph, 0));

    hostKmh.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 88, 0, 0, 0, 0, 0, 0, 0 }));
    hostKmh.queueFrame(makeFrame(0x02, std::vector<uint8_t>{ 0, 0, 0, 0, 0, 0, 0, 0, 0x04, 0xCE, 0, 0, 0, 0, 0 }));
    hostMph.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 88, 0, 0, 0, 0, 0, 0, 0 }));
    hostMph.queueFrame(makeFrame(0x02, std::vector<uint8_t>{ 0, 0, 0, 0, 0, 0, 0, 0, 0x04, 0xCE, 0, 0, 0, 0, 0 }));

    loopAtMs(coreKmh, hostKmh, 100, 0);
    loopAtMs(coreMph, hostMph, 100, 0);
    hostKmh.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 88, 0, 0, 0, 0, 0, 0, 0 }));
    hostMph.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 88, 0, 0, 0, 0, 0, 0, 0 }));
    loopAtMs(coreKmh, hostKmh, 1500, 0);
    loopAtMs(coreMph, hostMph, 1500, 0);

    TEST_ASSERT_EQUAL(DISPLAY_TEXT, hostKmh.displayMode);
    TEST_ASSERT_EQUAL_STRING("HI", hostKmh.displayText.c_str());
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, hostMph.displayMode);
    TEST_ASSERT_EQUAL_STRING("76.4", hostMph.displayText.c_str());
}

static void test_battery_overlay_beats_comm_overlay()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 42, 0, 0, 0, 0, 0, 0, 0 }));
    loopAtMs(core, host, 100, 0);

    loopAtMs(core, host, 30000, 1);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_RLS, statusOf(core).commCode);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("BAT", host.displayText.c_str());
}

static void test_calibration_prompt_beats_comm_overlay()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 42, 0, 0, 0, 0, 0, 0, 0 }));
    loopAtMs(core, host, 100, 0);

    host.calibrationButton = true;
    loopAtMs(core, host, 200, 0);
    loopAtMs(core, host, 300, 0);
    loopAtMs(core, host, 2301, 0);

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

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 52, 0, 0, 0, 0, 0, 0, 0 }));
    loopAtMs(core, host, 100, 0);

    host.axesAvailable = false;
    host.queueFrame(bad);
    loopAtMs(core, host, 250, 0);
    host.queueFrame(bad);
    loopAtMs(core, host, 350, 0);
    host.queueFrame(bad);
    loopAtMs(core, host, 450, 0);

    TEST_ASSERT_TRUE(statusOf(core).faultFlags & ELRS_FAULT_ADC_STALE);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_CRC, statusOf(core).commCode);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("ADC", host.displayText.c_str());
}

static void test_button_pack_overlay_beats_comm_overlay()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 57, 0, 0, 0, 0, 0, 0, 0 }));
    loopAtMs(core, host, 100, 0);
    loopAtMs(core, host, 120, 0);

    host.packAvailable = false;
    queueBytes(host, std::vector<uint8_t>{ 0xC8, 0x01 });
    loopAtMs(core, host, 1300, 0);
    queueBytes(host, std::vector<uint8_t>{ 0xC8, 0x01 });
    loopAtMs(core, host, 1400, 0);
    queueBytes(host, std::vector<uint8_t>{ 0xC8, 0x01 });
    loopAtMs(core, host, 1500, 0);

    TEST_ASSERT_TRUE(statusOf(core).faultFlags & ELRS_FAULT_BUTTONPACK_STALE);
    TEST_ASSERT_EQUAL_UINT8(ELRS_COMM_FRM, statusOf(core).commCode);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("BPK", host.displayText.c_str());
}

static void test_battery_overlay_and_calibration_prompt_still_override_normal_display()
{
    FakeHost host;
    ELRSCrsfCore core;

    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{ 0, 0, 42, 0, 0, 0, 0, 0, 0, 0 }));
    loopAtMs(core, host, 100, 0);
    loopAtMs(core, host, 1200, 0);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("42", host.displayText.c_str());

    loopAtMs(core, host, 60000, 1);
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("BAT", host.displayText.c_str());

    host.calibrationButton = true;
    loopAtMs(core, host, 61100, 0);
    loopAtMs(core, host, 61200, 0);
    loopAtMs(core, host, 63301, 0);
    TEST_ASSERT_TRUE(core.isCalibrating());
    TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
    TEST_ASSERT_EQUAL_STRING("CEN", host.displayText.c_str());
}

static void test_expired_overlays_do_not_return_after_millis_rollover()
{
    // Run both the ADC fault overlay and the communication-only overlay paths.
    for(int adcFault = 0; adcFault < 2; adcFault++) {
        FakeHost host;
        ELRSCrsfCore core;
        TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0, 0));
        host.axesAvailable = !adcFault;
        loopAt(core, host, 3600000UL, 1000, 0);
        TEST_ASSERT_EQUAL_STRING(adcFault ? "ADC" : "NRY", host.displayText.c_str());

        host.axesAvailable = true;
        const unsigned long times[] = {3600200UL, 3602000UL, 0xffffff00UL, 100UL};
        for(unsigned long now : times) {
            host.queueFrame(makeFrame(0x14, {0, 0, 88, 0, 0, 0, 0, 0, 0, 0}));
            loopAt(core, host, now, 2000, 0);
            if(now != 3600200UL) TEST_ASSERT_EQUAL_STRING("88", host.displayText.c_str());
        }
        TEST_ASSERT_EQUAL_UINT8(ELRS_FAULT_NONE, core.getStatus().faultFlags);
    }
}

static void test_adc_overlay_expires_across_millis_rollover()
{
    // The second start makes the expiry deadline exactly zero after rollover.
    const unsigned long starts[] = {0xfffffe00UL, 0xfffffc18UL};
    for(unsigned long start : starts) {
        FakeHost host;
        ELRSCrsfCore core;
        TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), start - 2000, 0));
        host.queueFrame(makeFrame(0x14, {0, 0, 88, 0, 0, 0, 0, 0, 0, 0}));
        loopAt(core, host, start - 300, 1000, 0);
        TEST_ASSERT_EQUAL_STRING("88", host.displayText.c_str());

        host.axesAvailable = false;
        host.queueFrame(makeFrame(0x14, {0, 0, 88, 0, 0, 0, 0, 0, 0, 0}));
        loopAt(core, host, start, 2000, 0);
        TEST_ASSERT_EQUAL_STRING("ADC", host.displayText.c_str());
        host.axesAvailable = true;
        const unsigned long offsets[] = {200, 700, 1000, 1200};
        for(unsigned long offset : offsets) {
            host.queueFrame(makeFrame(0x14, {0, 0, 88, 0, 0, 0, 0, 0, 0, 0}));
            const uint32_t now = (uint32_t)(start + offset);
            loopAt(core, host, now, 2000 + offset, 0);
            TEST_ASSERT_EQUAL_STRING(offset < 1000 ? "ADC" : "88", host.displayText.c_str());
        }
        TEST_ASSERT_EQUAL_UINT8(ELRS_FAULT_NONE, core.getStatus().faultFlags);
    }
}

static void test_comm_overlay_expires_across_millis_rollover()
{
    FakeHost host;
    ELRSCrsfCore core;
    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0xfffff000UL, 0));
    loopAt(core, host, 0xfffffe00UL, 1000, 0);
    TEST_ASSERT_EQUAL_STRING("NRY", host.displayText.c_str());
    const unsigned long times[] = {0xffffff00UL, 100UL, 1000UL};
    for(unsigned long now : times) {
        host.queueFrame(makeFrame(0x14, {0, 0, 88, 0, 0, 0, 0, 0, 0, 0}));
        loopAt(core, host, now, 2000, 0);
        TEST_ASSERT_EQUAL_STRING(now == 1000UL ? "88" : "NRY", host.displayText.c_str());
    }
}

static void test_telemetry_received_at_millis_zero_is_fresh()
{
    for(int source = 0; source < 5; source++) {
        FakeHost host;
        ELRSCrsfCore core;
        TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0xffffff00UL, 0));
        if(source == 0) host.queueFrame(makeFrame(0x14, {0, 0, 88, 0, 0, 0, 0, 0, 0, 0}));
        if(source == 1 || source == 3) host.queueFrame(makeFrame(0x02, {0, 0, 0, 0, 0, 0, 0, 0, 0, (uint8_t)(source == 1 ? 126 : 0), 0, 0, 0, 0, 0}));
        if(source == 2 || source == 4) host.queueFrame(makeFrame(0x0A, {0, (uint8_t)(source == 2 ? 77 : 0)}));
        loopAt(core, host, 0, 1000, 0);
        loopAt(core, host, 1000, 2000, 0);
        if(source == 0) {
            TEST_ASSERT_EQUAL_STRING("88", host.displayText.c_str());
        } else {
            TEST_ASSERT_EQUAL(DISPLAY_TEXT, host.displayMode);
            TEST_ASSERT_EQUAL_STRING(source == 1 ? "12.6" : (source == 2 ? "7.7" : "0.0"), host.displayText.c_str());
        }
        loopAt(core, host, 2501, 3000, 0);
        TEST_ASSERT_EQUAL(ELRSCrsfCore::SPEED_SOURCE_NONE, core.activeSpeedSource());
        loopAt(core, host, 0xffffff00UL, 4000, 0);
        loopAt(core, host, 1000, 5000, 0);
        TEST_ASSERT_EQUAL(ELRSCrsfCore::SPEED_SOURCE_NONE, core.activeSpeedSource());
    }
}

static void test_self_test_expires_across_millis_rollover()
{
    const unsigned long starts[] = {0xfffffe00UL, 0xfffffc18UL};
    for(unsigned long start : starts) {
        FakeHost host;
        ELRSCrsfCore core;
        TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), start - 2000, 0));
        core.startSelfTest(start, 1000);
        for(unsigned long offset : {0UL, 200UL, 999UL, 1000UL}) {
            loopAt(core, host, (uint32_t)(start + offset), 1000 + offset, 0);
            TEST_ASSERT_EQUAL(offset < 1000, core.getStatus().selfTestActive);
        }
    }
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

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0, 0));
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

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0, 0));
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

static void test_unanswered_module_probes_stop_until_reconnect_or_save()
{
    FakeHost host;
    ELRSCrsfCore core;
    beginAt(core, host, defaultConfig(), 0, 0);
    for(unsigned long now = 0; now <= 60000; now += 20) loopAt(core, host, now, now * 1000UL);
    TEST_ASSERT_EQUAL_INT(3, countWrittenFrameType(host, 0x28));
    TEST_ASSERT_GREATER_THAN_INT(2900, countWrittenFrameType(host, 0x16));
    host.queueFrame(makeFrame(0x3A, std::vector<uint8_t>{0xEA, 0xEE}));
    loopAt(core, host, 61000, 61000000);
    loopAt(core, host, 61200, 61200000);
    loopAt(core, host, 61220, 61220000);
    TEST_ASSERT_EQUAL_INT(4, countWrittenFrameType(host, 0x28));
    host.queueFrame(makeDeviceInfoFrame("ELRS", 0));
    loopAt(core, host, 61240, 61240000);
    loopAt(core, host, 64000, 64000000); // Completed session must still notice link loss.
    host.queueFrame(makeFrame(0x3A, std::vector<uint8_t>{0xEA, 0xEE}));
    loopAt(core, host, 65000, 65000000);
    loopAt(core, host, 65200, 65200000);
    loopAt(core, host, 65220, 65220000);
    TEST_ASSERT_EQUAL_INT(5, countWrittenFrameType(host, 0x28));
    core.requestModuleConfigUpdate(0, 0, 0, 66000);
    loopAt(core, host, 67000, 67000000);
    loopAt(core, host, 67020, 67020000);
    TEST_ASSERT_EQUAL_INT(6, countWrittenFrameType(host, 0x28));
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

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0, 0));
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

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0, 0));
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

static void moduleTimerLoopAt(ELRSCrsfCore &core, FakeHost &host, uint32_t start, uint32_t elapsed)
{
    loopAt(core, host, (uint32_t)(start + elapsed), elapsed * 1000UL);
}

static void test_module_start_and_save_delay_survive_millis_rollover()
{
    for(uint32_t start : {(uint32_t)0xffffff00UL, (uint32_t)(0UL - 1000UL)}) {
        for(bool save : {false, true}) {
            FakeHost host;
            ELRSCrsfCore core;
            const ELRSCrsfCoreConfig config = defaultConfig();
            beginAt(core, host, config, save ? 0 : start, 0);
            if(save) core.requestModuleConfigUpdate(config.telemetryRatio, config.maxPower, config.dynamicPower, start);
            moduleTimerLoopAt(core, host, start, 20);
            moduleTimerLoopAt(core, host, start, 980);
            TEST_ASSERT_EQUAL_INT(0, countWrittenFrameType(host, 0x28));
            TEST_ASSERT_FALSE(logsContain(host, "probing module settings"));
            moduleTimerLoopAt(core, host, start, 1000);
            TEST_ASSERT_TRUE(logsContain(host, "probing module settings"));
            moduleTimerLoopAt(core, host, start, 1020);
            TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x28));
        }
    }
}

static void test_module_probe_zero_deadline_still_retries_after_500ms()
{
    FakeHost host;
    ELRSCrsfCore core;
    const uint32_t start = 0UL - 1500UL;
    beginAt(core, host, defaultConfig(), start, 0);
    moduleTimerLoopAt(core, host, start, 1000); // Probe deadline is exactly zero.
    moduleTimerLoopAt(core, host, start, 1020);
    moduleTimerLoopAt(core, host, start, 1499);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x28));
    moduleTimerLoopAt(core, host, start, 1500);
    moduleTimerLoopAt(core, host, start, 2480);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x28));
    moduleTimerLoopAt(core, host, start, 2500);
    moduleTimerLoopAt(core, host, start, 2520);
    TEST_ASSERT_EQUAL_INT(2, countWrittenFrameType(host, 0x28));
    TEST_ASSERT_FALSE(logsContain(host, "module settings probe timed out"));
}

static void test_module_parameter_zero_deadline_still_retries_after_500ms()
{
    FakeHost host;
    ELRSCrsfCore core;
    const uint32_t start = 0UL - 1540UL;
    beginAt(core, host, defaultConfig(), start, 0);
    moduleTimerLoopAt(core, host, start, 1000);
    moduleTimerLoopAt(core, host, start, 1020);
    host.queueFrame(makeDeviceInfoFrame("ELRS", 1));
    moduleTimerLoopAt(core, host, start, 1040); // Parameter deadline is exactly zero.
    moduleTimerLoopAt(core, host, start, 1120);
    moduleTimerLoopAt(core, host, start, 1539);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x2C));
    moduleTimerLoopAt(core, host, start, 1540);
    moduleTimerLoopAt(core, host, start, 1560);
    TEST_ASSERT_EQUAL_INT(2, countWrittenFrameType(host, 0x2C));
    const std::vector<uint8_t> *retry = findWrittenFrameType(host, 0x2C, 1);
    TEST_ASSERT_EQUAL_UINT8(1, (*retry)[5]);
    TEST_ASSERT_EQUAL_UINT8(0, (*retry)[6]);
    TEST_ASSERT_FALSE(logsContain(host, "parameter scan timed out"));
}

static void test_module_write_delay_survives_zero_deadline()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    config.telemetryRatio = ELRS_TLM_RATIO_1_4;
    const uint32_t start = 0UL - 1600UL;
    beginAt(core, host, config, start, 0);
    moduleTimerLoopAt(core, host, start, 1000);
    moduleTimerLoopAt(core, host, start, 1020);
    host.queueFrame(makeDeviceInfoFrame("ELRS", 1));
    moduleTimerLoopAt(core, host, start, 1040);
    moduleTimerLoopAt(core, host, start, 1120);
    host.queueFrame(makeTextSelectionEntryFrame(1, "Telem Ratio", "Std;1:2;1:4;1:8;Off", 0, 4));
    moduleTimerLoopAt(core, host, start, 1300); // Write's 300ms delay ends at zero.
    for(uint32_t elapsed = 1320; elapsed < 1600; elapsed += 20) moduleTimerLoopAt(core, host, start, elapsed);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x2D));
    TEST_ASSERT_FALSE(logsContain(host, "module settings apply complete"));
    for(uint32_t elapsed = 1600; elapsed <= 1660; elapsed += 20) moduleTimerLoopAt(core, host, start, elapsed);
    TEST_ASSERT_TRUE(logsContain(host, "module settings apply complete"));
}

static void test_module_settings_ignore_duplicate_chunk_after_retry()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    config.transport.packetRateHz = 50;
    config.telemetryRatio = ELRS_TLM_RATIO_1_4;
    beginAt(core, host, config, 0, 0);
    loopAt(core, host, 0, 0);
    loopAt(core, host, 1000, 1000000);
    loopAt(core, host, 1020, 1020000);
    host.queueFrame(makeDeviceInfoFrame("ELRS", 1));
    loopAt(core, host, 1030, 1030000);
    loopAt(core, host, 1120, 1120000);
    loopAt(core, host, 1530, 1530000);
    loopAt(core, host, 1540, 1540000);
    loopAt(core, host, 1550, 1550000); // Recovery leaves one full 50Hz slot after 1530.
    TEST_ASSERT_EQUAL_INT(2, countWrittenFrameType(host, 0x2C));
    TEST_ASSERT_EQUAL_UINT8(0, (*findWrittenFrameType(host, 0x2C, 1))[6]);

    const std::vector<uint8_t> data = makeTextSelectionEntryData("Telem Ratio", "Std;1:2;1:4;1:8;Off", 0, 4);
    const std::vector<uint8_t> first(data.begin(), data.begin() + data.size() / 2);
    const std::vector<uint8_t> last(data.begin() + data.size() / 2, data.end());
    host.queueFrame(makeParameterChunkFrame(1, 1, first));
    loopAt(core, host, 1550, 1550000);
    host.queueFrame(makeParameterChunkFrame(1, 1, first));
    loopAt(core, host, 1560, 1560000);
    loopAt(core, host, 1640, 1640000);
    loopAt(core, host, 1660, 1660000); // First slot after the 100ms service gap. // Service spacing is measured from the actual retry slot.
    TEST_ASSERT_EQUAL_INT(3, countWrittenFrameType(host, 0x2C));
    TEST_ASSERT_EQUAL_UINT8(1, (*findWrittenFrameType(host, 0x2C, 2))[6]);
    host.queueFrame(makeParameterChunkFrame(1, 0, last));
    for(unsigned long now = 1660; now <= 2400; now += 20) loopAt(core, host, now, now * 1000UL);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x2D));
    TEST_ASSERT_EQUAL_UINT8(1, (*findWrittenFrameType(host, 0x2D, 0))[5]);
    TEST_ASSERT_EQUAL_UINT8(2, (*findWrittenFrameType(host, 0x2D, 0))[6]);
    TEST_ASSERT_TRUE(logsContain(host, "module settings apply complete"));
}

static void test_module_settings_preserve_chunks_when_retry_read_is_still_queued()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    config.transport.packetRateHz = 50;
    config.telemetryRatio = ELRS_TLM_RATIO_1_4;
    beginAt(core, host, config, 0, 0);
    loopAt(core, host, 0, 0);
    loopAt(core, host, 1000, 1000000);
    loopAt(core, host, 1020, 1020000);
    host.queueFrame(makeDeviceInfoFrame("ELRS", 1));
    loopAt(core, host, 1030, 1030000);
    loopAt(core, host, 1120, 1120000);
    loopAt(core, host, 1530, 1530000); // Retry chunk zero is queued, but not sent yet.
    const std::vector<uint8_t> data = makeTextSelectionEntryData("Telem Ratio", "Std;1:2;1:4;1:8;Off", 0, 4);
    const std::vector<uint8_t> first(data.begin(), data.begin() + 18);
    const std::vector<uint8_t> middle(data.begin() + 18, data.begin() + 30);
    const std::vector<uint8_t> last(data.begin() + 30, data.end());
    host.queueFrame(makeParameterChunkFrame(1, 2, first));
    loopAt(core, host, 1531, 1531000);
    host.queueFrame(makeParameterChunkFrame(1, 2, first));
    loopAt(core, host, 1532, 1532000);
    loopAt(core, host, 1540, 1540000);
    loopAt(core, host, 1550, 1550000);
    host.queueFrame(makeParameterChunkFrame(1, 2, first));
    loopAt(core, host, 1550, 1550000);
    loopAt(core, host, 1640, 1640000);
    loopAt(core, host, 1660, 1660000); // First slot after the 100ms service gap.
    TEST_ASSERT_EQUAL_INT(3, countWrittenFrameType(host, 0x2C));
    TEST_ASSERT_EQUAL_UINT8(1, (*findWrittenFrameType(host, 0x2C, 2))[6]);

    // Skipping a chunk, an increased remaining count, and another field cannot alter progress.
    host.queueFrame(makeParameterChunkFrame(1, 0, last));
    host.queueFrame(makeParameterChunkFrame(1, 3, first));
    host.queueFrame(makeParameterChunkFrame(2, 1, middle));
    loopAt(core, host, 1660, 1660000);
    for(unsigned long now = 1680; now <= 2020; now += 20) {
        const size_t before = host.writes.size();
        loopAt(core, host, now, now * 1000UL);
        TEST_ASSERT_EQUAL_INT(before + 1, host.writes.size());
        TEST_ASSERT_EQUAL_HEX8(0x16, host.writes.back()[2]);
    }
    loopAt(core, host, 2040, 2040000);
    loopAt(core, host, 2060, 2060000);
    loopAt(core, host, 2080, 2080000); // Retry queued at 2060 uses the next scheduled slot.
    TEST_ASSERT_EQUAL_INT(4, countWrittenFrameType(host, 0x2C));
    TEST_ASSERT_EQUAL_UINT8(1, (*findWrittenFrameType(host, 0x2C, 3))[6]);
    host.queueFrame(makeParameterChunkFrame(1, 1, middle));
    loopAt(core, host, 2090, 2090000);
    host.queueFrame(makeParameterChunkFrame(1, 2, first));
    host.queueFrame(makeParameterChunkFrame(1, 1, middle));
    loopAt(core, host, 2100, 2100000);
    loopAt(core, host, 2180, 2180000);
    TEST_ASSERT_EQUAL_INT(5, countWrittenFrameType(host, 0x2C));
    TEST_ASSERT_EQUAL_UINT8(2, (*findWrittenFrameType(host, 0x2C, 4))[6]);
    host.queueFrame(makeParameterChunkFrame(1, 0, last));
    for(unsigned long now = 2200; now <= 2820; now += 20) loopAt(core, host, now, now * 1000UL);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x2D));
    TEST_ASSERT_EQUAL_UINT8(2, (*findWrittenFrameType(host, 0x2D, 0))[6]);
    TEST_ASSERT_TRUE(logsContain(host, "module settings apply complete"));
    TEST_ASSERT_FALSE(logsContain(host, "parameter scan timed out"));
}

static void test_module_settings_retry_probe_before_long_backoff()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    config.transport.packetRateHz = 50;

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0, 0));
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

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0, 0));
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

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0, 0));

    loopAt(core, host, 0, 0);
    loopAt(core, host, 1000, 1000000);
    loopAt(core, host, 1002, 1002000);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x28));

    loopAt(core, host, 1055, 1055000);
    TEST_ASSERT_EQUAL_UINT32(0, statusOf(core).lastReplyTimeoutAt);
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, countWrittenFrameType(host, 0x16), writtenFrameTypes(host).c_str());

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

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0, 0));
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

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0, 0));
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

static void test_module_ping_keeps_rc_running_while_waiting_for_settings()
{
    for(uint16_t rate : {50, 250, 500}) {
        FakeHost host;
        ELRSCrsfCore core;
        ELRSCrsfCoreConfig config = defaultConfig();
        config.transport.packetRateHz = rate;
        TEST_ASSERT_TRUE(beginAt(core, host, config, 0, 0));
        const unsigned long interval = 1000 / rate;
        loopAt(core, host, 1000, 1000000);
        loopAt(core, host, 1000 + interval, (1000 + interval) * 1000UL);
        TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x28));
        int rcCount = countWrittenFrameType(host, 0x16);
        for(unsigned long now = 1000 + 2 * interval; now < 1250; now += interval) {
            host.axes[AXIS_THROTTLE] = 1500;
            loopAt(core, host, now, now * 1000UL);
            TEST_ASSERT_EQUAL_INT(++rcCount, countWrittenFrameType(host, 0x16));
            TEST_ASSERT_FALSE(host.driverEnabled);
        }
        TEST_ASSERT_NOT_EQUAL(992, core.channelAt(2));
    }
}

static void test_service_probe_drains_synchronous_loopback_echo_from_uart_buffer()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();

    config.transport.packetRateHz = 500;
    config.transport.replyTimeoutMs = 50;

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0, 0));

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

    TEST_ASSERT_TRUE(beginAt(core, host, config, 0, 0));

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

static void calibrationPress(ELRSCrsfCore &core, FakeHost &host, unsigned long &now, bool longPress = false)
{
    host.calibrationButton = true;
    now += 100;
    loopAt(core, host, now, now * 1000UL);
    now += 60;
    loopAt(core, host, now, now * 1000UL);
    if(longPress) {
        now += 2000;
        loopAt(core, host, now, now * 1000UL);
    }
    host.calibrationButton = false;
    now += 100;
    loopAt(core, host, now, now * 1000UL);
    now += 60;
    loopAt(core, host, now, now * 1000UL);
}

static void test_pending_calibration_capture_is_fresh_and_advances_once()
{
    FakeHost host;
    ELRSCrsfCore core;
    beginAt(core, host, defaultConfig(), 0);
    unsigned long now = 0;
    calibrationPress(core, host, now, true);
    host.costedIo = true;
    host.axesPending = true;
    calibrationPress(core, host, now);
    const size_t before = host.writes.size();
    for(int i = 0; i < 10; i++) { now += 2; loopAt(core, host, now, now * 1000); }
    TEST_ASSERT_GREATER_THAN_UINT32(before + 2, host.writes.size());
    for(int i = 0; i < 4; i++) host.axes[i] = 1200;
    host.axesPending = false;
    now += 2; loopAt(core, host, now, now * 1000);
    now += 1200; loopAt(core, host, now, now * 1000);
    TEST_ASSERT_EQUAL_STRING("TLO", host.displayText.c_str());
    now += 200; loopAt(core, host, now, now * 1000);
    TEST_ASSERT_EQUAL_STRING("TLO", host.displayText.c_str());
    TEST_ASSERT_EQUAL_INT(0, host.savedCalibrationCount);
}

static void test_pending_calibration_capture_error_timeout_and_cancellation()
{
    for(int cancel = 0; cancel < 5; cancel++) {
        FakeHost host;
        ELRSCrsfCore core;
        beginAt(core, host, defaultConfig(), 0);
        unsigned long now = 0;
        calibrationPress(core, host, now, true);
        host.axesPending = true;
        calibrationPress(core, host, now);
        if(cancel == 0) host.fakePower = true;
        if(cancel == 1) core.startSelfTest(now);
        if(cancel == 2) { host.axesAvailable = false; host.axesPending = false; }
        if(cancel == 3) { now += 101; loopAt(core, host, now, now * 1000); }
        if(cancel == 4) calibrationPress(core, host, now, true); // Leave calibration.
        now += 2; loopAt(core, host, now, now * 1000);
        host.fakePower = false; core.stopSelfTest(); host.axesAvailable = true; host.axesPending = false;
        now += 2; loopAt(core, host, now, now * 1000);
        now += 1200; loopAt(core, host, now, now * 1000);
        TEST_ASSERT_EQUAL_INT(0, host.savedCalibrationCount);
        if(cancel == 4) TEST_ASSERT_FALSE(core.isCalibrating());
        else TEST_ASSERT_EQUAL_STRING("CEN", host.displayText.c_str());
    }
}

static void test_invalid_button_calibration_preserves_minimum_throttle()
{
    FakeHost host;
    ELRSCrsfCore core;
    host.axes[AXIS_THROTTLE] = 0;
    beginAt(core, host, defaultConfig(), 0, 0);
    unsigned long now = 0;
    calibrationPress(core, host, now, true);
    for(int i = 0; i < 9; i++) calibrationPress(core, host, now);
    TEST_ASSERT_FALSE(core.isCalibrating());
    TEST_ASSERT_EQUAL_INT(0, host.savedCalibrationCount);
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(2));
}

static void test_failed_button_capture_does_not_advance_calibration()
{
    FakeHost host;
    ELRSCrsfCore core;
    beginAt(core, host, defaultConfig(), 0, 0);
    unsigned long now = 0;
    calibrationPress(core, host, now, true);
    host.axesAvailable = false;
    calibrationPress(core, host, now);
    host.axesAvailable = true;
    now += 1200;
    loopAt(core, host, now, now * 1000UL);
    TEST_ASSERT_EQUAL_STRING("CEN", host.displayText.c_str());
    calibrationPress(core, host, now);
    TEST_ASSERT_EQUAL_STRING("TLO", host.displayText.c_str());
}

static void test_valid_descending_button_calibration_is_saved()
{
    FakeHost host;
    ELRSCrsfCore core;
    beginAt(core, host, defaultConfig(), 0, 0);
    unsigned long now = 0;
    calibrationPress(core, host, now, true);
    for(int point = 0; point < 9; point++) {
        for(int axis = 0; axis < 4; axis++) host.axes[axis] = !point ? 1024 : ((point & 1) ? 2047 : 0);
        calibrationPress(core, host, now);
    }
    TEST_ASSERT_EQUAL_INT(4, host.savedCalibrationCount);
    TEST_ASSERT_EQUAL_INT16(2047, host.calibration[AXIS_THROTTLE].minimum);
    TEST_ASSERT_EQUAL_INT16(0, host.calibration[AXIS_THROTTLE].maximum);
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(2));
}

static void test_failed_button_calibration_save_reports_error()
{
    FakeHost host;
    ELRSCrsfCore core;
    host.calibrationWriteOk = false;
    beginAt(core, host, defaultConfig(), 0, 0);
    unsigned long now = 0;
    calibrationPress(core, host, now, true);
    for(int point = 0; point < 9; point++) {
        for(int axis = 0; axis < 4; axis++) host.axes[axis] = !point ? 1024 : ((point & 1) ? 2047 : 0);
        calibrationPress(core, host, now);
    }
    TEST_ASSERT_EQUAL_INT(4, host.savedCalibrationCount);
    TEST_ASSERT_FALSE(core.isCalibrating());
    TEST_ASSERT_EQUAL_STRING("ERR", host.displayText.c_str());
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(2));
}

static void test_transport_sends_across_micros_rollover()
{
    const uint16_t rates[] = {50, 100, 150, 250, 500};
    for(uint16_t rate : rates) {
        FakeHost host;
        ELRSCrsfTransport transport;
        ELRSCrsfTransportConfig config;
        config.packetRateHz = rate;
        const unsigned long interval = (1000000UL + rate - 1) / rate;
        const unsigned long before = 0xFFFFFFFFUL - interval / 2;
        beginAt(transport, host, config, 0, before);
        transportAt(transport, host, 0, before);
        transportAt(transport, host, 1, (uint32_t)(before + interval));
        TEST_ASSERT_EQUAL_INT(2, host.writes.size());
        transportAt(transport, host, 2, (uint32_t)(before + 2 * interval));
        TEST_ASSERT_EQUAL_INT(3, host.writes.size());
    }
}

static void test_malformed_parameter_reply_keeps_retries_active()
{
    FakeHost host;
    ELRSCrsfCore core;
    beginAt(core, host, defaultConfig(), 0, 0);
    for(unsigned long now = 20; now <= 1020; now += 20) loopAt(core, host, now, now * 1000UL);
    host.queueFrame(makeDeviceInfoFrame("ELRS", 1));
    loopAt(core, host, 1040, 1040000);
    loopAt(core, host, 1120, 1120000);
    TEST_ASSERT_EQUAL_INT(1, countWrittenFrameType(host, 0x2C));
    host.queueFrame(makeParameterChunkFrame(1, 0, std::vector<uint8_t>{0, 9, 'x'}));
    loopAt(core, host, 1130, 1130000);
    for(unsigned long now = 1160; now <= 4000; now += 20) {
        host.queueFrame(makeFrame(0x14, std::vector<uint8_t>{0, 0, 88, 0, 0, 0, 0, 0, 0, 0}));
        loopAt(core, host, now, now * 1000UL);
    }
    TEST_ASSERT_GREATER_THAN_INT(1, countWrittenFrameType(host, 0x2C));
    TEST_ASSERT_TRUE(logsContain(host, "parameter scan timed out"));
}

static void test_service_reply_window_and_echo_cross_millis_rollover()
{
    FakeHost host;
    ELRSCrsfTransport transport;
    ELRSCrsfTransportConfig config;
    config.packetRateHz = 250;
    const unsigned long before = 0xFFFFFFFFUL - 249;
    const std::vector<uint8_t> ping = makeFrame(0x28, std::vector<uint8_t>{0, 0xEA});
    beginAt(transport, host, config, before, 1000);
    transport.queueServiceFrame(ping.data(), ping.size());
    transportAt(transport, host, before, 1000); // Bootstrap reply deadline wraps exactly to zero.
    host.queueFrame(ping); // Delayed local echo must not release the reply window.
    transportAt(transport, host, before + 10, 11000);
    TEST_ASSERT_FALSE(transport.status().everReplied);
    TEST_ASSERT_EQUAL_INT(2, host.writes.size());
    transportAt(transport, host, 0xFFFFFFFFUL, 250000);
    TEST_ASSERT_EQUAL_INT(3, host.writes.size());
    transportAt(transport, host, 0, 251000);
    TEST_ASSERT_EQUAL_INT(3, host.writes.size());
    TEST_ASSERT_EQUAL_UINT32(0, transport.status().lastReplyTimeoutAt);
    TEST_ASSERT_EQUAL_HEX8(0x16, host.writes.back()[2]);
}

static void test_delayed_outbound_frames_do_not_count_as_module_replies()
{
    FakeHost host;
    ELRSCrsfTransport transport;
    ELRSCrsfTransportConfig config;
    config.packetRateHz = 250;
    beginAt(transport, host, config, 0, 0);
    const std::vector<uint8_t> ping = makeFrame(0x28, std::vector<uint8_t>{0, 0xEA});
    transport.queueServiceFrame(ping.data(), ping.size());
    transportAt(transport, host, 0, 0);
    transportAt(transport, host, 4, 4000);
    const std::vector<uint8_t> oldRc = host.writes.back();
    uint16_t channels[16] = {};
    channels[0] = 1200;
    transport.setChannels(channels);
    transportAt(transport, host, 8, 8000);
    host.queueFrame(ping);
    host.queueFrame(oldRc);
    host.queueFrame(makeFrame(0x2C, std::vector<uint8_t>{0xEE, 0xEF, 1, 0}));
    transportAt(transport, host, 12, 12000);
    TEST_ASSERT_FALSE(transport.status().everReplied);
    host.queueFrame(makeDeviceInfoFrame("ELRS", 0));
    transportAt(transport, host, 16, 16000);
    TEST_ASSERT_TRUE(transport.status().everReplied);
}

static void test_oversized_parameter_restarts_from_chunk_zero_and_recovers()
{
    FakeHost host;
    ELRSCrsfCore core;
    beginAt(core, host, defaultConfig(), 0, 0);
    for(unsigned long now = 20; now <= 1020; now += 20) loopAt(core, host, now, now * 1000UL);
    host.queueFrame(makeDeviceInfoFrame("ELRS", 1));
    loopAt(core, host, 1040, 1040000);
    loopAt(core, host, 1120, 1120000);
    for(int chunk = 0; chunk < 4; chunk++) {
        const unsigned long now = 1130 + chunk * 110;
        host.queueFrame(makeParameterChunkFrame(1, 4 - chunk, std::vector<uint8_t>(56, 0)));
        loopAt(core, host, now, now * 1000UL);
        loopAt(core, host, now + 100, (now + 100) * 1000UL);
    }
    loopAt(core, host, 1860, 1860000);
    loopAt(core, host, 1960, 1960000);
    const std::vector<uint8_t> *retry = findWrittenFrameType(host, 0x2C, 4);
    TEST_ASSERT_NOT_NULL(retry);
    TEST_ASSERT_EQUAL_UINT8(0, (*retry)[6]);
    host.queueFrame(makeParameterChunkFrame(1, 0, makeTextSelectionEntryData("Telem Ratio", "Std;1:2;1:4;1:8;Off", 0, 4)));
    for(unsigned long now = 1980; now <= 2800; now += 20) loopAt(core, host, now, now * 1000UL);
    TEST_ASSERT_TRUE(logsContain(host, "module settings apply complete"));
}

static void test_hysteresis_holds_jitter_and_tracks_slow_motion()
{
    FakeHost host;
    ELRSCrsfCore core;
    beginAt(core, host, defaultConfig(), 0);
    unsigned long now = 0;
    for(int offset = -4; offset <= 5; offset++) {
        for(int axis = 0; axis < 4; axis++) host.axes[axis] = 1024 + offset;
        now += 20;
        loopAtMs(core, host, now, 0);
        for(int channel = 0; channel < 4; channel++) TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(channel));
    }
    for(int axis = 0; axis < 4; axis++) host.axes[axis] = 1030;
    loopAtMs(core, host, now + 20, 0);
    for(int channel = 0; channel < 4; channel++) TEST_ASSERT_NOT_EQUAL(992, core.channelAt(channel));
}

static void test_hysteresis_reaches_endpoints_and_reseeds_after_error()
{
    FakeHost host;
    ELRSCrsfCore core;
    host.axes[0] = 2;
    beginAt(core, host, defaultConfig(), 0);
    host.axes[0] = 0;
    loopAtMs(core, host, 20, 0);
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(0));
    host.axes[0] = 2045;
    loopAtMs(core, host, 40, 0);
    host.axes[0] = 2047;
    loopAtMs(core, host, 60, 0);
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(0));
    host.axesAvailable = false;
    loopAtMs(core, host, 80, 0);
    host.axesAvailable = true;
    host.axes[0] = 2043;
    loopAtMs(core, host, 100, 0);
    TEST_ASSERT_EQUAL_UINT16(1808, core.channelAt(0));
}

static void test_hysteresis_returns_to_neutral_for_all_profile_directions()
{
    for(int descending = 0; descending < 2; descending++) {
        for(int reverse = 0; reverse < 2; reverse++) {
            for(int side : {-1, 1}) {
                FakeHost host;
                ELRSCrsfCore core;
                ELRSCrsfCoreConfig config = defaultConfig();
                config.inputRouting = {4, 3, 2, 1};
                config.throttleIdleDeadband = 0;
                for(int axis = 0; axis < 4; axis++) {
                    config.axisProfiles[axis] = elrsDefaultInputAxisProfile();
                    config.axisProfiles[axis].minimum = descending ? 1020 : 1000;
                    config.axisProfiles[axis].center = 1010;
                    config.axisProfiles[axis].maximum = descending ? 1000 : 1020;
                    config.axisProfiles[axis].reverse = reverse;
                    host.axes[axis] = 1010 + side * 4;
                }
                beginAt(core, host, config, 0);
                for(int channel = 0; channel < 4; channel++) TEST_ASSERT_NOT_EQUAL(992, core.channelAt(channel));
                for(int axis = 0; axis < 4; axis++) host.axes[axis] = 1010;
                loopAtMs(core, host, 20, 0);
                for(int channel = 0; channel < 4; channel++) TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(channel));
                // Once neutral, small ADC excursions must still be held.
                for(int axis = 0; axis < 4; axis++) host.axes[axis] = 1010 + side * 4;
                loopAtMs(core, host, 40, 0);
                for(int channel = 0; channel < 4; channel++) TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(channel));
            }
        }
    }
}

static void test_throttle_idle_band_handles_all_profile_directions()
{
    for(int descending = 0; descending <= 1; descending++) {
        for(int reverse = 0; reverse <= 1; reverse++) {
            ELRSCrsfCoreConfig config = defaultConfig();
            ELRSInputAxisProfile &profile = config.axisProfiles[AXIS_THROTTLE];
            profile = elrsDefaultInputAxisProfile();
            profile.minimum = descending ? 1500 : 300;
            profile.center = 900;
            profile.maximum = descending ? 300 : 1500;
            profile.reverse = reverse;
            int idle = reverse ? profile.maximum : profile.minimum;
            int full = reverse ? profile.minimum : profile.maximum;
            int direction = (full > idle) ? 1 : -1;
            for(int offset = 0; offset <= 5; offset++) {
                FakeHost host;
                ELRSCrsfCore core;
                host.axes[AXIS_THROTTLE] = idle + direction * offset;
                beginAt(core, host, config, 0);
                TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(2));
            }
            FakeHost host;
            ELRSCrsfCore core;
            host.axes[AXIS_THROTTLE] = profile.center;
            beginAt(core, host, config, 0);
            TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(2));
            host.axes[AXIS_THROTTLE] = full;
            loopAtMs(core, host, 20, 0);
            TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(2));
            host.axes[AXIS_THROTTLE] = idle + direction * 20;
            loopAtMs(core, host, 40, 0);
            TEST_ASSERT_TRUE(core.channelAt(2) > 172);
        }
    }
}

static void test_throttle_idle_entry_bypasses_hysteresis()
{
    for(int narrow = 0; narrow <= 1; narrow++) {
        for(int descending = 0; descending <= 1; descending++) {
            for(int reverse = 0; reverse <= 1; reverse++) {
                ELRSCrsfCoreConfig config = defaultConfig();
                config.throttleIdleDeadband = narrow ? 32 : 5;
                ELRSInputAxisProfile &profile = config.axisProfiles[AXIS_THROTTLE];
                profile.minimum = descending ? (narrow ? 1020 : 1500) : (narrow ? 1000 : 300);
                profile.center = narrow ? 1010 : 900;
                profile.maximum = descending ? (narrow ? 1000 : 300) : (narrow ? 1020 : 1500);
                profile.reverse = reverse;
                int idle = reverse ? profile.maximum : profile.minimum;
                int full = reverse ? profile.minimum : profile.maximum;
                int direction = (full > idle) ? 1 : -1;
                FakeHost host;
                ELRSCrsfCore core;
                host.axes[AXIS_THROTTLE] = idle + direction * 10;
                beginAt(core, host, config, 0);
                TEST_ASSERT_TRUE(core.channelAt(2) > 172);
                host.axes[AXIS_THROTTLE] = idle + direction * (narrow ? 9 : 5);
                for(unsigned long now = 20; now <= 60; now += 20) {
                    loopAtMs(core, host, now, 0);
                    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(2));
                }
                if(!narrow) {
                    host.axes[AXIS_THROTTLE] = idle + direction * 10;
                    loopAtMs(core, host, 80, 0);
                    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(2));
                    host.axes[AXIS_THROTTLE] = idle + direction * 11;
                    loopAtMs(core, host, 100, 0);
                    TEST_ASSERT_TRUE(core.channelAt(2) > 172);
                }
            }
        }
    }
}

static void test_input_tolerances_are_adjustable_and_can_be_disabled()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    config.adcHysteresis = 8;
    beginAt(core, host, config, 0);
    host.axes[0] = 1032;
    loopAtMs(core, host, 20, 0);
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(0));
    host.axes[0] = 1033;
    loopAtMs(core, host, 40, 0);
    TEST_ASSERT_EQUAL_UINT16(998, core.channelAt(0));
    config.adcHysteresis = 0;
    host.axes[0] = 1024;
    beginAt(core, host, config, 60);
    host.axes[0] = 1028;
    loopAtMs(core, host, 80, 0);
    TEST_ASSERT_EQUAL_UINT16(995, core.channelAt(0));
    config.throttleIdleDeadband = 0;
    config.axisProfiles[AXIS_THROTTLE] = elrsDefaultInputAxisProfile();
    config.axisProfiles[AXIS_THROTTLE].minimum = 300;
    config.axisProfiles[AXIS_THROTTLE].center = 900;
    config.axisProfiles[AXIS_THROTTLE].maximum = 1500;
    host.axes[AXIS_THROTTLE] = 305;
    beginAt(core, host, config, 100);
    TEST_ASSERT_EQUAL_UINT16(179, core.channelAt(2));
}

static void test_throttle_idle_band_preserves_center_deadband_endpoints()
{
    ELRSInputAxisProfile profile = elrsDefaultInputAxisProfile();
    profile.minimum = 1000;
    profile.center = 1010;
    profile.maximum = 1020;
    profile.deadband = 9;
    TEST_ASSERT_EQUAL_INT16(1000, elrsInputModelThrottleToUs(profile, 1000, 5));
    TEST_ASSERT_EQUAL_INT16(1000, elrsInputModelThrottleToUs(profile, 1005, 5));
    TEST_ASSERT_EQUAL_INT16(1500, elrsInputModelThrottleToUs(profile, 1010, 5));
    TEST_ASSERT_EQUAL_INT16(2000, elrsInputModelThrottleToUs(profile, 1020, 5));
    profile.reverse = 1;
    TEST_ASSERT_EQUAL_INT16(1000, elrsInputModelThrottleToUs(profile, 1020, 5));
    TEST_ASSERT_EQUAL_INT16(2000, elrsInputModelThrottleToUs(profile, 1000, 5));
}

static void test_throttle_idle_band_preserves_narrow_profiles()
{
    for(int descending = 0; descending <= 1; descending++) {
        for(int reverse = 0; reverse <= 1; reverse++) {
            ELRSInputAxisProfile profile = elrsDefaultInputAxisProfile();
            profile.minimum = descending ? 1002 : 1000;
            profile.center = 1001;
            profile.maximum = descending ? 1000 : 1002;
            profile.reverse = reverse;
            TEST_ASSERT_EQUAL_INT16(1000, elrsInputModelThrottleToUs(profile, reverse ? profile.maximum : profile.minimum, 32));
            TEST_ASSERT_EQUAL_INT16(1500, elrsInputModelThrottleToUs(profile, 1001, 32));
            TEST_ASSERT_EQUAL_INT16(2000, elrsInputModelThrottleToUs(profile, reverse ? profile.minimum : profile.maximum, 32));
        }
    }
    ELRSInputAxisProfile profile = elrsDefaultInputAxisProfile();
    profile.minimum = 1000;
    profile.center = 1010;
    profile.maximum = 1020;
    TEST_ASSERT_EQUAL_INT16(1000, elrsInputModelThrottleToUs(profile, 1009, 32));
    TEST_ASSERT_EQUAL_INT16(1500, elrsInputModelThrottleToUs(profile, 1010, 32));
    TEST_ASSERT_EQUAL_INT16(2000, elrsInputModelThrottleToUs(profile, 1020, 32));
}

static void test_switch_mapping_routes_each_input_without_leaking_old_channels()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    for(int i = 0; i < ELRS_SWITCH_INPUT_COUNT; i++) config.switchRouting.channels[i] = 5 + (i + 3) % 12;
    beginAt(core, host, config, 0);
    for(int input = 0; input < ELRS_SWITCH_INPUT_COUNT; input++) {
        host.stop = input == 0;
        host.fakePower = input == 1;
        host.buttonA = input == 2;
        host.buttonB = input == 3;
        host.packStates = input >= 4 ? 1 << (input - 4) : 0;
        loopAtMs(core, host, 20 * (input + 1), 0);
        for(int i = 0; i < ELRS_SWITCH_INPUT_COUNT; i++) {
            TEST_ASSERT_EQUAL_UINT16(i == input ? 1811 : 172, core.channelAt(config.switchRouting.channels[i] - 1));
        }
        for(int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(i));
        TEST_ASSERT_EQUAL(host.stop, host.stopLed);
        TEST_ASSERT_EQUAL(host.fakePower, core.fakePowerOn());
    }
    host.axesAvailable = false;
    loopAtMs(core, host, 500, 0);
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(2));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(config.switchRouting.channels[11] - 1));
}

static void test_switch_mapping_preserves_self_test_and_buttonpack_fallback()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    for(int i = 0; i < ELRS_SWITCH_INPUT_COUNT; i++) config.switchRouting.channels[i] = 16 - i;
    host.packAvailable = false;
    beginAt(core, host, config, 0);
    loopAtMs(core, host, 200, 0);
    for(int i = 4; i < 12; i++) TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(config.switchRouting.channels[i] - 1));
    host.packAvailable = true;
    host.packStates = 1;
    loopAtMs(core, host, 220, 0);
    host.packAvailable = false;
    loopAtMs(core, host, 500, 0);
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(config.switchRouting.channels[4] - 1));
    core.startSelfTest(500);
    loopAtMs(core, host, 520, 0);
    for(int i = 0; i < 12; i++) TEST_ASSERT_EQUAL_UINT16(i == 0 ? 1811 : 172, core.channelAt(config.switchRouting.channels[i] - 1));
}

static void test_switch_mapping_rejects_duplicates_and_invalid_channels()
{
    ELRSSwitchRouting routing = elrsDefaultSwitchRouting();
    TEST_ASSERT_TRUE(elrsIsValidSwitchRouting(routing));
    for(int i = 0; i < 12; i++) TEST_ASSERT_EQUAL_UINT8(5 + i, routing.channels[i]);
    const uint8_t invalid[] = {17, 255, 6};
    for(uint8_t channel : invalid) {
        routing = elrsDefaultSwitchRouting();
        routing.channels[0] = channel;
        TEST_ASSERT_FALSE(elrsIsValidSwitchRouting(routing));
        FakeHost host;
        ELRSCrsfCore core;
        ELRSCrsfCoreConfig config = defaultConfig();
        config.switchRouting = routing;
        host.stop = true;
        beginAt(core, host, config, 0);
        TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(4));
        TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(5));
    }
}

static void test_all_inputs_can_use_every_channel_without_collisions()
{
    for(int rotation = 0; rotation < 16; rotation++) {
        FakeHost host;
        ELRSCrsfCore core;
        ELRSCrsfCoreConfig config = defaultConfig();
        config.inputRouting = {
            (uint8_t)(1 + rotation % 16), (uint8_t)(1 + (1 + rotation) % 16),
            (uint8_t)(1 + (2 + rotation) % 16), (uint8_t)(1 + (3 + rotation) % 16)
        };
        for(int i = 0; i < 12; i++) config.switchRouting.channels[i] = 1 + (4 + i + rotation) % 16;
        beginAt(core, host, config, 0);
        for(int input = 0; input < 12; input++) {
            host.stop = input == 0;
            host.fakePower = input == 1;
            host.buttonA = input == 2;
            host.buttonB = input == 3;
            host.packStates = input >= 4 ? 1 << (input - 4) : 0;
            loopAtMs(core, host, 20 * (input + 1), 0);
            for(int i = 0; i < 12; i++) {
                TEST_ASSERT_EQUAL_UINT16(i == input ? 1811 : 172, core.channelAt(config.switchRouting.channels[i] - 1));
            }
            for(int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_UINT16(992, core.channelAt((i + rotation) % 16));
        }
        host.axesAvailable = false;
        loopAtMs(core, host, 500, 0);
        for(int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_UINT16(992, core.channelAt((i + rotation) % 16));
        core.startSelfTest(500);
        loopAtMs(core, host, 520, 0);
        TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(config.switchRouting.channels[0] - 1));
        for(int i = 1; i < 12; i++) TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(config.switchRouting.channels[i] - 1));
    }
}

static void test_local_actions_require_release_after_startup_and_suppression()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    for(uint8_t &enabled : config.localActions) enabled = 1;
    host.stop = host.fakePower = host.buttonA = host.buttonB = true;
    host.packStates = 255;
    beginAt(core, host, config, 1000);
    loopAtMs(core, host, 1100, 0);
    TEST_ASSERT_EQUAL_UINT16(0, host.localValidMask);
    host.stop = host.fakePower = host.buttonA = host.buttonB = false;
    host.packStates = 0;
    loopAtMs(core, host, 1110, 0);
    TEST_ASSERT_EQUAL_UINT16(0xfff, host.localValidMask);
    host.stop = host.fakePower = host.buttonA = host.buttonB = true;
    host.packStates = 255;
    loopAtMs(core, host, 1120, 0);
    TEST_ASSERT_EQUAL_UINT16(0xfff, host.localStates);
    TEST_ASSERT_EQUAL_UINT16(0xfff, host.localValidMask);
    for(int input = 0; input < ELRS_SWITCH_INPUT_COUNT; input++) {
        TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(config.switchRouting.channels[input] - 1));
    }
    host.stop = host.fakePower = host.buttonA = host.buttonB = false;
    host.packStates = 0;
    loopAtMs(core, host, 1200, 0);
    core.startSelfTest(1300, 100);
    host.buttonA = true;
    loopAtMs(core, host, 1390, 0); // A press near self-test expiry must remain blocked after expiry.
    loopAtMs(core, host, 1450, 0);
    TEST_ASSERT_EQUAL_UINT16(0xffb, host.localValidMask);
    host.buttonA = false;
    loopAtMs(core, host, 1460, 0);
    TEST_ASSERT_EQUAL_UINT16(0xfff, host.localValidMask);
}

static void test_local_actions_opt_in_and_ignore_failed_pack_reads()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    config.localActions[4] = 1;
    config.localActions[2] = 1;
    config.localActions[0] = 255;
    host.packAvailable = false;
    beginAt(core, host, config, 1000);
    TEST_ASSERT_EQUAL_UINT16(4, host.localValidMask);
    host.packStates = 1;
    host.packAvailable = true;
    loopAtMs(core, host, 1100, 0);
    TEST_ASSERT_EQUAL_UINT16(4, host.localValidMask); // Held on initial successful read.
    host.packStates = 0;
    loopAtMs(core, host, 1110, 0);
    TEST_ASSERT_EQUAL_UINT16(20, host.localValidMask);
    host.packStates = 1;
    loopAtMs(core, host, 1120, 0);
    TEST_ASSERT_EQUAL_UINT16(20, host.localValidMask);
    host.packAvailable = false;
    loopAtMs(core, host, 1120, 0); // Two loops can share millis(); this read still failed.
    TEST_ASSERT_EQUAL_UINT16(4, host.localValidMask);
    loopAtMs(core, host, 1170, 0);
    TEST_ASSERT_EQUAL_UINT16(4, host.localValidMask);
    host.packAvailable = true;
    loopAtMs(core, host, 1200, 0);
    TEST_ASSERT_EQUAL_UINT16(4, host.localValidMask); // Recovery cannot continue a pending press.
    host.packStates = 0;
    loopAtMs(core, host, 1210, 0);
    TEST_ASSERT_EQUAL_UINT16(20, host.localValidMask);
    beginAt(core, host, defaultConfig(), 1300);
    TEST_ASSERT_EQUAL_UINT16(0, host.localValidMask); // None enabled by default.
}

static void test_local_actions_require_release_after_calibration()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    config.localActions[2] = 1;
    unsigned long now = 1000;
    beginAt(core, host, config, now);
    calibrationPress(core, host, now, true);
    TEST_ASSERT_TRUE(core.isCalibrating());
    host.buttonA = true;
    loopAtMs(core, host, ++now, 0);
    TEST_ASSERT_EQUAL_UINT16(0, host.localValidMask);
    calibrationPress(core, host, now, true); // Cancel, with O.O still held.
    TEST_ASSERT_FALSE(core.isCalibrating());
    loopAtMs(core, host, ++now, 0);
    TEST_ASSERT_EQUAL_UINT16(0, host.localValidMask);
    host.buttonA = false;
    loopAtMs(core, host, ++now, 0);
    TEST_ASSERT_EQUAL_UINT16(4, host.localValidMask);
}

static void test_prop_controls_keep_display_leds_and_calibration_while_transmitting()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    config.propControls = true;
    config.usePowerLed = config.useLevelMeter = true;
    for(uint8_t &enabled : config.localActions) enabled = 1;
    host.powerLed = host.levelMeter = host.stopLed = true;
    beginAt(core, host, config, 1000);
    TEST_ASSERT_TRUE(host.powerLed && host.levelMeter && host.stopLed);
    host.axes[AXIS_AILERON] = 2047;
    host.stop = true;
    loopAt(core, host, 1300, 1300000);
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(config.switchRouting.channels[0] - 1));
    TEST_ASSERT_TRUE(!host.writes.empty());
    TEST_ASSERT_EQUAL_INT(0, host.displayShows);
    unsigned long now = 1300;
    calibrationPress(core, host, now, true);
    loopAtMs(core, host, now + 1000, 1); // Battery alerts also belong to the prop loop.
    TEST_ASSERT_FALSE(core.isCalibrating());
    TEST_ASSERT_EQUAL_INT(0, host.displayShows);
    TEST_ASSERT_EQUAL_INT(0, host.localScanCount);
    TEST_ASSERT_TRUE(host.powerLed && host.levelMeter && host.stopLed);
    host.axesAvailable = false;
    loopAtMs(core, host, now + 1200, 0);
    TEST_ASSERT_TRUE(core.getStatus().faultFlags & ELRS_FAULT_ADC_STALE);
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(config.inputRouting.aileronChannel - 1));
}

static void test_none_routing_preserves_multiple_disabled_inputs()
{
    ELRSGimbalRouting gimbals = {0, 2, 0, 4};
    ELRSSwitchRouting switches = elrsDefaultSwitchRouting();
    switches.channels[0] = switches.channels[1] = 0;
    TEST_ASSERT_TRUE(elrsIsValidInputRouting(gimbals, switches));
    elrsSanitizeInputRouting(gimbals, switches);
    TEST_ASSERT_EQUAL_UINT8(0, gimbals.aileronChannel);
    TEST_ASSERT_EQUAL_UINT8(0, gimbals.throttleChannel);
    TEST_ASSERT_EQUAL_UINT8(2, gimbals.elevatorChannel);
    TEST_ASSERT_EQUAL_UINT8(4, gimbals.rudderChannel);
    TEST_ASSERT_EQUAL_UINT8(0, switches.channels[0]);
    TEST_ASSERT_EQUAL_UINT8(0, switches.channels[1]);
    // Assigned duplicates remain invalid, including across input types.
    switches.channels[1] = 2;
    TEST_ASSERT_FALSE(elrsIsValidInputRouting(gimbals, switches));
    switches.channels[1] = 7;
    TEST_ASSERT_FALSE(elrsIsValidInputRouting(gimbals, switches));
    for(int channel = 17; channel <= 255; channel++) {
        gimbals = {0, 2, 0, (uint8_t)channel};
        switches = elrsDefaultSwitchRouting();
        TEST_ASSERT_FALSE(elrsIsValidGimbalRouting(gimbals));
        switches.channels[0] = channel;
        TEST_ASSERT_FALSE(elrsIsValidSwitchRouting(switches));
    }
}

static void test_none_switch_keeps_local_actions_and_other_channels_independent()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    config.switchRouting.channels[2] = 0;
    config.switchRouting.channels[4] = 0;
    config.localActions[2] = config.localActions[4] = 1;
    TEST_ASSERT_TRUE(beginAt(core, host, config, 1000));
    host.buttonA = true;
    host.packStates = 1;
    host.buttonB = true;
    loopAtMs(core, host, 1020, 0);
    TEST_ASSERT_EQUAL_UINT16(20, host.localValidMask);
    TEST_ASSERT_EQUAL_UINT16(28, host.localStates);
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(6));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(8));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(7));
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(0));
}

static void test_none_gimbals_leave_free_channels_and_preserve_assigned_axes()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    config.inputRouting = {0, 2, 0, 4};
    host.axes[AXIS_AILERON] = host.axes[AXIS_ELEVATOR] = 2047;
    host.axes[AXIS_RUDDER] = host.axes[AXIS_THROTTLE] = 0;
    TEST_ASSERT_TRUE(beginAt(core, host, config, 1000));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(0));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(1));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(2));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(3));
    core.startSelfTest(1000, 100);
    loopAtMs(core, host, 1020, 0);
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(0));
    TEST_ASSERT_EQUAL_UINT16(992, core.channelAt(1));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(2));
    loopAtMs(core, host, 1200, 0);
    unsigned long now = 1200;
    calibrationPress(core, host, now, true);
    TEST_ASSERT_TRUE(core.isCalibrating());
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(0));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(2));
}

static void test_none_frees_channels_for_other_input_types()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    config.inputRouting.aileronChannel = 0;
    config.inputRouting.throttleChannel = 5;
    config.switchRouting.channels[0] = 1;
    host.stop = true;
    host.axes[AXIS_THROTTLE] = 2047;
    TEST_ASSERT_TRUE(elrsIsValidInputRouting(config.inputRouting, config.switchRouting));
    TEST_ASSERT_TRUE(beginAt(core, host, config, 1000));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(0));
    TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(2));
    TEST_ASSERT_EQUAL_UINT16(1811, core.channelAt(4));
}

static void test_all_none_configuration_survives_sanitizing_and_core_modes()
{
    FakeHost host;
    ELRSCrsfCore core;
    ELRSCrsfCoreConfig config = defaultConfig();
    config.inputRouting = {0, 0, 0, 0};
    for(uint8_t &channel : config.switchRouting.channels) channel = 0;
    TEST_ASSERT_TRUE(elrsIsValidInputRouting(config.inputRouting, config.switchRouting));
    elrsSanitizeInputRouting(config.inputRouting, config.switchRouting);
    TEST_ASSERT_EQUAL_UINT8(0, elrsSanitizeGimbalRouting(config.inputRouting).throttleChannel);
    for(uint8_t channel : elrsSanitizeSwitchRouting(config.switchRouting).channels) TEST_ASSERT_EQUAL_UINT8(0, channel);
    host.stop = host.fakePower = host.buttonA = host.buttonB = true;
    host.packStates = 255;
    TEST_ASSERT_TRUE(beginAt(core, host, config, 1000));
    for(int channel = 0; channel < 16; channel++) TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(channel));
    core.startSelfTest(1000, 100);
    loopAtMs(core, host, 1020, 0);
    for(int channel = 0; channel < 16; channel++) TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(channel));
    host.fakePower = false;
    loopAtMs(core, host, 1200, 0);
    unsigned long now = 1200;
    calibrationPress(core, host, now, true);
    TEST_ASSERT_TRUE(core.isCalibrating());
    for(int channel = 0; channel < 16; channel++) TEST_ASSERT_EQUAL_UINT16(172, core.channelAt(channel));
}

// Catch missing fields, wrong wire units/signs, and stale-family leakage.
static void test_telemetry_samples_decode_supported_numeric_sources()
{
    FakeHost host;
    ELRSCrsfCore core;
    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x02, {0,0,0,0,0,0,0,0,0,126,0x30,0x39,0x03,0xB6,8}));
    host.queueFrame(makeFrame(0x0A, {0,77}));
    host.queueFrame(makeFrame(0x08, {0,126,0,34,0,9,196,77}));
    host.queueFrame(makeFrame(0x14, {105,0,85,253,0,0,0,0,0,0}));
    loopAtMs(core, host, 100, 0);
    const float expected[] = {12.6f,7.7f,12.6f,3.4f,77,2500,-50,123.45f,8,85,-105,-3};
    for(uint8_t source = 1; source <= 12; source++) {
        const ELRSTelemetrySample sample = core.telemetrySample(source, 100);
        TEST_ASSERT_TRUE(sample.received);
        TEST_ASSERT_TRUE(sample.available);
        TEST_ASSERT_EQUAL_UINT32(0, sample.ageMs);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, expected[source - 1], sample.value);
    }
    TEST_ASSERT_EQUAL_UINT8(1, core.telemetrySample(0, 100).source);
    TEST_ASSERT_EQUAL_STRING("V", elrsTelemetrySourceUnit(3, 0));
    TEST_ASSERT_EQUAL_STRING("mph", elrsTelemetrySourceUnit(1, 1));
    TEST_ASSERT_FALSE(core.telemetrySample(13, 100).available);
    TEST_ASSERT_FALSE(core.telemetrySample(14, 100).available);
}

static void test_telemetry_samples_track_presence_freshness_and_invalid_fields()
{
    FakeHost host;
    ELRSCrsfCore core;
    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    TEST_ASSERT_FALSE(core.telemetrySample(3, 0).received);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, core.telemetrySample(3, 0).ageMs);
    host.queueFrame(makeFrame(0x02, std::vector<uint8_t>(15, 0)));
    host.queueFrame(makeFrame(0x08, {0,126,0,0,0,0,0,77}));
    loopAtMs(core, host, 0, 0);
    TEST_ASSERT_TRUE(core.telemetrySample(1, 0).available);
    TEST_ASSERT_EQUAL_FLOAT(0, core.telemetrySample(1, 0).value);
    TEST_ASSERT_TRUE(core.telemetrySample(9, 0).available); // Zero satellites is received, not proof of a fix.
    TEST_ASSERT_TRUE(core.telemetrySample(3, 1999).available);
    host.queueFrame(makeFrame(0x14, {0,0,80,0,0,0,0,0,0,0}));
    loopAtMs(core, host, 2000, 0);
    TEST_ASSERT_FALSE(core.telemetrySample(1, 2000).available);
    TEST_ASSERT_TRUE(core.telemetrySample(1, 2000).received);
    TEST_ASSERT_FALSE(core.telemetrySample(3, 2000).available);
    TEST_ASSERT_EQUAL_FLOAT(0, core.telemetrySample(3, 2000).value);
    TEST_ASSERT_EQUAL_UINT8(10, core.telemetrySample(0, 2000).source);
    auto badBattery = makeFrame(0x08, {0,100,0,0,0,0,0,50});
    badBattery.back() ^= 255;
    host.queueFrame(badBattery);
    host.queueFrame(makeFrame(0x08, {0,100}));
    host.queueFrame(makeFrame(0x22, {1,2,3}));
    loopAtMs(core, host, 2100, 0);
    TEST_ASSERT_EQUAL_UINT32(2100, core.telemetrySample(3, 2100).ageMs);
    host.queueFrame(makeFrame(0x08, {0,126,0,34,0,9,196,255}));
    host.queueFrame(makeFrame(0x02, {0,0,0,0,0,0,0,0,0,126,255,255,0,0,0}));
    host.queueFrame(makeFrame(0x14, {105,0,255,253,0,0,0,0,0,0}));
    loopAtMs(core, host, 2200, 0);
    TEST_ASSERT_FALSE(core.telemetrySample(5, 2200).available);
    TEST_ASSERT_TRUE(core.telemetrySample(3, 2200).available);
    TEST_ASSERT_FALSE(core.telemetrySample(8, 2200).available);
    TEST_ASSERT_TRUE(core.telemetrySample(1, 2200).available);
    TEST_ASSERT_FALSE(core.telemetrySample(10, 2200).available);
    TEST_ASSERT_TRUE(core.telemetrySample(12, 2200).available);
    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0xfffffff0UL));
    TEST_ASSERT_FALSE(core.telemetrySample(3, 0xfffffff0UL).received);
    host.queueFrame(makeFrame(0x08, {0,126,0,0,0,0,0,77}));
    loopAtMs(core, host, 0xfffffff0UL, 0);
    TEST_ASSERT_TRUE(core.telemetrySample(3, 1983).available);
    TEST_ASSERT_FALSE(core.telemetrySample(3, 1984).available);
}

static void test_display_assignment_scaling_and_formatting()
{
    struct Case { float value, multiplier, offset; uint8_t decimals; const char *text; };
    const Case cases[] = {
        {123,0.5f,0,1,"61.5"}, {12.6f,1,0,1,"12.6"}, {85,1,0,0,"85"},
        {2500,0.001f,0,2,"2.50"}, {-105,1,20,0,"-85"}, {1000,1,0,0,"HI"},
        {999,1,0,0,"999"}, {99.9f,1,0,1,"99.9"}, {9.99f,1,0,2,"9.99"},
        {-99,1,0,0,"-99"}, {-9.9f,1,0,1,"-9.9"}, {-0.99f,1,0,2,"-.99"},
        {-100,1,0,0,"LO"}, {-10,1,0,1,"LO"}, {-1,1,0,2,"LO"},
        {99.95f,1,0,1,"HI"}, {1.25f,1,0,1,"1.3"}, {-1.25f,1,0,1,"-1.3"},
        {-0.001f,1,0,2,"0.00"}, {7,0,2,0,"2"}, {7,-1,0,0,"-7"}
    };
    for(const Case &item : cases) {
        ELRSDisplayConfig config = {1,item.decimals,item.multiplier,item.offset};
        ELRSTelemetrySample sample = {1,item.value,true,true,0};
        char text[8];
        elrsFormatTelemetry(config, sample, 0, text);
        TEST_ASSERT_EQUAL_STRING(item.text, text);
    }
    char text[8];
    ELRSDisplayConfig config = elrsDefaultDisplayConfig();
    TEST_ASSERT_EQUAL_UINT8(14, config.source);
    config.source = 1; config.multiplier = 0.5f; config.decimalPlaces = 1;
    ELRSTelemetrySample sample = {1,100,true,true,0};
    elrsFormatTelemetry(config, sample, 1, text);
    TEST_ASSERT_EQUAL_STRING("31.1", text);
    sample.available = false;
    elrsFormatTelemetry(config, sample, 0, text);
    TEST_ASSERT_EQUAL_STRING("---", text);
    config.source = 13;
    elrsFormatTelemetry(config, sample, 0, text);
    TEST_ASSERT_EQUAL_STRING("", text);
    config.source = 14;
    TEST_ASSERT_EQUAL_UINT8(0, elrsEffectiveDisplayConfig(config, false).source);
    TEST_ASSERT_EQUAL_UINT8(14, elrsEffectiveDisplayConfig(config, true).source);
    config.source = 99;
    TEST_ASSERT_FALSE(elrsIsValidDisplayConfig(config));
    config.source = 1; config.decimalPlaces = 3;
    TEST_ASSERT_FALSE(elrsIsValidDisplayConfig(config));
    config.decimalPlaces = 1; config.multiplier = 1001;
    TEST_ASSERT_FALSE(elrsIsValidDisplayConfig(config));
    config.multiplier = NAN;
    TEST_ASSERT_FALSE(elrsIsValidDisplayConfig(config));
    config = {0,2,100,500}; sample.available = true; sample.value = 12.6f;
    elrsFormatTelemetry(config, sample, 0, text);
    TEST_ASSERT_EQUAL_STRING("12.6", text); // Auto uses neutral scale and source precision.
    sample.value = INFINITY;
    elrsFormatTelemetry(config, sample, 0, text);
    TEST_ASSERT_EQUAL_STRING("---", text);
}

static void test_display_assignment_outage_recovery_units_and_overlays()
{
    FakeHost host;
    ELRSCrsfCore core;
    auto config = defaultConfig();
    config.displayConfig = {1,1,0.5f,0};
    config.propControls = true;
    TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
    TEST_ASSERT_TRUE(core.telemetryDisplayAssigned());
    host.queueFrame(makeFrame(0x02, {0,0,0,0,0,0,0,0,0x03,0xE8,0,0,0,0,0}));
    loopAtMs(core, host, 1200, 0);
    TEST_ASSERT_EQUAL_INT(0, host.displayShows);
    core.renderAssignedDisplay(host, 1200, 0);
    TEST_ASSERT_EQUAL_STRING("50.0", host.displayText.c_str());
    host.queueFrame(makeFrame(0x14, {0,0,88,0,0,0,0,0,0,0}));
    loopAtMs(core, host, 3200, 0);
    core.renderAssignedDisplay(host, 3200, 0);
    TEST_ASSERT_EQUAL_STRING("---", host.displayText.c_str());
    host.queueFrame(makeFrame(0x02, {0,0,0,0,0,0,0,0,0x03,0xE8,0,0,0,0,0}));
    loopAtMs(core, host, 3400, 0);
    core.renderAssignedDisplay(host, 3400, 0);
    TEST_ASSERT_EQUAL_STRING("50.0", host.displayText.c_str());
    config.displayConfig.source = 14;
    TEST_ASSERT_TRUE(beginAt(core, host, config, 3500));
    TEST_ASSERT_FALSE(core.telemetryDisplayAssigned());
    const int shown = host.displayShows;
    core.renderAssignedDisplay(host, 4000, 0);
    TEST_ASSERT_EQUAL_INT(shown, host.displayShows);
    config.propControls = false; config.displayConfig.source = ELRS_DISPLAY_OFF;
    TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
    loopAtMs(core, host, 1500, 0);
    TEST_ASSERT_EQUAL_STRING("", host.displayText.c_str());
    loopAtMs(core, host, 30001, 1);
    TEST_ASSERT_EQUAL_STRING("BAT", host.displayText.c_str());
}

static void test_firma_rpm_uses_existing_display_for_actual_and_scaled_mph()
{
    for(uint8_t source : {15, 16}) {
        FakeHost host;
        ELRSCrsfCore core;
        auto config = defaultConfig();
        config.displayConfig = {source, 255, 1, 0};
        TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
        // 10,000 eRPM / 2 pole pairs / 6.55 reduction, with 64 mm tires.
        host.queueFrame(makeFrame(0x0C, {0, 0, 0x27, 0x10}));
        loopAtMs(core, host, 1500, 0); // Startup ELR banner retains priority for one second.
        const auto sample = core.telemetrySample(source, 1500);
        TEST_ASSERT_TRUE(sample.available);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, source == 15 ? 5.722172f : 57.22172f, sample.value);
        TEST_ASSERT_EQUAL_STRING(source == 15 ? "5.7" : "57.2", host.displayText.c_str());
    }
}

static void test_rpm_accepts_elrs_serial_sources_and_hott_update_interval()
{
    // SRXL2/Scorpion use 0; HoTT EAM/GAM/ESC use 1/2/3. CRSF permits any source ID.
    for(uint8_t id : {0, 1, 2, 3, 128, 255}) {
        FakeHost host;
        ELRSCrsfCore core;
        auto config = defaultConfig(); config.displayConfig = {15, 255, 1, 0};
        TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
        host.queueFrame(makeFrame(0x0C, {id, 0, 0x27, 0x10, 0x7F, 0xFF, 0xFF}));
        loopAtMs(core, host, 1500, 0);
        TEST_ASSERT_EQUAL_STRING("5.7", host.displayText.c_str()); // Use the first RPM, not RPM max/other sensors.
        loopAtMs(core, host, 6699, 0); // HoTT can leave unchanged RPM unsent for five seconds.
        TEST_ASSERT_TRUE(core.telemetrySample(15, 6699).available);
        host.queueFrame(makeFrame(0x0C, {id, 0, 0x27, 0x10, 0})); // Ignore trailing extension bytes.
        loopAtMs(core, host, 6700, 0);
        TEST_ASSERT_EQUAL_UINT32(0, core.telemetrySample(15, 6700).ageMs);
        loopAtMs(core, host, 12700, 0);
        TEST_ASSERT_FALSE(core.telemetrySample(15, 12700).available);
        loopAtMs(core, host, 6700, 0);
        TEST_ASSERT_FALSE(core.telemetrySample(15, 6700).available);
    }
}

static void test_rpm_accepts_elrs_mavlink_passthrough()
{
    FakeHost host;
    ELRSCrsfCore core;
    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    // ELRS MAVLink RPM -> ArduPilot 0x500A: signed little-endian RPM/10 pairs.
    host.queueFrame(makeFrame(0x80, {0xF0, 0x0A, 0x50, 0xE8, 0x03, 0xD0, 0x07}));
    loopAtMs(core, host, 1500, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.722172f, core.telemetrySample(15, 1500).value);
    host.queueFrame(makeFrame(0x80, {0xF2, 2, 0x01, 0x50, 0, 0, 0, 0, 0x0A, 0x50, 0x18, 0xFC, 0, 0}));
    loopAtMs(core, host, 1600, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.722172f, core.telemetrySample(15, 1600).value); // -10,000 RPM.
    const std::vector<std::vector<uint8_t>> invalid = {
        {}, {0xF0}, {0xF0, 0x0A, 0x50, 0, 0, 0}, // Truncated single item.
        {0xF0, 0x01, 0x50, 0, 0, 0, 0}, // Another ArduPilot sensor.
        {0xF2, 0}, {0xF2, 2, 0x0A, 0x50, 0, 0, 0, 0}, // Invalid count/length.
        {0xF1, 0x0A, 0x50, 0, 0, 0, 0} // Status text, not RPM.
    };
    for(const auto &payload : invalid) host.queueFrame(makeFrame(0x80, payload));
    auto bad = makeFrame(0x80, {0xF0, 0x0A, 0x50, 0, 0, 0, 0}); bad.back() ^= 0xFF; host.queueFrame(bad);
    loopAtMs(core, host, 1700, 0);
    TEST_ASSERT_EQUAL_UINT32(100, core.telemetrySample(15, 1700).ageMs);
    host.queueFrame(makeFrame(0x80, {0xF0, 0x0A, 0x50, 0, 0, 0, 0}));
    loopAtMs(core, host, 1800, 0);
    TEST_ASSERT_EQUAL_FLOAT(0, core.telemetrySample(15, 1800).value);
}

static void test_rpm_presence_zero_reverse_and_independent_expiry()
{
    FakeHost host;
    ELRSCrsfCore core;
    TEST_ASSERT_TRUE(beginAt(core, host, defaultConfig(), 0));
    host.queueFrame(makeFrame(0x0C, {0, 0, 0, 0}));
    loopAtMs(core, host, 0, 0);
    TEST_ASSERT_TRUE(core.telemetrySample(15, 0).available);
    TEST_ASSERT_EQUAL_FLOAT(0, core.telemetrySample(15, 0).value);
    host.queueFrame(makeFrame(0x0C, {0, 0xFF, 0xD8, 0xF0})); // -10,000 RPM.
    loopAtMs(core, host, 200, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.722172f, core.telemetrySample(15, 200).value);
    host.queueFrame(makeFrame(0x0C, {0, 0, 0}));
    host.queueFrame(makeFrame(0x0C, {0, 0}));
    auto bad = makeFrame(0x0C, {0, 0, 0, 0}); bad.back() ^= 0xFF;
    host.queueFrame(bad);
    host.queueFrame(makeFrame(0x08, {0,123,0,30,0,0,0,0}));
    loopAtMs(core, host, 6100, 0);
    TEST_ASSERT_EQUAL_UINT32(5900, core.telemetrySample(15, 6100).ageMs);
    loopAtMs(core, host, 6200, 0);
    TEST_ASSERT_FALSE(core.telemetrySample(15, 6200).available);
    TEST_ASSERT_TRUE(core.telemetrySample(3, 6200).available);
    loopAtMs(core, host, 200, 0); // The same counter value after a full wrap cannot revive expired RPM.
    TEST_ASSERT_FALSE(core.telemetrySample(15, 200).available);
}

static void test_rpm_vehicle_parameters_preview_and_existing_formatting()
{
    FakeHost host;
    ELRSCrsfCore core;
    auto config = defaultConfig();
    config.vehicleConfig = {4, 10.0f, 101.6f, 15.0f};
    config.displayConfig = {ELRS_DISPLAY_RPM_SCALED_MPH, 255, 1, 0};
    config.speedDisplayUnits = ELRS_SPEED_UNITS_MPH;
    TEST_ASSERT_TRUE(beginAt(core, host, config, 0));
    host.queueFrame(makeFrame(0x0C, {0, 0, 0x27, 0x10}));
    loopAtMs(core, host, 1500, 0);
    TEST_ASSERT_EQUAL_STRING("89.2", host.displayText.c_str());
    auto preview = config.vehicleConfig;
    preview.rpmType = ELRS_RPM_SHAFT;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 11.899973f, core.telemetrySample(15, 1500, &preview).value);
    preview = config.vehicleConfig;
    preview.motorPoles = 8;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.974993f, core.telemetrySample(15, 1500, &preview).value);
    preview = config.vehicleConfig; preview.tireDiameterMm = 203.2f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 11.899973f, core.telemetrySample(15, 1500, &preview).value);
    preview = config.vehicleConfig; preview.gearRatio = 20;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.974993f, core.telemetrySample(15, 1500, &preview).value);
    for(int bad = 0; bad < 6; bad++) {
        preview = config.vehicleConfig;
        if(bad == 0) preview.motorPoles = 3;
        if(bad == 1) preview.gearRatio = 0;
        if(bad == 2) preview.tireDiameterMm = NAN;
        if(bad == 3) preview.scaleFactor = INFINITY;
        if(bad == 4) preview.tireDiameterMm = 0;
        if(bad == 5) preview.rpmType = 2;
        TEST_ASSERT_FALSE(core.telemetrySample(15, 1500, &preview).available);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.949986f, core.telemetrySample(15, 1500).value);
    host.queueFrame(makeFrame(0x0C, {0, 0x80, 0, 0})); // Minimum signed 24-bit RPM.
    loopAtMs(core, host, 1700, 0);
    TEST_ASSERT_EQUAL_STRING("HI", host.displayText.c_str());
}

int main(int argc, char **argv)
{
    (void)argc;
    // The production changes these catch are lost hysteresis, stale held state, and missing idle mapping.
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_firma_rpm_uses_existing_display_for_actual_and_scaled_mph);
    RUN_TEST(test_rpm_accepts_elrs_serial_sources_and_hott_update_interval);
    RUN_TEST(test_rpm_accepts_elrs_mavlink_passthrough);
    RUN_TEST(test_rpm_presence_zero_reverse_and_independent_expiry);
    RUN_TEST(test_rpm_vehicle_parameters_preview_and_existing_formatting);
    RUN_TEST(test_display_assignment_scaling_and_formatting);
    RUN_TEST(test_display_assignment_outage_recovery_units_and_overlays);
    RUN_TEST(test_telemetry_samples_decode_supported_numeric_sources);
    RUN_TEST(test_telemetry_samples_track_presence_freshness_and_invalid_fields);
    RUN_TEST(test_none_routing_preserves_multiple_disabled_inputs);
    RUN_TEST(test_none_switch_keeps_local_actions_and_other_channels_independent);
    RUN_TEST(test_none_gimbals_leave_free_channels_and_preserve_assigned_axes);
    RUN_TEST(test_none_frees_channels_for_other_input_types);
    RUN_TEST(test_all_none_configuration_survives_sanitizing_and_core_modes);
    RUN_TEST(test_local_actions_require_release_after_startup_and_suppression);
    RUN_TEST(test_local_actions_opt_in_and_ignore_failed_pack_reads);
    RUN_TEST(test_local_actions_require_release_after_calibration);
    RUN_TEST(test_prop_controls_keep_display_leds_and_calibration_while_transmitting);
    RUN_TEST(test_adc_fault_and_self_test_keep_remapped_throttle_neutral);
    RUN_TEST(test_hysteresis_returns_to_neutral_for_all_profile_directions);
    RUN_TEST(test_gimbal_curve_values_and_bounds);
    RUN_TEST(test_gimbal_curve_matches_original_integer_rounding);
    RUN_TEST(test_throttle_curves_preserve_idle_center_and_endpoints);
    RUN_TEST(test_gimbal_curves_keep_direct_safe_outputs_and_reseed_on_recovery);
    RUN_TEST(test_gimbal_curves_preserve_neutral_direction_and_deadbands);
    RUN_TEST(test_gimbal_curve_sanitization_preserves_profiles);
    RUN_TEST(test_gimbal_curves_are_independent_and_follow_channel_routing);
    RUN_TEST(test_switch_mapping_routes_each_input_without_leaking_old_channels);
    RUN_TEST(test_switch_mapping_preserves_self_test_and_buttonpack_fallback);
    RUN_TEST(test_switch_mapping_rejects_duplicates_and_invalid_channels);
    RUN_TEST(test_all_inputs_can_use_every_channel_without_collisions);
    RUN_TEST(test_throttle_idle_entry_bypasses_hysteresis);
    RUN_TEST(test_throttle_idle_band_preserves_center_deadband_endpoints);
    RUN_TEST(test_input_tolerances_are_adjustable_and_can_be_disabled);
    RUN_TEST(test_throttle_idle_band_preserves_narrow_profiles);
    RUN_TEST(test_hysteresis_holds_jitter_and_tracks_slow_motion);
    RUN_TEST(test_hysteresis_reaches_endpoints_and_reseeds_after_error);
    RUN_TEST(test_throttle_idle_band_handles_all_profile_directions);
    RUN_TEST(test_pending_calibration_capture_is_fresh_and_advances_once);
    RUN_TEST(test_pending_calibration_capture_error_timeout_and_cancellation);
    RUN_TEST(test_invalid_button_calibration_preserves_minimum_throttle);
    RUN_TEST(test_failed_button_capture_does_not_advance_calibration);
    RUN_TEST(test_valid_descending_button_calibration_is_saved);
    RUN_TEST(test_failed_button_calibration_save_reports_error);
    RUN_TEST(test_transport_sends_across_micros_rollover);
    RUN_TEST(test_malformed_parameter_reply_keeps_retries_active);
    RUN_TEST(test_service_reply_window_and_echo_cross_millis_rollover);
    RUN_TEST(test_delayed_outbound_frames_do_not_count_as_module_replies);
    RUN_TEST(test_oversized_parameter_restarts_from_chunk_zero_and_recovers);
    RUN_TEST(test_tx_deadline_advances_past_real_io_completion);
    RUN_TEST(test_late_tx_preserves_receive_opportunity);
    RUN_TEST(test_bounded_polling_jitter_keeps_healthy_tx_cadence);
    RUN_TEST(test_tx_pacing_retains_fractional_periods_and_rollover);
    RUN_TEST(test_costed_service_slots_keep_rc_and_real_reply_deadlines);
    RUN_TEST(test_pending_axes_keep_filter_and_rc_slots);
    RUN_TEST(test_new_identical_samples_and_separate_input_rollover);
    RUN_TEST(test_rc_frame_packing_and_driver_enable);
    RUN_TEST(test_transport_inversion_setting_is_passed_to_hal);
    RUN_TEST(test_transport_debug_suppresses_raw_frame_dumps_by_default);
    RUN_TEST(test_transport_raw_frame_dump_requires_explicit_opt_in);
    RUN_TEST(test_transport_raw_frame_dump_logs_non_rc_replies_only);
    RUN_TEST(test_ads1015_single_ended_config_uses_4v096_range);
    RUN_TEST(test_adc_debug_log_only_emits_on_axis_change);
    RUN_TEST(test_light_iir_filter_moves_quarter_step_toward_sample);
    RUN_TEST(test_light_iir_filter_converges_on_small_stable_changes);
    RUN_TEST(test_output_limits_scale_each_side_of_neutral);
    RUN_TEST(test_output_limits_validate_and_clamp);
    RUN_TEST(test_output_limits_follow_axes_through_reverse_and_routing);
    RUN_TEST(test_output_limits_preserve_safe_neutral_and_idle);
    RUN_TEST(test_output_limits_runtime_invalid_pairs_use_defaults);
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
    RUN_TEST(test_gimbal_routing_collision_uses_defaults);
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
    RUN_TEST(test_expired_overlays_do_not_return_after_millis_rollover);
    RUN_TEST(test_adc_overlay_expires_across_millis_rollover);
    RUN_TEST(test_comm_overlay_expires_across_millis_rollover);
    RUN_TEST(test_telemetry_received_at_millis_zero_is_fresh);
    RUN_TEST(test_self_test_expires_across_millis_rollover);
    RUN_TEST(test_module_settings_are_discovered_and_written);
    RUN_TEST(test_module_settings_retry_without_blocking_rc_output);
    RUN_TEST(test_unanswered_module_probes_stop_until_reconnect_or_save);
    RUN_TEST(test_module_settings_request_remaining_chunks_before_advancing_field);
    RUN_TEST(test_module_settings_retry_timed_out_chunk_before_scan_backoff);
    RUN_TEST(test_module_settings_ignore_duplicate_chunk_after_retry);
    RUN_TEST(test_module_start_and_save_delay_survive_millis_rollover);
    RUN_TEST(test_module_probe_zero_deadline_still_retries_after_500ms);
    RUN_TEST(test_module_parameter_zero_deadline_still_retries_after_500ms);
    RUN_TEST(test_module_write_delay_survives_zero_deadline);
    RUN_TEST(test_module_settings_preserve_chunks_when_retry_read_is_still_queued);
    RUN_TEST(test_module_settings_retry_probe_before_long_backoff);
    RUN_TEST(test_module_probe_does_not_lower_configured_500hz_runtime_rate);
    RUN_TEST(test_bootstrap_probe_waits_long_enough_for_late_first_module_reply);
    RUN_TEST(test_module_settings_apply_after_targets_are_found_without_full_scan);
    RUN_TEST(test_module_settings_do_not_write_packet_rate_target_or_change_transport_rate);
    RUN_TEST(test_module_ping_keeps_rc_running_while_waiting_for_settings);
    RUN_TEST(test_service_probe_drains_synchronous_loopback_echo_from_uart_buffer);
    RUN_TEST(test_service_probe_drain_preserves_following_module_reply_bytes);
    return UNITY_END();
}
