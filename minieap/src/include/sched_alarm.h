#ifndef SYSU_SCHED_ALARM_H
#define SYSU_SCHED_ALARM_H
#include "minieap_common.h"
int schedule_alarm(int secs, void (*func)(void *), void *user);
void unschedule_alarm(int id);
RESULT sched_alarm_init(void);
void sched_alarm_destroy(void);
int sched_alarm_timeout(void);
void sched_alarm_dispatch(void);
#endif
