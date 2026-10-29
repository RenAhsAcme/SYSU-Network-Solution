#ifndef SYSU_AUTH_H
#define SYSU_AUTH_H
#include "minieap_common.h"
#include <stdint.h>
#define SYSU_VERSION "1.1.0"
#define SYSU_RUN_DIR "/var/run/sysu-auth"
#define SYSU_STATUS SYSU_RUN_DIR "/status.json"
int64_t monotonic_ms(void);
void status_update(const char *state, const char *reason, int retry_secs);
void status_init(void);
void status_refresh(void);
RESULT drop_privileges(void);
void eap_link_changed(int up);
#endif
