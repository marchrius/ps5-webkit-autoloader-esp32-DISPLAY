#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <map>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#define F(x) x
#define INPUT_PULLUP 2
#define LOW 0
inline uint32_t mockMillis = 0;
inline int mockButton = 1, mockPin = -1;
inline uint32_t millis() { return mockMillis; }
inline void pinMode(int pin, int) { mockPin = pin; }
inline int digitalRead(int pin) { mockPin = pin; return mockButton; }
class String {
    std::string value;
public:
    String() = default;
    String(const char *s): value(s) {}
    String(const std::string &s): value(s) {}
    String(unsigned n): value(std::to_string(n)) {}
    size_t length() const { return value.size(); }
    char operator[](size_t i) const { return value[i]; }
    const char *c_str() const { return value.c_str(); }
    void replace(const char *from, const char *to) {
        size_t pos = 0;
        while ((pos = value.find(from, pos)) != std::string::npos) {
            value.replace(pos, strlen(from), to); pos += strlen(to);
        }
    }
    void toCharArray(char *target, size_t size) const { snprintf(target, size, "%s", c_str()); }
    String &operator+=(const String &other) { value += other.value; return *this; }
    friend String operator+(const String &a, const String &b) { return a.value + b.value; }
    friend bool operator==(const String &a, const String &b) { return a.value == b.value; }
    friend bool operator!=(const String &a, const String &b) { return !(a == b); }
};
struct SerialMock { template<class... A> void printf(const char *, A...) {} void println(const char *) {} };
inline SerialMock Serial;
struct ESPMock { int restarts = 0; void restart() { ++restarts; } };
inline ESPMock ESP;
