#include <cassert>
#include "../network-config.h"

static String tokenFrom(const String &html)
{
    std::string text = html.c_str(), marker = "name='token' value='";
    auto start=text.find(marker); assert(start != std::string::npos); start += marker.size();
    return text.substr(start, text.find("'", start)-start);
}
static void advance(NetworkConfig &config, unsigned ms)
{
    mockMillis += ms; config.poll();
}
int main()
{
    NetworkConfig initial;
    initial.begin("PS5 Master Key", "12345678");
#if defined(CONFIG_IDF_TARGET_ESP32C3)
    assert(mockPin == 9);
#else
    assert(mockPin == 0);
#endif
    assert(initial.configuring() && WiFi.modeValue == WIFI_AP);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    assert(WiFi.advertisedDns==initial.apIp());
#endif
    WebServer form; initial.registerRoutes(form);
    form.request("/wifi", HTTP_GET);
    String wifiToken=tokenFrom(form.body);
    form.args={{"ssid","Home"},{"password","secret123"},{"token","wrong"}};
    form.request("/wifi",HTTP_POST); assert(form.code==403);
    form.args["token"]=wifiToken;
    form.args["password"]="short";
    form.request("/wifi",HTTP_POST); assert(form.code==400);
    form.args["password"]="secret123";
    form.request("/wifi",HTTP_POST); assert(form.code==200);
    assert(std::string(form.body.c_str()).find("secret123")==std::string::npos);
    advance(initial,1500); assert(ESP.restarts==1);

    NetworkConfig saved; saved.begin("PS5 Master Key","12345678");
    assert(!saved.configuring() && WiFi.modeValue==WIFI_AP_STA);
    assert(WiFi.name=="Home" && WiFi.password=="secret123");
    WiFi.state=WL_CONNECTED; WiFi.local=IPAddress(192,168,4,30); WiFi.gateway=IPAddress(192,168,4,1);
    advance(saved,500); assert(saved.connected());
    assert(saved.ip()==WiFi.local && saved.apIp()!=WiFi.local);
    assert(saved.apIp()==IPAddress(192,168,5,1)); // Router occupies the default AP subnet.
    assert(WiFi.apStarts==3);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    assert(WiFi.advertisedDns==saved.apIp());
#endif
    WebServer settings; saved.registerRoutes(settings);
    settings.authenticated=false;
    settings.request("/settings",HTTP_GET); assert(settings.code==401);
    settings.authenticated=true;
    settings.request("/settings",HTTP_GET); String settingsToken=tokenFrom(settings.body);
    assert(settings.authUser=="admin" && settings.authPassword=="12345678");
    assert(std::string(settings.body.c_str()).find("192.168.5.1")!=std::string::npos);
    assert(std::string(settings.body.c_str()).find("192.168.4.30")!=std::string::npos);
    settings.args={{"token",settingsToken},{"ap_ssid","Custom <AP>"},{"ap_password","bad"}};
    settings.request("/settings",HTTP_POST); assert(settings.code==400);
    settings.args["ap_password"]="custom123";
    settings.args["token"]="wrong";
    settings.request("/settings",HTTP_POST); assert(settings.code==403);
    settings.args["token"]=settingsToken;
    settings.request("/settings",HTTP_POST); assert(settings.code==200);
    advance(saved,1500);

    NetworkConfig custom; custom.begin("PS5 Master Key","12345678");
    assert(String(custom.setupSsid())=="Custom <AP>" && String(custom.displayKey())=="custom123");
    WebServer customPage; custom.registerRoutes(customPage);
    customPage.request("/settings",HTTP_GET);
    assert(std::string(customPage.body.c_str()).find("Custom &lt;AP&gt;")!=std::string::npos);
    customPage.request("/wifi",HTTP_GET); assert(customPage.code==403);
    WiFi.state=0; int attempts=WiFi.connections;
    advance(custom,30000); assert(WiFi.connections==attempts+1);
    assert(!custom.configuring() && Preferences::values.count("credentials"));
    assert(WiFi.modeValue==WIFI_AP_STA); // AP remains present while home router is unavailable.

    // A short press is harmless; exactly five seconds erases only home credentials.
    mockButton=LOW; custom.poll(); advance(custom,4999);
    assert(Preferences::values.count("credentials"));
    mockButton=1; custom.poll();
    mockButton=LOW; custom.poll(); advance(custom,5000);
    assert(!Preferences::values.count("credentials"));
    assert(Preferences::values.count("access-point"));
    mockButton=1;
    NetworkConfig reset; reset.begin("PS5 Master Key","12345678");
    assert(reset.configuring() && WiFi.modeValue==WIFI_AP);
    assert(String(reset.setupSsid())=="Custom <AP>" && String(reset.displayKey())=="custom123");
}
