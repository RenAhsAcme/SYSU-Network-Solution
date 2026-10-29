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
#include "sysu_protocol.h"
#include <stdlib.h>
#include <string.h>

static uint8_t local[6], server[6], saved[FRAME_BUF_SIZE], response[FRAME_BUF_SIZE];
static const uint8_t broadcast[6] = {1, 0x80, 0xc2, 0, 0, 3};
static const uint8_t ruijie_group[6] = {1, 0xd0, 0xf8, 0, 0, 0};
static EAP_STATE state = EAP_STATE_UNKNOWN;
static int timer, retries, failures, link_up = -1, request_id = -1;
static size_t saved_len, response_len;
static int64_t round_started, accepted_at, response_at, auxiliary_at;
static void start_auth(void *unused);
static void timeout(void *unused);
static RESULT send_packet(EAPOL_TYPE kind, EAP_TYPE type, ETH_EAP_FRAME *request);

static void arm(int secs, void (*func)(void *)) {
    if (func == timeout) {
        int remaining = (int)((90000 - (monotonic_ms() - round_started) + 999) / 1000);
        if (remaining < secs) secs = remaining > 0 ? remaining : 0;
    }
    unschedule_alarm(timer);
    timer = schedule_alarm(secs, func, NULL);
}
static RESULT resend(void) {
    ETH_EAP_FRAME frame = {.content = response, .actual_len = response_len, .buffer_len = sizeof(response)};
    RESULT result = get_if_impl()->send_frame(get_if_impl(), &frame);
    PR_DBG("EAP 响应：长度=%zu，结果=%d", response_len, result);
    response_at = monotonic_ms();
    return result;
}
static void retry(const char *reason) {
    if (state == EAP_STATE_SUCCESS && link_up > 0 && strcmp(reason, "server_logoff"))
        send_packet(EAPOL_LOGOFF, 0, NULL);
    packet_plugin_reset();
    unschedule_alarm(timer);
    state = EAP_STATE_FAILURE;
    if (accepted_at && monotonic_ms() - accepted_at >= 300000) failures = 0;
    accepted_at = 0;
    int wait = get_program_config()->wait_after_fail_secs;
    for (int i = 0; i < failures && wait < 300; i++) wait = wait > 150 ? 300 : wait * 2;
    if (failures < 8) failures++;
    status_update("retrying", reason, wait);
    PR_WARN("认证未完成（%s），%d 秒后重试", reason, wait);
    if (link_up > 0) arm(wait, start_auth);
}
static RESULT send_packet(EAPOL_TYPE kind, EAP_TYPE type, ETH_EAP_FRAME *request) {
    PACKET_BUILDER *builder = packet_builder_get();
    uint8_t buf[FRAME_BUF_SIZE] = {0}, protocol[2] = {0x88, 0x8e};
    if (!builder) return FAILURE;
    const uint8_t *dest = kind == EAPOL_START ? server : sysu_protocol_is_nas() ? server : broadcast;
    builder->set_eth_field(builder, FIELD_DST_MAC, dest);
    builder->set_eth_field(builder, FIELD_SRC_MAC, local);
    builder->set_eth_field(builder, FIELD_ETH_PROTO, protocol);
    builder->set_eap_fields(builder, kind, EAP_RESPONSE, type, request ? request->content[19] : 0, get_eap_config());
    if (type == MD5_CHALLENGE)
        builder->set_eap_md5_seed(builder, request->content + sizeof(FRAME_HEADER) + 1, 16);
    int len = builder->build_packet(builder, buf);
    if (len < 0) return FAILURE;
    ETH_EAP_FRAME frame = {.content = buf, .buffer_len = sizeof(buf), .actual_len = (size_t)len};
    if (IS_FAIL(packet_plugin_prepare_frame(&frame))) return FAILURE;
    if (kind == EAP_PACKET) {
        memcpy(response, buf, frame.actual_len);
        response_len = frame.actual_len;
        return resend();
    }
    return get_if_impl()->send_frame(get_if_impl(), &frame);
}
static void start_auth(void *unused) {
    if (link_up <= 0) return;
    packet_plugin_reset();
    memcpy(server, ruijie_group, 6);
    request_id = -1;
    retries = 0;
    saved_len = response_len = 0;
    round_started = monotonic_ms();
    state = EAP_STATE_START_SENT;
    status_update("authenticating", "discovering", 0);
    if (IS_FAIL(send_packet(EAPOL_START, 0, NULL))) { retry("send_failed"); return; }
    arm(get_program_config()->stage_timeout, timeout);
}
static void timeout(void *unused) {
    if (monotonic_ms() - round_started >= 90000) { retry("round_timeout"); return; }
    if (++retries > get_program_config()->max_retries) { retry("timeout"); return; }
    RESULT result;
    if (state == EAP_STATE_START_SENT) {
        memcpy(server, broadcast, 6);
        result = send_packet(EAPOL_START, 0, NULL);
    } else result = resend();
    if (IS_FAIL(result)) { retry("send_failed"); return; }
    arm(get_program_config()->stage_timeout, timeout);
}
RESULT eap_state_machine_init(void) { return packet_builder_get() ? SUCCESS : FAILURE; }
void eap_state_machine_stop(void) {
    unschedule_alarm(timer);
    timer = 0;
    if (link_up > 0 && request_id >= 0 && state != EAP_STATE_UNKNOWN && state != EAP_STATE_FAILURE)
        send_packet(EAPOL_LOGOFF, 0, NULL);
    packet_plugin_reset();
    state = EAP_STATE_UNKNOWN;
    request_id = -1;
}
void eap_session_event(int event) {
    if (state == EAP_STATE_SUCCESS && event)
        retry(event == 1 ? "server_reauth" : event == 3 ? "maintenance_timeout" : "server_logoff");
}
void eap_heartbeat_failed(void) {
    if (state == EAP_STATE_SUCCESS) retry("heartbeat_send_failed");
}
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
    state = EAP_STATE_UNKNOWN;
    if (!up) {
        accepted_at = 0;
        status_update("waiting", "link_down", 0);
        PR_INFO("等待认证接口链路恢复");
        return;
    }
    if (IS_FAIL(obtain_iface_mac(get_program_config()->ifname, local))) exit(EXIT_FAILURE);
    status_update("waiting", "link_ready", 1);
    arm(1, start_auth);
}
void eap_state_machine_recv_handler(ETH_EAP_FRAME *frame) {
    if (link_up <= 0 || !validate_eap_frame(frame)) return;
    uint8_t *p = frame->content;
    if (memcmp(p, local, 6) && memcmp(p, broadcast, 6) && memcmp(p, ruijie_group, 6)) return;
    if (!memcmp(p + 6, local, 6) || (p[6] & 1)) return;
    int code = p[18], id = p[19];
    if (state == EAP_STATE_FAILURE || state == EAP_STATE_UNKNOWN) return;
    if (state != EAP_STATE_START_SENT && memcmp(server, p + 6, 6)) return;
    PR_DBG("EAP 接收：代码=%d，标识=%d，类型=%u，状态=%d", code, id,
        code == EAP_REQUEST ? p[22] : 0, state);
    if (code == EAP_REQUEST) {
        int type = p[22];
        if (type != IDENTITY && type != MD5_CHALLENGE) {
            if (monotonic_ms() - auxiliary_at < 500) return;
            uint8_t previous[FRAME_BUF_SIZE];
            size_t previous_len = response_len;
            int64_t previous_at = response_at;
            memcpy(previous, response, response_len);
            packet_plugin_on_frame_received(frame);
            RESULT result = send_packet(EAP_PACKET, 3, frame);
            memcpy(response, previous, previous_len);
            response_len = previous_len;
            response_at = previous_at;
            auxiliary_at = monotonic_ms();
            if (IS_FAIL(result)) retry("send_failed");
            return;
        }
        if (type == MD5_CHALLENGE && state != EAP_STATE_IDENTITY_SENT &&
            state != EAP_STATE_CHALLENGE_SENT && state != EAP_STATE_SUCCESS) return;
        size_t len = frame->actual_len;
        if (saved_len == len && !memcmp(saved, p, len)) {
            /* 重传复用完整响应，且不延长当前认证的超时预算。 */
            if (monotonic_ms() - response_at >= 500 && IS_FAIL(resend())) retry("send_failed");
            return;
        }
        if (state != EAP_STATE_SUCCESS && monotonic_ms() - round_started >= 90000) {
            retry("round_timeout"); return;
        }
        if (state == EAP_STATE_SUCCESS && type == MD5_CHALLENGE) packet_plugin_reset();
        memcpy(server, p + 6, 6);
        memcpy(saved, p, len);
        saved_len = len;
        request_id = id;
        packet_plugin_on_frame_received(frame);
        EAP_TYPE response_type = type == IDENTITY || type == MD5_CHALLENGE ? type : 3;
        if (IS_FAIL(send_packet(EAP_PACKET, response_type, frame))) { retry("send_failed"); return; }
        if (type != IDENTITY && type != MD5_CHALLENGE) return;
        if (state == EAP_STATE_SUCCESS && type == IDENTITY && !sysu_protocol_is_nas()) return;
        if (state == EAP_STATE_SUCCESS) round_started = monotonic_ms();
        state = type == IDENTITY ? EAP_STATE_IDENTITY_SENT : EAP_STATE_CHALLENGE_SENT;
        retries = 0;
        status_update("authenticating", type == IDENTITY ? "identity" : "challenge", 0);
        arm(type == IDENTITY ? 45 : get_program_config()->stage_timeout, timeout);
    } else if (code == EAP_SUCCESS) {
        if (state != EAP_STATE_CHALLENGE_SENT || id != request_id) return;
        unschedule_alarm(timer);
        state = EAP_STATE_SUCCESS;
        if (!accepted_at) accepted_at = monotonic_ms();
        status_update("authenticated", "accepted", 0);
        packet_plugin_on_frame_received(frame);
        PR_INFO("认证成功");
    } else if (code == EAP_FAILURE) {
        if (state == EAP_STATE_START_SENT || (state != EAP_STATE_SUCCESS && id != request_id)) return;
        retry(state == EAP_STATE_SUCCESS ? "server_logoff" : "rejected");
    }
}
