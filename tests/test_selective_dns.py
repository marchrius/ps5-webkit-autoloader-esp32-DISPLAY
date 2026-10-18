"""Exercise the actual firmware DNS proxy against loopback UDP/TCP sockets.

Run: python3 -m unittest discover -s tests -v
Requires a C++11 compiler; no third-party Python packages or Internet access.
"""

import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


def query(host, kind=1, ident=0x1234, edns=False):
    labels = b"".join(bytes([len(label)]) + label.encode() for label in host.split(".")) + b"\0"
    header = struct.pack("!6H", ident, 0x0100, 1, 0, 0, int(edns))
    packet = header + labels + struct.pack("!HH", kind, 1)
    return packet + (b"\0" + struct.pack("!HHIH", 41, 4096, 0, 0) if edns else b"")


def answer(packet):
    result = bytearray(packet)
    result[2:4] = b"\x81\x80"
    return bytes(result)


def receive_exact(sock, length):
    data = b""
    while len(data) < length:
        part = sock.recv(length - len(data))
        if not part:
            raise AssertionError("TCP socket closed before complete DNS frame")
        data += part
    return data


class DnsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="portal-dns-test-")
        cls.executable = str(Path(cls.build.name) / "dns-server")
        compiler = os.environ.get("CXX", shutil.which("c++"))
        if not compiler:
            raise unittest.SkipTest("A C++11 compiler is required")
        subprocess.run([compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-g",
                        str(ROOT / "tests/dns_server_harness.cpp"), "-o", cls.executable], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.upstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.upstream.bind(("127.0.0.1", 0))
        self.upstream.settimeout(1)
        self.upstream_port = self.upstream.getsockname()[1]
        self.upstream_tcp = socket.socket()
        self.upstream_tcp.bind(("127.0.0.1", self.upstream_port))
        self.upstream_tcp.listen(2)
        self.upstream_tcp.settimeout(1)
        self.client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.client.settimeout(1)
        self.process = None
        self.start_server()

    def start_server(self, setup=False, ap_ip=None):
        if self.process:
            self.stop_server()
        args = [self.executable, str(self.upstream_port), str(int(setup))]
        if ap_ip:
            args += [ap_ip, "255.255.255.255"]
        self.process = subprocess.Popen(args,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        line = self.process.stdout.readline()
        if not line:
            self.fail(self.process.stderr.read())
        self.address = ("127.0.0.1", int(line.strip()))

    def stop_server(self):
        self.process.terminate()
        _, errors = self.process.communicate(timeout=3)
        self.assertNotIn("Sanitizer", errors)

    def tearDown(self):
        self.stop_server()
        self.client.close()
        self.upstream.close()
        self.upstream_tcp.close()

    def local(self, packet):
        self.client.sendto(packet, self.address)
        result, _ = self.client.recvfrom(8192)
        return result

    def assert_not_forwarded(self):
        self.upstream.settimeout(0.03)
        with self.assertRaises(socket.timeout):
            self.upstream.recvfrom(8192)
        self.upstream.settimeout(1)

    def test_local_ipv4_and_case_insensitive_names(self):
        for host in ["manuals.playstation.net", "ENA.NET.PlayStation.NET"]:
            packet = query(host, edns=True)
            result = self.local(packet)
            self.assertEqual(result[:2], packet[:2])
            self.assertEqual(struct.unpack("!6H", result[:12])[2:], (1, 1, 0, 0))
            self.assertEqual(result[-4:], socket.inet_aton("127.0.0.42"))
        self.assert_not_forwarded()

    def test_ap_and_lan_clients_get_reachable_local_address_on_udp_and_tcp(self):
        # A /32 loopback subnet tests interface selection without OS IP aliases.
        for ap_ip, expected in [("127.0.0.1", "127.0.0.1"),
                                ("127.0.0.2", "127.0.0.42")]:
            self.start_server(ap_ip=ap_ip)
            self.assertEqual(self.local(query("manuals.playstation.net"))[-4:],
                             socket.inet_aton(expected))
            with socket.create_connection(self.address, timeout=1) as client:
                packet = query("ena.net.playstation.net")
                client.sendall(struct.pack("!H", len(packet)) + packet)
                size = struct.unpack("!H", receive_exact(client, 2))[0]
                self.assertEqual(receive_exact(client, size)[-4:], socket.inet_aton(expected))
        self.start_server(setup=True, ap_ip="127.0.0.1")
        self.assertEqual(self.local(query("example.com"))[-4:], socket.inet_aton("127.0.0.42"))

    def test_local_ipv6_returns_no_data(self):
        result = self.local(query("manuals.playstation.net", 28))
        self.assertEqual(result[3] & 15, 0)
        self.assertEqual(struct.unpack("!H", result[6:8])[0], 0)
        self.assert_not_forwarded()

    def test_update_zones_are_blocked_for_ipv4_and_ipv6(self):
        for host in ["ps5.update.playstation.net", "fus01.ps5.update.playstation.net",
                     "fuk01.ps5.update.playstation.net", "dus01.ps5.update.playstation.net",
                     "pc.ps5.update.playstation.net", "update.playstation.net", "update.net.playstation.net"]:
            for kind in [1, 28]:
                result = self.local(query(host, kind))
                self.assertEqual(result[3] & 15, 3, host)
                self.assertEqual(result[6:12], b"\0" * 6)
        self.assert_not_forwarded()

    def test_other_domains_preserve_edns_ipv6_and_upstream_reply(self):
        for host, kind in [("www.msftconnecttest.com", 1), ("example.com", 28),
                           ("notps5.update.playstation.net", 1),
                           ("fus01.ps5.update.playstation.net.example.com", 1)]:
            packet = query(host, kind, edns=True)
            self.client.sendto(packet, self.address)
            forwarded, sender = self.upstream.recvfrom(8192)
            self.assertEqual(forwarded[2:], packet[2:])
            response = answer(forwarded)
            self.upstream.sendto(response, sender)
            result, _ = self.client.recvfrom(8192)
            self.assertEqual(result, packet[:2] + response[2:])

    def test_out_of_order_responses_and_duplicate_client_ids(self):
        other = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        other.settimeout(1)
        try:
            first, second = query("one.example"), query("two.example")
            self.client.sendto(first, self.address)
            a, sender = self.upstream.recvfrom(8192)
            other.sendto(second, self.address)
            b, sender2 = self.upstream.recvfrom(8192)
            self.assertNotEqual(a[:2], b[:2])
            self.upstream.sendto(answer(b), sender2)
            self.upstream.sendto(answer(a), sender)
            self.assertEqual(other.recvfrom(8192)[0], answer(second))
            self.assertEqual(self.client.recvfrom(8192)[0], answer(first))
        finally:
            other.close()

    def test_invalid_upstream_source_and_question_are_ignored(self):
        packet = query("example.com")
        self.client.sendto(packet, self.address)
        forwarded, sender = self.upstream.recvfrom(8192)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as forged:
            forged.sendto(answer(forwarded), sender)
        bad = bytearray(answer(forwarded))
        bad[13] = ord("x")
        self.upstream.sendto(bad, sender)
        wrong_id = bytearray(answer(forwarded))
        wrong_id[0] ^= 0x80
        self.upstream.sendto(wrong_id, sender)
        self.client.settimeout(0.08)
        with self.assertRaises(socket.timeout):
            self.client.recvfrom(8192)
        self.client.settimeout(1)
        self.upstream.sendto(answer(forwarded), sender)
        self.assertEqual(self.client.recvfrom(8192)[0], answer(packet))

    def test_malformed_response_records_are_ignored(self):
        packet = query("example.com")
        self.client.sendto(packet, self.address)
        forwarded, sender = self.upstream.recvfrom(8192)
        base = bytearray(answer(forwarded))
        base[6:8] = b"\0\1"
        records = [
            b"",  # Header promises an answer which is missing.
            b"\xc0\x0c" + struct.pack("!HHIH", 1, 1, 30, 4) + b"\x7f",  # Short RDATA.
            struct.pack("!H", 0xc000 | len(base)) + struct.pack("!HHIH", 1, 1, 30, 4) + b"\0" * 4,
            b"\xc0\x0c" + b"\0" * 8,  # Missing fixed record fields.
        ]
        for record in records:
            self.upstream.sendto(base + record, sender)
        self.client.settimeout(0.08)
        with self.assertRaises(socket.timeout):
            self.client.recvfrom(8192)
        self.client.settimeout(1)
        record = b"\xc0\x0c" + struct.pack("!HHIH", 1, 1, 30, 4) + socket.inet_aton("1.2.3.4")
        response = bytes(base) + record
        self.upstream.sendto(response, sender)
        self.assertEqual(self.client.recvfrom(8192)[0], packet[:2] + response[2:])

    def test_tcp_wrong_id_or_broken_reply_is_rejected(self):
        for broken in ["id", "question", "records"]:
            packet = query("example.com")
            with socket.create_connection(self.address, timeout=1) as client:
                client.sendall(struct.pack("!H", len(packet)) + packet)
                upstream, _ = self.upstream_tcp.accept()
                with upstream:
                    upstream.settimeout(1)
                    size = struct.unpack("!H", receive_exact(upstream, 2))[0]
                    response = bytearray(answer(receive_exact(upstream, size)))
                    if broken == "id":
                        response[0] ^= 0x80
                    elif broken == "question":
                        response[13] = ord("x")
                    else:
                        response[6:8] = b"\0\1"
                    upstream.sendall(struct.pack("!H", len(response)) + response)
                    self.assertEqual(client.recv(1), b"")
            self.assertEqual(self.local(query("manuals.playstation.net"))[-4:], socket.inet_aton("127.0.0.42"))

    def test_timeout_does_not_block_local_queries(self):
        self.client.sendto(query("timeout.example"), self.address)
        self.upstream.recvfrom(8192)
        started = time.monotonic()
        self.assertEqual(self.local(query("manuals.playstation.net"))[-4:], socket.inet_aton("127.0.0.42"))
        self.assertLess(time.monotonic() - started, 0.5)
        self.client.settimeout(5)
        result, _ = self.client.recvfrom(8192)
        self.assertEqual(result[3] & 15, 2)

    def test_full_forward_queue_returns_servfail_without_blocking_local_dns(self):
        for index in range(8):
            self.client.sendto(query(f"pending{index}.example", ident=index), self.address)
            self.upstream.recvfrom(8192)
        overflow = self.local(query("overflow.example", ident=99))
        self.assertEqual(overflow[:2], b"\0\x63")
        self.assertEqual(overflow[3] & 15, 2)
        self.assertEqual(self.local(query("manuals.playstation.net"))[-4:], socket.inet_aton("127.0.0.42"))

    def test_malformed_questions_return_formerr(self):
        for body in [b"\xc0\x0c\0\1\0\1", b"\x3fshort", b"\x03a\0b\0\0\1\0\1", b"\0\0\1\0\1extra"]:
            result = self.local(struct.pack("!6H", 7, 0x100, 1, 0, 0, 0) + body)
            self.assertEqual(result[3] & 15, 1)
        self.assert_not_forwarded()

    def test_oversized_udp_response_requests_tcp_retry(self):
        packet = query("large.example")
        self.client.sendto(packet, self.address)
        forwarded, sender = self.upstream.recvfrom(8192)
        self.upstream.sendto(answer(forwarded) + b"\0" * 4200, sender)
        result, _ = self.client.recvfrom(8192)
        self.assertTrue(result[2] & 2)
        self.assertEqual(result[:2], packet[:2])

    def test_setup_redirects_all_names_locally(self):
        self.start_server(setup=True)
        for host in ["example.com", "www.msftconnecttest.com", "fus01.ps5.update.playstation.net"]:
            self.assertEqual(self.local(query(host))[-4:], socket.inet_aton("127.0.0.42"))
        self.assert_not_forwarded()

    def test_tcp_local_blocked_and_forwarded_with_partial_frames(self):
        for host in ["manuals.playstation.net", "pc.ps5.update.playstation.net", "example.com"]:
            packet = query(host)
            with socket.create_connection(self.address, timeout=1) as client:
                framed = struct.pack("!H", len(packet)) + packet
                client.sendall(framed[:1])
                time.sleep(0.005)
                client.sendall(framed[1:])
                if host == "example.com":
                    upstream, _ = self.upstream_tcp.accept()
                    with upstream:
                        upstream.settimeout(1)
                        size = struct.unpack("!H", receive_exact(upstream, 2))[0]
                        forwarded = receive_exact(upstream, size)
                        self.assertEqual(forwarded, packet)
                        response = answer(forwarded)
                        data = struct.pack("!H", len(response)) + response
                        upstream.sendall(data[:3])
                        time.sleep(0.005)
                        upstream.sendall(data[3:])
                size = struct.unpack("!H", receive_exact(client, 2))[0]
                result = receive_exact(client, size)
                self.assertEqual(result[:2], packet[:2])
                self.assertEqual(result[3] & 15, 3 if host.startswith("pc.") else 0)
                if host.startswith("manuals."):
                    self.assertEqual(result[-4:], socket.inet_aton("127.0.0.42"))


if __name__ == "__main__":
    unittest.main()
