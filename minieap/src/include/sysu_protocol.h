#ifndef SYSU_PROTOCOL_H
#define SYSU_PROTOCOL_H
#include "eth_frame.h"
#include "minieap_common.h"
int sysu_protocol_is_nas(void);
void sysu_protocol_reset(void);
void sysu_protocol_observe(const ETH_EAP_FRAME *frame);
RESULT sysu_protocol_append(ETH_EAP_FRAME *frame);
#endif
