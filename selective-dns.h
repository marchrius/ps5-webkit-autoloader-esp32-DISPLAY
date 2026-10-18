#pragma once

#include "dns-protocol.h"
#ifdef ARDUINO
#include <Arduino.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include "esp_random.h"
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <random>
#endif
#include <fcntl.h>
#include <cerrno>

// Nonblocking UDP/TCP DNS proxy. Limits bound both memory and concurrent work;
// slow DNS servers never hold up HTTP, OLED updates or the BOOT reset button.
class SelectiveDnsServer {
    struct Pending {
        bool active = false;
        uint16_t upstreamId = 0;
        uint16_t clientId = 0;
        sockaddr_in client = {};
        uint32_t started = 0;
        size_t length = 0;
        uint8_t question[PortalDns::MaxQuestion] = {};
        PortalDns::Question parsed;
    } pending[8];

    struct Tcp {
        int client = -1;
        int upstream = -1;
        bool connecting = false;
        bool receiving = false;
        bool sending = false;
        size_t used = 0;
        size_t expected = 0;
        size_t sent = 0;
        uint32_t started = 0;
        uint16_t clientId = 0;
        uint32_t clientAddress = 0;
        PortalDns::Question question;
        uint8_t data[PortalDns::MaxPacket + 2] = {};
    } tcp[1];

    int listener = -1, upstream = -1, tcpListener = -1;
    uint16_t boundPort = 0;
    sockaddr_in resolver = {};
    uint32_t apAddress = 0, apMask = 0;
    uint32_t localAddress = 0;
    bool setupMode = false;
    uint8_t packet[PortalDns::MaxPacket + 16] = {};

    static uint32_t now()
    {
#ifdef ARDUINO
        return millis();
#else
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
#endif
    }
    static uint16_t randomId()
    {
#ifdef ARDUINO
        return esp_random();
#else
        static std::mt19937 generator(std::random_device{}());
        return generator();
#endif
    }
    static bool nonblocking(int fd)
    {
        return fd >= 0 && fcntl(fd, F_SETFL, O_NONBLOCK) == 0;
    }
    static void closeFd(int &fd) { if (fd >= 0) close(fd); fd = -1; }
    static bool expired(uint32_t started) { return uint32_t(now() - started) >= 4000; }
    static void log(const char *action, const char *name)
    {
#if defined(ARDUINO) && USB_DEBUG
        Serial.printf("[DNS] %s %s\n", action, name);
#else
        (void)action; (void)name;
#endif
    }
    void sendToClient(const uint8_t *p, size_t len, const sockaddr_in &client)
    {
        sendto(listener, p, len, 0, reinterpret_cast<const sockaddr *>(&client), sizeof(client));
    }
    bool localReply(uint8_t *p, size_t &len, PortalDns::Question &q, uint32_t clientAddress)
    {
        if (len < 12 || (PortalDns::read16(p + 2) & 0x8000)) { len = 0; return true; }
        if (!PortalDns::parseQuestion(p, len, q) ||
            (PortalDns::read16(p + 2) & 0x7800) ||
            PortalDns::read16(p + 6) || PortalDns::read16(p + 8) ||
            !PortalDns::validRecords(p, len, q)) {
            len = PortalDns::reply(p, len, 1);
            return true;
        }
        PortalDns::Policy action = PortalDns::policy(q.name);
        if (setupMode || action == PortalDns::Policy::Local) {
            const uint32_t address = !setupMode && apAddress && apMask &&
                (clientAddress & apMask) == (apAddress & apMask) ? apAddress : localAddress;
            len = PortalDns::reply(p, len, 0, reinterpret_cast<const uint8_t *>(&address));
            log("LOCAL", q.name);
            return true;
        }
        if (action == PortalDns::Policy::Block) {
            len = PortalDns::reply(p, len, 3);
            log("BLOCK", q.name);
            return true;
        }
        return false;
    }
    void receiveQuery()
    {
        sockaddr_in client = {};
        socklen_t size = sizeof(client);
        int bytes = recvfrom(listener, packet, sizeof(packet), 0,
                             reinterpret_cast<sockaddr *>(&client), &size);
        if (bytes <= 0) return;
        size_t len = bytes;
        if (len > PortalDns::MaxPacket) {
            len = PortalDns::reply(packet, len, 0, nullptr, true);
            if (len) sendToClient(packet, len, client);
            return;
        }
        PortalDns::Question q;
        if (localReply(packet, len, q, client.sin_addr.s_addr)) {
            if (len) sendToClient(packet, len, client);
            return;
        }
        Pending *slot = nullptr;
        for (auto &p : pending) if (!p.active) { slot = &p; break; }
        if (!slot || !resolver.sin_addr.s_addr) {
            len = PortalDns::reply(packet, len, 2);
            sendToClient(packet, len, client);
            return;
        }
        uint16_t id;
        bool unique;
        do {
            id = randomId(); unique = true;
            for (const auto &p : pending) if (p.active && p.upstreamId == id) unique = false;
        } while (!unique);
        slot->clientId = PortalDns::read16(packet);
        slot->upstreamId = id;
        slot->client = client;
        slot->parsed = q;
        slot->length = q.end;
        std::memcpy(slot->question, packet, q.end);
        slot->started = now();
        slot->active = true;
        PortalDns::write16(packet, id);
        if (sendto(upstream, packet, len, 0, reinterpret_cast<sockaddr *>(&resolver),
                   sizeof(resolver)) != int(len)) {
            len = PortalDns::reply(slot->question, slot->length, 2);
            sendToClient(slot->question, len, client);
            slot->active = false;
        }
        log("FORWARD", q.name);
    }
    void receiveReply()
    {
        sockaddr_in source = {};
        socklen_t size = sizeof(source);
        int bytes = recvfrom(upstream, packet, sizeof(packet), 0,
                             reinterpret_cast<sockaddr *>(&source), &size);
        if (bytes < 12 || source.sin_addr.s_addr != resolver.sin_addr.s_addr ||
            source.sin_port != resolver.sin_port) return;
        for (auto &p : pending) {
            if (!p.active || p.upstreamId != PortalDns::read16(packet) ||
                !PortalDns::matches(packet, bytes, p.parsed)) continue;
            if (bytes > int(PortalDns::MaxPacket)) {
                size_t len = PortalDns::reply(p.question, p.length, 0, nullptr, true);
                sendToClient(p.question, len, p.client);
            } else {
                PortalDns::write16(packet, p.clientId);
                sendToClient(packet, bytes, p.client);
            }
            p.active = false;
            break;
        }
    }
    void resetTcp(Tcp &t)
    {
        closeFd(t.client); closeFd(t.upstream);
        t.connecting = t.receiving = t.sending = false;
        t.used = t.expected = t.sent = 0;
    }
    void tcpError(Tcp &t)
    {
        // Preserve the complete question while waiting on the upstream socket.
        size_t length = PortalDns::reply(t.data + 2, t.used >= 2 ? t.used - 2 : 0, 2);
        if (!length) { resetTcp(t); return; }
        PortalDns::write16(t.data, length);
        closeFd(t.upstream);
        t.used = length + 2; t.sent = 0; t.sending = true;
        t.receiving = t.connecting = false;
    }
    void pollTcp(Tcp &t)
    {
        if (t.client < 0) return;
        if (expired(t.started)) { resetTcp(t); return; }
        if (t.sending) {
            int bytes = send(t.client, t.data + t.sent, t.used - t.sent, 0);
            if (bytes > 0) t.sent += bytes;
            if (t.sent == t.used || (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) resetTcp(t);
            return;
        }
        if (t.connecting) {
            fd_set writes; FD_ZERO(&writes); FD_SET(t.upstream, &writes);
            timeval zero = {};
            if (select(t.upstream + 1, nullptr, &writes, nullptr, &zero) <= 0) return;
            int error = 0; socklen_t n = sizeof(error);
            if (getsockopt(t.upstream, SOL_SOCKET, SO_ERROR, &error, &n) || error) { tcpError(t); return; }
            int bytes = send(t.upstream, t.data + t.sent, t.used - t.sent, 0);
            if (bytes > 0) t.sent += bytes;
            if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) { tcpError(t); return; }
            if (t.sent == t.used) {
                t.connecting = false; t.receiving = true;
                t.used = t.expected = 0;
            }
            return;
        }
        int fd = t.receiving ? t.upstream : t.client;
        size_t need = t.used < 2 ? 2 : t.expected;
        int bytes = recv(fd, t.data + t.used, need - t.used, 0);
        if (bytes == 0 || (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) { resetTcp(t); return; }
        if (bytes <= 0) return;
        t.used += bytes;
        if (t.used == 2) {
            size_t length = PortalDns::read16(t.data);
            if (length < 12 || length > PortalDns::MaxPacket) { resetTcp(t); return; }
            t.expected = length + 2;
        }
        if (t.used != t.expected) return;
        if (t.receiving) {
            if (PortalDns::read16(t.data + 2) != t.clientId ||
                !PortalDns::matches(t.data + 2, t.used - 2, t.question)) { resetTcp(t); return; }
            closeFd(t.upstream); t.sent = 0; t.sending = true;
            return;
        }
        size_t len = t.used - 2;
        if (localReply(t.data + 2, len, t.question, t.clientAddress)) {
            if (!len) { resetTcp(t); return; }
            PortalDns::write16(t.data, len);
            t.used = len + 2; t.sending = true; t.sent = 0;
            return;
        }
        t.upstream = socket(AF_INET, SOCK_STREAM, 0);
        t.clientId = PortalDns::read16(t.data + 2);
        if (!resolver.sin_addr.s_addr || !nonblocking(t.upstream)) { tcpError(t); return; }
        int result = connect(t.upstream, reinterpret_cast<sockaddr *>(&resolver), sizeof(resolver));
        if (result < 0 && errno != EINPROGRESS) { tcpError(t); return; }
        t.connecting = true; t.sent = 0;
        log("FORWARD TCP", t.question.name);
    }

public:
    ~SelectiveDnsServer() { stop(); }
    uint16_t port() const { return boundPort; }
    void stop()
    {
        closeFd(listener); closeFd(upstream); closeFd(tcpListener);
        for (auto &p : pending) p.active = false;
        for (auto &t : tcp) resetTcp(t);
    }
    bool begin(uint32_t localIp, uint32_t upstreamIp, bool configuring,
               uint16_t port = 53, uint16_t upstreamPort = 53,
               uint32_t apIp = 0, uint32_t apSubnet = 0)
    {
        stop();
        localAddress = localIp; setupMode = configuring;
        apAddress = apIp; apMask = apSubnet;
        resolver = {}; resolver.sin_family = AF_INET;
        resolver.sin_addr.s_addr = upstreamIp == localIp || (apIp && upstreamIp == apIp) ? 0 : upstreamIp;
        resolver.sin_port = htons(upstreamPort);
        listener = socket(AF_INET, SOCK_DGRAM, 0);
        upstream = socket(AF_INET, SOCK_DGRAM, 0);
        tcpListener = socket(AF_INET, SOCK_STREAM, 0);
        if (!nonblocking(listener) || !nonblocking(upstream) || !nonblocking(tcpListener)) { stop(); return false; }
        sockaddr_in address = {}; address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY; address.sin_port = htons(port);
        int reuse = 1; setsockopt(tcpListener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address))) { stop(); return false; }
        socklen_t len = sizeof(address);
        getsockname(listener, reinterpret_cast<sockaddr *>(&address), &len);
        boundPort = ntohs(address.sin_port);
        if (bind(tcpListener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) ||
            listen(tcpListener, 2)) { stop(); return false; }
        return true;
    }
    void poll()
    {
        if (listener < 0) return;
        for (auto &p : pending) if (p.active && expired(p.started)) {
            size_t len = PortalDns::reply(p.question, p.length, 2);
            sendToClient(p.question, len, p.client); p.active = false;
            log("TIMEOUT", p.parsed.name);
        }
        for (int i = 0; i < 4; ++i) { receiveReply(); receiveQuery(); }
        for (auto &t : tcp) {
            if (t.client < 0) {
                sockaddr_in client = {}; socklen_t size = sizeof(client);
                t.client = accept(tcpListener, reinterpret_cast<sockaddr *>(&client), &size);
                t.clientAddress = client.sin_addr.s_addr;
                if (t.client >= 0) {
                    if (!nonblocking(t.client)) { resetTcp(t); continue; }
                    t.started = now();
                }
            }
            pollTcp(t);
        }
    }
};
