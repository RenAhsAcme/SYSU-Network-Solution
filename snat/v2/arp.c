#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

struct arp_packet {
    uint16_t hardware, protocol;
    uint8_t hlen, plen;
    uint16_t operation;
    uint8_t sender_mac[6], sender_ip[4], target_mac[6], target_ip[4];
} __attribute__((packed));

struct own_mac { uint8_t address[6]; struct own_mac *next; };

static uint64_t now_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) { perror("clock"); exit(2); }
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static unsigned random_below(unsigned n)
{
    uint32_t r, threshold = -n % n;
    do {
        ssize_t rc;
        do { rc = getrandom(&r, sizeof(r), 0); } while (rc < 0 && errno == EINTR);
        if (rc != sizeof(r)) { perror("getrandom"); exit(2); }
    } while (r < threshold);
    return r % n;
}

static bool claim(const void *data, size_t len, const uint8_t ip[4], bool probing,
                  const struct own_mac *own)
{
    if (len < sizeof(struct arp_packet)) return false;
    struct arp_packet p;
    memcpy(&p, data, sizeof(p));
    if (ntohs(p.hardware) != ARPHRD_ETHER || ntohs(p.protocol) != ETH_P_IP ||
        p.hlen != 6 || p.plen != 4 ||
        (ntohs(p.operation) != ARPOP_REQUEST && ntohs(p.operation) != ARPOP_REPLY)) return false;
    if ((p.sender_mac[0] & 1) || !memcmp(p.sender_mac, "\0\0\0\0\0\0", 6)) return false;
    for (; own; own = own->next)
        if (!memcmp(p.sender_mac, own->address, 6)) return false;
    if (!memcmp(p.sender_ip, ip, 4)) return true;
    return probing && !memcmp(p.sender_ip, "\0\0\0\0", 4) && !memcmp(p.target_ip, ip, 4);
}

static int self_test(void)
{
    uint8_t ip[4] = {10, 0, 0, 3};
    struct own_mac own = {{2, 0, 0, 0, 0, 1}, NULL};
    struct arp_packet p = {.hardware = htons(ARPHRD_ETHER), .protocol = htons(ETH_P_IP),
        .hlen = 6, .plen = 4, .operation = htons(ARPOP_REPLY), .sender_mac = {2,0,0,0,0,2}};
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "ARP self-test line %d\n", __LINE__); return 1; } } while (0)
    memcpy(p.sender_ip, ip, 4);
    CHECK(claim(&p, sizeof(p), ip, false, &own));
    for (size_t n = 0; n < sizeof(p); n++) CHECK(!claim(&p, n, ip, true, &own));
    memcpy(p.sender_mac, own.address, 6);
    CHECK(!claim(&p, sizeof(p), ip, true, &own));
    p.sender_mac[5] = 2; memset(p.sender_ip, 0, 4); memcpy(p.target_ip, ip, 4);
    CHECK(!claim(&p, sizeof(p), ip, false, &own));
    CHECK(claim(&p, sizeof(p), ip, true, &own));
    p.operation = htons(ARPOP_REQUEST);
    p.sender_ip[0] = 10;
    CHECK(!claim(&p, sizeof(p), ip, false, &own));
    p.hlen = 5; CHECK(!claim(&p, sizeof(p), ip, true, &own));
    puts("ARP parser: claims/probes/own MAC/truncation/malformed frames passed");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "self-test")) return self_test();
    if (argc < 4 || argc > 11 || (strcmp(argv[1], "probe") && strcmp(argv[1], "listen") && strcmp(argv[1], "announce"))) {
        fprintf(stderr, "Usage: arp probe DEVICE IPV4 | arp listen DEVICE IPV4... (max 8)\n");
        return 2;
    }
    bool probing = !strcmp(argv[1], "probe");
    bool announcing = !strcmp(argv[1], "announce");
    if ((probing || announcing) && argc != 4) return 2;
    unsigned index = if_nametoindex(argv[2]);
    if (!index) { perror("interface"); return 2; }
    uint8_t ips[8][4];
    for (int i = 3; i < argc; i++) {
        if (inet_pton(AF_INET, argv[i], ips[i-3]) != 1) return 2;
        uint8_t *a = ips[i-3];
        if (!(a[0] == 10 || (a[0] == 172 && a[1] >= 16 && a[1] <= 31) ||
              (a[0] == 192 && a[1] == 168))) return 2;
    }
    pid_t parent = getppid();
    if (parent == 1 || prctl(PR_SET_PDEATHSIG, SIGTERM) || getppid() != parent) return 2;
    struct ifaddrs *all;
    if (getifaddrs(&all)) { perror("getifaddrs"); return 2; }
    struct own_mac *own = NULL;
    uint8_t device_mac[6] = {0};
    for (struct ifaddrs *a = all; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_PACKET) continue;
        struct sockaddr_ll *ll = (void *)a->ifa_addr;
        if (ll->sll_halen != 6) continue;
        struct own_mac *entry = calloc(1, sizeof(*entry));
        if (!entry) return 2;
        memcpy(entry->address, ll->sll_addr, 6); entry->next = own; own = entry;
        if ((unsigned)ll->sll_ifindex == index) memcpy(device_mac, ll->sll_addr, 6);
    }
    freeifaddrs(all);
    int fd = socket(AF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC, htons(ETH_P_ARP));
    struct sockaddr_ll dev = {.sll_family = AF_PACKET, .sll_protocol = htons(ETH_P_ARP),
        .sll_ifindex = (int)index, .sll_halen = 6};
    memset(dev.sll_addr, 255, 6);
    if (fd < 0 || bind(fd, (void *)&dev, sizeof(dev))) { perror("ARP socket"); return 2; }
    struct arp_packet packet = {.hardware = htons(ARPHRD_ETHER), .protocol = htons(ETH_P_IP),
        .hlen = 6, .plen = 4, .operation = htons(ARPOP_REQUEST)};
    memcpy(packet.sender_mac, device_mac, 6); memcpy(packet.target_ip, ips[0], 4);
    if (announcing) memcpy(packet.sender_ip, ips[0], 4);
    unsigned sent = 0, reported = 0;
    uint64_t deadline = now_ms() + (announcing ? 0 : random_below(1001));
    setvbuf(stdout, NULL, _IOLBF, 0);
    puts("{\"event\":\"listening\"}");
    for (;;) {
        uint64_t now = now_ms();
        int timeout = probing || announcing ? (deadline > now ? (int)(deadline-now) : 0) : -1;
        struct pollfd pfd = {.fd = fd, .events = POLLIN};
        int rc = poll(&pfd, 1, timeout);
        if (rc < 0) { if (errno == EINTR) continue; perror("poll"); return 2; }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return 2;
        if (pfd.revents & POLLIN) {
            uint8_t buffer[2048]; struct sockaddr_ll source; socklen_t slen = sizeof(source);
            ssize_t n = recvfrom(fd, buffer, sizeof(buffer), 0, (void *)&source, &slen);
            if (n < 0) { if (errno == EINTR) continue; return 2; }
            if (source.sll_pkttype == PACKET_OUTGOING || source.sll_ifindex != (int)index) continue;
            for (int i = 3; i < argc; i++) {
                if (reported & (1u << (i-3))) continue;
                if (!claim(buffer, n, ips[i-3], probing, own)) continue;
                reported |= 1u << (i-3);
                printf("{\"event\":\"conflict\",\"address\":\"%s\"}\n", argv[i]);
                if (probing) return 1;
            }
        }
        if ((probing || announcing) && now_ms() >= deadline) {
            if (sent == 3 || (announcing && sent == 2)) { puts("{\"event\":\"clear\"}"); return 0; }
            if (sendto(fd, &packet, sizeof(packet), 0, (void *)&dev, sizeof(dev)) != sizeof(packet)) {
                perror("ARP send"); return 2;
            }
            sent++;
            if (announcing && sent == 2) return 0;
            deadline = now_ms() + (announcing || sent == 3 ? 2000 : 1000 + random_below(1001));
        }
    }
}
