#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "esp_random.h"
#include "esp_arduino_version.h"

class NetworkConfig {
    struct Credentials {
        uint32_t version = 1;
        char ssid[33] = {};
        char password[65] = {};
    } credentials;
    struct AccessPoint {
        char ssid[33] = {};
        char password[64] = {};
    };
    Preferences preferences;
    bool storageReady = false;
    bool setupMode = true;
    bool online = false;
    uint32_t attemptStarted = 0, lastCheck = 0, restartAt = 0;
    uint32_t buttonStarted = 0;
    bool buttonHeld = false;
    unsigned resetSeconds = 0;
    String token;
    IPAddress previousIp, previousDns, previousSubnet;
    IPAddress accessPointIp = IPAddress(192, 168, 4, 1);
    static constexpr uint8_t bootPin =
#if defined(CONFIG_IDF_TARGET_ESP32C3)
        9;
#else
        0;
#endif
    void startAccessPoint()
    {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        // Core 3 takes lease-start as argument four and advertised DNS as five.
        WiFi.softAPConfig(accessPointIp, accessPointIp, apSubnet(), IPAddress(0,0,0,0), accessPointIp);
#else
        WiFi.softAPConfig(accessPointIp, accessPointIp, apSubnet());
#endif
        if (!WiFi.softAP(apName.c_str(), apKey.c_str())) Serial.println("[WIFI] Access point failed");
    }
    bool avoidSubnetCollision()
    {
        // A /24 AP must not overlap the router's subnet: clients otherwise try
        // ARP instead of sending upstream packets to the gateway.
        const uint32_t station = uint32_t(WiFi.localIP());
        const uint32_t stationMask = uint32_t(WiFi.subnetMask());
        const uint32_t commonMask = stationMask & uint32_t(apSubnet());
        if ((uint32_t(accessPointIp) & commonMask) != (station & commonMask)) return false;
        const IPAddress candidates[] = {
            IPAddress(192,168,4,1), IPAddress(192,168,5,1),
            IPAddress(172,31,254,1), IPAddress(10,254,254,1)
        };
        for (const IPAddress &candidate : candidates) {
            if ((uint32_t(candidate) & commonMask) == (station & commonMask)) continue;
            accessPointIp = candidate;
            // Recreate the AP so already-associated clients renew DHCP leases.
            WiFi.softAPdisconnect(false);
            startAccessPoint();
            Serial.printf("[WIFI] AP subnet changed to %s to avoid router overlap\n", apIp().toString().c_str());
            return true;
        }
        Serial.println("[WIFI] Router subnet overlaps all private AP ranges");
        return false;
    }
    String apName, apKey;

    static bool printable(const String &value)
    {
        for (size_t i = 0; i < value.length(); ++i)
            if (uint8_t(value[i]) < 32 || uint8_t(value[i]) > 126) return false;
        return true;
    }
    static String escape(String value)
    {
        value.replace("&", "&amp;"); value.replace("<", "&lt;");
        value.replace(">", "&gt;"); value.replace("\"", "&quot;");
        value.replace("'", "&#39;");
        return value;
    }
    void settings(WebServer &server, bool saveSettings)
    {
        server.sendHeader("Cache-Control", "no-store");
        if (!server.authenticate("admin", apKey.c_str())) { server.requestAuthentication(); return; }
        if (saveSettings) {
            if (server.arg("token") != token) { server.send(403, "text/plain", "Ricarica la pagina e riprova."); return; }
            String name = server.arg("ap_ssid"), key = server.arg("ap_password");
            if (key.length() == 0) key = apKey;
            if (name.length() == 0 || name.length() > 32 || !printable(name) ||
                key.length() < 8 || key.length() > 63 || !printable(key)) {
                server.send(400, "text/plain", "Nome: 1-32 caratteri; password: 8-63. Usa lettere, numeri, spazi e simboli senza accenti."); return;
            }
            AccessPoint saved;
            name.toCharArray(saved.ssid, sizeof(saved.ssid));
            key.toCharArray(saved.password, sizeof(saved.password));
            if (!storageReady || preferences.putBytes("access-point", &saved, sizeof(saved)) != sizeof(saved)) {
                server.send(500, "text/plain", "Salvataggio fallito."); return;
            }
            server.send(200, "text/html; charset=utf-8",
                "<!doctype html><html lang='it'><meta name='viewport' content='width=device-width'><title>Impostazioni salvate</title>"
                "<h1>Nome e password salvati</h1><p>La scheda si riavvia. Le nuove impostazioni saranno visibili sull'OLED.</p>"
                "<p>La rete della scheda resta attiva anche quando e collegata al Wi-Fi di casa. BOOT premuto 5 secondi cancella quella configurazione e conserva il nome e la password appena scelti.</p></html>");
            restartAt = millis() + 1500;
            return;
        }
        String html = F("<!doctype html><html lang='it'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Rete della scheda</title><style>body{font:16px system-ui;background:#101725;color:#edf4ff;padding:24px}main{max-width:420px;margin:4vh auto}"
            "label{display:block;margin:20px 0 8px}input,button{box-sizing:border-box;width:100%;font:inherit;padding:14px;border-radius:8px}input{background:#1e2a40;color:white;border:1px solid #52637f}"
            "button{margin-top:24px;background:#a5d6ff;color:#101725;border:0}p{line-height:1.5}</style></head><body><main><h1>Rete della scheda</h1>"
            "<p>Questa rete resta attiva per la configurazione e per collegare i dispositivi al filtro DNS. Nome e password restano visibili sull'OLED.</p>"
            "<form method='post' action='/settings'><input type='hidden' name='token' value='");
        String addresses = String("<p>IP rete della scheda (AP): <b>") + apIp().toString() +
            "</b><br>IP rete di casa (STA): <b>" +
            (online ? WiFi.localIP().toString() : String("non connessa")) +
            "</b></p><form method='post' action='/settings'>";
        html.replace("<form method='post' action='/settings'>", addresses.c_str());
        html += token;
        html += F("'><label for='ap_ssid'>Nome Wi-Fi della scheda</label><input id='ap_ssid' name='ap_ssid' maxlength='32' required value='");
        html += escape(apName);
        html += F("'><label for='ap_password'>Nuova password della scheda</label><input id='ap_password' name='ap_password' type='password' minlength='8' maxlength='63' autocomplete='new-password'>"
            "<p>Lascia vuoto per conservare la password attuale. Usa lettere, numeri, spazi e simboli senza accenti.</p>"
            "<button type='submit'>Salva e riavvia</button></form><p>Per accedere a questa pagina: utente <b>admin</b>, password della rete della scheda mostrata sull'OLED.</p></main></body></html>");
        server.send(200, "text/html; charset=utf-8", html);
    }

    static bool validPassword(const String &password)
    {
        if (password.length() == 0) return true;
        for (size_t i = 0; i < password.length(); ++i) if (!password[i]) return false;
        if (password.length() >= 8 && password.length() <= 63) return true;
        if (password.length() != 64) return false;
        for (size_t i = 0; i < password.length(); ++i) if (!isxdigit(uint8_t(password[i]))) return false;
        return true;
    }
    void startConnection()
    {
        WiFi.setMinSecurity(credentials.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN);
        WiFi.begin(credentials.ssid, credentials.password);
        attemptStarted = millis();
        Serial.printf("[WIFI] Connecting to %s\n", credentials.ssid);
    }
    void save(WebServer &server)
    {
        server.sendHeader("Cache-Control", "no-store");
        if (!setupMode) { server.send(403, "text/plain", "Configurazione gia salvata. Usa BOOT per resettarla."); return; }
        if (!server.hasArg("token") || server.arg("token") != token) {
            server.send(403, "text/plain", "Pagina scaduta: ricaricala e riprova."); return;
        }
        String ssid = server.arg("ssid"), password = server.arg("password");
        if (ssid.length() == 0 || ssid.length() > 32 || !validPassword(password)) {
            server.send(400, "text/plain", "SSID non valido o password non valida (vuota per reti aperte, 8-63 caratteri oppure 64 cifre esadecimali)."); return;
        }
        for (size_t i = 0; i < ssid.length(); ++i) if (!ssid[i]) {
            server.send(400, "text/plain", "SSID non valido."); return;
        }
        Credentials saved;
        ssid.toCharArray(saved.ssid, sizeof(saved.ssid));
        password.toCharArray(saved.password, sizeof(saved.password));
        if (!storageReady || preferences.putBytes("credentials", &saved, sizeof(saved)) != sizeof(saved)) {
            server.send(500, "text/plain", "Salvataggio fallito. Riprova."); return;
        }
        // Do not echo or log the password. One NVS blob keeps the pair atomic.
        server.send(200, "text/html; charset=utf-8",
            "<!doctype html><html lang='it'><meta name='viewport' content='width=device-width'>"
            "<title>Wi-Fi salvato</title><h1>Configurazione salvata</h1>"
            "<p>La scheda si riavvia e si collega alla rete scelta. Puoi collegare la PS5 alla rete della scheda oppure alla stessa rete di casa.</p>"
            "<p>Per i dispositivi sulla rete di casa usa come DNS l'indirizzo mostrato sull'OLED. Impostalo sia come DNS primario sia secondario.</p>"
            "<p>Se la connessione fallisce, verifica che la rete sia a 2,4 GHz. Per cambiare i dati tieni BOOT premuto 5 secondi a scheda accesa.</p></html>");
        restartAt = millis() + 1500;
    }
    void page(WebServer &server)
    {
        server.sendHeader("Cache-Control", "no-store");
        if (!setupMode) { server.send(403, "text/plain", "Wi-Fi gia configurato. Per cancellare i dati tieni BOOT premuto 5 secondi a scheda accesa."); return; }
        String html = F("<!doctype html><html lang='it'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>PS5 Master Key - Wi-Fi</title><style>body{font:16px system-ui;background:#101725;color:#edf4ff;margin:0;padding:24px}"
            "main{max-width:420px;margin:5vh auto}h1{font-size:28px}label{display:block;margin:22px 0 8px}input,button{box-sizing:border-box;width:100%;padding:14px;border-radius:8px;font:inherit}"
            "input{background:#1e2a40;color:white;border:1px solid #52637f}button{margin-top:24px;border:0;background:#a5d6ff;color:#101725;font-weight:bold}p{line-height:1.5;color:#c8d6ea}</style></head>"
            "<body><main><h1>Collega la scheda al Wi-Fi</h1><p>Inserisci i dati della rete di casa a 2,4 GHz. Verranno salvati sulla scheda: non servira ricompilare il firmware.</p>"
            "<form method='post' action='/wifi'><input type='hidden' name='token' value='");
        html += token;
        html += F("'><label for='ssid'>Nome della rete (SSID)</label><input id='ssid' name='ssid' maxlength='32' required autocomplete='off' autocapitalize='none' spellcheck='false'>"
            "<label for='password'>Password Wi-Fi</label><input id='password' type='password' name='password' maxlength='64' autocomplete='new-password'>"
            "<p>Lascia vuoto solo per una rete senza password.</p><button type='submit'>Salva e connetti</button></form>"
            "<p><a href='/settings' style='color:#a5d6ff'>Cambia nome e password della rete della scheda</a> (utente admin, password mostrata sull'OLED).</p>"
            "<p>Per riconfigurare in seguito: tieni BOOT premuto per 5 secondi a scheda accesa.</p></main></body></html>");
        server.send(200, "text/html; charset=utf-8", html);
    }

public:
    bool configuring() const { return setupMode; }
    bool connected() const { return online; }
    const char *ssid() const { return setupMode ? apName.c_str() : credentials.ssid; }
    const char *setupSsid() const { return apName.c_str(); }
    const char *displayKey() const { return apKey.c_str(); }
    IPAddress apIp() const { return accessPointIp; }
    IPAddress apSubnet() const { return IPAddress(255,255,255,0); }
    IPAddress ip() const { return setupMode ? WiFi.softAPIP() : (online ? WiFi.localIP() : IPAddress()); }
    IPAddress dns() const
    {
        if (setupMode || !online) return IPAddress();
        IPAddress upstream = WiFi.dnsIP();
        if (!uint32_t(upstream) || (upstream == WiFi.localIP() || upstream == apIp())) upstream = WiFi.dnsIP(1);
        if (!uint32_t(upstream) || (upstream == WiFi.localIP() || upstream == apIp())) upstream = WiFi.gatewayIP();
        if ((upstream == WiFi.localIP() || upstream == apIp())) return IPAddress();
        return upstream;
    }
    String status() const
    {
        if (resetSeconds) return String("Reset tra ") + (5 - resetSeconds) + "s";
        if (setupMode) return "APRI /wifi";
        if (online) return "DNS READY / UPD OFF";
        return WiFi.status() == WL_CONNECT_FAILED ? "WiFi FALLITA" : "CONNESSIONE...";
    }
    void begin(const char *name, const char *key)
    {
        apName = name; apKey = key;
        pinMode(bootPin, INPUT_PULLUP);
        storageReady = preferences.begin("portal-wifi", false);
        if (storageReady && preferences.getBytesLength("access-point") == sizeof(AccessPoint)) {
            AccessPoint saved;
            if (preferences.getBytes("access-point", &saved, sizeof(saved)) == sizeof(saved) &&
                saved.ssid[32] == 0 && saved.password[63] == 0 && saved.ssid[0] &&
                strlen(saved.password) >= 8 && printable(String(saved.ssid)) && printable(String(saved.password))) {
                apName = saved.ssid; apKey = saved.password;
            }
        }
        if (storageReady && preferences.getBytesLength("credentials") == sizeof(credentials)) {
            preferences.getBytes("credentials", &credentials, sizeof(credentials));
            setupMode = credentials.version != 1 || !credentials.ssid[0] ||
                        credentials.ssid[32] != 0 || credentials.password[64] != 0;
            if (!setupMode) setupMode = !validPassword(String(credentials.password));
        }
        WiFi.persistent(false);
        WiFi.setAutoReconnect(true);
        char nonce[33];
        snprintf(nonce, sizeof(nonce), "%08lx%08lx%08lx%08lx",
                 (unsigned long)esp_random(), (unsigned long)esp_random(),
                 (unsigned long)esp_random(), (unsigned long)esp_random());
        token = nonce;
        WiFi.mode(setupMode ? WIFI_AP : WIFI_AP_STA);
        startAccessPoint();
        if (setupMode) {
            Serial.println("[WIFI] No saved configuration: open http://192.168.4.1/wifi");
        } else {
            startConnection();
        }
    }
    void registerRoutes(WebServer &server)
    {
        server.on("/wifi", HTTP_GET, [this, &server]() { page(server); });
        server.on("/wifi", HTTP_POST, [this, &server]() { save(server); });
        server.on("/settings", HTTP_GET, [this, &server]() { settings(server, false); });
        server.on("/settings", HTTP_POST, [this, &server]() { settings(server, true); });
    }
    void redirectToSetup(WebServer &server)
    {
        server.sendHeader("Cache-Control", "no-store");
        String location = String("http://") + apIp().toString() + "/wifi";
        server.sendHeader("Location", location);
        server.send(302, "text/plain", String("Apri ") + location);
    }
    bool poll()
    {
        uint32_t time = millis();
        if (restartAt && int32_t(time - restartAt) >= 0) ESP.restart();
        bool changed = false;
        if (digitalRead(bootPin) == LOW) {
            if (!buttonHeld) { buttonHeld = true; buttonStarted = time; }
            unsigned seconds = uint32_t(time - buttonStarted) / 1000;
            if (seconds >= 5) {
                if (storageReady && (!preferences.isKey("credentials") || preferences.remove("credentials"))) {
                    Serial.println("[WIFI] BOOT held 5s: configuration cleared");
                    WiFi.disconnect(true, true);
                    ESP.restart();
                } else {
                    Serial.println("[WIFI] Cannot clear configuration");
                    buttonStarted = time;
                }
            }
            if (seconds != resetSeconds) { resetSeconds = seconds; changed = true; }
        } else {
            if (buttonHeld) changed = true;
            buttonHeld = false; resetSeconds = 0;
        }
        if (uint32_t(time - lastCheck) < 500) return changed;
        lastCheck = time;
        // Keep long AP names/passwords scrolling on the OLED, even when online.
        changed = true;
        bool wasOnline = online;
        online = !setupMode && WiFi.status() == WL_CONNECTED && uint32_t(WiFi.localIP());
        if (online && (wasOnline != online || WiFi.localIP() != previousIp || WiFi.subnetMask() != previousSubnet)) {
            changed |= avoidSubnetCollision();
            previousSubnet = WiFi.subnetMask();
        }
        if (wasOnline != online || ip() != previousIp || dns() != previousDns) {
            previousIp = ip(); previousDns = dns(); changed = true;
            Serial.printf("[WIFI] %s IP=%s DNS=%s\n", online ? "Connected" : (setupMode ? "Setup" : "Disconnected"),
                          ip().toString().c_str(), dns().toString().c_str());
        }
        if (!setupMode && !online && uint32_t(time - attemptStarted) >= 30000) {
            WiFi.disconnect(); startConnection(); changed = true;
        }
        return changed;
    }
};
