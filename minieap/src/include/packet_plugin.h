#ifndef SYSU_PACKET_PLUGIN_H
#define SYSU_PACKET_PLUGIN_H
#include "minieap_common.h"
#include "eth_frame.h"
typedef struct _packet_plugin { void *priv; } PACKET_PLUGIN;
int init_packet_plugin_list(void);
void packet_plugin_destroy(void);
void packet_plugin_reset(void);
RESULT packet_plugin_prepare_frame(ETH_EAP_FRAME *frame);
RESULT packet_plugin_on_frame_received(ETH_EAP_FRAME *frame);
#endif
