#pragma once
#include "Arduino.h"
class Preferences {
public:
    inline static std::map<std::string, std::vector<uint8_t>> values;
    bool begin(const char *, bool) { return true; }
    size_t getBytesLength(const char *key) { return values[key].size(); }
    size_t getBytes(const char *key, void *out, size_t size) {
        size = std::min(size, values[key].size()); memcpy(out, values[key].data(), size); return size;
    }
    size_t putBytes(const char *key, const void *data, size_t size) {
        const auto *bytes = static_cast<const uint8_t *>(data); values[key] = {bytes, bytes+size}; return size;
    }
    bool isKey(const char *key) { return values.count(key) && !values[key].empty(); }
    bool remove(const char *key) { return values.erase(key); }
};
