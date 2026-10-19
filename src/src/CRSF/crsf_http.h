#pragma once
#ifdef HAVE_CRSF
#include <WebServer.h>
#include "crsf_mqtt.h"

class CRSFWebServer : public WebServer {
public:
    explicit CRSFWebServer(void (*looper)()) : WebServer(80), _looper(looper) {}

    void handleClient() override
    {
        bool waiting = false;
        for(unsigned offset = 0; offset < 4; offset++) {
            const unsigned i = (_nextPending + offset) % 4;
            if(!_pending[i]) {
                _pending[i] = _server.available();
                if(!_pending[i]) continue;
                _pendingSince[i] = millis();
            }
            if(!_pending[i].available()) {
                if(!_pending[i].connected() || (uint32_t)(millis() - _pendingSince[i]) >= HTTP_MAX_DATA_WAIT)
                    _pending[i].stop();
                else waiting = true;
                continue;
            }
            _nextPending = (i + 1) % 4;
            _currentClient = _pending[i];
            _pending[i].stop();
            {
                CRSFClient input = cooperativeClient();
                input.setTimeout(HTTP_MAX_DATA_WAIT / 1000);
                if(_parseRequest(input)) {
                    _contentLength = CONTENT_LENGTH_NOT_SET;
                    _handleRequest();
                }
            }
            _currentClient.stop();
            _currentUpload.reset();
            _currentRaw.reset();
            return;
        }
        if(waiting) _looper();
    }

protected:
    size_t _currentClientWrite(const char *data, size_t size) override
    {
        CRSFClient output = cooperativeClient();
        const size_t written = output.write((const uint8_t *)data, size);
        if(written != size) _currentClient.stop();
        return written;
    }
    size_t _currentClientWrite_P(PGM_P data, size_t size) override
    {
        return _currentClientWrite(data, size);
    }

private:
    CRSFClient cooperativeClient()
    {
        CRSFClient client;
        client.WiFiClient::operator=(_currentClient);
        client.setCooperative(true);
        client.setLooper(_looper);
        return client;
    }
    // Keep speculative browser sockets while serving ready requests from the four-client backlog.
    WiFiClient _pending[4];
    uint32_t _pendingSince[4];
    unsigned _nextPending = 0;
    void (*_looper)();
};
#endif
