#ifndef SYSU_SESSION_H
#define SYSU_SESSION_H
#include "minieap_common.h"
#include <stddef.h>
#include <stdint.h>
typedef enum { SESSION_NONE, SESSION_PENDING, SESSION_CONFIRMED, SESSION_BACKOFF } SESSION_STATE;
typedef enum { SESSION_NO_EVENT, SESSION_REAUTH, SESSION_LOGOFF, SESSION_EXPIRED } SESSION_EVENT;
RESULT session_init(const char *ifname);
void session_destroy(void);
void session_reset(void);
int session_configure(const uint8_t *tlvs, size_t len);
void session_authenticated(void);
void session_update_address(void);
int session_fd(void);
int session_timeout(void);
SESSION_EVENT session_dispatch(void);
SESSION_EVENT session_tick(void);
SESSION_STATE session_state(void);
#endif
