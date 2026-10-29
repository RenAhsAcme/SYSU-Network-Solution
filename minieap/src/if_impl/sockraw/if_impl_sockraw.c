#include "if_impl.h"
#include "config.h"
#include "logging.h"
#include "sched_alarm.h"
#include "sysu.h"
#include "eap_state_machine.h"
#include "session.h"
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <unistd.h>

typedef struct {
    char ifname[IFNAMSIZ];
    int sockfd, linkfd, signalfd, index, stopped;
    void (*handler)(ETH_EAP_FRAME *);
} raw_socket;
#define PRIV ((raw_socket *)this->priv)

static RESULT set_name(IF_IMPL *this, const char *name) {
    if (strlen(name) >= IFNAMSIZ) return FAILURE;
    strcpy(PRIV->ifname, name);
    PRIV->index = if_nametoindex(name);
    return PRIV->index ? SUCCESS : FAILURE;
}
static RESULT get_name(IF_IMPL *this, char *buf, int len) {
    size_t n = strlen(PRIV->ifname) + 1;
    if (len < 0 || (size_t)len < n) return FAILURE;
    memcpy(buf, PRIV->ifname, n);
    return SUCCESS;
}
static RESULT prepare(IF_IMPL *this) {
    PRIV->sockfd = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, htons(0x888e));
    struct sockaddr_ll addr = {.sll_family = AF_PACKET, .sll_protocol = htons(0x888e),
        .sll_ifindex = PRIV->index};
    if (PRIV->sockfd < 0 || bind(PRIV->sockfd, (struct sockaddr *)&addr, sizeof(addr))) return FAILURE;
    /* 只订阅目标接口的认证帧，不修改网卡的混杂模式。 */
    int ignore = 1;
    if (setsockopt(PRIV->sockfd, SOL_PACKET, PACKET_IGNORE_OUTGOING, &ignore, sizeof(ignore))) return FAILURE;
    PRIV->linkfd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_ROUTE);
    struct sockaddr_nl nl = {.nl_family = AF_NETLINK, .nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR};
    if (PRIV->linkfd < 0 || bind(PRIV->linkfd, (struct sockaddr *)&nl, sizeof(nl))) return FAILURE;
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &signals, NULL)) return FAILURE;
    PRIV->signalfd = signalfd(-1, &signals, SFD_CLOEXEC | SFD_NONBLOCK);
    return PRIV->signalfd >= 0 ? SUCCESS : FAILURE;
}
static RESULT refresh_link(IF_IMPL *this) {
    struct ifreq req = {0};
    strcpy(req.ifr_name, PRIV->ifname);
    if (ioctl(PRIV->sockfd, SIOCGIFFLAGS, &req)) return FAILURE;
    eap_link_changed((req.ifr_flags & IFF_UP) && (req.ifr_flags & IFF_RUNNING));
    return SUCCESS;
}
static RESULT capture(IF_IMPL *this) {
    uint8_t buf[4096];
    if (IS_FAIL(refresh_link(this))) return FAILURE;
    struct pollfd fds[] = {{PRIV->sockfd, POLLIN, 0}, {PRIV->linkfd, POLLIN, 0}, {PRIV->signalfd, POLLIN, 0}, {session_fd(), POLLIN, 0}};
    while (!PRIV->stopped) {
        int wait = sched_alarm_timeout(), session_wait = session_timeout();
        if (session_wait >= 0 && (wait < 0 || session_wait < wait)) wait = session_wait;
        int n = poll(fds, 4, wait);
        if (n < 0) { if (errno == EINTR) continue; return FAILURE; }
        if (fds[2].revents & POLLIN) return SUCCESS;
        if (fds[1].revents & POLLIN) {
            int address_changed = 0;
            struct sockaddr_nl from = {0};
            socklen_t fromlen = sizeof(from);
            ssize_t len = recvfrom(PRIV->linkfd, buf, sizeof(buf), MSG_TRUNC, (struct sockaddr *)&from, &fromlen);
            if (len < 0) {
                if (errno != EAGAIN && errno != EINTR && errno != ENOBUFS) {
                    PR_ERRNO("读取链路通知失败"); return FAILURE;
                }
                if (IS_FAIL(refresh_link(this))) return FAILURE;
                address_changed = 1;
                len = 0;
            }
            if (len > (ssize_t)sizeof(buf)) {
                if (IS_FAIL(refresh_link(this))) return FAILURE;
                address_changed = 1;
                len = 0;
            }
            if (from.nl_pid == 0) {
                for (struct nlmsghdr *msg = (struct nlmsghdr *)buf; NLMSG_OK(msg, len); msg = NLMSG_NEXT(msg, len)) {
                    if (msg->nlmsg_type == RTM_NEWADDR || msg->nlmsg_type == RTM_DELADDR) {
                        if (msg->nlmsg_len >= NLMSG_LENGTH(sizeof(struct ifaddrmsg)) &&
                            ((struct ifaddrmsg *)NLMSG_DATA(msg))->ifa_index == (unsigned)PRIV->index) address_changed = 1;
                        continue;
                    }
                    if (msg->nlmsg_len < NLMSG_LENGTH(sizeof(struct ifinfomsg))) continue;
                    struct ifinfomsg *info = NLMSG_DATA(msg);
                    if (info->ifi_index != PRIV->index) continue;
                    if (msg->nlmsg_type == RTM_DELLINK) return FAILURE;
                    if (msg->nlmsg_type == RTM_NEWLINK)
                        eap_link_changed((info->ifi_flags & IFF_UP) && (info->ifi_flags & IFF_RUNNING));
                }
            }
            if (address_changed) session_update_address();
        }
        if (fds[0].revents & POLLIN) {
            struct sockaddr_ll from;
            socklen_t fromlen = sizeof(from);
            ssize_t len = recvfrom(PRIV->sockfd, buf, FRAME_BUF_SIZE, MSG_TRUNC, (struct sockaddr *)&from, &fromlen);
            if (len > 0 && len <= FRAME_BUF_SIZE && from.sll_pkttype != PACKET_OUTGOING) {
                ETH_EAP_FRAME frame = {.actual_len = (size_t)len, .buffer_len = FRAME_BUF_SIZE, .content = buf};
                PRIV->handler(&frame);
            } else if (len < 0 && errno != EAGAIN && errno != EINTR) {
                if (errno != ENETDOWN && errno != ENOBUFS) {
                    PR_ERRNO("接收认证报文失败"); return FAILURE;
                }
                if (IS_FAIL(refresh_link(this))) return FAILURE;
            }
        }
        if (fds[3].revents & POLLIN) eap_session_event(session_dispatch());
        for (unsigned i = 0; i < 4; i++) {
            if (fds[i].revents & (POLLHUP | POLLNVAL)) { PR_ERR("认证通道已关闭（%u）", i); return FAILURE; }
            if (!(fds[i].revents & POLLERR)) continue;
            int error = 0;
            socklen_t size = sizeof(error);
            if (getsockopt(fds[i].fd, SOL_SOCKET, SO_ERROR, &error, &size)) return FAILURE;
            /* 网口临时断开不应重启进程；链路事件负责暂停与恢复。 */
            if ((i == 0 || i == 3) && (!error || error == ENETDOWN || error == ENOBUFS)) {
                if (IS_FAIL(refresh_link(this))) return FAILURE;
                continue;
            }
            if (i == 1 && (!error || error == ENOBUFS)) {
                if (IS_FAIL(refresh_link(this))) return FAILURE;
                session_update_address();
                continue;
            }
            PR_ERR("认证通道错误（%u，%d）", i, error);
            return FAILURE;
        }
        sched_alarm_dispatch();
        eap_session_event(session_tick());
    }
    return SUCCESS;
}
static RESULT send_frame(IF_IMPL *this, ETH_EAP_FRAME *frame) {
    if (!frame || !frame->content || frame->actual_len > frame->buffer_len || frame->actual_len < 14) return FAILURE;
    struct sockaddr_ll addr = {.sll_family = AF_PACKET, .sll_protocol = htons(0x888e),
        .sll_ifindex = PRIV->index, .sll_halen = 6};
    memcpy(addr.sll_addr, frame->content, 6);
    return sendto(PRIV->sockfd, frame->content, frame->actual_len, 0, (struct sockaddr *)&addr, sizeof(addr)) == (ssize_t)frame->actual_len ? SUCCESS : FAILURE;
}
static void set_handler(IF_IMPL *this, void (*handler)(ETH_EAP_FRAME *)) { PRIV->handler = handler; }
static void destroy(IF_IMPL *this) {
    if (PRIV->sockfd >= 0) close(PRIV->sockfd);
    if (PRIV->linkfd >= 0) close(PRIV->linkfd);
    if (PRIV->signalfd >= 0) close(PRIV->signalfd);
    free(this->priv);
    free(this);
}
IF_IMPL *sockraw_new(void) {
    IF_IMPL *this = calloc(1, sizeof(*this));
    raw_socket *raw = calloc(1, sizeof(*raw));
    if (!this || !raw) { free(this); free(raw); return NULL; }
    raw->sockfd = raw->linkfd = raw->signalfd = -1;
    this->priv = raw;
    this->set_ifname = set_name;
    this->get_ifname = get_name;
    this->prepare_interface = prepare;
    this->start_capture = capture;
    this->send_frame = send_frame;
    this->set_frame_handler = set_handler;
    this->destroy = destroy;
    return this;
}
