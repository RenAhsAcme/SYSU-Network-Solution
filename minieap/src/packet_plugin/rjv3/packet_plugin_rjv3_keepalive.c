#include "packet_plugin_rjv3_priv.h"
#include "packet_plugin_rjv3_keepalive.h"
#include "if_impl.h"
#include "net_util.h"
#include "sched_alarm.h"
#include "misc.h"
#include "sysu.h"
#include "logging.h"
#include <string.h>
#include <stdlib.h>

static int timer;
static uint32_t echokey, echono;
static uint8_t destination[6];
void rjv3_set_keepalive_echokey(uint32_t key) { echokey = key; }
void rjv3_set_keepalive_echono(uint32_t no) { echono = no; }
void rjv3_set_keepalive_dest_mac(uint8_t *mac) { memcpy(destination, mac, 6); }
void rjv3_keepalive_reset(void) {
    unschedule_alarm(timer);
    timer = 0;
    echokey = echono = 0;
}
RESULT rjv3_send_new_keepalive_frame(PACKET_PLUGIN *this) {
    uint32_t a = echokey + echono, b = echono++;
    uint8_t packet[45] = {0};
    uint8_t payload[] = {
        0xff,0xff,0x37,0x77,0x7f,0x9f,0xff,0xff,0xd9,0x13,0xff,0xff,0x37,0x77,
        0x7f,0x9f,0xff,0xff,0xf7,0x2b,0xff,0xff,0x37,0x77,0x7f,0x3f,0xff
    };
    for (int i = 0; i < 4; i++) {
        payload[6 + i] = (uint8_t)~bit_reverse((uint8_t)(a >> (24 - 8 * i)));
        payload[16 + i] = (uint8_t)~bit_reverse((uint8_t)(b >> (24 - 8 * i)));
    }
    IF_IMPL *iface = get_if_impl();
    char name[16];
    memcpy(packet, destination, 6);
    if (IS_FAIL(iface->get_ifname(iface, name, sizeof(name))) || IS_FAIL(obtain_iface_mac(name, packet + 6))) return FAILURE;
    packet[12] = 0x88; packet[13] = 0x8e;
    packet[14] = 1; packet[15] = 0xbf; packet[17] = 30;
    memcpy(packet + 18, payload, sizeof(payload));
    ETH_EAP_FRAME frame = {.content = packet, .buffer_len = sizeof(packet), .actual_len = sizeof(packet)};
    return iface->send_frame(iface, &frame);
}
void rjv3_send_keepalive_timed(void *data) {
    PACKET_PLUGIN *this = data;
    rjv3_priv *params = this->priv;
    if (IS_FAIL(rjv3_send_new_keepalive_frame(this))) {
        status_update("error", "heartbeat_send_failed", 0);
        PR_ERR("心跳发送失败，退出并由服务管理器恢复");
        exit(EXIT_FAILURE);
    }
    timer = schedule_alarm(params->heartbeat_interval, rjv3_send_keepalive_timed, this);
}
void rjv3_start_keepalive(PACKET_PLUGIN *this) {
    timer = schedule_alarm(1, rjv3_send_keepalive_timed, this);
}
