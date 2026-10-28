#include "eap_state_machine.h"
#include "config.h"
#include "if_impl.h"
#include "packet_builder.h"
#include "packet_plugin.h"
#include "packet_util.h"
#include "net_util.h"
#include "sched_alarm.h"
#include "logging.h"
#include "sysu.h"
#include <stdlib.h>
#include <string.h>

static uint8_t local[6], server[6], saved[FRAME_BUF_SIZE];
static const uint8_t broadcast[6] = {1, 0x80, 0xc2, 0, 0, 3};
static EAP_STATE state = EAP_STATE_UNKNOWN;
static int timer, retries, failures, link_up = -1, request_id = -1;
static size_t saved_len;
static void start_auth(void *unused);
static void timeout(void *unused);

static void arm(int secs, void (*func)(void *)) {
    unschedule_alarm(timer);
    timer = schedule_alarm(secs, func, NULL);
}
static void retry(const char *reason) {
    packet_plugin_reset();
    unschedule_alarm(timer);
    state = EAP_STATE_FAILURE;
    int wait = get_program_config()->wait_after_fail_secs;
    for (int i = 0; i < failures && wait < 300; i++) wait = wait > 150 ? 300 : wait * 2;
    if (failures < 8) failures++;
    status_update("retrying", reason, wait);
    PR_WARN("认证未完成（%s），%d 秒后重试", reason, wait);
    arm(wait, start_auth);
}
static RESULT send_packet(EAPOL_TYPE kind, EAP_TYPE type, ETH_EAP_FRAME *request) {
    PACKET_BUILDER *builder = packet_builder_get();
    IF_IMPL *iface = get_if_impl();
    uint8_t buf[FRAME_BUF_SIZE] = {0}, protocol[2] = {0x88, 0x8e};
    if (!builder) return FAILURE;
    builder->set_eth_field(builder, FIELD_DST_MAC, server);
    builder->set_eth_field(builder, FIELD_SRC_MAC, local);
    builder->set_eth_field(builder, FIELD_ETH_PROTO, protocol);
    builder->set_eap_fields(builder, kind, EAP_RESPONSE, type, request ? request->header->eap_hdr.id[0] : 0, get_eap_config());
    if (type == MD5_CHALLENGE)
        builder->set_eap_md5_seed(builder, request->content + sizeof(FRAME_HEADER) + 1, 16);
    int len = builder->build_packet(builder, buf);
    if (len < 0) return FAILURE;
    ETH_EAP_FRAME frame = {.content = buf, .buffer_len = sizeof(buf), .actual_len = (size_t)len};
    if (IS_FAIL(packet_plugin_prepare_frame(&frame))) return FAILURE;
    return iface->send_frame(iface, &frame);
}
static void start_auth(void *unused) {
    if (!link_up) return;
    packet_plugin_reset();
    memcpy(server, broadcast, 6);
    request_id = -1;
    retries = 0;
    saved_len = 0;
    state = EAP_STATE_START_SENT;
    status_update("authenticating", "discovering", 0);
    if (IS_FAIL(send_packet(EAPOL_START, 0, NULL))) { retry("send_failed"); return; }
    arm(get_program_config()->stage_timeout, timeout);
}
static void timeout(void *unused) {
    if (++retries > get_program_config()->max_retries) { retry("timeout"); return; }
    ETH_EAP_FRAME frame = {.content = saved, .actual_len = saved_len, .buffer_len = sizeof(saved)};
    RESULT result;
    if (state == EAP_STATE_START_SENT) result = send_packet(EAPOL_START, 0, NULL);
    else result = send_packet(EAP_PACKET, state == EAP_STATE_IDENTITY_SENT ? IDENTITY : MD5_CHALLENGE, &frame);
    if (IS_FAIL(result)) { retry("send_failed"); return; }
    arm(get_program_config()->stage_timeout, timeout);
}
RESULT eap_state_machine_init(void) { return packet_builder_get() ? SUCCESS : FAILURE; }
void eap_link_changed(int up) {
    if (up == link_up) {
        if (!up) return;
        uint8_t current[6];
        if (IS_FAIL(obtain_iface_mac(get_program_config()->ifname, current))) exit(EXIT_FAILURE);
        if (!memcmp(current, local, sizeof(current))) return;
    }
    link_up = up;
    unschedule_alarm(timer);
    packet_plugin_reset();
    if (!up) {
        state = EAP_STATE_UNKNOWN;
        status_update("waiting", "link_down", 0);
        PR_INFO("等待认证接口链路恢复");
        return;
    }
    if (IS_FAIL(obtain_iface_mac(get_program_config()->ifname, local))) exit(EXIT_FAILURE);
    failures = 0;
    start_auth(NULL);
}
void eap_state_machine_recv_handler(ETH_EAP_FRAME *frame) {
    if (!link_up || !validate_eap_frame(frame)) return;
    uint8_t *p = frame->content;
    if (memcmp(p, local, 6) && memcmp(p, broadcast, 6)) return;
    if (!memcmp(p + 6, local, 6) || (p[6] & 1)) return;
    int code = p[18], id = p[19];
    if (state == EAP_STATE_FAILURE || state == EAP_STATE_UNKNOWN) return;
    if (state != EAP_STATE_START_SENT && memcmp(server, p + 6, 6)) return;
    if (code == EAP_REQUEST) {
        int type = p[22];
        if (type != IDENTITY && type != MD5_CHALLENGE) return;
        if (type == MD5_CHALLENGE && state != EAP_STATE_IDENTITY_SENT && state != EAP_STATE_CHALLENGE_SENT) return;
        if (state == EAP_STATE_SUCCESS) packet_plugin_reset();
        memcpy(server, p + 6, 6);
        memcpy(saved, p, frame->actual_len);
        saved_len = frame->actual_len;
        request_id = id;
        packet_plugin_on_frame_received(frame);
        if (IS_FAIL(send_packet(EAP_PACKET, type, frame))) { retry("send_failed"); return; }
        state = type == IDENTITY ? EAP_STATE_IDENTITY_SENT : EAP_STATE_CHALLENGE_SENT;
        retries = 0;
        status_update("authenticating", type == IDENTITY ? "identity" : "challenge", 0);
        arm(get_program_config()->stage_timeout, timeout);
    } else if (code == EAP_SUCCESS) {
        if (state != EAP_STATE_CHALLENGE_SENT || id != request_id) return;
        unschedule_alarm(timer);
        state = EAP_STATE_SUCCESS;
        failures = 0;
        status_update("authenticated", "accepted", 0);
        packet_plugin_on_frame_received(frame);
        PR_INFO("认证成功");
    } else if (code == EAP_FAILURE) {
        if (state == EAP_STATE_START_SENT || (state != EAP_STATE_SUCCESS && id != request_id)) return;
        retry(state == EAP_STATE_SUCCESS ? "server_logoff" : "rejected");
    }
}
