#pragma once
#include "Arduino.h"
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_AUTH_OPEN 0
#define WIFI_AP 2
#define WIFI_AP_STA 3
#define WL_CONNECTED 3
#define WL_CONNECT_FAILED 4
class IPAddress {
    uint32_t address = 0;
public:
    IPAddress() = default;
    explicit IPAddress(uint32_t raw): address(raw) {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d): address(a | (b<<8) | (c<<16) | (uint32_t(d)<<24)) {}
    operator uint32_t() const { return address; }
    String toString() const {
        char out[20]; snprintf(out, sizeof(out), "%u.%u.%u.%u", address&255,(address>>8)&255,(address>>16)&255,(address>>24)&255); return out;
    }
};
struct WiFiMock {
    int modeValue = 0, state = 0, connections = 0, apStarts = 0;
    IPAddress local, ap, subnet = IPAddress(255,255,255,0), upstream = IPAddress(1,1,1,1), gateway;
    String apName, apPassword, name, password;
    void setMinSecurity(int) {}
    void begin(const char *s, const char *p) { name=s; password=p; ++connections; }
    void persistent(bool) {}
    void setAutoReconnect(bool) {}
    void mode(int m) { modeValue=m; }
    IPAddress advertisedDns;
    bool softAPConfig(IPAddress a, IPAddress, IPAddress, IPAddress = IPAddress(), IPAddress dns = IPAddress()) { ap=a; advertisedDns=dns; return true; }
    bool softAP(const char *s, const char *p) { apName=s; apPassword=p; ++apStarts; return true; }
    void softAPdisconnect(bool) {}
    void disconnect(bool=false, bool=false) { state=0; }
    IPAddress softAPIP() const { return ap; }
    IPAddress localIP() const { return local; }
    IPAddress subnetMask() const { return subnet; }
    IPAddress dnsIP(int=0) const { return upstream; }
    IPAddress gatewayIP() const { return gateway; }
    int status() const { return state; }
};
inline WiFiMock WiFi;
