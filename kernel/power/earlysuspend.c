/* kernel/power/earlysuspend.c
 *
 * Copyright (C) 2005-2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/earlysuspend.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rtc.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>

#include "power.h"
#include <mach/boot_shared_imem_cookie.h>////ZTE_PM_liukejing 20130621
#include <mach/msm_iomap.h>
#include <linux/io.h>

enum {
	DEBUG_USER_STATE = 1U << 0,
	DEBUG_SUSPEND = 1U << 2,
	DEBUG_VERBOSE = 1U << 3,
};
static int debug_mask = DEBUG_USER_STATE | DEBUG_SUSPEND;
module_param_named(debug_mask, debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);

#define FEATURE_ZTE_EARLYSUSPEND_DEBUG	//LHX_PM_20110303_01 add for earlysuspend debug to find which modem consume huge current during earlysuspend
#ifdef FEATURE_ZTE_EARLYSUSPEND_DEBUG
static int debug_earlysuspend_level = 500;
module_param_named(earlysuspend_level, debug_earlysuspend_level, int, S_IRUGO | S_IWUSR | S_IWGRP);
#endif
static int debug_amssawake_timeout_crash = 0;////ZTE_PM_liukejing 20130621
static struct boot_shared_imem_cookie_type *shared_imem_ptr = 
    (struct boot_shared_imem_cookie_type *)MSM_IMEM_BASE;////ZTE_PM_liukejing 20130621
module_param_named(amssawake_timeout_crash, debug_amssawake_timeout_crash, int, S_IRUGO | S_IWUSR | S_IWGRP);
static DEFINE_MUTEX(early_suspend_lock);
static LIST_HEAD(early_suspend_handlers);
static void early_suspend(struct work_struct *work);
static void late_resume(struct work_struct *work);
static DECLARE_WORK(early_suspend_work, early_suspend);
static DECLARE_WORK(late_resume_work, late_resume);
static DEFINE_SPINLOCK(state_lock);
enum {
	SUSPEND_REQUESTED = 0x1,
	SUSPENDED = 0x2,
	SUSPEND_REQUESTED_AND_SUSPENDED = SUSPEND_REQUESTED | SUSPENDED,
};
static int state;

void register_early_suspend(struct early_suspend *handler)
{
	struct list_head *pos;

	mutex_lock(&early_suspend_lock);
	list_for_each(pos, &early_suspend_handlers) {
		struct early_suspend *e;
		e = list_entry(pos, struct early_suspend, link);
		if (e->level > handler->level)
			break;
	}
	list_add_tail(&handler->link, pos);
	if ((state & SUSPENDED) && handler->suspend)
		handler->suspend(handler);
	mutex_unlock(&early_suspend_lock);
}
EXPORT_SYMBOL(register_early_suspend);

void unregister_early_suspend(struct early_suspend *handler)
{
	mutex_lock(&early_suspend_lock);
	list_del(&handler->link);
	mutex_unlock(&early_suspend_lock);
}
EXPORT_SYMBOL(unregister_early_suspend);

#ifndef CONFIG_ZTE_PLATFORM_LCD_ON_TIME
#define CONFIG_ZTE_PLATFORM_LCD_ON_TIME 1
#endif

#ifdef CONFIG_ZTE_PLATFORM_LCD_ON_TIME
extern void zte_update_lateresume_2_earlysuspend_time(bool resume_or_earlysuspend);	//LHX_PM_20110411_01 resume_or_earlysuspend? lateresume : earlysuspend
#endif
static void early_suspend(struct work_struct *work)
{
	struct early_suspend *pos;
	unsigned long irqflags;
	int abort = 0;

	mutex_lock(&early_suspend_lock);
	spin_lock_irqsave(&state_lock, irqflags);
	if (state == SUSPEND_REQUESTED)
		state |= SUSPENDED;
	else
		abort = 1;
	spin_unlock_irqrestore(&state_lock, irqflags);
    shared_imem_ptr->modemawake_timeout_crash = debug_amssawake_timeout_crash;////ZTE_PM_liukejing 20130621
	if (abort) {
		if (debug_mask & DEBUG_SUSPEND)
			pr_info("early_suspend: abort, state %d\n", state);
		mutex_unlock(&early_suspend_lock);
		goto abort;
	}

	if (debug_mask & DEBUG_SUSPEND)
		pr_info("early_suspend: call handlers\n");
	list_for_each_entry(pos, &early_suspend_handlers, link) {
		if (pos->suspend != NULL) {
			#ifdef FEATURE_ZTE_EARLYSUSPEND_DEBUG
			if(debug_earlysuspend_level < pos->level)
			{
				pr_info("NO early_suspend: handlers level = %d \n",pos->level);
				break;
			}
			#endif
			if (debug_mask & DEBUG_SUSPEND)
				pr_info("early_suspend: handlers level=%d, calling %pf\n", pos->level, pos->suspend);
			pos->suspend(pos);
		}
	}
	mutex_unlock(&early_suspend_lock);

	if (debug_mask & DEBUG_SUSPEND)
		pr_info("early_suspend: sync\n");
	suspend_sys_sync_queue();
#ifdef CONFIG_ZTE_PLATFORM_LCD_ON_TIME
	zte_update_lateresume_2_earlysuspend_time(false);//LHX_PM_20110411_01,update earlysuspend time
#endif
abort:
	spin_lock_irqsave(&state_lock, irqflags);
	if (state == SUSPEND_REQUESTED_AND_SUSPENDED)
		wake_unlock(&main_wake_lock);
	spin_unlock_irqrestore(&state_lock, irqflags);
}

#ifndef CONFIG_ZTE_PLATFORM
#define CONFIG_ZTE_PLATFORM
#endif
//#ifdef CONFIG_ZTE_PLATFORM
//extern int cpufreq_set_min_freq(int flag);
//#endif
static void late_resume(struct work_struct *work)
{
	struct early_suspend *pos;
	unsigned long irqflags;
	int abort = 0;

	//#ifdef CONFIG_ZTE_PLATFORM
	//cpufreq_set_min_freq(1);//set to max freq
    //#endif
	mutex_lock(&early_suspend_lock);
	spin_lock_irqsave(&state_lock, irqflags);
	if (state == SUSPENDED)
		state &= ~SUSPENDED;
	else
		abort = 1;
	spin_unlock_irqrestore(&state_lock, irqflags);

	if (abort) {
		if (debug_mask & DEBUG_SUSPEND)
			pr_info("late_resume: abort, state %d\n", state);
		goto abort;
	}
	if (debug_mask & DEBUG_SUSPEND)
		pr_info("late_resume: call handlers\n");
	list_for_each_entry_reverse(pos, &early_suspend_handlers, link) {
		if (pos->resume != NULL) {
			if (debug_mask & DEBUG_SUSPEND)
				pr_info("late_resume: handlers level=%d, calling %pf\n", pos->level, pos->resume);

			pos->resume(pos);
		}
	}
	if (debug_mask & DEBUG_SUSPEND)
		pr_info("late_resume: done\n");
#ifdef CONFIG_ZTE_PLATFORM_LCD_ON_TIME
	zte_update_lateresume_2_earlysuspend_time(true);//LHX_PM_20110411_01 update resume time
#endif
abort:
	mutex_unlock(&early_suspend_lock);
	//#ifdef CONFIG_ZTE_PLATFORM
	//cpufreq_set_min_freq(0);//set to max freq
    //#endif
}

void request_suspend_state(suspend_state_t new_state)
{
	unsigned long irqflags;
	int old_sleep;
	int wq_status = -1;

	spin_lock_irqsave(&state_lock, irqflags);
	old_sleep = state & SUSPEND_REQUESTED;
	if (debug_mask & DEBUG_USER_STATE) {
		struct timespec ts;
		struct rtc_time tm;
		getnstimeofday(&ts);
		rtc_time_to_tm(ts.tv_sec, &tm);
		pr_info("request_suspend_state: %s (%d->%d) at %lld "
			"(%d-%02d-%02d %02d:%02d:%02d.%09lu UTC)\n",
			new_state != PM_SUSPEND_ON ? "sleep" : "wakeup",
			requested_suspend_state, new_state,
			ktime_to_ns(ktime_get()),
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec);
	}
	if (!old_sleep && new_state != PM_SUSPEND_ON) {
		state |= SUSPEND_REQUESTED;
		wq_status = queue_work(suspend_work_queue, &early_suspend_work);
	} else if (old_sleep && new_state == PM_SUSPEND_ON) {
		state &= ~SUSPEND_REQUESTED;
		wake_lock(&main_wake_lock);
		wq_status = queue_work(suspend_work_queue, &late_resume_work);
	}
	pr_info("[early_suspend] wq status=%d \n",wq_status);
	requested_suspend_state = new_state;
	spin_unlock_irqrestore(&state_lock, irqflags);
}

suspend_state_t get_suspend_state(void)
{
	return requested_suspend_state;
}
