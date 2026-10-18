#ifndef SYSU_PACKET_UTIL_H
#define SYSU_PACKET_UTIL_H
#include "minieap_common.h"
#include "eth_frame.h"
RESULT append_to_frame(ETH_EAP_FRAME *frame, const uint8_t *data, int len);
int validate_eap_frame(const ETH_EAP_FRAME *frame);
#endif
