#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_arduino_version.h>
#include <sdkconfig.h>

// Forwarding is an SDK build option: defining these macros in sketch flags
// cannot add NAPT to precompiled SDK libraries that were built without it.
#if !defined(ARDUINO_ARCH_ESP32) || ESP_ARDUINO_VERSION_MAJOR < 3
#error "Gateway mode requires Arduino ESP32 3.x with IPv4 forwarding and NAPT enabled"
#endif
#if !defined(CONFIG_LWIP_IP_FORWARD) || !CONFIG_LWIP_IP_FORWARD || \
    !defined(CONFIG_LWIP_IPV4_NAPT) || !CONFIG_LWIP_IPV4_NAPT
#error "Gateway mode requires SDK CONFIG_LWIP_IP_FORWARD and CONFIG_LWIP_IPV4_NAPT"
#endif

// DHCP must advertise the ESP AP address as DNS (NetworkConfiguration owns
// softAPConfig). NAPT forwards IPv4 Internet traffic; DNS policy lives in the
// selective proxy. This does not filter arbitrary ports, DoH or hardcoded IPs.
class GatewayRouting {
    bool enabled = false;
    bool available = false;
    bool attempted = false;
    bool previousOnline = false;
    uint32_t address = 0;
    uint32_t previousAddress = 0;
    uint32_t lastAttempt = 0;

public:
    bool ready() const { return available; }

    void poll(bool online, IPAddress apIp)
    {
        const uint32_t currentAddress = uint32_t(apIp);
        const bool started = WiFi.AP.started();
        const bool wanted = online && currentAddress && started;
        if (wanted != previousOnline || currentAddress != previousAddress) {
            attempted = false;
            previousOnline = wanted;
            previousAddress = currentAddress;
        }
        available = enabled && wanted && address == currentAddress;
        if (!started) {
            // A stopped AP cannot be controlled. Invalidate readiness and
            // retain the cleanup obligation in case its netif is reused.
            address = 0;
            return;
        }
        if (available || (!wanted && !enabled)) return;
        const uint32_t currentTime = millis();
        if (attempted && uint32_t(currentTime - lastAttempt) < 1000) return;
        attempted = true;
        lastAttempt = currentTime;

        // IDF disable is idempotent on an UP netif, including a recreated
        // AP whose NAPT state was cleared before this poll. A down netif
        // can fail transiently; keep retrying until the AP is up.
        if (enabled) {
            if (!WiFi.AP.enableNAPT(false)) return;
            enabled = false;
            address = 0;
        }
        if (!wanted) return;
        if (WiFi.AP.enableNAPT(true)) {
            address = currentAddress;
            enabled = available = true;
        }
    }
};
