#include "packet_plugin.h"
#include "packet_plugin_rjv3_priv.h"
#include "packet_plugin_rjv3_keepalive.h"
#include "sched_alarm.h"
#include "config.h"
#include "sysu.h"
#include "logging.h"
#include "if_impl.h"
#include <string.h>

static rjv3_priv params;
static uint8_t request_data[FRAME_BUF_SIZE];
static ETH_EAP_FRAME request = {.content = request_data, .buffer_len = sizeof(request_data)};
static PACKET_PLUGIN protocol = {&params};
int init_packet_plugin_list(void) {
    params.heartbeat_interval = get_program_config()->heartbeat;
    return 1;
}
void packet_plugin_reset(void) {
    rjv3_keepalive_reset();
    params.last_recv_packet = NULL;
}
void packet_plugin_destroy(void) { packet_plugin_reset(); }
RESULT packet_plugin_prepare_frame(ETH_EAP_FRAME *frame) { return rjv3_append_priv(&protocol, frame); }
RESULT packet_plugin_on_frame_received(ETH_EAP_FRAME *frame) {
    if (frame->header->eap_hdr.code[0] == EAP_REQUEST) {
        if (frame->actual_len > sizeof(request_data)) return FAILURE;
        memcpy(request_data, frame->content, frame->actual_len);
        request.actual_len = frame->actual_len;
        params.last_recv_packet = &request;
    }
    if (frame->header->eap_hdr.code[0] != EAP_SUCCESS) return SUCCESS;
    rjv3_keepalive_reset();
    if (params.heartbeat_interval == 0) return SUCCESS;
    if (IS_FAIL(rjv3_process_result_prop(frame))) {
        PR_WARN("认证已通过，但未取得有效心跳密钥");
        return SUCCESS;
    }
    rjv3_start_keepalive(&protocol);
    return SUCCESS;
}
