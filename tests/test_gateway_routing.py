"""Compile and exercise the NAPT lifecycle without an ESP32 or network access."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GatewayRoutingTests(unittest.TestCase):
    def test_napt_lifecycle_and_required_sdk_flags(self):
        compiler = os.environ.get("CXX", shutil.which("c++"))
        if not compiler:
            self.skipTest("A C++11 compiler is required")
        with tempfile.TemporaryDirectory(prefix="gateway-test-") as directory:
            directory = Path(directory)
            (directory / "Arduino.h").write_text('''#pragma once
#include <cstdint>
extern uint32_t mockTime;
inline uint32_t millis() { return mockTime; }
struct IPAddress {
    uint32_t value;
    explicit IPAddress(uint32_t v): value(v) {}
    operator uint32_t() const { return value; }
};
''')
            (directory / "WiFi.h").write_text('''#pragma once
struct MockAP {
    bool running = true, succeeds = true, napt = false;
    int enables = 0, disables = 0;
    bool started() const { return running; }
    bool enableNAPT(bool enable) {
        if (enable) ++enables; else ++disables;
        if (!running || !succeeds) return false;
        napt = enable;  // Like IDF, disabling an already inactive UP netif succeeds.
        return true;
    }
};
struct MockWiFi { MockAP AP; };
extern MockWiFi WiFi;
''')
            (directory / "esp_arduino_version.h").write_text('#define ESP_ARDUINO_VERSION_MAJOR 3\n')
            (directory / "sdkconfig.h").write_text('')
            harness = directory / "test.cpp"
            harness.write_text('''#include "gateway-routing.h"
#include <cassert>
uint32_t mockTime = 0;
MockWiFi WiFi;
int main() {
    GatewayRouting routing;
    routing.poll(false, IPAddress(1));
    assert(!routing.ready() && WiFi.AP.enables == 0);
    routing.poll(true, IPAddress(1));
    assert(routing.ready() && WiFi.AP.enables == 1);
    routing.poll(true, IPAddress(1));
    assert(WiFi.AP.enables == 1);
    // Collision handling can recreate the AP between polls, clearing NAPT.
    // The helper still believes the old IP is enabled; disable must be idempotent.
    WiFi.AP.napt = false;
    routing.poll(true, IPAddress(2));
    assert(WiFi.AP.napt);
    assert(routing.ready() && WiFi.AP.disables == 1 && WiFi.AP.enables == 2);
    routing.poll(false, IPAddress(2));
    assert(!routing.ready() && WiFi.AP.disables == 2 && !WiFi.AP.napt);
    WiFi.AP.succeeds = false;
    routing.poll(true, IPAddress(2));
    assert(!routing.ready() && WiFi.AP.enables == 3);
    mockTime = 999;
    routing.poll(true, IPAddress(2));
    assert(WiFi.AP.enables == 3);
    mockTime = 1000;
    WiFi.AP.succeeds = true;
    routing.poll(true, IPAddress(2));
    assert(routing.ready() && WiFi.AP.enables == 4);
    WiFi.AP.running = false;
    routing.poll(true, IPAddress(2));
    assert(!routing.ready());
    WiFi.AP.running = true;
    routing.poll(true, IPAddress(2));
    assert(routing.ready() && WiFi.AP.enables == 5 && WiFi.AP.disables == 3);
    WiFi.AP.succeeds = false;
    routing.poll(false, IPAddress(2));
    assert(!routing.ready() && WiFi.AP.disables == 4);
    routing.poll(false, IPAddress(2));
    assert(WiFi.AP.disables == 4);
    mockTime += 1000;
    WiFi.AP.succeeds = true;
    routing.poll(false, IPAddress(2));
    assert(!routing.ready() && WiFi.AP.disables == 5);
    routing.poll(true, IPAddress(0));
    assert(!routing.ready() && WiFi.AP.enables == 5);
    // If a stopped AP returns while offline, clear any preserved NAPT bit.
    WiFi = MockWiFi();
    GatewayRouting offlineRestart;
    offlineRestart.poll(true, IPAddress(3));
    WiFi.AP.running = false;
    offlineRestart.poll(false, IPAddress(3));
    WiFi.AP.running = true;
    offlineRestart.poll(false, IPAddress(3));
    assert(!offlineRestart.ready() && !WiFi.AP.napt && WiFi.AP.disables == 1);
}
''')
            command = [compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror',
                       '-fsanitize=address,undefined', '-DARDUINO_ARCH_ESP32',
                       '-I' + str(directory), '-I' + str(ROOT), str(harness),
                       '-o', str(directory / 'test')]
            flags = ['-DCONFIG_LWIP_IP_FORWARD=1', '-DCONFIG_LWIP_IPV4_NAPT=1']
            subprocess.run(command + flags, check=True, capture_output=True)
            subprocess.run([str(directory / 'test')], check=True)
            for missing_flags in [[], flags[:1], flags[1:],
                                  ['-DCONFIG_LWIP_IP_FORWARD=0', flags[1]]]:
                result = subprocess.run(command + missing_flags, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Gateway mode requires SDK', result.stderr)


if __name__ == '__main__':
    unittest.main()
