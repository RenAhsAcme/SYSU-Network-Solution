#include "config.h"
#include "if_impl.h"
#include "logging.h"
#include "misc.h"
#include "net_util.h"
#include "packet_builder.h"
#include "packet_util.h"
#include "session.h"
#include "sysu_protocol.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, configure_calls, reset_calls;
static size_t configured_len;
static uint8_t configured[FRAME_BUF_SIZE], last_challenge[16], vz_hash[128];
static int session_has_parameters;
static char username[65] = "offline-user", password[65] = "campus-password-123456789";
static PROG_CONFIG config = {.ifname = "test0"};
static EAP_CONFIG credentials = {username, password};
static const uint8_t local_mac[6] = {2,1,2,3,4,5};
static const uint8_t server_mac[6] = {2,6,7,8,9,10};
static const uint8_t seed[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
static const uint8_t server_parameters[] = {
    0x59,4,0,0,0,30, 0x61,1,9, 0x5b,4,192,0,2,1, 0x5a,4,0,0,15,160
};
#define CHECK(test) do { checks++; if (!(test)) { \
    fprintf(stderr,"%s:%d: %s\n",__func__,__LINE__,#test); exit(1); \
} } while (0)
PROG_CONFIG *get_program_config(void) { return &config; }
EAP_CONFIG *get_eap_config(void) { return &credentials; }
void print_log(const char *level, const char *func, const char *format, ...) {
    (void)level; (void)func; (void)format;
}
void chk_free(void **ptr) { free(*ptr); *ptr = NULL; }
RESULT obtain_iface_mac(const char *ifname, uint8_t *mac) {
    CHECK(!strcmp(ifname,config.ifname));
    memcpy(mac,local_mac,sizeof(local_mac));
    return SUCCESS;
}
unsigned char *computeV4(const unsigned char *src, int len) {
    CHECK(src && len == 16);
    memcpy(last_challenge,src,sizeof(last_challenge));
    memset(vz_hash,'f',sizeof(vz_hash));
    return vz_hash;
}
void session_reset(void) { reset_calls++; session_has_parameters = 0; }
int session_configure(const uint8_t *tlvs, size_t len) {
    configure_calls++;
    CHECK(len <= sizeof(configured));
    configured_len = len;
    memcpy(configured,tlvs,len);
    /* 会话解析器另有真实测试；此替身仅记录传递边界和会话复位。 */
    if (len >= sizeof(server_parameters) && !memcmp(tlvs,server_parameters,sizeof(server_parameters)))
        session_has_parameters = 1;
    return session_has_parameters;
}
static unsigned be16(const uint8_t *p) { return (unsigned)p[0] * 256 + p[1]; }
static void put16(uint8_t *p, size_t n) { p[0] = n >> 8; p[1] = n; }
static void check_hex(const uint8_t *bytes, size_t len, const char *hex) {
    CHECK(strlen(hex) == len*2);
    for (size_t i = 0; i < len; i++) {
        unsigned value;
        CHECK(sscanf(hex+i*2,"%2x",&value) == 1);
        CHECK(bytes[i] == value);
    }
}
static ETH_EAP_FRAME incoming(uint8_t *buf, size_t cap, int code, int type,
                             const uint8_t *tail, size_t tail_len, int inside) {
    size_t base = code == EAP_REQUEST ? type == MD5_CHALLENGE ? 40 : 23 : 22;
    CHECK(base+tail_len <= cap);
    memset(buf,0,cap);
    memcpy(buf,local_mac,6); memcpy(buf+6,server_mac,6);
    buf[12] = 0x88; buf[13] = 0x8e; buf[14] = 1;
    buf[18] = code; buf[19] = 42;
    put16(buf+16,base-18+(inside ? tail_len : 0));
    put16(buf+20,base-18+(inside ? tail_len : 0));
    if (code == EAP_REQUEST) buf[22] = type;
    if (code == EAP_REQUEST && type == MD5_CHALLENGE) {
        buf[23] = 16; memcpy(buf+24,seed,16);
    }
    if (tail_len) memcpy(buf+base,tail,tail_len);
    return (ETH_EAP_FRAME){.content=buf,.actual_len=base+tail_len,.buffer_len=cap};
}
static ETH_EAP_FRAME build(uint8_t out[FRAME_BUF_SIZE], EAPOL_TYPE kind, EAP_TYPE type) {
    const uint8_t protocol[] = {0x88,0x8e};
    PACKET_BUILDER *builder = packet_builder_get();
    CHECK(builder != NULL);
    builder->set_eth_field(builder,FIELD_SRC_MAC,local_mac);
    builder->set_eth_field(builder,FIELD_DST_MAC,server_mac);
    builder->set_eth_field(builder,FIELD_ETH_PROTO,protocol);
    builder->set_eap_fields(builder,kind,EAP_RESPONSE,type,42,&credentials);
    if (type == MD5_CHALLENGE)
        builder->set_eap_md5_seed(builder,(uint8_t *)seed,sizeof(seed));
    int len = builder->build_packet(builder,out);
    CHECK(len >= 18 && len <= FRAME_BUF_SIZE);
    return (ETH_EAP_FRAME){.content=out,.actual_len=(size_t)len,.buffer_len=FRAME_BUF_SIZE};
}
static const uint8_t *find_tlv(const ETH_EAP_FRAME *frame, size_t begin, unsigned type, size_t *len) {
    for (size_t pos = begin; pos+2 <= frame->actual_len;) {
        size_t size = frame->content[pos+1];
        CHECK(size <= frame->actual_len-pos-2);
        if (frame->content[pos] == type) { *len = size; return frame->content+pos+2; }
        pos += size+2;
    }
    CHECK(0);
    return NULL;
}
static void check_direct_tlvs(const ETH_EAP_FRAME *frame, size_t begin) {
    static const uint8_t types[] = {0x11,'P','-','9','/','v','M','T','b','p','o','y'};
    static const size_t sizes[] = {36,17,6,32,32,0,128,0,1,1,sizeof("RG-SU For Linux V1.30"),1};
    size_t pos = begin;
    for (size_t i = 0; i < sizeof(types); i++) {
        CHECK(pos+2 <= frame->actual_len);
        CHECK(frame->content[pos] == types[i]);
        CHECK(frame->content[pos+1] == sizes[i]);
        pos += sizes[i]+2;
    }
    CHECK(pos == frame->actual_len);
    size_t len;
    const uint8_t *v = find_tlv(frame,begin,0x11,&len);
    CHECK(!memcmp(v,"8021x.exe",sizeof("8021x.exe")));
    for (unsigned i = sizeof("8021x.exe"); i < 32; i++) CHECK(v[i] == 0);
    CHECK(!memcmp(v+32,(uint8_t[]){1,30,1,2},4));
    v = find_tlv(frame,begin,'P',&len); CHECK(v[16] == 1);
    v = find_tlv(frame,begin,'-',&len); CHECK(!memcmp(v,local_mac,6));
    v = find_tlv(frame,begin,'9',&len);
    CHECK(!memcmp(v,"internet",sizeof("internet")));
    for (unsigned i = sizeof("internet"); i < 32; i++) CHECK(v[i] == 0);
    v = find_tlv(frame,begin,'/',&len);
    /* Python hashlib 的冻结向量：username=offline-user，密码=campus-password-123456789。 */
    check_hex(v,len,"708a7969a81d3a1d3028ca57d0b339361a3f38ec080ed5b01f0690a0c1f74b80");
    v = find_tlv(frame,begin,'M',&len);
    for (size_t i = 0; i < len; i++) CHECK(v[i] == 'f');
    v = find_tlv(frame,begin,'b',&len); CHECK(v[0] == 0);
    v = find_tlv(frame,begin,'p',&len); CHECK(v[0] == (sizeof(void *) == 8 ? 0x40 : 0x20));
    v = find_tlv(frame,begin,'o',&len); CHECK(!memcmp(v,"RG-SU For Linux V1.30",len));
    v = find_tlv(frame,begin,'y',&len); CHECK(v[0] == 2);
}
static void test_outgoing(void) {
    uint8_t out[FRAME_BUF_SIZE] = {0}, packet[128];
    sysu_protocol_reset();
    ETH_EAP_FRAME frame = build(out,EAPOL_START,0);
    CHECK(frame.actual_len == 18 && be16(out+16) == 0);
    CHECK(sysu_protocol_append(&frame) == SUCCESS && frame.actual_len == 18);
    frame = build(out,EAPOL_LOGOFF,0);
    CHECK(frame.actual_len == 18 && be16(out+16) == 0);
    CHECK(sysu_protocol_append(&frame) == SUCCESS && frame.actual_len == 18);
    frame = build(out,EAP_PACKET,IDENTITY);
    size_t standard = frame.actual_len;
    CHECK(standard == 23+strlen(username));
    CHECK(out[18] == EAP_RESPONSE && out[19] == 42 && out[22] == IDENTITY);
    CHECK(!memcmp(out+23,username,strlen(username)));
    CHECK(be16(out+16) == 5+strlen(username) && be16(out+20) == 5+strlen(username));
    CHECK(sysu_protocol_append(&frame) == SUCCESS);
    CHECK(be16(out+16) == standard-18 && be16(out+20) == standard-18);
    check_direct_tlvs(&frame,standard);
    CHECK(!memcmp(last_challenge,(uint8_t[16]){0},16));
    ETH_EAP_FRAME request = incoming(packet,sizeof(packet),EAP_REQUEST,MD5_CHALLENGE,NULL,0,0);
    CHECK(validate_eap_frame(&request));
    sysu_protocol_observe(&request);
    frame = build(out,EAP_PACKET,MD5_CHALLENGE);
    CHECK(frame.actual_len == 40 && be16(out+16) == 22 && be16(out+20) == 22 && out[23] == 16);
    /* hashlib.md5(bytes([42])+password+bytes(range(16)))，不使用用户名参与摘要。 */
    check_hex(out+24,16,"cff4568c317ec990282605dec78bc74c");
    CHECK(sysu_protocol_append(&frame) == SUCCESS && out[40] == 0x11);
    CHECK(be16(out+16) == 22 && be16(out+20) == 22);
    CHECK(!memcmp(last_challenge,seed,16));
    check_direct_tlvs(&frame,40);
    frame = build(out,EAP_PACKET,(EAP_TYPE)3);
    CHECK(frame.actual_len == 23 && be16(out+16) == 5 && be16(out+20) == 5 && out[22] == 3);
}
static void test_nas(void) {
    const uint8_t oui[][3] = {{0,0xd0,0xf8},{0,0x1a,0xa9},{0,0x14,0x4b},{0x58,0x69,0x6c}};
    uint8_t packet[128], out[FRAME_BUF_SIZE];
    for (unsigned i = 0; i < sizeof(oui)/sizeof(*oui); i++) {
        sysu_protocol_reset();
        ETH_EAP_FRAME request = incoming(packet,sizeof(packet),EAP_REQUEST,MD5_CHALLENGE,
                                        server_parameters,sizeof(server_parameters),1);
        memcpy(packet+6,oui[i],3);
        unsigned calls = configure_calls;
        sysu_protocol_observe(&request);
        CHECK(sysu_protocol_is_nas() && configure_calls == calls);
        ETH_EAP_FRAME frame = build(out,EAP_PACKET,MD5_CHALLENGE);
        CHECK(frame.actual_len == 40+strlen(username));
        CHECK(be16(out+16) == 22+strlen(username) && be16(out+20) == 22+strlen(username));
        check_hex(out+24,16,"cff4568c317ec990282605dec78bc74c");
        CHECK(!memcmp(out+40,username,strlen(username)));
        packet[8] ^= 1;
        sysu_protocol_observe(&request);
        CHECK(!sysu_protocol_is_nas());
    }
}
static void test_server_parameters(void) {
    uint8_t packet[128], padding[20] = {0};
    for (int code = EAP_REQUEST; code <= EAP_SUCCESS; code += 2) {
        for (int inside = 0; inside <= 1; inside++) {
            sysu_protocol_reset();
            unsigned calls = configure_calls, resets = reset_calls;
            ETH_EAP_FRAME frame = incoming(packet,sizeof(packet),code,MD5_CHALLENGE,
                                          server_parameters,sizeof(server_parameters),inside);
            CHECK(validate_eap_frame(&frame));
            sysu_protocol_observe(&frame);
            CHECK(configure_calls == calls+1 && configured_len == sizeof(server_parameters));
            CHECK(!memcmp(configured,server_parameters,configured_len));
            CHECK(session_has_parameters && reset_calls == resets);
            frame = incoming(packet,sizeof(packet),code,MD5_CHALLENGE,NULL,0,0);
            calls = configure_calls;
            sysu_protocol_observe(&frame);
            CHECK(configure_calls == calls && session_has_parameters && reset_calls == resets);
            frame = incoming(packet,sizeof(packet),code,MD5_CHALLENGE,padding,sizeof(padding),0);
            sysu_protocol_observe(&frame);
            CHECK(session_has_parameters && reset_calls == resets);
        }
    }
}
static void test_frame_validation(void) {
    uint8_t packet[FRAME_BUF_SIZE+1];
    ETH_EAP_FRAME good = incoming(packet,sizeof(packet),EAP_REQUEST,MD5_CHALLENGE,NULL,0,0);
    CHECK(validate_eap_frame(&good));
    for (size_t n = 0; n < 40; n++) {
        uint8_t *short_packet = malloc(n ? n : 1);
        CHECK(short_packet != NULL);
        memcpy(short_packet,packet,n);
        ETH_EAP_FRAME truncated = {.content=short_packet,.actual_len=n,.buffer_len=n};
        CHECK(!validate_eap_frame(&truncated));
        free(short_packet);
    }
    const struct { unsigned offset, value; } malformed[] = {
        {12,0x08},{13,0},{14,0},{14,4},{15,EAPOL_START},{18,EAP_RESPONSE},
        {16,1},{17,3},{17,21},{17,23},{20,1},{21,3},{21,21},{21,23},
        {23,0},{23,15},{23,17},{23,255}
    };
    for (unsigned i = 0; i < sizeof(malformed)/sizeof(*malformed); i++) {
        uint8_t copy[40]; memcpy(copy,packet,sizeof(copy));
        copy[malformed[i].offset] = malformed[i].value;
        ETH_EAP_FRAME bad = {.content=copy,.actual_len=40,.buffer_len=40};
        CHECK(!validate_eap_frame(&bad));
    }
    for (unsigned size = 0; size <= 255; size++) {
        uint8_t sized[24+255] = {0};
        memcpy(sized,packet,24);
        sized[23] = size;
        put16(sized+16,size+6); put16(sized+20,size+6);
        ETH_EAP_FRAME value = {.content=sized,.actual_len=24+size,.buffer_len=sizeof(sized)};
        CHECK(validate_eap_frame(&value) == (size == 16));
    }
    ETH_EAP_FRAME bad = good; bad.buffer_len = 39; CHECK(!validate_eap_frame(&bad));
    bad = good; bad.actual_len = FRAME_BUF_SIZE+1; CHECK(!validate_eap_frame(&bad));
    bad = good; bad.content = NULL; CHECK(!validate_eap_frame(&bad));
    CHECK(!validate_eap_frame(NULL));
    good = incoming(packet,sizeof(packet),EAP_SUCCESS,0,NULL,0,0);
    CHECK(validate_eap_frame(&good));
    good = incoming(packet,sizeof(packet),EAP_FAILURE,0,NULL,0,0);
    CHECK(validate_eap_frame(&good));
    good = incoming(packet,sizeof(packet),EAP_REQUEST,2,NULL,0,0);
    CHECK(validate_eap_frame(&good));
}
static void test_capacity_and_limits(void) {
    uint8_t out[FRAME_BUF_SIZE];
    sysu_protocol_reset();
    ETH_EAP_FRAME frame = build(out,EAP_PACKET,MD5_CHALLENGE);
    frame.buffer_len = frame.actual_len;
    CHECK(sysu_protocol_append(&frame) == FAILURE && frame.actual_len == 40);
    CHECK(append_to_frame(&frame,NULL,1) == FAILURE);
    CHECK(append_to_frame(&frame,out,-1) == FAILURE);
    CHECK(append_to_frame(&frame,NULL,0) == SUCCESS);
    memset(username,'u',64); username[64] = 0;
    memset(password,'p',64); password[64] = 0;
    frame = build(out,EAP_PACKET,IDENTITY);
    CHECK(frame.actual_len == 87 && be16(out+20) == 69);
    CHECK(sysu_protocol_append(&frame) == SUCCESS);
    size_t len; (void)find_tlv(&frame,87,'/',&len); CHECK(len == 64);
    frame = build(out,EAP_PACKET,MD5_CHALLENGE);
    CHECK(frame.actual_len == 40 && be16(out+20) == 22);
    CHECK(sysu_protocol_append(&frame) == SUCCESS);
    (void)find_tlv(&frame,40,'/',&len); CHECK(len == 64);
}
int main(void) {
    test_outgoing();
    test_nas();
    test_server_parameters();
    test_frame_validation();
    test_capacity_and_limits();
    packet_builder_destroy();
    printf("protocol tests: %u assertions passed (real builder/protocol/validator/MD5; fixed Vz)\n",checks);
    return 0;
}
