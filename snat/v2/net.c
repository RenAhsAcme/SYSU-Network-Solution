#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <libnetfilter_conntrack/libnetfilter_conntrack.h>
#include <linux/netfilter/nf_conntrack_common.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int family;
static unsigned char address[16], destination[16];
static bool deleting, proving;
static unsigned port, count, errors;
static struct nfct_handle *writer;

static time_t monotonic(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) exit(2);
    return t.tv_sec;
}

static int callback(enum nf_conntrack_msg_type type, struct nf_conntrack *ct, void *data)
{
    (void)type; (void)data;
    if (nfct_get_attr_u8(ct, ATTR_L3PROTO) != family) return NFCT_CB_CONTINUE;
    int bytes = family == AF_INET ? 4 : 16;
    int src = family == AF_INET ? ATTR_ORIG_IPV4_SRC : ATTR_ORIG_IPV6_SRC;
    int dst = family == AF_INET ? ATTR_REPL_IPV4_DST : ATTR_REPL_IPV6_DST;
    const void *s = nfct_get_attr(ct, src), *d = nfct_get_attr(ct, dst);
    bool direct = s && !memcmp(s, address, bytes);
    bool nat = d && !memcmp(d, address, bytes) && (nfct_get_attr_u32(ct, ATTR_STATUS) & IPS_SRC_NAT);
    if (!direct && !nat) return NFCT_CB_CONTINUE;
    if (proving) {
        int od = family == AF_INET ? ATTR_ORIG_IPV4_DST : ATTR_ORIG_IPV6_DST;
        int rs = family == AF_INET ? ATTR_REPL_IPV4_SRC : ATTR_REPL_IPV6_SRC;
        const void *o = nfct_get_attr(ct, od), *r = nfct_get_attr(ct, rs);
        if (!direct || !d || memcmp(d, address, bytes) || !o || memcmp(o, destination, bytes) ||
            !r || memcmp(r, destination, bytes) || nfct_get_attr_u8(ct, ATTR_L4PROTO) != IPPROTO_TCP ||
            ntohs(nfct_get_attr_u16(ct, ATTR_ORIG_PORT_SRC)) != port) return NFCT_CB_CONTINUE;
    }
    /* 仅忽略由探测专用规则标记的连接，不能漏计直接使用池地址的业务。 */
    if (!deleting && !proving && nfct_get_attr_u32(ct, ATTR_MARK) == UINT32_C(0x53534e32))
        return NFCT_CB_CONTINUE;
    count++;
    if (deleting && nfct_query(writer, NFCT_Q_DESTROY, ct) < 0 && errno != ENOENT) errors++;
    return NFCT_CB_CONTINUE;
}

static int conntrack(int argc, char **argv)
{
    if (argc < 4 || (strcmp(argv[1], "count") && strcmp(argv[1], "retire") && strcmp(argv[1], "prove"))) return 2;
    family = !strcmp(argv[2], "4") ? AF_INET : !strcmp(argv[2], "6") ? AF_INET6 : 0;
    if (!family || inet_pton(family, argv[3], address) != 1) return 2;
    deleting = !strcmp(argv[1], "retire"); proving = !strcmp(argv[1], "prove");
    if (proving) {
        if (argc != 6 || inet_pton(family, argv[4], destination) != 1) return 2;
        char *end; unsigned long p = strtoul(argv[5], &end, 10);
        if (*end || p < 1 || p > 65535) return 2;
        port = p;
    } else if (argc != 4) return 2;
    struct nfct_handle *reader = nfct_open(CONNTRACK, 0);
    if (!reader) { perror("conntrack open"); return 2; }
    if (deleting) {
        writer = nfct_open(CONNTRACK, 0);
        if (!writer) return 2;
    }
    nfct_callback_register(reader, NFCT_T_ALL, callback, NULL);
    uint8_t f = family;
    int rc = nfct_query(reader, NFCT_Q_DUMP, &f);
    if (rc < 0) { perror("conntrack dump"); errors++; }
    nfct_close(reader);
    if (writer) nfct_close(writer);
    printf("{\"count\":%u,\"errors\":%u}\n", count, errors);
    return errors ? 2 : proving && !count ? 1 : 0;
}

static bool global(const struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        uint32_t n = ntohl(((const struct sockaddr_in *)sa)->sin_addr.s_addr);
        return n >= 0x01000000 && n < 0xe0000000 && (n >> 24) != 10 && (n >> 24) != 127 &&
            (n >> 16) != 0xa9fe && (n >> 16) != 0xc0a8 && (n & 0xfff00000) != 0xac100000 &&
            (n & 0xffc00000) != 0x64400000;
    }
    const unsigned char *a = ((const struct sockaddr_in6 *)sa)->sin6_addr.s6_addr;
    return (a[0] & 0xe0) == 0x20;
}

static int resolve(int argc, char **argv)
{
    if (argc != 4) return 2;
    struct addrinfo hint = {.ai_family = !strcmp(argv[2], "4") ? AF_INET : AF_INET6, .ai_socktype = SOCK_STREAM}, *list;
    if (strcmp(argv[2], "4") && strcmp(argv[2], "6")) return 2;
    for (const char *p = argv[3]; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '.')) return 2;
    if (strlen(argv[3]) > 253 || !*argv[3]) return 2;
    if (getaddrinfo(argv[3], NULL, &hint, &list)) return 1;
    int rc = 1;
    for (struct addrinfo *a = list; a; a = a->ai_next) {
        if (!global(a->ai_addr)) continue;
        char text[INET6_ADDRSTRLEN];
        const void *addr = a->ai_family == AF_INET ? (void *)&((struct sockaddr_in *)a->ai_addr)->sin_addr :
            (void *)&((struct sockaddr_in6 *)a->ai_addr)->sin6_addr;
        if (inet_ntop(a->ai_family, addr, text, sizeof(text))) { puts(text); rc = 0; break; }
    }
    freeaddrinfo(list);
    return rc;
}

static int run(int argc, char **argv)
{
    if (argc < 4) return 2;
    char *end; long limit = strtol(argv[2], &end, 10);
    if (*end || limit < 1 || limit > 120 || argv[3][0] != '/') return 2;
    pid_t child = fork();
    if (child < 0) return 2;
    if (!child) {
        if (setpgid(0, 0) || prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() == 1) _exit(126);
        execv(argv[3], argv + 3); _exit(127);
    }
    setpgid(child, child);
    struct timespec pause = {.tv_nsec = 20000000};
    time_t deadline = monotonic() + limit;
    int status;
    while (waitpid(child, &status, WNOHANG) == 0) {
        if (monotonic() >= deadline) {
            kill(-child, SIGKILL); waitpid(child, &status, 0); return 124;
        }
        nanosleep(&pause, NULL);
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

static int buckets(int argc, char **argv)
{
    if (argc < 4 || argc > 8 || strlen(argv[2]) != 16) return 2;
    char *end; uint64_t seed = strtoull(argv[2], &end, 16);
    if (*end) return 2;
    unsigned char parsed[16];
    for (int i = 3; i < argc; i++)
        if (inet_pton(strchr(argv[i], ':') ? AF_INET6 : AF_INET, argv[i], parsed) != 1) return 2;
    putchar('[');
    for (unsigned bucket = 0; bucket < 1024; bucket++) {
        uint64_t maximum = 0; int chosen = 0;
        for (int i = 3; i < argc; i++) {
            uint64_t h = UINT64_C(14695981039346656037) ^ seed ^ bucket;
            for (const unsigned char *p = (void *)argv[i]; *p; p++) h = (h ^ *p) * UINT64_C(1099511628211);
            /* 仅用于映射分布，不承担密码学功能。 */
            h ^= h >> 30; h *= UINT64_C(0xbf58476d1ce4e5b9);
            h ^= h >> 27; h *= UINT64_C(0x94d049bb133111eb); h ^= h >> 31;
            if (i == 3 || h > maximum || (h == maximum && strcmp(argv[i], argv[chosen+3]) < 0)) {
                maximum = h; chosen = i-3;
            }
        }
        printf("%s%d", bucket ? "," : "", chosen);
    }
    puts("]"); return 0;
}

static volatile sig_atomic_t terminating;
static volatile sig_atomic_t reloading;
static void signal_controller(int signal)
{
    if (signal == SIGUSR1) reloading = 1;
    else terminating = 1;
}

static int supervise(int argc, char **argv)
{
    if (argc != 4 || strcmp(argv[2], "/usr/bin/ucode") ||
        strcmp(argv[3], "/usr/libexec/sysu-snat/v2/daemon.uc")) return 2;
    int lock = open("/var/run/sysu-snat/v2/controller.lock", O_CREAT | O_WRONLY | O_CLOEXEC, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) return 1;
    struct sigaction action = {.sa_handler = signal_controller};
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL); sigaction(SIGINT, &action, NULL);
    sigaction(SIGUSR1, &action, NULL);
    pid_t child = fork();
    if (child < 0) return 2;
    if (!child) {
        if (setpgid(0, 0) || prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() == 1) _exit(126);
        execv(argv[2], argv + 2); _exit(127);
    }
    setpgid(child, child);
    int status = 0;
    for (;;) {
        if (reloading) { reloading = 0; kill(child, SIGUSR1); }
        if (terminating) kill(-child, SIGKILL);
        if (waitpid(child, &status, 0) == child) break;
        if (errno != EINTR) { kill(-child, SIGKILL); close(lock); return 2; }
    }
    // 锁只在监护进程持有；控制器消失后回收其整个任务组，避免孤儿持锁。
    kill(-child, SIGKILL);
    close(lock);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    if (!strcmp(argv[1], "supervise")) return supervise(argc, argv);
    pid_t parent = getppid();
    if (parent == 1 || prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent) return 2;
    if (!strcmp(argv[1], "run")) return run(argc, argv);
    if (!strcmp(argv[1], "buckets")) return buckets(argc, argv);
    if (!strcmp(argv[1], "resolve")) return resolve(argc, argv);
    return conntrack(argc, argv);
}
