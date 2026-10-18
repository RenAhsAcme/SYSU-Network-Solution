#include "config.h"
#include "if_impl.h"
#include "packet_plugin.h"
#include "packet_builder.h"
#include "eap_state_machine.h"
#include "sched_alarm.h"
#include "logging.h"
#include "sysu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>

static void cleanup(void) {
    status_update("stopped", "stopped", 0);
    free_if_impl();
    packet_plugin_destroy();
    packet_builder_destroy();
    sched_alarm_destroy();
    free_config();
}
int main(int argc, char **argv) {
    const char *path = "/var/etc/sysu-auth.conf";
    int check = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) { puts("SYSU Auth " SYSU_VERSION); return 0; }
        if (!strcmp(argv[i], "--help")) {
            puts("sysu-auth [--conf-file PATH] [--check]\nOpenWrt SYSU authentication; credentials are read from a private file.");
            return 0;
        }
        if (!strcmp(argv[i], "--check")) check = 1;
        else if (!strcmp(argv[i], "--conf-file") && i + 1 < argc) path = argv[++i];
        else { fputs("Unsupported argument\n", stderr); return 2; }
    }
    struct rlimit no_core = {0, 0};
    if (setrlimit(RLIMIT_CORE, &no_core)) return 1;
    start_log();
    if (IS_FAIL(parse_config_file(path))) return 2;
    if (check) { free_config(); puts("Configuration valid"); return 0; }
    int lock = open(SYSU_RUN_DIR "/lock", O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) {
        PR_ERR("无法取得运行锁，或已有 SYSU 认证实例");
        free_config();
        return 1;
    }
    status_init();
    atexit(cleanup);
    if (!init_if_impl_list() || !init_packet_plugin_list()) return 1;
    IF_IMPL *iface = get_if_impl();
    if (IS_FAIL(iface->set_ifname(iface, get_program_config()->ifname)) ||
        IS_FAIL(iface->prepare_interface(iface)) || IS_FAIL(drop_privileges()) ||
        IS_FAIL(sched_alarm_init()) || IS_FAIL(eap_state_machine_init())) return 1;
    iface->set_frame_handler(iface, eap_state_machine_recv_handler);
    PR_INFO("SYSU 认证 " SYSU_VERSION " 已启动");
    return IS_FAIL(iface->start_capture(iface)) ? 1 : 0;
}
