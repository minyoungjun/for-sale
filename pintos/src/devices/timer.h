#ifndef DEVICES_TIMER_H
#define DEVICES_TIMER_H

#include <round.h>
#include <stdint.h>
#include "lib/kernel/list.h"

/* Number of timer interrupts per second. */
#define TIMER_FREQ 100

void timer_init (void);
void timer_calibrate (void);

int64_t timer_ticks (void);
int64_t timer_elapsed (int64_t);

/* Sleep and yield the CPU to other threads. */
void timer_sleep (int64_t ticks);
void timer_msleep (int64_t milliseconds);
void timer_usleep (int64_t microseconds);
void timer_nsleep (int64_t nanoseconds);

/* Busy wait. */
void timer_mdelay (int64_t milliseconds);
void timer_udelay (int64_t microseconds);
void timer_ndelay (int64_t nanoseconds);

void timer_print_stats (void);

/* Non-busy-wait 구현에 필요한 함수 */
struct blocked_thread {
	struct thread* t;
	int64_t unblockable_time;  /* 이 시간 이후부터 unblock될 수 있다.*/
	struct list_elem elem;
};

void alarmclock_init (void);
void check_unblockable (void);
bool cmp_func (const struct list_elem *, const struct list_elem *, void* );

#endif /* devices/timer.h */
