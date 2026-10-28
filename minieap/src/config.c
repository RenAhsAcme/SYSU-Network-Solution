#include "config.h"
#include "logging.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char username[USERNAME_MAX_LEN + 1], password[PASSWORD_MAX_LEN + 1];
static EAP_CONFIG credentials = {username, password};
static PROG_CONFIG config;

static int number(const char *value, int min, int max, int *out) {
    char *end;
    errno = 0;
    if (!*value || !isdigit((unsigned char)*value)) return 0;
    long n = strtol(value, &end, 10);
    if (errno || *end || n < min || n > max) return 0;
    *out = (int)n;
    return 1;
}

RESULT parse_config_file(const char *path) {
    static const char *keys[] = {"username", "password", "nic", "heartbeat",
        "stage-timeout", "wait-after-fail", "max-retries"};
    unsigned seen = 0;
    char line[256];
    struct stat st;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return FAILURE;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || (st.st_mode & 077) ||
        st.st_uid != geteuid() || st.st_size > 4096) {
        close(fd);
        PR_ERR("配置文件必须由当前用户所有、仅该用户可访问，且不超过 4096 字节");
        return FAILURE;
    }
    FILE *fp = fdopen(fd, "r");
    if (!fp) { close(fd); return FAILURE; }
    config = (PROG_CONFIG){.heartbeat = 0, .stage_timeout = 5,
        .wait_after_fail_secs = 30, .max_retries = 3};
    memset(username, 0, sizeof(username));
    explicit_bzero(password, sizeof(password));
    int valid = 1;
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (!len || (line[len - 1] != '\n' && !feof(fp))) { valid = 0; break; }
        if (line[len - 1] == '\n') line[--len] = 0;
        if (!len || line[0] == '#') continue;
        char *value = strchr(line, '=');
        if (!value) { valid = 0; break; }
        *value++ = 0;
        unsigned key;
        for (key = 0; key < sizeof(keys) / sizeof(keys[0]); key++)
            if (!strcmp(line, keys[key])) break;
        if (key == sizeof(keys) / sizeof(keys[0]) || (seen & (1u << key))) {
            valid = 0; break;
        }
        seen |= 1u << key;
        for (const unsigned char *p = (unsigned char *)value; *p; p++)
            if (*p < 32 || *p == 127) valid = 0;
        size_t n = strlen(value);
        switch (key) {
        case 0:
        case 1:
            if (!n || n > 64) { valid = 0; break; }
            memcpy(key ? password : username, value, n + 1);
            break;
        case 2:
            if (!n || n >= IFNAMSIZ) { valid = 0; break; }
            for (size_t i = 0; i < n; i++)
                if (!isalnum((unsigned char)value[i]) && !strchr("_.-", value[i])) valid = 0;
            memcpy(config.ifname, value, n + 1);
            break;
        case 3: valid &= number(value, 0, 3600, &config.heartbeat); break;
        case 4: valid &= number(value, 1, 60, &config.stage_timeout); break;
        case 5: valid &= number(value, 5, 300, &config.wait_after_fail_secs); break;
        case 6: valid &= number(value, 1, 10, &config.max_retries); break;
        }
        if (!valid) break;
    }
    valid &= !ferror(fp) && (seen & 7) == 7;
    fclose(fp);
    explicit_bzero(line, sizeof(line));
    if (!valid) { free_config(); PR_ERR("配置无效：检查必填项、重复项、长度和数值范围"); }
    return valid ? SUCCESS : FAILURE;
}
void free_config(void) {
    explicit_bzero(password, sizeof(password));
    explicit_bzero(username, sizeof(username));
}
PROG_CONFIG *get_program_config(void) { return &config; }
EAP_CONFIG *get_eap_config(void) { return &credentials; }
