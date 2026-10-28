#include "sched_alarm.h"
#include "sysu.h"
#include "logging.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
static struct timer {
    int id;
    int64_t deadline;
    void (*func)(void *);
    void *user;
} timers[8];
static unsigned sequence;
RESULT sched_alarm_init(void) { memset(timers, 0, sizeof(timers)); return SUCCESS; }
void sched_alarm_destroy(void) { memset(timers, 0, sizeof(timers)); }
int schedule_alarm(int secs, void (*func)(void *), void *user) {
    if (secs < 0 || !func) return -1;
    for (unsigned i = 0; i < 8; i++) {
        if (timers[i].id) continue;
        sequence = sequence % INT_MAX + 1;
        timers[i] = (struct timer){(int)sequence, monotonic_ms() + (int64_t)secs * 1000, func, user};
        return (int)sequence;
    }
    PR_ERR("定时器容量不足");
    exit(EXIT_FAILURE);
}
void unschedule_alarm(int id) {
    for (unsigned i = 0; i < 8; i++)
        if (timers[i].id == id) timers[i].id = 0;
}
int sched_alarm_timeout(void) {
    int64_t wait = INT_MAX, now = monotonic_ms();
    int found = 0;
    for (unsigned i = 0; i < 8; i++) {
        if (!timers[i].id) continue;
        found = 1;
        int64_t remaining = timers[i].deadline - now;
        if (remaining < wait) wait = remaining;
    }
    return !found ? -1 : wait <= 0 ? 0 : (int)wait;
}
void sched_alarm_dispatch(void) {
    int64_t now = monotonic_ms();
    for (unsigned i = 0; i < 8; i++) {
        if (!timers[i].id || timers[i].deadline > now) continue;
        struct timer event = timers[i];
        timers[i].id = 0;
        event.func(event.user);
    }
}
