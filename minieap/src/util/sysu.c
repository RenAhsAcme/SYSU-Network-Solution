#include "sysu.h"
#include "config.h"
#include "logging.h"
#include "session.h"
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int ready;
static int64_t online_since;
static const char *current_state = "starting", *current_reason = "starting";
static int64_t retry_at;
int64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) _exit(1);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static void write_status(void) {
    if (!ready) return;
    int fd = open(SYSU_STATUS ".tmp", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) { PR_ERRNO("无法更新运行状态"); return; }
    FILE *fp = fdopen(fd, "w");
    if (!fp) { close(fd); return; }
    static const char *maintenance[] = {"none", "pending", "confirmed", "backoff"};
    int n = fprintf(fp, "{\"state\":\"%s\",\"reason\":\"%s\",\"device\":\"%s\",\"maintenance\":\"%s\","
        "\"since\":%lld,\"retry_at\":%lld,\"pid\":%ld,\"version\":\"" SYSU_VERSION "\"}\n",
        current_state, current_reason, get_program_config()->ifname, maintenance[session_state()],
        (long long)(online_since / 1000), (long long)(retry_at / 1000),
        (long)getpid());
    int err = fclose(fp);
    if (n < 0 || err || rename(SYSU_STATUS ".tmp", SYSU_STATUS)) PR_ERR("状态写入失败");
}
void status_init(void) { ready = 1; status_update("starting", "starting", 0); }
void status_refresh(void) { write_status(); }
void status_update(const char *state, const char *reason, int retry_secs) {
    if (!strcmp(state, "authenticated")) {
        if (strcmp(current_state, state)) online_since = monotonic_ms();
    } else { online_since = 0; }
    current_state = state;
    current_reason = reason;
    retry_at = retry_secs ? monotonic_ms() + (int64_t)retry_secs * 1000 : 0;
    write_status();
}
RESULT drop_privileges(void) {
    struct passwd *user = getpwnam("sysu-auth");
    if (!user || user->pw_uid == 0 || setgroups(0, NULL) ||
        setgid(user->pw_gid) || setuid(user->pw_uid) ||
        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) || prctl(PR_SET_DUMPABLE, 0, 0, 0, 0)) {
        PR_ERR("无法切换到专用用户，拒绝继续运行");
        return FAILURE;
    }
    return SUCCESS;
}
