#include "sysu_protocol.h"
#include "config.h"
#include "net_util.h"
#include "packet_util.h"
#include "md5.h"
#include "checkV4.h"
#include "session.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int nas;
static uint8_t challenge[16];
int sysu_protocol_is_nas(void) { return nas; }
void sysu_protocol_reset(void) {
    nas = 0;
    memset(challenge, 0, sizeof(challenge));
    session_reset();
}
void sysu_protocol_observe(const ETH_EAP_FRAME *frame) {
    static const uint8_t oui[][3] = {{0,0xd0,0xf8}, {0,0x1a,0xa9}, {0,0x14,0x4b}, {0x58,0x69,0x6c}};
    const uint8_t *p = frame->content;
    nas = 0;
    for (unsigned i = 0; i < sizeof(oui) / sizeof(oui[0]); i++)
        if (!memcmp(p + 6, oui[i], 3)) nas = 1;
    size_t end = frame->actual_len;
    if (p[18] == EAP_REQUEST && p[22] == MD5_CHALLENGE && end >= 40) {
        memcpy(challenge, p + 24, sizeof(challenge));
        /* 服务器参数也可随最后一轮 MD5 请求送达，不能只解析 Success。 */
        if (!nas && end > 40) session_configure(p + 40, end - 40);
    } else if (p[18] == EAP_SUCCESS && end > 22 && !nas) {
        session_configure(p + 22, end - 22);
    }
}
static RESULT tlv(ETH_EAP_FRAME *frame, uint8_t type, const void *data, size_t len) {
    uint8_t header[2] = {type, (uint8_t)len};
    if (len > 254 || IS_FAIL(append_to_frame(frame, header, 2))) return FAILURE;
    return append_to_frame(frame, data, (int)len);
}
static void ipv4_info(uint8_t out[17]) {
    struct ifreq req = {0};
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", get_program_config()->ifname);
    if (fd >= 0) {
        if (!ioctl(fd, SIOCGIFADDR, &req)) memcpy(out, &((struct sockaddr_in *)&req.ifr_addr)->sin_addr, 4);
        if (!ioctl(fd, SIOCGIFNETMASK, &req)) memcpy(out + 4, &((struct sockaddr_in *)&req.ifr_netmask)->sin_addr, 4);
        close(fd);
    }
    FILE *fp = fopen("/proc/net/route", "r");
    char line[256], name[IFNAMSIZ];
    unsigned long dest, gateway;
    unsigned flags;
    if (fp) {
        while (fgets(line, sizeof(line), fp))
            if (sscanf(line, "%15s %lx %lx %x", name, &dest, &gateway, &flags) == 4 &&
                !strcmp(name, get_program_config()->ifname) && dest == 0 && (flags & 2)) {
                uint32_t addr = (uint32_t)gateway;
                memcpy(out + 8, &addr, 4);
                break;
            }
        fclose(fp);
    }
    fp = fopen("/tmp/resolv.conf.d/resolv.conf.auto", "r");
    if (fp) {
        char value[64];
        while (fgets(line, sizeof(line), fp))
            if (sscanf(line, "nameserver %63s", value) == 1 && inet_pton(AF_INET, value, out + 12) == 1) break;
        fclose(fp);
    }
    out[16] = 1;
}
RESULT sysu_protocol_append(ETH_EAP_FRAME *frame) {
    if (frame->content[15] != EAP_PACKET) return SUCCESS;
    uint8_t version[36] = "8021x.exe", ip[17] = {0}, mac[6] = {0};
    uint8_t service[32] = "internet", cipher[PASSWORD_MAX_LEN] = {0};
    uint8_t input[USERNAME_MAX_LEN + 16];
    uint8_t previous[16] = {'!', 'j', 'i', 'e', 'r', 'u', 'i', '9', '0', '0', '2', 'p', 'm', 's', 'u', 's'};
    EAP_CONFIG *config = get_eap_config();
    size_t userlen = strlen(config->username), passlen = strlen(config->password);
    size_t cipherlen = (passlen + 15) & ~(size_t)15;
    if (!cipherlen) cipherlen = 16;
    memcpy(input, config->username, userlen);
    for (size_t offset = 0; offset < cipherlen; offset += 16) {
        memcpy(input + userlen, previous, 16);
        memcpy(previous, ComputeHash(input, (UINT4)(userlen + 16)), 16);
        for (unsigned i = 0; i < 16; i++)
            cipher[offset + i] = previous[i] ^ (offset + i < passlen ? (uint8_t)config->password[offset + i] : 0);
        memcpy(previous, cipher + offset, 16);
    }
    explicit_bzero(input, sizeof(input));
    explicit_bzero(previous, sizeof(previous));
    version[32] = 1; version[33] = 30; version[34] = 1; version[35] = 2;
    ipv4_info(ip);
    if (IS_FAIL(obtain_iface_mac(get_program_config()->ifname, mac))) return FAILURE;
    uint8_t zero = 0, bits = sizeof(void *) == 8 ? 0x40 : 0x20, direct = 2;
    uint8_t *hash = computeV4(challenge, 16);
    if (!hash) return FAILURE;
#define ADD(type, data, len) do { if (IS_FAIL(tlv(frame, type, data, len))) return FAILURE; } while (0)
    ADD(0x11, version, sizeof(version));
    ADD('P', ip, sizeof(ip));
    ADD('-', mac, sizeof(mac));
    ADD('9', service, sizeof(service));
    RESULT result = tlv(frame, '/', cipher, cipherlen);
    explicit_bzero(cipher, sizeof(cipher));
    if (IS_FAIL(result)) return FAILURE;
    ADD('v', NULL, 0);
    ADD('M', hash, 128);
    ADD('T', NULL, 0);
    ADD('b', &zero, 1);
    ADD('p', &bits, 1);
    ADD('o', "RG-SU For Linux V1.30", sizeof("RG-SU For Linux V1.30"));
    ADD('y', &direct, 1);
#undef ADD
    /* 校园网 NAS 要求标准长度只覆盖 EAP 体，扩展保留在尾部。 */
    return SUCCESS;
}
