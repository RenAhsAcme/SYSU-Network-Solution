#include "packet_plugin_rjv3_priv.h"
#include "packet_plugin_rjv3_prop.h"
#include "packet_plugin_rjv3_keepalive.h"
#include "eth_frame.h"
#include "if_impl.h"
#include "checkV4.h"
#include "config.h"
#include "linkedlist.h"
#include "logging.h"
#include "net_util.h"
#include "packet_util.h"
#include "misc.h"
#include "sched_alarm.h"
#include "rjcrc16.h"
#include "rjencode.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <arpa/inet.h>

#ifdef __linux__
#include <sys/random.h>
#endif

#define IS_MD5_FRAME(frame) \
    (frame != NULL && frame->header->eapol_hdr.type[0] == EAP_PACKET \
        && frame->header->eap_hdr.type[0] == MD5_CHALLENGE)

static void rjv3_set_dhcp_en(uint8_t* dhcp_en_arr, DHCP_TYPE dhcp_type) {
    dhcp_en_arr[3] = (dhcp_type != DHCP_NONE);
}

static void rjv3_set_local_mac(uint8_t* mac_buf) {
    IF_IMPL* _if_impl = get_if_impl();
    if (_if_impl == NULL) return;

    char ifname[IFNAMSIZ] = {0};
    _if_impl->get_ifname(_if_impl, ifname, IFNAMSIZ);

    obtain_iface_mac(ifname, mac_buf);
}

static void rjv3_set_pwd_hash(uint8_t* hash_buf, ETH_EAP_FRAME* request) {
    if (IS_MD5_FRAME(request)) {
        uint8_t* _hash_buf;
        EAP_CONFIG* _eap_config = get_eap_config();

        _hash_buf = (uint8_t*)computePwd(request->content + sizeof(FRAME_HEADER) + 1,
                                          _eap_config->username, _eap_config->password);
        memmove(hash_buf, _hash_buf, 16);
        /* 1 = sizeof(MD5-Value-Size), this is where MD5-Value starts */
    }
}

static void rjv3_set_ipv6_addr(uint8_t* ll_slaac, uint8_t* ll_temp, uint8_t* global) {
    LIST_ELEMENT *_ip_list = NULL, *_ip_curr;
    IF_IMPL* _if_impl = get_if_impl();
    if (_if_impl == NULL) return;

    char ifname[IFNAMSIZ] = {0};
    _if_impl->get_ifname(_if_impl, ifname, IFNAMSIZ);

    obtain_iface_ip_mask(ifname, &_ip_list);
    _ip_curr = _ip_list;
    if (!_ip_curr) return;

#define IP_ELEM ((IP_ADDR*)(_ip_curr->content))
    do {
        if (IP_ELEM->family == AF_INET6) {
            if ((IP_ELEM->ip[0] & 0xf0) == 0x20) { // 2xxx:: Global scope (ROUGH)
                memmove(global, IP_ELEM->ip, 16);
            } else if (IP_ELEM->ip[0] == 0xfe && IP_ELEM->ip[1] == 0x80) { // fe80:: Link local
                if (IP_ELEM->ip[11] == 0xff && IP_ELEM->ip[12] == 0xfe) {
                    /* ::xxff:fexx:: is the symbol of SLAAC */
                    memmove(ll_slaac, IP_ELEM->ip, 16);
                } else {
                    /* Privacy extension, will be used to form an global address also */
                    memmove(ll_temp, IP_ELEM->ip, 16);
                }
            }
        }
    } while ((_ip_curr = _ip_curr->next));
    list_destroy(&_ip_list, TRUE);
}

static void rjv3_set_v3_hash(uint8_t* hash_buf, ETH_EAP_FRAME* request) {
    uint8_t* _v3_buf;

    /* computeV4 returns its internal buffer, which can not be freed */
    if (IS_MD5_FRAME(request)) {
        _v3_buf = computeV4(request->content + sizeof(FRAME_HEADER) + 1, /* position of MD5-Value */
                            *(request->content + sizeof(FRAME_HEADER))); /* position of MD5-Value-Size */
    } else {
        uint8_t _v3_pad[RJV3_PAD_SIZE] = {0};
        _v3_buf = computeV4(_v3_pad, RJV3_PAD_SIZE);
    }
    memmove(hash_buf, _v3_buf, 0x80);
}

static void rjv3_set_service_name(uint8_t* name_buf, char* cmd_opt) {
    memmove(name_buf, cmd_opt, strlen(cmd_opt));
}

static void rjv3_set_secondary_dns(char* dns_ascii_buf, char* fake_dns) {
    if (fake_dns != NULL) {
        memmove(dns_ascii_buf, fake_dns, strnlen(fake_dns, INET6_ADDRSTRLEN));
        return;
    }

    LIST_ELEMENT* dns_list = NULL;

    obtain_dns_list(&dns_list);

    /* Only care about 2nd one */
    if (dns_list && dns_list->next) {
        strncpy(dns_ascii_buf, dns_list->next->content, INET6_ADDRSTRLEN);
    } else {
        /* 第二 DNS 未配置时，保持与旧客户端一致的空字段。 */
    }
    free_dns_list(&dns_list);
    return;
}

#define PRIV ((rjv3_priv*)(this->priv))

/*
 * Calculate values for commonly seen fields, and add them to a list
 *
 * Returns how much space it would take to "serialize" all the fields in the list
 */
static int rjv3_append_common_fields(PACKET_PLUGIN* this, LIST_ELEMENT** list, int append_pwd_hash) {
    int _len = 0, _this_len = -1;
    uint8_t _dhcp_en[RJV3_SIZE_DHCP] = {0x00, 0x00, 0x00, 0x01};
    uint8_t _local_mac[RJV3_SIZE_MAC] = {0};
    uint8_t _pwd_hash[RJV3_SIZE_PWD_HASH] = {0};
    char _sec_dns[INET6_ADDRSTRLEN] = {0};
    uint8_t _misc_2[RJV3_SIZE_MISC_2] = {0x01};
    uint8_t _ll_ipv6[RJV3_SIZE_LL_IPV6] = {0};
    uint8_t _ll_ipv6_tmp[RJV3_SIZE_LL_IPV6_T] = {0};
    uint8_t _glb_ipv6[RJV3_SIZE_GLB_IPV6] = {0};
    uint8_t _v3_hash[RJV3_SIZE_V3_HASH] = {0};
    uint8_t _service[RJV3_SIZE_SERVICE] = {0};
    uint8_t _hdd_ser[RJV3_SIZE_HDD_SER] = {0};
    /* misc 6 */
    uint8_t _misc_7[RJV3_SIZE_MISC_7] = {0};
    uint8_t _os_bits[RJV3_SIZE_OS_BITS] = {0x40};
    const char* _ver_str = "RG-SU For Linux V1.0";

    rjv3_set_dhcp_en(_dhcp_en, DHCP_AFTER_AUTH);

    rjv3_set_local_mac(_local_mac);

    rjv3_set_pwd_hash(_pwd_hash, PRIV->last_recv_packet);

    rjv3_set_secondary_dns(_sec_dns, NULL);

    rjv3_set_ipv6_addr(_ll_ipv6, _ll_ipv6_tmp, _glb_ipv6);

    rjv3_set_v3_hash(_v3_hash, PRIV->last_recv_packet);

    rjv3_set_service_name(_service, "internet");



#define CHK_ADD(x) \
    _this_len = x; \
    if (_this_len < 0) { \
        return -1; \
    } else { \
        _len += _this_len; \
    }

    CHK_ADD(append_rjv3_prop(list, RJV3_TYPE_DHCP,     _dhcp_en,               sizeof(_dhcp_en)));
    CHK_ADD(append_rjv3_prop(list, RJV3_TYPE_MAC,      _local_mac,             sizeof(_local_mac)));

    if (append_pwd_hash) {
        CHK_ADD(append_rjv3_prop(list, RJV3_TYPE_PWD_HASH, _pwd_hash,          sizeof(_pwd_hash)));
    } else {
        CHK_ADD(append_rjv3_prop(list, RJV3_TYPE_PWD_HASH, NULL,               0));
    }

    CHK_ADD(append_rjv3_prop(list, RJV3_TYPE_SEC_DNS,  (uint8_t*)_sec_dns,    strlen(_sec_dns)));
    CHK_ADD(append_rjv3_prop(list, RJV3_TYPE_MISC_2,   _misc_2,                sizeof(_misc_2)));
    CHK_ADD(append_rjv3_prop(list, RJV3_TYPE_LL_IPV6,  _ll_ipv6,               sizeof(_ll_ipv6)));
    CHK_ADD(append_rjv3_prop(list, RJV3_TYPE_LL_IPV6_T,_ll_ipv6_tmp,           sizeof(_ll_ipv6_tmp)));
    CHK_ADD(append_rjv3_prop(list, RJV3_TYPE_GLB_IPV6, _glb_ipv6,              sizeof(_glb_ipv6)));
    CHK_ADD(append_rjv3_prop(list, RJV3_TYPE_V3_HASH,  _v3_hash,               sizeof(_v3_hash)));
    CHK_ADD(append_rjv3_prop(list, RJV3_TYPE_SERVICE,  _service,               sizeof(_service)));
    CHK_ADD(append_rjv3_prop(list, RJV3_TYPE_HDD_SER,  _hdd_ser,               sizeof(_hdd_ser)));
    CHK_ADD(append_rjv3_prop(list, RJV3_TYPE_MISC_6,   NULL,                   0));
    CHK_ADD(append_rjv3_prop(list, RJV3_TYPE_MISC_7,   _misc_7,                sizeof(_misc_7)));
    CHK_ADD(append_rjv3_prop(list, RJV3_TYPE_OS_BITS,  _os_bits,               sizeof(_os_bits)));
    CHK_ADD(append_rjv3_prop(list, RJV3_TYPE_VER_STR,  (uint8_t*)_ver_str,    strlen(_ver_str) + 1)); // Zero terminated

    return _len;
}

/* The bytes containing twisted IPv4 addresses */
static RESULT rjv3_append_priv_header(struct _packet_plugin* this, ETH_EAP_FRAME* frame) {
    DHCP_INFO_PROP _dhcp_prop = {0};

    _dhcp_prop.magic[0] = 0x00;
    _dhcp_prop.magic[1] = 0x00;
    _dhcp_prop.magic[2] = 0x13;
    _dhcp_prop.magic[3] = 0x11;

    _dhcp_prop.dhcp_enabled = 1;

    uint16_t checksum = htons(crc16((uint8_t*)&_dhcp_prop, 21));
    memcpy(_dhcp_prop.crc16_hash, &checksum, sizeof(checksum));

    rj_encode((uint8_t*)&_dhcp_prop, sizeof(_dhcp_prop));
    if (IS_FAIL(append_to_frame(frame, (uint8_t*)&_dhcp_prop, sizeof(_dhcp_prop)))) return FAILURE;

    uint8_t _magic[4] = {0x00, 0x00, 0x13, 0x11};
    if (IS_FAIL(append_to_frame(frame, _magic, sizeof(_magic)))) return FAILURE;

    uint8_t _prog_name[RJV3_SIZE_PROG_NAME] = RJV3_PROG_NAME;
    if (IS_FAIL(append_to_frame(frame, _prog_name, sizeof(_prog_name)))) return FAILURE;

    uint8_t _version[4] = {0x01, 0x1f, 0x01, 0x02}; /* May be different */
    return append_to_frame(frame, _version, sizeof(_version));
}

/* Append everything proprietary */
RESULT rjv3_append_priv(struct _packet_plugin* this, ETH_EAP_FRAME* frame) {
    /*
     * Size field, its format is NOT the same as those header1.magic == 0x1a ones.
     * Thus do not use the prop APIs.
     */
    /*
        struct _size_field {
            RJ_PROP_HEADER1 header1; // TODO Different in 2nd packet
            uint8_t whole_trailer_len[2]; // First 0x1a prop to end
            <other 0x1a fields follow>
        }
     */
    int _props_len = 0, _single_len = 0;
    uint8_t _std_prop_buf[FRAME_BUF_SIZE] = {0}; // Buffer for 0x1a props
    LIST_ELEMENT* _prop_list = NULL;

    if (IS_FAIL(rjv3_append_priv_header(this, frame))) return FAILURE;

    /* Let's make the big news! */
    _single_len = rjv3_append_common_fields(this, &_prop_list, IS_MD5_FRAME(frame));
    if (_single_len < 0) {
        destroy_rjv3_prop_list(&_prop_list);
        return FAILURE;
    }

    _single_len = append_rjv3_prop_list_to_buffer(_prop_list, _std_prop_buf, FRAME_BUF_SIZE);
    if (_single_len < 0) { destroy_rjv3_prop_list(&_prop_list); return FAILURE; }
    _props_len = _single_len;

    /* The outside */
    /* logoffReason(1) magic(4) length(2) */
    uint8_t _header[7] = {0x00, 0x00, 0x00, 0x13, 0x11, 0x00, 0x00};
    _header[5] = (_props_len >> 8 & 0xff);
    _header[6] = (_props_len & 0xff);

    RESULT result = append_to_frame(frame, _header, sizeof(_header));
    if (!IS_FAIL(result)) result = append_to_frame(frame, _std_prop_buf, _props_len);
    destroy_rjv3_prop_list(&_prop_list);
    return result;
}

RESULT rjv3_process_result_prop(ETH_EAP_FRAME* frame) {
    LIST_ELEMENT *props = NULL;
    int offset = sizeof(ETHERNET_HEADER) + sizeof(EAPOL_HEADER) + 4;
    if (frame->actual_len < (size_t)offset || IS_FAIL(parse_rjv3_buf_to_prop_list(
        &props, frame->content + offset, (int)frame->actual_len - offset, TRUE))) return FAILURE;
    RJ_PROP *key = find_rjv3_prop(props, 1);
    RESULT result = FAILURE;
    if (key && PROP_TO_CONTENT_SIZE(key) >= 10) {
        uint32_t value = 0, serial = 0;
        for (int i = 6; i < 10; i++) value = (value << 8) | bit_reverse((uint8_t)~key->content[i]);
        if (getrandom(&serial, sizeof(serial), 0) == sizeof(serial)) {
            rjv3_set_keepalive_echokey(value);
            rjv3_set_keepalive_echono(serial & 0xffff);
            rjv3_set_keepalive_dest_mac(frame->header->eth_hdr.src_mac);
            result = SUCCESS;
        }
    }
    destroy_rjv3_prop_list(&props);
    return result;
}
