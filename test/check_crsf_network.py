"""Run with python test/check_crsf_network.py; exercise CRSF's cooperative MQTT client."""
from check_crsf_adc import ROOT, compile_and_run


def function(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature + '\n{')
    return source[start:source.index('\n}', start) + 2] + '\n'


header = (ROOT / 'src/src/CRSF/crsf_mqtt.h').read_text()
client = header[header.index('class CRSFClient'):header.rindex('\n};') + 3]
mqtt = (ROOT / 'src/mqtt.cpp').read_text()
for removed in ('connectTCP', 'writeClient', 'runLooper', 'setCooperative', 'cooperative', 'inLooper'):
    assert removed not in mqtt, f'{removed} belongs in crsf_mqtt.h'

fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cerrno>
unsigned long now = 0, lastService = 0, maxGap = 0;
int services = 0, audioCalls = 0;
unsigned long millis() { return now; }
void delay(unsigned long ms) { now += ms; }
void audio_loop() { audioCalls++; }
void serviceCRSF(bool actions) {
    assert(!actions);
    if(now - lastService > maxGap) maxGap = now - lastService;
    lastService = now; services++;
}
#define fd_set TestFdSet
#define timeval TestTimeval
#define sockaddr TestSockaddr
#define sockaddr_in TestSockaddrIn
#define socklen_t unsigned
struct TestFdSet {};
struct TestTimeval { long tv_sec, tv_usec; };
struct TestSockaddr {};
struct TestSockaddrIn { int sin_family; struct { uint32_t s_addr; } sin_addr; uint16_t sin_port; };
#define FD_ZERO(x) ((void)0)
#define FD_SET(x,y) ((void)0)
#define AF_INET 2
#define SOCK_STREAM 1
#define SOL_SOCKET 1
#define SO_ERROR 2
#define SO_SNDTIMEO 3
#define SO_RCVTIMEO 4
#define F_GETFL 1
#define F_SETFL 2
#define O_NONBLOCK 4
#define MSG_DONTWAIT 1
uint16_t htons(uint16_t x) { return x; }
enum Scenario { TIMEOUT, OK, REFUSED, SELECT_ERROR, SOCKET_ERROR, OPEN_ERROR, FLAGS_ERROR, SEND_ERROR, SHORT_STALL } scenario;
int closed = 0, adopted = 0, nativeConnects = 0, nativeWrites = 0, sendCalls = 0;
int socket(int, int, int) { return scenario == OPEN_ERROR ? -1 : 7; }
int fcntl(int, int, int = 0) { return scenario == FLAGS_ERROR ? -1 : 0; }
int lwip_connect(int, const sockaddr *, unsigned) { errno = EINPROGRESS; return -1; }
int select(int, fd_set *, fd_set *, fd_set *, timeval *wait) {
    assert(wait->tv_sec == 0 && wait->tv_usec <= 1000);
    now += wait->tv_usec / 1000;
    if(scenario == SELECT_ERROR) return -1;
    return scenario == TIMEOUT ? 0 : 1;
}
int getsockopt(int, int, int, void *value, socklen_t *) {
    *(int *)value = scenario == REFUSED ? ECONNREFUSED : 0;
    return scenario == SOCKET_ERROR ? -1 : 0;
}
int setsockopt(int, int, int, const void *, unsigned) { return 0; }
int close(int) { closed++; return 0; }
int send(int, const void *, size_t size, int flags) {
    assert(flags == MSG_DONTWAIT); sendCalls++;
    if(scenario == SEND_ERROR) { errno = ECONNRESET; return -1; }
    if(scenario == SHORT_STALL && sendCalls > 1) { errno = EAGAIN; return -1; }
    if(sendCalls == 2) { errno = EAGAIN; return -1; }
    return (int)(size > 2 ? 2 : size);
}
struct IPAddress { operator uint32_t() const { return 0x0100007f; } };
struct WiFiClient {
    int descriptor = -1;
    WiFiClient() {}
    explicit WiFiClient(int fd) : descriptor(fd) { adopted++; }
    virtual ~WiFiClient() {}
    virtual int connect(IPAddress, uint16_t, int32_t) { nativeConnects++; now += 5000; return 0; }
    virtual int connect(const char *, uint16_t, int32_t) { nativeConnects++; now += 5000; return 0; }
    virtual size_t write(const uint8_t *, size_t size) { nativeWrites++; now += 1000; return size; }
    virtual int available() { return 0; }
    int fd() const { return descriptor; }
    void stop() { if(descriptor >= 0) closed++; descriptor = -1; }
};
void reset(Scenario next) {
    now = lastService = maxGap = 0; services = audioCalls = 0;
    closed = adopted = nativeConnects = nativeWrites = sendCalls = 0; scenario = next;
}
'''

cases = r'''
void mqttLooper() {
    serviceCRSF(false);
    audio_loop();
}
int main() {
    CRSFClient mqtt; mqtt.setCooperative(true); mqtt.setLooper(mqttLooper);
    reset(TIMEOUT);
    assert(!mqtt.connect(IPAddress(), 1883, 5000));
    assert(now >= 5000 && now <= 5001 && services >= 4999 && maxGap <= 1 && closed == 1);
    reset(OK);
    assert(mqtt.connect(IPAddress(), 1883, 5000));
    assert(adopted == 1 && services > 0 && maxGap <= 1);
    reset(SHORT_STALL);
    uint8_t bytes[] = {1, 2, 3, 4, 5};
    assert(mqtt.write(bytes, sizeof(bytes)) == 0);
    assert(closed == 1 && services >= 4999 && maxGap <= 1);
    reset(OK); mqtt.available();
    assert(services == 1 && audioCalls == 1);
    mqtt.setCooperative(false); reset(OK);
    assert(!mqtt.connect(IPAddress(), 1883, 5000) && nativeConnects == 1 && services == 0);
}
'''

compile_and_run(fixture + client + cases, [])

guard_fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
unsigned long now = 0;
unsigned long millis() { return now; }
void delay(unsigned long ms) { now += ms; }
bool mqttPublish(const char *, const char *, unsigned int);
void audio_loop();
void serviceCRSF(bool actions) { assert(!actions); }
bool opModeCRSF = true, useMQTT = true;
struct WiFiClient { int available() { return 0; } int read() { return -1; } } client;
class PubSubClient {
public:
    WiFiClient *_client = &client;
    unsigned long socketTimeout = 25;
    void (*looper)() = nullptr;
    uint8_t packet[4] = {9, 8, 7, 6};
    bool publish(const char *, uint8_t *, unsigned int, bool) { packet[0] = 0; return true; }
    bool readByte(uint8_t *result);
};
PubSubClient mqttClient;
static bool mqttLooperActive = false;
bool nestedAccepted = false;
'''
guard_case = r'''
void audio_loop() { nestedAccepted = mqttPublish("nested", "BAD", 3); }
int main() {
    mqttClient.looper = mqttLooper;
    uint8_t byte = 0;
    assert(!mqttClient.readByte(&byte));
    assert(!nestedAccepted && mqttClient.packet[0] == 9);
}
'''
compile_and_run(guard_fixture + function('src/remote_wifi.cpp', 'bool mqttPublish(const char *topic, const char *pl, unsigned int len)') +
                function('src/remote_wifi.cpp', 'static void mqttLooper()') +
                function('src/mqtt.cpp', 'bool PubSubClient::readByte(uint8_t *result)') + guard_case, [])
print('CRSF MQTT client services the radio every millisecond and closes partial packets after five seconds')
print('Blocked MQTT reads reject nested audio publishes before the packet buffer can change')
