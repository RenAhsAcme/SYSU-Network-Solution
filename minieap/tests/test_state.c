#include "config.h"
#include "eap_state_machine.h"
#include "if_impl.h"
#include "logging.h"
#include "net_util.h"
#include "packet_builder.h"
#include "packet_plugin.h"
#include "sched_alarm.h"
#include "sysu.h"
#include "sysu_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #test); exit(1); \
} } while (0)

static const uint8_t local_mac[6] = {2, 1, 2, 3, 4, 5};
static const uint8_t server_mac[6] = {2, 6, 7, 8, 9, 10};
static uint8_t packets[256][FRAME_BUF_SIZE], header[23], seed[16];
static size_t lengths[256];
static unsigned sent, builds, prepares, resets;
static int64_t now = 100000;
static int nas, retry_secs;
static char last_state[32], last_reason[64];
static PROG_CONFIG config = {.ifname = "test0", .stage_timeout = 5,
    .wait_after_fail_secs = 30, .max_retries = 1};
static EAP_CONFIG credentials = {"offline-user", "offline-password"};

int64_t monotonic_ms(void) { return now; }
PROG_CONFIG *get_program_config(void) { return &config; }
EAP_CONFIG *get_eap_config(void) { return &credentials; }
void status_update(const char *state, const char *reason, int seconds) {
    snprintf(last_state, sizeof(last_state), "%s", state);
    snprintf(last_reason, sizeof(last_reason), "%s", reason);
    retry_secs = seconds;
}
void print_log(const char *level, const char *func, const char *format, ...) {}
RESULT obtain_iface_mac(const char *ifname, uint8_t *mac) {
    CHECK(!strcmp(ifname, "test0"));
    memcpy(mac, local_mac, 6);
    return SUCCESS;
}
static RESULT capture_send(IF_IMPL *iface, ETH_EAP_FRAME *frame) {
    CHECK(sent < sizeof(packets) / sizeof(packets[0]));
    CHECK(frame->actual_len <= sizeof(packets[0]));
    memcpy(packets[sent], frame->content, frame->actual_len);
    lengths[sent++] = frame->actual_len;
    return SUCCESS;
}
static IF_IMPL iface = {.send_frame = capture_send};
IF_IMPL *get_if_impl(void) { return &iface; }
int sysu_protocol_is_nas(void) { return nas; }
void packet_plugin_reset(void) { resets++; nas = 0; }
RESULT packet_plugin_prepare_frame(ETH_EAP_FRAME *frame) {
    CHECK(frame->actual_len < frame->buffer_len);
    frame->content[frame->actual_len++] = (uint8_t)++prepares;
    return SUCCESS;
}
RESULT packet_plugin_on_frame_received(ETH_EAP_FRAME *frame) {
    const uint8_t oui[3] = {0, 0xd0, 0xf8};
    if (frame->content[18] == EAP_REQUEST)
        nas = !memcmp(frame->content + 6, oui, sizeof(oui));
    return SUCCESS;
}
static void set_eth(PACKET_BUILDER *builder, int field, const uint8_t *value) {
    memcpy(header + (field == FIELD_DST_MAC ? 0 : field == FIELD_SRC_MAC ? 6 : 12),
        value, field == FIELD_ETH_PROTO ? 2 : 6);
}
static void set_eap(PACKET_BUILDER *builder, EAPOL_TYPE kind, EAP_CODE code,
    EAP_TYPE type, int id, EAP_CONFIG *eap) {
    header[14] = 1; header[15] = kind;
    header[18] = code; header[19] = id; header[22] = type;
}
static void set_seed(PACKET_BUILDER *builder, uint8_t *value, int len) {
    CHECK(len == sizeof(seed));
    memcpy(seed, value, sizeof(seed));
}
static int build(PACKET_BUILDER *builder, uint8_t *out) {
    builds++;
    size_t len = header[15] == EAP_PACKET ? header[22] == MD5_CHALLENGE ? 40 : 23 : 18;
    memcpy(out, header, sizeof(header));
    out[16] = 0; out[17] = len - 18;
    if (header[15] == EAP_PACKET) {
        out[20] = 0; out[21] = len - 18;
        if (header[22] == MD5_CHALLENGE) { out[23] = 16; memcpy(out + 24, seed, 16); }
    }
    return (int)len;
}
static PACKET_BUILDER builder = {.set_eth_field = set_eth, .set_eap_fields = set_eap,
    .set_eap_md5_seed = set_seed, .build_packet = build};
PACKET_BUILDER *packet_builder_get(void) { return &builder; }

static void advance(int64_t ms) {
    while (ms >= 0) {
        int wait = sched_alarm_timeout();
        if (wait < 0 || wait > ms) { now += ms; return; }
        now += wait;
        ms -= wait;
        sched_alarm_dispatch();
    }
}
static ETH_EAP_FRAME make_frame(uint8_t out[64], int code, int id, int type) {
    memset(out, 0, 64);
    memcpy(out, local_mac, 6); memcpy(out + 6, server_mac, 6);
    out[12] = 0x88; out[13] = 0x8e; out[14] = 1;
    out[18] = code; out[19] = id; out[22] = type;
    size_t len = code == EAP_REQUEST ? type == MD5_CHALLENGE ? 40 : 23 : 22;
    out[17] = out[21] = len - 18;
    if (type == MD5_CHALLENGE && code == EAP_REQUEST) {
        out[23] = 16;
        for (unsigned i = 0; i < 16; i++) out[24 + i] = (uint8_t)(0xa0 + i);
    }
    return (ETH_EAP_FRAME){.content = out, .actual_len = len, .buffer_len = 64};
}
static void feed(int code, int id, int type) {
    uint8_t bytes[64];
    ETH_EAP_FRAME frame = make_frame(bytes, code, id, type);
    eap_state_machine_recv_handler(&frame);
}
static unsigned count_kind(int kind) {
    unsigned count = 0;
    for (unsigned i = 0; i < sent; i++) count += packets[i][15] == kind;
    return count;
}
static void begin(void) {
    CHECK(sched_alarm_init() == SUCCESS);
    CHECK(eap_state_machine_init() == SUCCESS);
    eap_link_changed(1);
    CHECK(sent == 0);
    advance(1000);
    CHECK(count_kind(EAPOL_START) == 1);
}
static void authenticate(void) {
    feed(EAP_REQUEST, 17, IDENTITY);
    feed(EAP_REQUEST, 18, MD5_CHALLENGE);
    feed(EAP_SUCCESS, 18, 0);
    CHECK(!strcmp(last_state, "authenticated"));
}
static void test_link_dedup(void) {
    CHECK(sched_alarm_init() == SUCCESS);
    CHECK(eap_state_machine_init() == SUCCESS);
    eap_link_changed(1);
    eap_link_changed(1);
    CHECK(!strcmp(last_reason, "link_ready"));
    CHECK(sent == 0);
    advance(999); eap_link_changed(1);
    CHECK(sent == 0);
    advance(1);
    CHECK(sent == 1 && count_kind(EAPOL_START) == 1);
    eap_link_changed(1);
    CHECK(sent == 1 && sched_alarm_timeout() == 5000);
}
static void test_duplicate_requests(void) {
    begin();
    feed(EAP_REQUEST, 17, IDENTITY);
    unsigned original = sent - 1, old_builds = builds, old_prepares = prepares;
    feed(EAP_REQUEST, 17, IDENTITY);
    CHECK(sent == original + 1);
    advance(499); feed(EAP_REQUEST, 17, IDENTITY);
    CHECK(sent == original + 1);
    advance(1); feed(EAP_REQUEST, 17, IDENTITY);
    CHECK(sent == original + 2);
    CHECK(builds == old_builds && prepares == old_prepares);
    CHECK(lengths[original] == lengths[sent - 1]);
    CHECK(!memcmp(packets[original], packets[sent - 1], lengths[original]));
    CHECK(sched_alarm_timeout() == 44500);
    advance(44499); feed(EAP_REQUEST, 17, IDENTITY);
    CHECK(sched_alarm_timeout() == 1);
    advance(1);
    CHECK(sched_alarm_timeout() == 5000);
    advance(5000);
    CHECK(!strcmp(last_reason, "timeout") && retry_secs == 30);
}
static void test_challenge_cache(void) {
    begin(); feed(EAP_REQUEST, 17, IDENTITY);
    feed(EAP_REQUEST, 18, MD5_CHALLENGE);
    unsigned original = sent - 1, old_builds = builds, old_prepares = prepares;
    advance(500); feed(EAP_REQUEST, 18, MD5_CHALLENGE);
    CHECK(builds == old_builds && prepares == old_prepares);
    CHECK(lengths[original] == lengths[sent - 1]);
    CHECK(!memcmp(packets[original], packets[sent - 1], lengths[original]));
    CHECK(sched_alarm_timeout() == 4500);
    uint8_t bytes[64];
    ETH_EAP_FRAME frame = make_frame(bytes, EAP_REQUEST, 18, MD5_CHALLENGE);
    bytes[24] ^= 1;
    eap_state_machine_recv_handler(&frame);
    CHECK(builds == old_builds + 1 && prepares == old_prepares + 1);
    CHECK(memcmp(packets[original], packets[sent - 1], lengths[original]));
}
static void test_discovery_timeout(void) {
    static const uint8_t ruijie[6] = {1, 0xd0, 0xf8, 0, 0, 0};
    static const uint8_t pae[6] = {1, 0x80, 0xc2, 0, 0, 3};
    begin();
    CHECK(!memcmp(packets[0], ruijie, 6));
    advance(5000);
    CHECK(sent == 2 && !memcmp(packets[1], pae, 6));
    advance(5000);
    CHECK(sent == 2 && !strcmp(last_reason, "timeout") && retry_secs == 30);
}
static void test_round_deadline(void) {
    begin(); feed(EAP_REQUEST, 17, IDENTITY);
    advance(40000); feed(EAP_REQUEST, 18, IDENTITY);
    advance(40000); feed(EAP_REQUEST, 19, IDENTITY);
    CHECK(sched_alarm_timeout() == 10000);
    advance(9999); feed(EAP_REQUEST, 19, IDENTITY);
    CHECK(!strcmp(last_state, "authenticating") && sched_alarm_timeout() == 1);
    advance(1);
    CHECK(!strcmp(last_reason, "round_timeout") && retry_secs == 30);
    unsigned before = sent;
    advance(29999);
    CHECK(sent == before);
    advance(1);
    CHECK(sent == before + 1 && count_kind(EAPOL_START) == 2);
}
static void test_auxiliary_preserves_challenge(void) {
    begin(); feed(EAP_REQUEST, 17, IDENTITY);
    feed(EAP_REQUEST, 18, MD5_CHALLENGE);
    unsigned original = sent - 1, old_builds = builds;
    advance(1000); feed(EAP_REQUEST, 90, 2);
    CHECK(packets[sent - 1][22] == 3 && packets[sent - 1][19] == 90);
    CHECK(!strcmp(last_reason, "challenge") && sched_alarm_timeout() == 4000);
    unsigned before = sent;
    feed(EAP_REQUEST, 91, 25); advance(499); feed(EAP_REQUEST, 90, 2);
    CHECK(sent == before);
    advance(1); feed(EAP_REQUEST, 91, 25);
    CHECK(sent == before + 1 && packets[sent - 1][22] == 3);
    CHECK(sched_alarm_timeout() == 3500);
    feed(EAP_REQUEST, 18, MD5_CHALLENGE);
    CHECK(builds == old_builds + 2);
    CHECK(lengths[original] == lengths[sent - 1]);
    CHECK(!memcmp(packets[original], packets[sent - 1], lengths[original]));
    feed(EAP_SUCCESS, 91, 0); feed(EAP_FAILURE, 91, 0);
    CHECK(!strcmp(last_state, "authenticating"));
    advance(3500);
    CHECK(packets[sent - 1][22] == MD5_CHALLENGE && packets[sent - 1][19] == 18);
    CHECK(!memcmp(packets[original], packets[sent - 1], lengths[original]));
    feed(EAP_SUCCESS, 18, 0);
    CHECK(!strcmp(last_state, "authenticated") && sched_alarm_timeout() == -1);
}
static void test_failure_backoff(void) {
    static const int waits[] = {30, 60, 120, 240, 300, 300};
    begin();
    for (unsigned i = 0; i < sizeof(waits) / sizeof(waits[0]); i++) {
        feed(EAP_REQUEST, 17, IDENTITY);
        feed(EAP_FAILURE, 17, 0);
        CHECK(!strcmp(last_state, "retrying") && !strcmp(last_reason, "rejected"));
        CHECK(retry_secs == waits[i] && sched_alarm_timeout() == waits[i] * 1000);
        feed(EAP_FAILURE, 17, 0); feed(EAP_REQUEST, 18, IDENTITY);
        CHECK(sched_alarm_timeout() == waits[i] * 1000 && retry_secs == waits[i]);
        unsigned starts = count_kind(EAPOL_START);
        advance((int64_t)waits[i] * 1000 - 1);
        CHECK(count_kind(EAPOL_START) == starts);
        advance(1);
        CHECK(count_kind(EAPOL_START) == starts + 1);
    }
}
static void test_online_identity(void) {
    begin(); authenticate();
    unsigned starts = count_kind(EAPOL_START), before = sent, old_resets = resets;
    CHECK(sched_alarm_timeout() == -1);
    feed(EAP_REQUEST, 19, IDENTITY);
    CHECK(sent == before + 1 && !strcmp(last_state, "authenticated"));
    CHECK(resets == old_resets);
    CHECK(sched_alarm_timeout() == -1);
    advance(300000);
    CHECK(count_kind(EAPOL_START) == starts && sent == before + 1);
    feed(EAP_REQUEST, 20, MD5_CHALLENGE);
    CHECK(!strcmp(last_state, "authenticating"));
    feed(EAP_SUCCESS, 20, 0);
    CHECK(!strcmp(last_state, "authenticated") && count_kind(EAPOL_START) == starts);
}
static void test_online_challenge_reset(void) {
    begin(); authenticate();
    unsigned old_resets = resets, starts = count_kind(EAPOL_START), old_builds = builds;
    advance(500); feed(EAP_REQUEST, 18, MD5_CHALLENGE);
    CHECK(resets == old_resets && builds == old_builds);
    CHECK(!strcmp(last_state, "authenticated") && sched_alarm_timeout() == -1);
    uint8_t bytes[64];
    ETH_EAP_FRAME frame = make_frame(bytes, EAP_REQUEST, 19, MD5_CHALLENGE);
    bytes[6] ^= 4;
    eap_state_machine_recv_handler(&frame);
    CHECK(resets == old_resets && builds == old_builds);
    feed(EAP_REQUEST, 19, MD5_CHALLENGE);
    CHECK(resets == old_resets + 1 && !strcmp(last_state, "authenticating"));
    feed(EAP_REQUEST, 20, MD5_CHALLENGE);
    CHECK(resets == old_resets + 1 && packets[sent - 1][19] == 20);
    old_builds = builds;
    advance(500); feed(EAP_REQUEST, 20, MD5_CHALLENGE);
    CHECK(resets == old_resets + 1 && builds == old_builds);
    feed(EAP_SUCCESS, 20, 0);
    CHECK(!strcmp(last_state, "authenticated") && resets == old_resets + 1);
    CHECK(count_kind(EAPOL_START) == starts && sched_alarm_timeout() == -1);
}
static void test_stop_once(void) {
    begin(); authenticate();
    unsigned before = sent;
    eap_state_machine_stop();
    CHECK(sent == before + 1 && count_kind(EAPOL_LOGOFF) == 1);
    eap_state_machine_stop();
    CHECK(sent == before + 1 && sched_alarm_timeout() == -1);
    advance(600000); feed(EAP_REQUEST, 21, IDENTITY);
    CHECK(sent == before + 1);
}
static void test_reject_untrusted(void) {
    uint8_t bytes[64];
    begin();
    feed(EAP_SUCCESS, 17, 0);
    CHECK(sent == 1);
    feed(EAP_REQUEST, 17, IDENTITY);
    unsigned before = sent;
    ETH_EAP_FRAME frame = make_frame(bytes, EAP_REQUEST, 18, MD5_CHALLENGE);
    bytes[6] ^= 4; eap_state_machine_recv_handler(&frame);
    CHECK(sent == before);
    frame = make_frame(bytes, EAP_REQUEST, 18, MD5_CHALLENGE);
    bytes[0] ^= 4; eap_state_machine_recv_handler(&frame);
    CHECK(sent == before);
    frame = make_frame(bytes, EAP_REQUEST, 18, MD5_CHALLENGE);
    memcpy(bytes + 6, local_mac, 6); eap_state_machine_recv_handler(&frame);
    CHECK(sent == before);
    frame = make_frame(bytes, EAP_REQUEST, 18, MD5_CHALLENGE);
    bytes[6] |= 1; eap_state_machine_recv_handler(&frame);
    CHECK(sent == before);
    frame = make_frame(bytes, EAP_REQUEST, 18, MD5_CHALLENGE);
    bytes[17] = 63; eap_state_machine_recv_handler(&frame);
    CHECK(sent == before);
    frame = make_frame(bytes, EAP_REQUEST, 18, MD5_CHALLENGE);
    bytes[23] = 15; eap_state_machine_recv_handler(&frame);
    CHECK(sent == before);
    frame = make_frame(bytes, EAP_REQUEST, 18, MD5_CHALLENGE);
    frame.actual_len = frame.buffer_len + 1; eap_state_machine_recv_handler(&frame);
    CHECK(sent == before);
    feed(EAP_REQUEST, 18, MD5_CHALLENGE);
    feed(EAP_SUCCESS, 19, 0); feed(EAP_FAILURE, 19, 0);
    CHECK(!strcmp(last_state, "authenticating"));
    feed(EAP_SUCCESS, 18, 0);
    CHECK(!strcmp(last_state, "authenticated"));
}
static void test_link_cancels(void) {
    begin(); feed(EAP_REQUEST, 17, IDENTITY);
    unsigned before = sent, old_resets = resets;
    eap_link_changed(0);
    CHECK(!strcmp(last_reason, "link_down") && resets == old_resets + 1);
    CHECK(sched_alarm_timeout() == -1);
    advance(600000); feed(EAP_REQUEST, 18, IDENTITY); eap_link_changed(0);
    CHECK(sent == before);
    eap_link_changed(1); eap_link_changed(1); advance(1000);
    CHECK(sent == before + 1 && count_kind(EAPOL_START) == 2);
    feed(EAP_REQUEST, 19, IDENTITY); feed(EAP_FAILURE, 19, 0);
    eap_link_changed(0);
    CHECK(sched_alarm_timeout() == -1);
    before = sent; advance(600000);
    CHECK(sent == before);
}
static void test_session_recovery(void) {
    begin(); authenticate();
    eap_session_event(1);
    CHECK(!strcmp(last_reason, "server_reauth") && retry_secs == 30);
    advance(1000); eap_session_event(1); eap_heartbeat_failed();
    CHECK(sched_alarm_timeout() == 29000);
    advance(29000); authenticate();
    eap_heartbeat_failed();
    CHECK(!strcmp(last_reason, "heartbeat_send_failed") && retry_secs == 60);
    advance(60000); authenticate(); advance(300000);
    eap_session_event(2);
    CHECK(!strcmp(last_reason, "server_logoff") && retry_secs == 30);
    eap_state_machine_stop();
    unsigned before = sent;
    advance(600000);
    CHECK(sent == before && sched_alarm_timeout() == -1);
}
int main(void) {
    const struct { const char *name; void (*run)(void); } tests[] = {
        {"link-up debounce and deduplication", test_link_dedup},
        {"cached requests, rate limit and unchanged deadline", test_duplicate_requests},
        {"cached MD5 response and changed challenge", test_challenge_cache},
        {"discovery multicast fallback and bounded retransmission", test_discovery_timeout},
        {"authentication round expires at its original deadline", test_round_deadline},
        {"notification and unsupported requests preserve MD5 response and id", test_auxiliary_preserves_challenge},
        {"bounded exponential authentication backoff", test_failure_backoff},
        {"online identity without disruptive authentication restart", test_online_identity},
        {"new online MD5 resets the previous session exactly once", test_online_challenge_reset},
        {"idempotent logoff and canceled reconnection", test_stop_once},
        {"untrusted frames and identifiers", test_reject_untrusted},
        {"link-down cancels authentication and retry timers", test_link_cancels},
        {"session events, deduplication and stable-session backoff reset", test_session_recovery}
    };
    for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        fflush(NULL);
        pid_t child = fork();
        CHECK(child >= 0);
        if (!child) { tests[i].run(); sched_alarm_destroy(); exit(0); }
        int status;
        CHECK(waitpid(child, &status, 0) == child);
        if (!WIFEXITED(status) || WEXITSTATUS(status)) {
            fprintf(stderr, "FAILED: %s\n", tests[i].name);
            return 1;
        }
        printf("PASS: %s\n", tests[i].name);
    }
    return 0;
}
