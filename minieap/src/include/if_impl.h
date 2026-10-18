#ifndef SYSU_IF_IMPL_H
#define SYSU_IF_IMPL_H
#include "minieap_common.h"
#include "eth_frame.h"
#define FRAME_BUF_SIZE 1512
typedef struct _if_impl {
    RESULT (*set_ifname)(struct _if_impl *, const char *);
    RESULT (*get_ifname)(struct _if_impl *, char *, int);
    RESULT (*prepare_interface)(struct _if_impl *);
    RESULT (*start_capture)(struct _if_impl *);
    RESULT (*send_frame)(struct _if_impl *, ETH_EAP_FRAME *);
    void (*set_frame_handler)(struct _if_impl *, void (*)(ETH_EAP_FRAME *));
    void (*destroy)(struct _if_impl *);
    void *priv;
} IF_IMPL;
int init_if_impl_list(void);
IF_IMPL *get_if_impl(void);
void free_if_impl(void);
#endif
