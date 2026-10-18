#pragma once
#include "Arduino.h"
#define HTTP_GET 0
#define HTTP_POST 1
struct WebServer {
    std::map<std::pair<std::string,int>, std::function<void()>> routes;
    std::map<std::string, String> args;
    bool authenticated = true, authRequested = false;
    int code = 0;
    String body, authUser, authPassword;
    void sendHeader(const char *, const String &) {}
    bool authenticate(const char *u, const char *p) { authUser=u; authPassword=p; return authenticated; }
    void requestAuthentication() { authRequested=true; code=401; }
    String arg(const char *key) { return args[key]; }
    bool hasArg(const char *key) { return args.count(key); }
    void send(int status, const char *, const String &text) { code=status; body=text; }
    void on(const char *path, int method, std::function<void()> callback) { routes[{path,method}]=callback; }
    void request(const char *path, int method) { code=0; body=""; routes.at({path,method})(); }
};
