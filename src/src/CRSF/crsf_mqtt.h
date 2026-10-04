/*
 * -------------------------------------------------------------------
 * Remote Control
 * (C) 2026 Thomas Winischhofer (A10001986)
 * https://github.com/realA10001986/Remote
 * https://remote.out-a-ti.me
 *
 * CRSF cooperative MQTT transport
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

#ifndef _CRSF_MQTT_H
#define _CRSF_MQTT_H

#ifdef HAVE_CRSF

#include <WiFiClient.h>
#include <errno.h>
#include <fcntl.h>
#include <lwip/sockets.h>

class CRSFClient : public WiFiClient {
public:
    void setCooperative(bool enabled) { cooperative = enabled; }
    void setLooper(void (*callback)()) { looper = callback; }

    int connect(IPAddress ip, uint16_t port, int32_t timeout) override
    {
        if(!cooperative) return WiFiClient::connect(ip, port, timeout);
        stop();
        int socketfd = socket(AF_INET, SOCK_STREAM, 0);
        if(socketfd < 0) return 0;
        int flags = fcntl(socketfd, F_GETFL, 0);
        if(flags < 0 || fcntl(socketfd, F_SETFL, flags | O_NONBLOCK) < 0) {
            close(socketfd);
            return 0;
        }
        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = (uint32_t)ip;
        address.sin_port = htons(port);
        int result = lwip_connect(socketfd, (sockaddr *)&address, sizeof(address));
        if(result < 0 && errno == EINPROGRESS) {
            unsigned long start = millis();
            do {
                runLooper();
                fd_set writable;
                FD_ZERO(&writable);
                FD_SET(socketfd, &writable);
                timeval wait = {0, 1000};
                result = select(socketfd + 1, NULL, &writable, NULL, &wait);
            } while(result == 0 && millis() - start < 5000);
            if(result > 0) {
                int error = 0;
                socklen_t length = sizeof(error);
                result = getsockopt(socketfd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0 ? 0 : -1;
            } else {
                result = -1;
            }
        }
        timeval socketTimeout = {5, 0};
        if(result != 0 || setsockopt(socketfd, SOL_SOCKET, SO_SNDTIMEO, &socketTimeout, sizeof(socketTimeout)) < 0 ||
           setsockopt(socketfd, SOL_SOCKET, SO_RCVTIMEO, &socketTimeout, sizeof(socketTimeout)) < 0 ||
           fcntl(socketfd, F_SETFL, flags) < 0) {
            close(socketfd);
            return 0;
        }
        WiFiClient::operator=(WiFiClient(socketfd));
        return 1;
    }

    int connect(const char *host, uint16_t port, int32_t timeout) override
    {
        return WiFiClient::connect(host, port, timeout);
    }

    int available() override
    {
        runLooper();
        return WiFiClient::available();
    }

    size_t write(const uint8_t *data, size_t size) override
    {
        if(!cooperative) return WiFiClient::write(data, size);
        size_t written = 0;
        unsigned long start = millis();
        while(written < size && millis() - start < 5000) {
            runLooper();
            int count = send(fd(), data + written, size - written, MSG_DONTWAIT);
            if(count > 0) {
                written += count;
            } else if(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                delay(1);
            } else {
                break;
            }
        }
        if(written == size) return size;
        stop(); // A partial MQTT packet cannot be retried on the same byte stream.
        return 0;
    }

private:
    void runLooper()
    {
        if(!cooperative || !looper) return;
        looper();
    }

    bool cooperative = false;
    void (*looper)() = NULL;
};

#endif
#endif
