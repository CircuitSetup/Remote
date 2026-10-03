"""Compile the real MQTT connect/send and WiFi wait callbacks with controlled sockets."""
from check_crsf_adc import ROOT, compile_and_run


def function(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature + '\n{')
    return source[start:source.index('\n}', start) + 2] + '\n'


fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <vector>
unsigned long now = 0, lastService = 0, maxGap = 0;
int services = 0, audioCalls = 0;
int wifiCalls = 0;
unsigned long renNow1 = 0, renNow2 = 0;
bool opModeCRSF = true, audioInitDone = true;
unsigned long millis() { return now; }
void delay(unsigned long ms) { now += ms; }
void audio_loop() { audioCalls++; }
void wifi_loop() { wifiCalls++; }
void showNumber(int) {}
void serviceCRSF(bool actions) {
    assert(!actions);
    if(now - lastService > maxGap) maxGap = now - lastService;
    lastService = now; services++;
}
enum { WM_LP_PREHTTPSEND, WM_LP_NONE, WM_LP_POSTHTTPSEND };
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
std::vector<uint8_t> sent;
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
int send(int, const void *data, size_t size, int flags) {
    assert(flags == MSG_DONTWAIT); sendCalls++;
    if(scenario == SEND_ERROR) { errno = ECONNRESET; return -1; }
    if(scenario == SHORT_STALL && sendCalls > 1) { errno = EAGAIN; return -1; }
    if(sendCalls == 2) { errno = EAGAIN; return -1; }
    size_t count = size > 2 ? 2 : size;
    const uint8_t *bytes = (const uint8_t *)data;
    sent.insert(sent.end(), bytes, bytes + count);
    return (int)count;
}
struct IPAddress { operator uint32_t() const { return 0x0100007f; } };
struct WiFiClient {
    int descriptor = -1;
    WiFiClient() {}
    explicit WiFiClient(int fd) : descriptor(fd) { adopted++; }
    bool connected() { return descriptor >= 0; }
    int connect(IPAddress, uint16_t, int) { nativeConnects++; now += 5000; return 0; }
    int connect(const char *, uint16_t, int) { nativeConnects++; now += 5000; return 0; }
    size_t write(const uint8_t *, size_t size) { nativeWrites++; now += 1000; return size; }
    int available() { return 0; }
    int read() { return 0; }
    int fd() { return descriptor; }
    void stop() { if(descriptor >= 0) closed++; descriptor = -1; }
};
#define MQTT_CONNECTING -5
#define MQTT_CONNECT_FAILED -2
#define MQTTCONNECT 16
#define MQTTPUBLISH 48
#define CHECK_STRING_LENGTH(l,s) if(l+2+strnlen(s, this->bufferSize)>this->bufferSize){_client->stop();return false;}
uint8_t mytt5_connect_props[8] = {};
class PubSubClient {
public:
    WiFiClient client;
    WiFiClient *_client = &client;
    bool cooperative = true, _v3 = true, inLooper = false, isConnected = false;
    const char *domain = nullptr;
    IPAddress ip;
    uint16_t port = 1883, nextMsgId = 0, keepAlive = 15, bufferSize = 512, mqtt_max_header_size = 5;
    unsigned long socketTimeout = 10, lastInActivity = 0, lastOutActivity = 0;
    int _state = -1, mqtt_version_header_length = 7;
    uint8_t storage[512] = {}, *buffer = storage, _phdr[7] = {};
    char _clientID[24] = "remote";
    void (*looper)();
    bool connected() { return isConnected; }
    uint16_t writeString(const char *text, uint8_t *buf, uint16_t pos) {
        size_t size = strlen(text); buf[pos++] = 0; buf[pos++] = size;
        memcpy(buf + pos, text, size); return pos + size;
    }
    uint8_t buildHeader(uint8_t, uint8_t *, uint16_t) { return 2; }
    bool connect(const char *user, const char *pass, bool cleanSession);
    bool connectTCP();
    size_t writeClient(const uint8_t *data, size_t size);
    bool write(uint8_t header, uint8_t *buf, uint16_t length);
    bool readByte(uint8_t *result);
    bool publish(const char* topic, const uint8_t* payload, unsigned int plength, bool retained = false);
    void runLooper();
};
void reset(Scenario next) {
    now = lastService = maxGap = 0; services = audioCalls = 0;
    closed = adopted = nativeConnects = nativeWrites = sendCalls = 0; sent.clear(); scenario = next;
}
'''
production = function('src/mqtt.cpp', 'bool PubSubClient::connect(const char *user, const char *pass, bool cleanSession)')
production += function('src/mqtt.cpp', 'bool PubSubClient::write(uint8_t header, uint8_t *buf, uint16_t length)')
production += function('src/mqtt.cpp', 'bool PubSubClient::readByte(uint8_t *result)')
production += function('src/mqtt.cpp', 'bool PubSubClient::publish(const char* topic, const uint8_t* payload, unsigned int plength, bool retained)')
for signature in ['bool PubSubClient::connectTCP()', 'size_t PubSubClient::writeClient(const uint8_t *data, size_t size)', 'void PubSubClient::runLooper()']:
    if signature + '\n{' in (ROOT / 'src/mqtt.cpp').read_text():
        production += function('src/mqtt.cpp', signature)
production += function('src/remote_wifi.cpp', 'static void wifiDelayReplacement(unsigned int mydel)')
production += function('src/remote_wifi.cpp', 'void gpCallback(int reason)')
production += function('src/remote_wifi.cpp', 'static void mqttLooper()')
production += function('src/remote_audio.cpp', 'static void mpren_looper(bool isSetup, bool checking, int fileNum)')
cases = r'''
PubSubClient *nestedClient;
bool nestedInside = false, nestedAccepted = false;
void nestedPublisher() {
    mqttLooper();
    if(!nestedInside) {
        nestedInside = true;
        nestedAccepted = nestedClient->publish("nested", (const uint8_t *)"BAD", 3);
        nestedInside = false;
    }
}
int main() {
    reset(TIMEOUT);
    PubSubClient mqtt; mqtt.looper = mqttLooper;
    assert(!mqtt.connect(nullptr, nullptr, true));
    assert(now >= 5000 && now <= 5001 && services >= 4999 && maxGap <= 1);
    assert(nativeConnects == 0 && closed == 1 && adopted == 0 && mqtt._state == MQTT_CONNECT_FAILED);
    reset(OK);
    assert(mqtt.connect(nullptr, nullptr, true));
    assert(adopted == 1 && closed == 0 && nativeWrites == 0 && services > 0 && maxGap <= 1);
    assert(mqtt._state == MQTT_CONNECTING && !sent.empty());
    mqtt.client.stop();
    for(Scenario failure : {REFUSED, SELECT_ERROR, SOCKET_ERROR, OPEN_ERROR, FLAGS_ERROR}) {
        reset(failure); assert(!mqtt.connect(nullptr, nullptr, true));
        assert(adopted == 0 && closed == (failure == OPEN_ERROR ? 0 : 1));
    }
    reset(SHORT_STALL);
    mqtt.client.descriptor = 7;
    uint8_t bytes[] = {1,2,3,4,5};
    assert(!mqtt.write(16, bytes, 3));
    assert(closed == 1 && !mqtt.client.connected() && sent.size() == 2 && maxGap <= 1 && services >= 4999);
    reset(SEND_ERROR);
    assert(!mqtt.connect(nullptr, nullptr, true));
    assert(closed == 1 && !mqtt.client.connected() && mqtt._state == MQTT_CONNECT_FAILED);
    reset(TIMEOUT); uint8_t byte;
    assert(!mqtt.readByte(&byte));
    assert(services >= 9 && maxGap <= 1);
    reset(OK); wifiDelayReplacement(100);
    assert(now == 100 && services >= 100 && maxGap <= 1);
    audioInitDone = false; reset(OK); wifiDelayReplacement(20);
    assert(now == 20 && services >= 20 && maxGap <= 1);
    reset(OK); gpCallback(WM_LP_NONE); assert(services == 1 && audioCalls == 0);
    reset(OK); mqtt.isConnected = true; mqtt.client.descriptor = 7;
    const uint8_t original[] = {9,8,7,6,5,4,3,2,1};
    memcpy(mqtt.buffer, original, sizeof(original));
    nestedClient = &mqtt; mqtt.looper = nestedPublisher;
    assert(mqtt.writeClient(mqtt.buffer, sizeof(original)) == sizeof(original));
    assert(!nestedAccepted && sent == std::vector<uint8_t>(original, original + sizeof(original)));
    mqtt.looper = mqttLooper; mqtt.isConnected = false; mqtt.client.stop();
    reset(OK); now = 100; lastService = now;
    mpren_looper(false, true, 0);
    assert(services == 1 && wifiCalls == 0); // Each file pass serves radio before the slower WiFi interval.
    now = 251; mpren_looper(false, true, 0);
    assert(services == 2 && wifiCalls == 1 && now == 252);
    mqtt.cooperative = false; reset(OK);
    assert(!mqtt.connect(nullptr, nullptr, true) && nativeConnects == 1 && services == 0);
    opModeCRSF = false; reset(OK); wifiDelayReplacement(20);
    assert(now == 20 && services == 0);
    puts("MQTT timeout, partial-send cleanup, failed CONNECT and WiFi waits keep CRSF serviced every millisecond");
}
'''
compile_and_run(fixture + production + cases, [])
