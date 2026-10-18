#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace PortalDns {
constexpr size_t MaxPacket = 4096;
constexpr size_t MaxQuestion = 271;

inline uint16_t read16(const uint8_t *p) { return uint16_t(p[0]) << 8 | p[1]; }
inline void write16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v & 255; }

struct Question {
    char name[254] = {};
    uint16_t type = 0;
    uint16_t klass = 0;
    size_t end = 0;
};

// Bounds-check every label; accept one uncompressed question, including EDNS
// records after it. Upstream questions must match this same wire format.
inline bool parseQuestion(const uint8_t *p, size_t len, Question &q)
{
    if (len < 12 || read16(p + 4) != 1) return false;
    size_t pos = 12, used = 0;
    for (;;) {
        if (pos >= len) return false;
        uint8_t label = p[pos++];
        if (label == 0) break;
        if (label > 63 || pos + label > len) return false;
        if (used) {
            if (used >= sizeof(q.name) - 1) return false;
            q.name[used++] = '.';
        }
        if (used + label >= sizeof(q.name)) return false;
        for (uint8_t i = 0; i < label; ++i) {
            uint8_t c = p[pos++];
            // Do not let embedded NULs or dots change policy matching.
            if (c == 0 || c == '.') return false;
            q.name[used++] = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
        }
    }
    if (pos + 4 > len || pos + 4 > MaxQuestion) return false;
    q.name[used] = 0;
    q.type = read16(p + pos);
    q.klass = read16(p + pos + 2);
    q.end = pos + 4;
    return true;
}

inline bool suffix(const char *name, const char *zone)
{
    size_t n = std::strlen(name), z = std::strlen(zone);
    return n >= z && std::strcmp(name + n - z, zone) == 0 &&
           (n == z || name[n - z - 1] == '.');
}

enum class Policy { Local, Block, Forward };

inline Policy policy(const char *name)
{
    if (std::strcmp(name, "manuals.playstation.net") == 0 ||
        std::strcmp(name, "ena.net.playstation.net") == 0) return Policy::Local;
    // Covers regional metadata (fus/fuk/...) and PUP download (dus/pc/...).
    // Sony's official PS5 download links use pc.ps5.update.playstation.net.
    if (suffix(name, "ps5.update.playstation.net") ||
        std::strcmp(name, "update.playstation.net") == 0 ||
        std::strcmp(name, "update.net.playstation.net") == 0) return Policy::Block;
    return Policy::Forward;
}

// Rebuild a short answer in place; remove EDNS/AD and preserve the client's RD.
inline size_t reply(uint8_t *p, size_t len, uint8_t rcode, const uint8_t *ip = nullptr,
                    bool truncated = false)
{
    if (len < 12) return 0;
    Question q;
    bool valid = parseQuestion(p, len, q);
    bool answer = valid && ip && q.klass == 1 && (q.type == 1 || q.type == 255);
    uint16_t flags = 0x8080 | (read16(p + 2) & 0x0100) | (rcode & 15);
    if (truncated) flags |= 0x0200;
    write16(p + 2, flags);
    write16(p + 4, valid ? 1 : 0);
    write16(p + 6, answer ? 1 : 0);
    write16(p + 8, 0);
    write16(p + 10, 0);
    size_t end = valid ? q.end : 12;
    if (answer) {
        const uint8_t record[] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 30, 0, 4};
        std::memcpy(p + end, record, sizeof(record));
        std::memcpy(p + end + sizeof(record), ip, 4);
        end += sizeof(record) + 4;
    }
    return end;
}

// Validate compressed owner names and record boundaries before trusting a reply.
// Pointers must point backwards; this also prevents cycles and unbounded work.
inline bool skipName(const uint8_t *p, size_t len, size_t &pos)
{
    size_t cursor = pos, expanded = 0;
    bool jumped = false;
    for (size_t steps = 0; steps < 128; ++steps) {
        if (cursor >= len) return false;
        const size_t start = cursor;
        uint8_t label = p[cursor++];
        if ((label & 0xc0) == 0xc0) {
            if (cursor >= len) return false;
            size_t target = size_t(label & 0x3f) << 8 | p[cursor++];
            if (target < 12 || target >= start) return false;
            if (!jumped) pos = cursor;
            jumped = true; cursor = target;
            continue;
        }
        if (label > 63 || cursor + label > len) return false;
        expanded += size_t(label) + 1;
        if (expanded > 255) return false;
        if (!label) {
            if (!jumped) pos = cursor;
            return true;
        }
        cursor += label;
    }
    return false;
}

inline bool validRecords(const uint8_t *p, size_t len, const Question &q)
{
    size_t pos = q.end;
    const uint32_t count = uint32_t(read16(p + 6)) + read16(p + 8) + read16(p + 10);
    // Every resource record needs at least a root owner plus ten fixed bytes.
    if (count > (len - pos) / 11) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!skipName(p, len, pos) || pos + 10 > len) return false;
        const size_t length = read16(p + pos + 8);
        pos += 10;
        if (length > len - pos) return false;
        pos += length;
    }
    return pos == len;
}

inline bool matches(const uint8_t *response, size_t len, const Question &query)
{
    Question q;
    return len >= 12 && (read16(response + 2) & 0xf800) == 0x8000 &&
           parseQuestion(response, len, q) && q.type == query.type &&
           q.klass == query.klass && std::strcmp(q.name, query.name) == 0 &&
           (len > MaxPacket || validRecords(response, len, q));
}
} // namespace PortalDns
