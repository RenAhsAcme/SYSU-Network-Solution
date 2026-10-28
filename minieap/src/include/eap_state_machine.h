#ifndef SYSU_EAP_STATE_H
#define SYSU_EAP_STATE_H
#include "minieap_common.h"
#include "eth_frame.h"
typedef enum {
    EAP_STATE_UNKNOWN, EAP_STATE_START_SENT, EAP_STATE_IDENTITY_SENT,
    EAP_STATE_CHALLENGE_SENT, EAP_STATE_SUCCESS, EAP_STATE_FAILURE
} EAP_STATE;
RESULT eap_state_machine_init(void);
void eap_state_machine_recv_handler(ETH_EAP_FRAME *frame);
#endif
