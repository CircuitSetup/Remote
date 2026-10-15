#pragma once
#ifdef HAVE_CRSF
#include <WebServer.h>
#include "crsf_mqtt.h"

class CRSFWebServer : public WebServer {
public:
    explicit CRSFWebServer(void (*looper)()) : WebServer(80), _looper(looper) {}

    void handleClient() override
    {
        if(_currentStatus == HC_NONE) {
            _currentClient = _server.available();
            if(!_currentClient) return;
            _currentStatus = HC_WAIT_READ;
            _statusChange = millis();
        }
        if(_currentClient.connected() && !_currentClient.available()) {
            // Empty browser preconnections must not occupy the only HTTP slot for five seconds.
            if((uint32_t)(millis() - _statusChange) < 100) {
                _looper();
                return;
            }
        } else if(_currentClient.connected()) {
            CRSFClient input = cooperativeClient();
            input.setTimeout(HTTP_MAX_DATA_WAIT / 1000);
            if(_parseRequest(input)) {
                _contentLength = CONTENT_LENGTH_NOT_SET;
                _handleRequest();
            }
        }
        _currentClient = WiFiClient();
        _currentStatus = HC_NONE;
        _currentUpload.reset();
        _currentRaw.reset();
    }

protected:
    size_t _currentClientWrite(const char *data, size_t size) override
    {
        CRSFClient output = cooperativeClient();
        return output.write((const uint8_t *)data, size);
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
    void (*_looper)();
};
#endif
