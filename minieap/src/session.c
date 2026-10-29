#include "session.h"
#include "config.h"
#include "logging.h"
#include "md5.h"
#include "sysu.h"
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <linux/filter.h>
#include <linux/if_packet.h>
#include <limits.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* 官方 x64 196705b7：HandshakeToSAM@437740、CreateDirPktHead@43f3f0、ResponseSender@4793b0。 */
#define DIR_HEADER 42
#define DIR_AUTH_TAIL 16
#define DIR_MAX 2048
#define PARAMS_REQUIRED 63

static const uint8_t kIp[64] = {
    58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4,
    62, 54, 46, 38, 30, 22, 14, 6, 64, 56, 48, 40, 32, 24, 16, 8,
    57, 49, 41, 33, 25, 17, 9, 1, 59, 51, 43, 35, 27, 19, 11, 3,
    61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7};
static const uint8_t kFp[64] = {
    40, 8, 48, 16, 56, 24, 64, 32, 39, 7, 47, 15, 55, 23, 63, 31,
    38, 6, 46, 14, 54, 22, 62, 30, 37, 5, 45, 13, 53, 21, 61, 29,
    36, 4, 44, 12, 52, 20, 60, 28, 35, 3, 43, 11, 51, 19, 59, 27,
    34, 2, 42, 10, 50, 18, 58, 26, 33, 1, 41, 9, 49, 17, 57, 25};
static const uint8_t kPc1[56] = {
    57, 49, 41, 33, 25, 17, 9, 1, 58, 50, 42, 34, 26, 18,
    10, 2, 59, 51, 43, 35, 27, 19, 11, 3, 60, 52, 44, 36,
    63, 55, 47, 39, 31, 23, 15, 7, 62, 54, 46, 38, 30, 22,
    14, 6, 61, 53, 45, 37, 29, 21, 13, 5, 28, 20, 12, 4};
static const uint8_t kPc2[48] = {
    14, 17, 11, 24, 1, 5, 3, 28, 15, 6, 21, 10,
    23, 19, 12, 4, 26, 8, 16, 7, 27, 20, 13, 2,
    41, 52, 31, 37, 47, 55, 30, 40, 51, 45, 33, 48,
    44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32};
static const uint8_t kShifts[16] = {1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1};
static const uint8_t kE[48] = {
    32, 1, 2, 3, 4, 5, 4, 5, 6, 7, 8, 9, 8, 9, 10, 11, 12, 13,
    12, 13, 14, 15, 16, 17, 16, 17, 18, 19, 20, 21, 20, 21, 22, 23, 24, 25,
    24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1};
static const uint8_t kS[8][64] = {
    {14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7,
     0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
     4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0,
     15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13},
    {15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10,
     3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5,
     0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15,
     13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9},
    {10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8,
     13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
     13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7,
     1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12},
    {7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15,
     13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
     10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4,
     3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14},
    {2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9,
     14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
     4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14,
     11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3},
    {12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11,
     10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
     9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6,
     4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13},
    {4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1,
     13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
     1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2,
     6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12},
    {13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7,
     1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
     7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8,
     2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11}};
static const uint8_t kP[32] = {
    16, 7, 20, 21, 29, 12, 28, 17, 1, 15, 23, 26, 5, 18, 31, 10,
    2, 8, 24, 14, 32, 27, 3, 9, 19, 13, 30, 6, 22, 11, 4, 25};


static uint64_t permute(uint64_t value, const uint8_t *table, int count, int width) {
    uint64_t out = 0;
    for (int i = 0; i < count; i++) out = (out << 1) | ((value >> (width - table[i])) & 1);
    return out;
}
static void des_keys(const uint8_t key[8], int decrypt, uint64_t out[16]) {
    uint64_t raw = 0;
    for (int i = 0; i < 8; i++) raw = (raw << 8) | key[i];
    raw = permute(raw, kPc1, 56, 64);
    uint32_t c = (raw >> 28) & 0xfffffff, d = raw & 0xfffffff;
    for (int r = 0; r < 16; r++) {
        int n = kShifts[r];
        c = ((c << n) | (c >> (28 - n))) & 0xfffffff;
        d = ((d << n) | (d >> (28 - n))) & 0xfffffff;
        out[decrypt ? 15 - r : r] = permute(((uint64_t)c << 28) | d, kPc2, 48, 56);
    }
}
static void des_block(const uint64_t keys[16], const uint8_t in[8], uint8_t out[8]) {
    uint64_t raw = 0;
    for (int i = 0; i < 8; i++) raw = (raw << 8) | in[i];
    raw = permute(raw, kIp, 64, 64);
    uint32_t l = raw >> 32, r = raw;
    for (int round = 0; round < 16; round++) {
        uint64_t e = permute(r, kE, 48, 32) ^ keys[round];
        uint32_t s = 0;
        for (int i = 0; i < 8; i++) {
            unsigned v = (e >> (42 - i * 6)) & 63;
            unsigned row = ((v & 32) >> 4) | (v & 1), col = (v >> 1) & 15;
            s = (s << 4) | kS[i][row * 16 + col];
        }
        uint32_t next = l ^ (uint32_t)permute(s, kP, 32, 32);
        l = r; r = next;
    }
    raw = permute(((uint64_t)r << 32) | l, kFp, 64, 64);
    for (int i = 0; i < 8; i++) out[i] = raw >> (56 - i * 8);
}
static void des_cbc(const uint8_t key[8], const uint8_t iv[8], uint8_t *data, size_t len, int decrypt) {
    uint64_t keys[16];
    uint8_t prev[8], block[8], saved[8];
    des_keys(key, decrypt, keys);
    memcpy(prev, iv, 8);
    for (size_t off = 0; off < len; off += 8) {
        memcpy(saved, data + off, 8);
        for (int i = 0; i < 8; i++) block[i] = saved[i] ^ (decrypt ? 0 : prev[i]);
        des_block(keys, block, data + off);
        if (decrypt) for (int i = 0; i < 8; i++) data[off + i] ^= prev[i];
        memcpy(prev, decrypt ? saved : data + off, 8);
    }
    explicit_bzero(keys, sizeof(keys));
    explicit_bzero(prev, sizeof(prev));
    explicit_bzero(block, sizeof(block));
    explicit_bzero(saved, sizeof(saved));
}
static void rc4_decode(uint8_t data[8]) {
    static const uint8_t key[] = "com.ruijie.www";
    uint8_t s[256]; unsigned j = 0, x = 0;
    for (unsigned i = 0; i < 256; i++) s[i] = i;
    for (unsigned i = 0; i < 256; i++) {
        j = (j + s[i] + key[i % (sizeof(key) - 1)]) & 255;
        uint8_t t = s[i]; s[i] = s[j]; s[j] = t;
    }
    j = 0;
    for (int i = 0; i < 8; i++) {
        x = (x + 1) & 255; j = (j + s[x]) & 255;
        uint8_t t = s[x]; s[x] = s[j]; s[j] = t;
        data[i] ^= s[(s[x] + s[j]) & 255];
    }
    explicit_bzero(s, sizeof(s));
}
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }
static void put32(uint8_t *p, uint32_t n) {
    for (int i = 0; i < 4; i++) p[i] = n >> (24 - 8 * i);
}
static void put64(uint8_t *p, uint64_t n) { put32(p, n >> 32); put32(p + 4, n); }

typedef struct {
    uint32_t peer_ip, hello;
    uint16_t local_port, server_port;
    uint8_t key[8], iv[8], radius, fields, version;
    uint64_t utc;
    int64_t received_at;
} session_params;
typedef struct {
    uint8_t ip[4], mask[4], gateway[4], dns[4], mac[6], next_mac[6];
} local_info;
static session_params params;
static local_info local;
static int sock = -1, ifindex, active, attempts, backoff, update_needed, confirmed_once, need_userinfo;
static int transaction_userinfo, lost_rounds, expired_reported;
static int64_t deadline, confirm_deadline;
static uint32_t sequence, pending_sequence;
static uint8_t wire_version;
static uint64_t timestamp_counter, receive_timestamp, receive_counter;
static uint32_t last_received_transaction;
static size_t packet_len;
static uint8_t packet[DIR_MAX];
static char device[IFNAMSIZ];
static SESSION_STATE state;

static void change_state(SESSION_STATE value) { state = value; status_refresh(); }
static int peer_allowed(uint32_t peer) {
    if (peer >> 24 == 0 || peer >> 24 == 127 || peer >> 28 >= 14 ||
        peer >> 16 == 0xa9fe || peer == UINT32_MAX) return 0;
    struct ifaddrs *all;
    if (getifaddrs(&all)) return 0;
    int valid = 1;
    for (struct ifaddrs *a = all; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        uint32_t ip = ntohl(((struct sockaddr_in *)a->ifa_addr)->sin_addr.s_addr);
        uint32_t mask = a->ifa_netmask ? ntohl(((struct sockaddr_in *)a->ifa_netmask)->sin_addr.s_addr) : 0;
        if (peer == ip || (strcmp(a->ifa_name, device) && mask && (peer & mask) == (ip & mask))) valid = 0;
    }
    freeifaddrs(all);
    return valid;
}
static int read_local(local_info *info) {
    struct ifaddrs *all;
    memset(info, 0, sizeof(*info));
    if (getifaddrs(&all)) return 0;
    int found = 0;
    for (struct ifaddrs *a = all; a; a = a->ifa_next) {
        if (!a->ifa_addr || strcmp(a->ifa_name, device) || a->ifa_addr->sa_family != AF_INET) continue;
        uint32_t mask = a->ifa_netmask ? ((struct sockaddr_in *)a->ifa_netmask)->sin_addr.s_addr : 0;
        if (found && mask == UINT32_MAX) continue;
        memcpy(info->ip, &((struct sockaddr_in *)a->ifa_addr)->sin_addr, 4);
        memcpy(info->mask, &mask, 4);
        found = 1;
        /* 优先主接口网段，避免选择同一设备上既有的 /32 地址池。 */
        if (mask != UINT32_MAX) break;
    }
    freeifaddrs(all);
    if (!found) return 0;
    char path[64], line[256], name[IFNAMSIZ];
    snprintf(path, sizeof(path), "/sys/class/net/%s/address", device);
    FILE *fp = fopen(path, "r");
    unsigned mac[6];
    if (!fp) return 0;
    int n = fscanf(fp, "%x:%x:%x:%x:%x:%x", mac, mac+1, mac+2, mac+3, mac+4, mac+5);
    fclose(fp);
    if (n != 6) return 0;
    for (int i = 0; i < 6; i++) info->mac[i] = mac[i];
    fp = fopen("/proc/net/route", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            unsigned long dest, gateway; unsigned flags;
            if (sscanf(line, "%15s %lx %lx %x", name, &dest, &gateway, &flags) == 4 &&
                !strcmp(name, device) && !dest && (flags & 3) == 3) {
                uint32_t value = gateway; memcpy(info->gateway, &value, 4); break;
            }
        }
        fclose(fp);
    }
    fp = fopen("/tmp/resolv.conf.d/resolv.conf.auto", "r");
    if (!fp) fp = fopen("/etc/resolv.conf", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            char address[46]; struct in_addr ip;
            if (sscanf(line, "nameserver %45s", address) == 1 && inet_pton(AF_INET, address, &ip) == 1 &&
                (ntohl(ip.s_addr) >> 24) != 127) { memcpy(info->dns, &ip, 4); break; }
        }
        fclose(fp);
    }
    uint32_t own = be32(info->ip), mask = be32(info->mask);
    uint32_t next = (own & mask) == (params.peer_ip & mask) ? params.peer_ip : be32(info->gateway);
    fp = fopen("/proc/net/arp", "r");
    if (!fp || !next) { if (fp) fclose(fp); return -1; }
    int neighbor = 0;
    while (fgets(line, sizeof(line), fp)) {
        char address[16], hardware[18], dev[IFNAMSIZ]; unsigned flags;
        struct in_addr ip;
        if (sscanf(line, "%15s %*s %x %17s %*s %15s", address, &flags, hardware, dev) != 4 ||
            strcmp(dev, device) || !(flags & 2) || inet_pton(AF_INET, address, &ip) != 1 || ntohl(ip.s_addr) != next) continue;
        if (sscanf(hardware, "%x:%x:%x:%x:%x:%x", mac, mac+1, mac+2, mac+3, mac+4, mac+5) != 6 || (mac[0] & 1)) continue;
        unsigned nonzero = 0;
        for (int i = 0; i < 6; i++) { info->next_mac[i] = mac[i]; nonzero |= mac[i]; }
        neighbor = nonzero != 0; break;
    }
    fclose(fp);
    return neighbor ? 1 : -1;
}
static int install_filter(void) {
    struct sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 12), BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x0800, 0, 15),
        BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 23), BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 17, 0, 13),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 26), BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, params.peer_ip, 0, 11),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 30), BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, be32(local.ip), 0, 9),
        BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 20), BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x3fff, 7, 0),
        BPF_STMT(BPF_LDX | BPF_B | BPF_MSH, 14),
        BPF_STMT(BPF_LD | BPF_H | BPF_IND, 14), BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, params.server_port, 0, 4),
        BPF_STMT(BPF_LD | BPF_H | BPF_IND, 16), BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, params.local_port, 0, 2),
        BPF_STMT(BPF_RET | BPF_K, DIR_MAX), BPF_STMT(BPF_RET | BPF_K, 0), BPF_STMT(BPF_RET | BPF_K, 0)
    };
    struct sock_fprog program = {sizeof(filter) / sizeof(filter[0]), filter};
    return !setsockopt(sock, SOL_SOCKET, SO_ATTACH_FILTER, &program, sizeof(program));
}
static int reject_all(void) {
    struct sock_filter filter = BPF_STMT(BPF_RET | BPF_K, 0);
    struct sock_fprog program = {1, &filter};
    return sock < 0 || !setsockopt(sock, SOL_SOCKET, SO_ATTACH_FILTER, &program, sizeof(program));
}
RESULT session_init(const char *ifname) {
    if (!ifname || strlen(ifname) >= sizeof(device)) return FAILURE;
    strcpy(device, ifname); ifindex = if_nametoindex(device);
    sock = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, htons(0x0800));
    struct sockaddr_ll bindaddr = {.sll_family = AF_PACKET, .sll_protocol = htons(0x0800), .sll_ifindex = ifindex};
    int ignore = 1;
    if (sock < 0 || !ifindex || bind(sock, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) ||
        setsockopt(sock, SOL_PACKET, PACKET_IGNORE_OUTGOING, &ignore, sizeof(ignore))) {
        session_destroy(); return FAILURE;
    }
    if (!reject_all()) { session_destroy(); return FAILURE; }
    return SUCCESS;
}
void session_reset(void) {
    reject_all();
    explicit_bzero(&params, sizeof(params)); params.local_port = 80; params.version = 1;
    explicit_bzero(&local, sizeof(local)); explicit_bzero(packet, sizeof(packet));
    active = attempts = backoff = update_needed = confirmed_once = need_userinfo = transaction_userinfo = 0;
    pending_sequence = last_received_transaction = 0; lost_rounds = expired_reported = 0; packet_len = 0; deadline = confirm_deadline = 0;
    wire_version = 0; timestamp_counter = receive_timestamp = receive_counter = 0; change_state(SESSION_NONE);
}
void session_destroy(void) { session_reset(); if (sock >= 0) close(sock); sock = -1; }
int session_configure(const uint8_t *tlvs, size_t len) {
    if ((!tlvs && len) || len > DIR_MAX || active) return 0;
    session_params next = params;
    if (!next.local_port) next.local_port = 80;
    if (!next.version) next.version = 1;
    int found = 0;
    for (size_t pos = 0; pos < len;) {
        if (tlvs[pos] == 0) { for (; pos < len; pos++) if (tlvs[pos]) return -1; break; }
        if (len - pos < 2 || tlvs[pos+1] > len - pos - 2) return -1;
        uint8_t type = tlvs[pos], n = tlvs[pos+1]; const uint8_t *p = tlvs + pos + 2;
        switch (type) {
        case 0x59:
            if (n != 4) return -1;
            next.hello = be32(p); found = 1; break;
        case 0x5a: case 0x6e:
            if (n != 4 || !be32(p) || be32(p) > 65535) return -1;
            if (type == 0x5a) { next.server_port = be32(p); next.fields |= 1; }
            else next.local_port = be32(p);
            found = 1; break;
        case 0x5b:
            if (n != 4 || !peer_allowed(be32(p))) return -1;
            next.peer_ip = be32(p); next.fields |= 2; found = 1; break;
        case 0x5c: case 0x5d: {
            if (n != 8) return -1;
            uint8_t *out = type == 0x5c ? next.key : next.iv;
            memcpy(out, p, 8); rc4_decode(out); next.fields |= type == 0x5c ? 4 : 8; found = 1; break;
        }
        case 0x5e:
            if (n != 8) return -1;
            next.utc = be64(p); next.received_at = monotonic_ms(); next.fields |= 32; found = 1; break;
        case 0x61:
            if (n != 1 || (p[0] != 9 && p[0] != 5)) return -1;
            next.radius = p[0]; next.fields |= 16; found = 1; break;
        case 0x79:
            if (n != 1 || !p[0]) return -1;
            next.version = p[0] > 2 ? 2 : p[0]; found = 1; break;
        }
        pos += 2 + n;
    }
    if (found) params = next;
    explicit_bzero(&next, sizeof(next)); return found;
}
static size_t put_tlv(uint8_t *out, uint8_t type, const uint8_t *data, size_t len) {
    out[0] = type; size_t header = params.radius == 9 ? 2 : 3;
    out[1] = params.radius == 9 ? len : len >> 8;
    if (header == 3) out[2] = len;
    if (len) memcpy(out + header, data, len);
    return header + len;
}
static size_t build_payload(uint8_t *out, int userinfo) {
    const char *user = get_eap_config()->username; size_t len = strlen(user), n = 0;
    if (!len || len > 64) return 0;
    uint8_t type = params.radius == 9 ? (userinfo ? 8 : 2) : (userinfo ? 0x2d : 0x23);
    n += put_tlv(out + n, 1, &type, 1);
    n += put_tlv(out + n, params.radius == 9 ? 3 : 2, (const uint8_t *)user, len);
    if (params.radius == 9) {
        n += put_tlv(out + n, 4, local.ip, 4); n += put_tlv(out + n, 5, local.mac, 6);
        if (userinfo) {
            n += put_tlv(out + n, 0x0b, local.mask, 4); n += put_tlv(out + n, 0x0c, local.gateway, 4);
            n += put_tlv(out + n, 0x0d, local.dns, 4);
        }
    } else if (userinfo) {
        static const uint8_t svc[] = "internet"; n += put_tlv(out + n, 3, svc, sizeof(svc) - 1);
        uint8_t server[6]; put32(server, params.peer_ip); server[4] = params.server_port >> 8; server[5] = params.server_port;
        n += put_tlv(out + n, 0xcd, server, sizeof(server)); n += put_tlv(out + n, 0xc9, local.ip, 4);
        n += put_tlv(out + n, 0xca, local.mask, 4); n += put_tlv(out + n, 0xcb, local.gateway, 4);
        n += put_tlv(out + n, 0xcc, local.dns, 4); n += put_tlv(out + n, 0xd3, NULL, 0);
    }
    return n;
}
static uint64_t timestamp_for(unsigned version) {
    return version == 1 ? params.utc + (uint64_t)(monotonic_ms() - params.received_at) / 1000 : params.utc + ++timestamp_counter;
}
static void packet_md5(uint8_t *data, size_t len, uint8_t digest[16]) {
    MD5_CTX ctx;
    MD5Init(&ctx); MD5Update(&ctx, data, len); MD5Update(&ctx, params.key, 8); MD5Update(&ctx, params.iv, 8);
    MD5Final(digest, &ctx); explicit_bzero(&ctx, sizeof(ctx));
}
static int build_packet(void) {
    explicit_bzero(packet, sizeof(packet));
    transaction_userinfo = need_userinfo || monotonic_ms() >= confirm_deadline;
    size_t body = build_payload(packet + DIR_HEADER, transaction_userinfo); if (!body) return 0;
    body = (body + 7) & ~(size_t)7;
    des_cbc(params.key, params.iv, packet + DIR_HEADER, body, 0);
    packet_len = DIR_HEADER + body; packet[0] = wire_version; packet[1] = 1;
    put32(packet + 2, 1); packet[6] = packet_len >> 8; packet[7] = packet_len;
    if (!++sequence) sequence++;
    pending_sequence = sequence; put32(packet + 24, sequence); put64(packet + 28, timestamp_for(wire_version));
    packet[36] = packet[37] = 1; put32(packet + 38, body);
    uint8_t digest[16]; packet_md5(packet, packet_len, digest); memcpy(packet + 8, digest, 16);
    return 1;
}
static uint32_t checksum_sum(uint32_t sum, const uint8_t *p, size_t len) {
    while (len > 1) { sum += (unsigned)p[0] << 8 | p[1]; p += 2; len -= 2; }
    if (len) sum += (unsigned)*p << 8;
    return sum;
}
static uint16_t checksum_end(uint32_t sum) {
    while (sum >> 16) sum = (sum & 65535) + (sum >> 16);
    return (uint16_t)~sum;
}
static uint32_t udp_sum(const uint8_t *ip, const uint8_t *udp, size_t len) {
    return checksum_sum(checksum_sum(17 + len, ip + 12, 8), udp, len);
}
static int send_packet(const uint8_t *data, size_t len) {
    uint8_t frame[DIR_MAX + 42] = {0};
    if (len > DIR_MAX || (!local.next_mac[0] && !local.next_mac[1] && !local.next_mac[2] &&
        !local.next_mac[3] && !local.next_mac[4] && !local.next_mac[5])) return 0;
    memcpy(frame, local.next_mac, 6); memcpy(frame + 6, local.mac, 6); frame[12] = 8;
    uint8_t *ip = frame + 14, *udp = ip + 20;
    size_t iplen = 28 + len, udplen = 8 + len;
    ip[0] = 0x45; ip[2] = iplen >> 8; ip[3] = iplen; ip[8] = 128; ip[9] = 17;
    memcpy(ip + 12, local.ip, 4); put32(ip + 16, params.peer_ip);
    uint16_t sum = checksum_end(checksum_sum(0, ip, 20)); ip[10] = sum >> 8; ip[11] = sum;
    udp[0] = params.local_port >> 8; udp[1] = params.local_port; udp[2] = params.server_port >> 8; udp[3] = params.server_port;
    udp[4] = udplen >> 8; udp[5] = udplen; memcpy(udp + 8, data, len);
    sum = checksum_end(udp_sum(ip, udp, udplen)); if (!sum) sum = 65535; udp[6] = sum >> 8; udp[7] = sum;
    struct sockaddr_ll to = {.sll_family = AF_PACKET, .sll_protocol = htons(0x0800), .sll_ifindex = ifindex, .sll_halen = 6};
    memcpy(to.sll_addr, local.next_mac, 6);
    int result = sendto(sock, frame, len + 42, 0, (struct sockaddr *)&to, sizeof(to)) == (ssize_t)(len + 42);
    explicit_bzero(frame, sizeof(frame)); return result;
}
static SESSION_EVENT defer(void) {
    int expired = transaction_userinfo && confirmed_once && attempts >= 3 && ++lost_rounds >= 2 && !expired_reported;
    if (!backoff) backoff = params.hello > 120 ? params.hello : 120;
    else backoff = backoff > 450 ? 900 : backoff * 2;
    if (backoff > 900) backoff = 900;
    pending_sequence = attempts = 0; need_userinfo = transaction_userinfo;
    deadline = monotonic_ms() + (int64_t)backoff * 1000; change_state(SESSION_BACKOFF);
    if (transaction_userinfo) PR_WARN("直连会话确认未获回应，%d 秒后重试", backoff);
    else PR_WARN("直连心跳发送失败，%d 秒后重试", backoff);
    if (expired) { expired_reported = 1; return SESSION_EXPIRED; }
    return SESSION_NO_EVENT;
}
void session_authenticated(void) {
    if (active || sock < 0 || (params.fields & PARAMS_REQUIRED) != PARAMS_REQUIRED) return;
    wire_version = params.version;
    PR_DBG("直连维护：模式=%u，版本=%u，间隔=%u 秒", params.radius, wire_version, params.hello);
    active = need_userinfo = 1; deadline = confirm_deadline = monotonic_ms(); change_state(SESSION_PENDING);
}
void session_update_address(void) {
    if (!active) return;
    local_info current;
    if (read_local(&current) <= 0 || !memcmp(&current, &local, sizeof(local))) return;
    local = current; update_needed = need_userinfo = 1;
    pending_sequence = attempts = 0; deadline = monotonic_ms(); change_state(SESSION_PENDING);
}
int session_fd(void) { return sock; }
SESSION_STATE session_state(void) { return state; }
int session_timeout(void) {
    if (!active || !deadline) return -1;
    int64_t wait = deadline - monotonic_ms(); return wait <= 0 ? 0 : wait > INT_MAX ? INT_MAX : (int)wait;
}
static unsigned hello_interval(void) {
    unsigned interval = params.hello;
    return interval && interval < 10 ? 10 : interval > 3600 ? 3600 : interval;
}
static unsigned confirm_interval(void) {
    unsigned interval = hello_interval() * 5;
    return interval < 600 ? 600 : interval > 1800 ? 1800 : interval;
}
static void schedule_maintenance(void) {
    int64_t hello = monotonic_ms() + (int64_t)hello_interval() * 1000;
    deadline = hello_interval() && hello < confirm_deadline ? hello : confirm_deadline;
}
SESSION_EVENT session_tick(void) {
    if (!active || !deadline || deadline > monotonic_ms()) return SESSION_NO_EVENT;
    if (pending_sequence) { if (attempts >= 3) return defer(); }
    else {
        int available = read_local(&local);
        if (!available) { deadline = monotonic_ms() + 5000; return SESSION_NO_EVENT; }
        if (available < 0 || !install_filter() || !build_packet()) return defer();
        attempts = update_needed = 0;
        if (transaction_userinfo) change_state(SESSION_PENDING);
    }
    attempts++; int sent = send_packet(packet, packet_len);
    PR_DBG("直连发送：地址确认=%d，版本=%u，事务=%u，尝试=%d，成功=%d",
        transaction_userinfo, wire_version, pending_sequence, attempts, sent);
    if (sent && !transaction_userinfo) {
        /* 原版 Hello 使用 PostPacketNoResponse；只有低频地址确认事务等待 ACK。 */
        pending_sequence = attempts = 0; schedule_maintenance();
    } else deadline = monotonic_ms() + 3000;
    return SESSION_NO_EVENT;
}
static int valid_timestamp(uint64_t value, unsigned version) {
    if (!params.utc) return 1;
    if (version == 2) return value >= receive_counter && value >= params.utc;
    uint64_t current = params.utc + (uint64_t)(monotonic_ms() - params.received_at) / 1000;
    return value > current ? value - current <= 60 : current - value <= 60;
}
static void acknowledge(const uint8_t *data) {
    uint8_t ack[DIR_HEADER] = {0};
    ack[0] = data[0]; ack[1] = 2; memcpy(ack + 2, data + 2, 4); ack[7] = DIR_HEADER;
    memcpy(ack + 24, data + 24, 4); put64(ack + 28, timestamp_for(data[0])); ack[36] = ack[37] = 1;
    uint8_t digest[16]; packet_md5(ack, sizeof(ack), digest); memcpy(ack + 8, digest, 16); send_packet(ack, sizeof(ack));
}
static SESSION_EVENT decode_response(uint8_t *data, size_t len) {
    if (len < DIR_HEADER || len > DIR_MAX || data[0] < 1 || data[0] > params.version ||
        (((size_t)data[6] << 8) | data[7]) != len || data[36] != 1 || data[37] != 1 ||
        !valid_timestamp(be64(data + 28), data[0])) return SESSION_NO_EVENT;
    uint8_t expected[16], digest[16]; memcpy(expected, data + 8, 16); memset(data + 8, 0, 16);
    packet_md5(data, len, digest); unsigned different = 0;
    for (int i = 0; i < 16; i++) different |= expected[i] ^ digest[i];
    if (different) return SESSION_NO_EVENT;
    if (data[1] == 2) {
        if (!pending_sequence || len != DIR_HEADER || be32(data + 2) != 1 ||
            be32(data + 24) != pending_sequence || be32(data + 38)) return SESSION_NO_EVENT;
        if (data[0] < wire_version) wire_version = data[0];
        PR_DBG("直连确认：版本=%u，事务=%u", wire_version, pending_sequence);
        PR_INFO("直连会话确认已收到匹配 ACK");
        confirmed_once = 1; lost_rounds = expired_reported = 0;
        if (data[0] == 2) receive_counter = be64(data + 28);
        pending_sequence = 0; backoff = attempts = 0;
        if (transaction_userinfo) need_userinfo = update_needed;
        confirm_deadline = monotonic_ms() + (int64_t)confirm_interval() * 1000;
        schedule_maintenance();
        if (update_needed) deadline = monotonic_ms();
        change_state(SESSION_CONFIRMED); return SESSION_NO_EVENT;
    }
    size_t body = len - DIR_HEADER;
    if (data[1] != 1 || !be32(data + 2) || !body || (body & 7) || be32(data + 38) != body) return SESSION_NO_EVENT;
    uint32_t id = be32(data + 24); if (!id) return SESSION_NO_EVENT;
    if (data[0] < wire_version) wire_version = data[0];
    acknowledge(data);
    if (id == last_received_transaction && be64(data + 28) <= receive_timestamp) return SESSION_NO_EVENT;
    last_received_transaction = id; receive_timestamp = be64(data + 28);
    if (data[0] == 2) receive_counter = receive_timestamp;
    des_cbc(params.key, params.iv, data + DIR_HEADER, body, 1);
    const uint8_t *p = data + DIR_HEADER;
    if (params.radius == 5 && body >= 4 && p[0] == 1 && p[1] == 0 && p[2] == 1) {
        if (p[3] == 3) return SESSION_LOGOFF;
        if (p[3] == 4) return SESSION_REAUTH;
    } else if (params.radius == 9 && body >= 4 && p[0] == 1 && p[1] == 1) {
        if (p[2] == 8 && p[3] <= body - 4) return SESSION_LOGOFF;
        if (p[2] == 9) {
            for (size_t pos = 3; pos + 2 <= body && p[pos];) {
                uint8_t type = p[pos], n = p[pos + 1];
                if (n > body - pos - 2) break;
                if (type == 0x13 && n == 1) {
                    if (!p[pos + 2]) return SESSION_LOGOFF;
                    PR_INFO("服务端已确认地址认证");
                    break;
                }
                pos += 2 + n;
            }
        }
    }
    return SESSION_NO_EVENT;
}
static SESSION_EVENT receive_frame(uint8_t *frame, size_t len) {
    if (len < 14 + 20 + 8 + DIR_HEADER || memcmp(frame, local.mac, 6) || memcmp(frame + 6, local.next_mac, 6) ||
        frame[12] != 8 || frame[13] != 0) return SESSION_NO_EVENT;
    uint8_t *ip = frame + 14; size_t ihl = (ip[0] & 15) * 4, iplen = (size_t)ip[2] << 8 | ip[3];
    if (ip[0] >> 4 != 4 || ihl < 20 || iplen < ihl + 8 || iplen > len - 14 ||
        ip[9] != 17 || ((ip[6] & 0x3f) | ip[7]) || checksum_end(checksum_sum(0, ip, ihl)) ||
        be32(ip + 12) != params.peer_ip || memcmp(ip + 16, local.ip, 4)) return SESSION_NO_EVENT;
    uint8_t *udp = ip + ihl; size_t udplen = (size_t)udp[4] << 8 | udp[5];
    if (udplen != iplen - ihl || udplen < 8 + DIR_HEADER ||
        ((unsigned)udp[0] << 8 | udp[1]) != params.server_port ||
        ((unsigned)udp[2] << 8 | udp[3]) != params.local_port ||
        ((udp[6] || udp[7]) && checksum_end(udp_sum(ip, udp, udplen)))) return SESSION_NO_EVENT;
    return decode_response(udp + 8, udplen - 8);
}
SESSION_EVENT session_dispatch(void) {
    uint8_t data[DIR_MAX];
    for (int count = 0; count < 16; count++) {
        struct sockaddr_ll from; socklen_t fromlen = sizeof(from);
        ssize_t n = recvfrom(sock, data, sizeof(data), MSG_TRUNC, (struct sockaddr *)&from, &fromlen);
        if (n < 0) break;
        if (!active || n > DIR_MAX || fromlen != sizeof(from) || from.sll_family != AF_PACKET ||
            from.sll_ifindex != ifindex || from.sll_pkttype != PACKET_HOST) continue;
        SESSION_EVENT event = receive_frame(data, n);
        if (event != SESSION_NO_EVENT) { explicit_bzero(data, sizeof(data)); return event; }
    }
    explicit_bzero(data, sizeof(data)); return SESSION_NO_EVENT;
}
