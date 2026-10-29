#define _GNU_SOURCE
#define getifaddrs fake_getifaddrs
#define freeifaddrs fake_freeifaddrs
#define sendto fake_sendto
#define recvfrom fake_recvfrom
#define setsockopt fake_setsockopt
#define fopen fake_fopen
#include "../src/session.c"
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>

static int64_t now = 100000;
static int sends, messages, refreshes, have_addresses = 1, fail_sends;
static uint8_t sent_frame[DIR_MAX + 42], incoming[DIR_MAX];
static size_t sent_len, incoming_len;
static struct sockaddr_ll incoming_peer;
static struct sock_filter installed[32];
static unsigned installed_len;
int64_t monotonic_ms(void) { return now; }
EAP_CONFIG *get_eap_config(void) { static EAP_CONFIG auth = {"test", "unused"}; return &auth; }
void status_refresh(void) { refreshes++; }
void print_log(const char *level, const char *func, const char *format, ...) { (void)level; (void)func; (void)format; messages++; }
int fake_getifaddrs(struct ifaddrs **out) {
    static struct ifaddrs pool, wan, lan;
    static struct sockaddr_in pool_ip, pool_mask, wan_ip, lan_ip, wan_mask, lan_mask;
    pool_ip.sin_family = pool_mask.sin_family = wan_ip.sin_family = lan_ip.sin_family = wan_mask.sin_family = lan_mask.sin_family = AF_INET;
    inet_pton(AF_INET, "172.25.193.2", &pool_ip.sin_addr); pool_mask.sin_addr.s_addr = UINT32_MAX;
    inet_pton(AF_INET, "172.25.192.188", &wan_ip.sin_addr); inet_pton(AF_INET, "255.255.252.0", &wan_mask.sin_addr);
    inet_pton(AF_INET, "192.168.1.1", &lan_ip.sin_addr); inet_pton(AF_INET, "255.255.255.0", &lan_mask.sin_addr);
    pool = (struct ifaddrs){.ifa_name="eth0", .ifa_addr=(void *)&pool_ip, .ifa_netmask=(void *)&pool_mask, .ifa_next=&wan};
    wan = (struct ifaddrs){.ifa_name="eth0", .ifa_addr=(void *)&wan_ip, .ifa_netmask=(void *)&wan_mask, .ifa_next=&lan};
    lan = (struct ifaddrs){.ifa_name="br-lan", .ifa_addr=(void *)&lan_ip, .ifa_netmask=(void *)&lan_mask};
    *out = have_addresses ? &pool : NULL; return 0;
}
void fake_freeifaddrs(struct ifaddrs *all) { (void)all; }
FILE *fake_fopen(const char *path, const char *mode) {
    (void)mode; const char *text = NULL;
    if (!strcmp(path, "/sys/class/net/eth0/address")) text = "00:01:02:03:04:05\n";
    else if (!strcmp(path, "/proc/net/route")) {
        static char route[80];
        snprintf(route, sizeof(route), "eth0 00000000 %08x 0003\n", (unsigned)htonl(0xac19c001));
        text = route;
    }
    else if (!strcmp(path, "/proc/net/arp")) text = "172.25.192.1 0x1 0x2 02:03:04:05:06:07 * eth0\n";
    else if (!strcmp(path, "/tmp/resolv.conf.d/resolv.conf.auto")) text = "nameserver 202.116.64.1\n";
    return text ? fmemopen((void *)text, strlen(text), "r") : NULL;
}
int fake_setsockopt(int fd, int level, int option, const void *value, socklen_t len) {
    (void)fd; (void)level; (void)len;
    if (option == SO_ATTACH_FILTER) { const struct sock_fprog *p = value; assert(p->len <= 32); installed_len = p->len; memcpy(installed, p->filter, p->len * sizeof(*installed)); }
    return 0;
}
ssize_t fake_sendto(int fd, const void *data, size_t len, int flags, const struct sockaddr *peer, socklen_t peerlen) {
    (void)fd; (void)flags;
    const struct sockaddr_ll *p = (const struct sockaddr_ll *)peer;
    assert(peerlen == sizeof(*p) && p->sll_family == AF_PACKET && p->sll_ifindex == ifindex && p->sll_protocol == htons(0x0800));
    assert(len <= sizeof(sent_frame)); memcpy(sent_frame, data, len); sent_len = len; sends++; return fail_sends ? -1 : (ssize_t)len;
}
ssize_t fake_recvfrom(int fd, void *data, size_t len, int flags, struct sockaddr *peer, socklen_t *peerlen) {
    (void)fd; (void)flags;
    if (!incoming_len) { errno = EAGAIN; return -1; }
    size_t n = incoming_len; memcpy(data, incoming, len < n ? len : n); memcpy(peer, &incoming_peer, sizeof(incoming_peer));
    *peerlen = sizeof(incoming_peer); incoming_len = 0; return n;
}
static void fill_params(void) {
    const uint8_t tlvs[] = {
        0x59,4,0,0,0,120, 0x5a,4,0,0,0,53, 0x5b,4,202,116,64,82,
        0x5c,8,0,1,2,3,4,5,6,7, 0x5d,8,8,9,10,11,12,13,14,15,
        0x5e,8,0,0,0,0,0x12,0x34,0x56,0x78, 0x61,1,9, 0x6e,4,0,0,0,138, 0x79,1,2
    };
    assert(session_configure(tlvs, sizeof(tlvs)) == 1);
}
static size_t response(uint8_t *out, uint32_t transaction, const uint8_t *plain, size_t len) {
    size_t body = (len + 7) & ~(size_t)7, total = DIR_HEADER + body;
    memset(out, 0, total); out[0] = params.version; out[1] = body ? 1 : 2; out[6] = total >> 8; out[7] = total;
    put32(out + 2, 1); put32(out + 24, transaction); put64(out + 28, params.utc + 100);
    out[36] = out[37] = 1; put32(out + 38, body);
    if (len) memcpy(out + DIR_HEADER, plain, len);
    des_cbc(params.key, params.iv, out + DIR_HEADER, body, 0);
    uint8_t digest[16]; packet_md5(out, total, digest); memcpy(out + 8, digest, 16); return total;
}
static size_t server_frame(uint8_t *out, const uint8_t *data, size_t len) {
    assert(send_packet(data, len)); memcpy(out, sent_frame, sent_len);
    memcpy(out, local.mac, 6); memcpy(out + 6, local.next_mac, 6);
    uint8_t *ip = out + 14, *udp = ip + 20;
    put32(ip + 12, params.peer_ip); memcpy(ip + 16, local.ip, 4); ip[10] = ip[11] = 0;
    uint16_t sum = checksum_end(checksum_sum(0, ip, 20)); ip[10] = sum >> 8; ip[11] = sum;
    udp[0] = params.server_port >> 8; udp[1] = params.server_port; udp[2] = params.local_port >> 8; udp[3] = params.local_port;
    udp[6] = udp[7] = 0; sum = checksum_end(udp_sum(ip, udp, len + 8)); if (!sum) sum = 65535; udp[6] = sum >> 8; udp[7] = sum;
    return sent_len;
}
static unsigned bpf_run(const uint8_t *frame, size_t len) {
    uint32_t a = 0, x = 0;
    for (unsigned pc = 0; pc < installed_len; pc++) {
        struct sock_filter f = installed[pc]; size_t at = f.k;
        switch (f.code) {
        case BPF_LD | BPF_H | BPF_IND: at += x; /* fall through */
        case BPF_LD | BPF_H | BPF_ABS: if (at + 2 > len) return 0; a = (unsigned)frame[at] << 8 | frame[at+1]; break;
        case BPF_LD | BPF_B | BPF_ABS: if (at >= len) return 0; a = frame[at]; break;
        case BPF_LD | BPF_W | BPF_ABS: if (at + 4 > len) return 0; a = be32(frame + at); break;
        case BPF_LDX | BPF_B | BPF_MSH: if (at >= len) return 0; x = (frame[at] & 15) * 4; break;
        case BPF_JMP | BPF_JEQ | BPF_K: pc += a == f.k ? f.jt : f.jf; break;
        case BPF_JMP | BPF_JSET | BPF_K: pc += a & f.k ? f.jt : f.jf; break;
        case BPF_RET | BPF_K: return f.k;
        default: assert(0);
        }
    }
    assert(0); return 0;
}
int main(void) {
    const uint8_t key[8] = {0x13,0x34,0x57,0x79,0x9b,0xbc,0xdf,0xf1};
    const uint8_t plain[8] = {1,0x23,0x45,0x67,0x89,0xab,0xcd,0xef};
    const uint8_t expected[8] = {0x85,0xe8,0x13,0x54,0x0f,0x0a,0xb4,0x05};
    uint64_t keys[16]; uint8_t block[16];
    des_keys(key, 0, keys); des_block(keys, plain, block); assert(!memcmp(block, expected, 8));
    des_keys(key, 1, keys); des_block(keys, expected, block); assert(!memcmp(block, plain, 8));
    uint8_t iv[8] = {0}; memcpy(block, plain, 8); memcpy(block + 8, plain, 8);
    des_cbc(key, iv, block, 16, 0); assert(memcmp(block, block+8, 8));
    des_cbc(key, iv, block, 16, 1); assert(!memcmp(block, plain, 8) && !memcmp(block+8, plain, 8));
    uint8_t raw[8] = {0,1,2,3,4,5,6,7}; memcpy(block, raw, 8); rc4_decode(block);
    const uint8_t rc4_expected[8] = {0x12,0xe9,0xfe,0xdb,0xa6,0x91,0x2f,0x74}; assert(!memcmp(block, rc4_expected, 8));
    rc4_decode(block); assert(!memcmp(block, raw, 8));

    strcpy(device, "eth0"); sock = 7; ifindex = 2; session_reset();
    assert(session_state() == SESSION_NONE && session_timeout() == -1 && installed_len == 1);
    assert(peer_allowed(0xca744052)); assert(!peer_allowed(0x7f000001)); assert(!peer_allowed(0xc0a80102));
    assert(!peer_allowed(0xac19c0bc)); assert(!peer_allowed(0xe0000001)); assert(!peer_allowed(0)); assert(!peer_allowed(0xa9fe0101));
    local_info selected; read_local(&selected); assert(!memcmp(selected.ip, (uint8_t[]){172,25,192,188}, 4));
    fill_params(); assert(params.fields == PARAMS_REQUIRED && params.local_port == 138 && params.server_port == 53 && params.version == 2);
    assert(params.radius == 9 && params.hello == 120 && params.utc == 0x12345678);
    memcpy(block, raw, 8); rc4_decode(block); assert(!memcmp(params.key, block, 8));
    session_params saved = params;
    const uint8_t unsafe[] = {0x59,4,0,0,0,60, 0x5b,4,127,0,0,1};
    assert(session_configure(unsafe, sizeof(unsafe)) == -1 && !memcmp(&saved, &params, sizeof(saved)));
    const uint8_t truncated[] = {0x5c,8,1,2}; assert(session_configure(truncated, sizeof(truncated)) == -1);
    assert(session_configure(NULL, 0) == 0 && !memcmp(&saved, &params, sizeof(saved)));
    const uint8_t padded[] = {0,0,1}; assert(session_configure(padded, sizeof(padded)) == -1);
    const uint8_t unsupported[] = {0x61,1,3}; assert(session_configure(unsupported, sizeof(unsupported)) == -1);
    assert(timestamp_for(params.version) == params.utc + 1 && timestamp_for(params.version) == params.utc + 2);
    params.version = 1; now += 1999; assert(timestamp_for(params.version) == params.utc + 1); now++; assert(timestamp_for(params.version) == params.utc + 2); params.version = 2;

    memcpy(local.ip, (uint8_t[]){172,25,192,188}, 4); memcpy(local.mac, (uint8_t[]){0,1,2,3,4,5}, 6);
    memcpy(local.next_mac, (uint8_t[]){2,3,4,5,6,7}, 6);
    uint8_t info[256]; size_t n = build_payload(info, 1);
    const uint8_t userinfo_start[] = {1,1,8,3,4,'t','e','s','t',4,4,172,25,192,188,5,6,0,1,2,3,4,5};
    assert(n == 41 && !memcmp(info, userinfo_start, sizeof(userinfo_start)));
    n = build_payload(info, 0);
    const uint8_t hello[] = {1,1,2,3,4,'t','e','s','t',4,4,172,25,192,188,5,6,0,1,2,3,4,5};
    assert(n == sizeof(hello) && !memcmp(info, hello, n));
    session_authenticated(); assert(active && state == SESSION_PENDING && sends == 0 && refreshes > 0 && wire_version == 2);
    assert(session_configure(unsafe, sizeof(unsafe)) == 0); assert(build_packet());
    assert(packet_len == 90 && be32(packet + 2) == 1 && be32(packet + 24) == pending_sequence && be32(packet + 38) == 48);
    assert(packet[0] == 2 && packet[1] == 1 && packet[7] == 90);
    memcpy(info, packet + DIR_HEADER, 48); des_cbc(params.key, params.iv, info, 48, 1); assert(!memcmp(info, userinfo_start, sizeof(userinfo_start)));
    assert(send_packet(packet, packet_len)); assert(sent_len == 132 && sent_frame[34] == 0 && sent_frame[35] == 138 && sent_frame[37] == 53);
    assert(checksum_end(checksum_sum(0, sent_frame+14, 20)) == 0 && checksum_end(udp_sum(sent_frame+14, sent_frame+34, 98)) == 0);
    sends = 0; session_tick(); assert(sends == 1 && attempts == 1);
    now += 2999; session_tick(); assert(sends == 1); now++; session_tick(); assert(sends == 2 && attempts == 2);
    now += 3000; session_tick(); assert(sends == 3 && attempts == 3);
    now += 3000; assert(session_tick() == SESSION_NO_EVENT && sends == 3 && state == SESSION_BACKOFF && backoff == 120 && need_userinfo);
    defer(); assert(backoff == 240); defer(); assert(backoff == 480); defer(); assert(backoff == 900); defer(); assert(backoff == 900);

    state = SESSION_PENDING; pending_sequence = 99; update_needed = 0;
    uint8_t received[DIR_MAX], original[DIR_MAX]; n = response(received, 98, NULL, 0);
    assert(n == 42 && decode_response(received, n) == SESSION_NO_EVENT && state == SESSION_PENDING);
    n = response(received, 99, NULL, 0); received[8] ^= 1;
    assert(decode_response(received, n) == SESSION_NO_EVENT && state == SESSION_PENDING);
    n = response(received, 99, NULL, 0); received[7]++;
    assert(decode_response(received, n) == SESSION_NO_EVENT && state == SESSION_PENDING);
    n = response(received, 99, NULL, 0);
    received[0] = 1; put64(received + 28, params.utc + (now - params.received_at) / 1000);
    memset(received + 8, 0, 16); uint8_t actual_digest[16]; packet_md5(received, n, actual_digest); memcpy(received + 8, actual_digest, 16);
    memcpy(original, received, n);
    uint64_t first_ack_timestamp = be64(received + 28);
    uint8_t invalid_version[DIR_MAX], version_frame[DIR_MAX]; memcpy(invalid_version, received, n); invalid_version[8] ^= 1;
    assert(decode_response(invalid_version, n) == SESSION_NO_EVENT && wire_version == 2 && pending_sequence == 99);
    size_t version_frame_len = server_frame(version_frame, received, n); version_frame[29] ^= 1;
    version_frame[24] = version_frame[25] = 0; uint16_t version_sum = checksum_end(checksum_sum(0, version_frame + 14, 20));
    version_frame[24] = version_sum >> 8; version_frame[25] = version_sum; version_frame[40] = version_frame[41] = 0;
    assert(receive_frame(version_frame, version_frame_len) == SESSION_NO_EVENT && wire_version == 2 && pending_sequence == 99);
    assert(decode_response(received, n) == SESSION_NO_EVENT && state == SESSION_CONFIRMED && !pending_sequence && backoff == 0 && !need_userinfo);
    assert(params.version == 2 && wire_version == 1);
    assert(confirm_deadline == now + 600000);
    assert(deadline == now + 120000); assert(decode_response(original, n) == SESSION_NO_EVENT);
    int before_hello = sends;
    now = deadline; session_tick();
    assert(sends == before_hello + 1 && !pending_sequence && !attempts && !transaction_userinfo && state == SESSION_CONFIRMED);
    assert(packet_len == 66 && be32(packet+38) == 24 && deadline == now + 120000);
    memcpy(info, packet + DIR_HEADER, 24); des_cbc(params.key, params.iv, info, 24, 1); assert(!memcmp(info, hello, sizeof(hello)));
    now += 9000; session_tick(); assert(sends == before_hello + 1);
    for (int cycle = 0; cycle < 3; cycle++) { now = deadline; session_tick(); }
    assert(sends == before_hello + 4 && !pending_sequence && state == SESSION_CONFIRMED && deadline == confirm_deadline);
    now = deadline; session_tick();
    assert(transaction_userinfo && pending_sequence && attempts == 1 && state == SESSION_PENDING && packet_len == 90);
    assert(params.version == 2 && packet[0] == 1 && be64(packet + 28) == first_ack_timestamp + 600);
    n = response(received, pending_sequence, NULL, 0); decode_response(received, n);
    assert(state == SESSION_CONFIRMED && !pending_sequence && confirm_deadline == now + 600000);
    params.hello = 0; schedule_maintenance(); assert(deadline == confirm_deadline && session_timeout() == 600000);
    params.hello = 3600; assert(confirm_interval() == 1800); params.hello = 120; schedule_maintenance();
    fail_sends = 1; now = deadline; int old_lost = lost_rounds; session_tick(); assert(pending_sequence && attempts == 1 && !transaction_userinfo);
    now += 3000; session_tick(); now += 3000; session_tick(); now += 3000; session_tick();
    assert(state == SESSION_BACKOFF && lost_rounds == old_lost && !need_userinfo);
    fail_sends = 0; now = deadline; session_tick();
    assert(!pending_sequence && !attempts && state == SESSION_BACKOFF && lost_rounds == old_lost);
    transaction_userinfo = 1;

    pending_sequence = 105; attempts = 3; deadline = now;
    assert(session_tick() == SESSION_NO_EVENT && lost_rounds == 1);
    pending_sequence = 106; attempts = 3; deadline = now;
    assert(session_tick() == SESSION_EXPIRED && expired_reported && lost_rounds == 2);
    pending_sequence = 107; attempts = 3; deadline = now;
    assert(session_tick() == SESSION_NO_EVENT && expired_reported);

    n = response(received, 108, NULL, 0); size_t framelen = server_frame(original, received, n); assert(install_filter());
    assert(bpf_run(original, framelen) == DIR_MAX); original[26] ^= 1; assert(!bpf_run(original, framelen)); original[26] ^= 1;
    original[37] ^= 1; assert(!bpf_run(original, framelen)); original[37] ^= 1;
    pending_sequence = 108; assert(receive_frame(original, framelen) == SESSION_NO_EVENT && state == SESSION_CONFIRMED && !pending_sequence);
    n = response(received, 109, NULL, 0); framelen = server_frame(original, received, n);
    pending_sequence = 109; original[6] ^= 1; receive_frame(original, framelen); assert(pending_sequence == 109); original[6] ^= 1;
    original[22] ^= 1; receive_frame(original, framelen); assert(pending_sequence == 109); original[22] ^= 1;
    original[40] ^= 1; receive_frame(original, framelen); assert(pending_sequence == 109); original[40] ^= 1;
    original[framelen-1] ^= 1; receive_frame(original, framelen); assert(pending_sequence == 109); original[framelen-1] ^= 1;
    memcpy(incoming, original, framelen); incoming_len = framelen;
    incoming_peer = (struct sockaddr_ll){.sll_family=AF_PACKET,.sll_ifindex=3,.sll_pkttype=PACKET_HOST};
    session_dispatch(); assert(pending_sequence == 109);
    memcpy(incoming, original, framelen); incoming_len = framelen; incoming_peer.sll_ifindex = ifindex;
    session_dispatch(); assert(!pending_sequence && state == SESSION_CONFIRMED);
    params.radius = 9;
    const uint8_t success[] = {1,1,9,0x13,1,1,0,0};
    n = response(received, 0x5eeb3, success, sizeof(success)); received[0] = 1; put32(received + 2, 0x5eeb3);
    put64(received + 28, params.utc + (now - params.received_at) / 1000);
    memset(received + 8, 0, 16); packet_md5(received, n, actual_digest); memcpy(received + 8, actual_digest, 16);
    wire_version = 2; assert(decode_response(received, n) == SESSION_NO_EVENT && wire_version == 1 && params.version == 2);
    assert(sent_frame[42] == 1 && sent_frame[43] == 2 && be32(sent_frame+44) == 0x5eeb3 && be32(sent_frame+66) == 0x5eeb3 && sent_len == 84);
    params.radius = 5; const uint8_t reauth[] = {1,0,1,4}; n = response(received, 200, reauth, sizeof(reauth));
    assert(decode_response(received, n) == SESSION_REAUTH);
    const uint8_t logoff[] = {1,0,1,3}; n = response(received, 201, logoff, sizeof(logoff)); assert(decode_response(received, n) == SESSION_LOGOFF);
    params.radius = 9; const uint8_t sam_logoff[] = {1,1,8,1,'x'}; n = response(received, 202, sam_logoff, sizeof(sam_logoff));
    memcpy(original, received, n); assert(decode_response(received, n) == SESSION_LOGOFF);
    assert(decode_response(original, n) == SESSION_NO_EVENT);
    session_reset(); assert(installed_len == 1 && !bpf_run(original, n)); now += 9999999; int last_sends = sends; session_tick();
    assert(state == SESSION_NONE && !active && session_timeout() == -1 && sends == last_sends);
    sock = -1; session_destroy(); assert(session_fd() == -1);
    puts("session: official SAM no-ACK Hello/header, 10-minute confirmation/ACK and trusted version downgrade, scoped raw transport, DES/RC4 and single expiry passed"); return 0;
}
