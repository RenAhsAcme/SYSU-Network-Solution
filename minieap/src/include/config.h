#ifndef SYSU_CONFIG_H
#define SYSU_CONFIG_H
#include "minieap_common.h"
#include <net/if.h>
#define USERNAME_MAX_LEN 64
#define PASSWORD_MAX_LEN 64
typedef struct { char *username, *password; } EAP_CONFIG;
typedef struct {
    char ifname[IFNAMSIZ];
    int heartbeat, stage_timeout, wait_after_fail_secs, max_retries;
} PROG_CONFIG;
RESULT parse_config_file(const char *path);
void free_config(void);
PROG_CONFIG *get_program_config(void);
EAP_CONFIG *get_eap_config(void);
#endif
