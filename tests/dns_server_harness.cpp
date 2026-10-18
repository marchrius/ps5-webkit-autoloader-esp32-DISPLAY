#include "../selective-dns.h"
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <thread>

int main(int argc, char **argv)
{
    if (argc != 3 && argc != 5) return 1;
    std::signal(SIGPIPE, SIG_IGN);
    SelectiveDnsServer server;
    if (!server.begin(inet_addr("127.0.0.42"), inet_addr("127.0.0.1"),
                      std::atoi(argv[2]) != 0, 0, std::atoi(argv[1]),
                      argc == 5 ? inet_addr(argv[3]) : 0,
                      argc == 5 ? inet_addr(argv[4]) : 0)) return 2;
    std::printf("%u\n", server.port());
    std::fflush(stdout);
    for (;;) {
        server.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
