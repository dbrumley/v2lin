
/****************************************************************************
 * Copyright (C) 2004, 2005, 2006 v2lin Team <http://v2lin.sf.net>
 * Copyright (C) 2000,2001  Monta Vista Software Inc.
 * 
 * This file is part of the v2lin Library.
 * VxWorks is a registered trademark of Wind River Systems, Inc.
 * 
 * Initial implementation Gary S. Robertson, 2000, 2001.
 * Contributed by Andrew Skiba, skibochka@sourceforge.net, 2004.
 * Contributed by Mike Kemelmakher, mike@ubxess.com, 2005.
 * Contributed by Constantine Shulyupin, conan.sh@gmail.com, 2006.
 * 
 * The v2lin library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * The v2lin Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 ****************************************************************************/

#include <errno.h>
#include <unistd.h>
#include <sched.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>


#include <string.h>
#include "v2lpthread.h"
#include "vxw_defs.h"
#include "vxw_hdrs.h"
#include "internal.h"
#include "v2ldebug.h"
#include "tickLib.h"

void v2pt_TickHandler(unsigned long long);
STATUS v2pt_StartExcTask(void);
STATUS v2pt_StartRootTask(void);

/*
**  process_timer_list is a system function used to service watchdog timers
**                     when a system clock tick expires.  It is called from
**                     the system exception task once per clock tick.
*/
extern void process_timer_list(void);

/*
**  Task control blocks for the v2pthread system tasks.
*/
static task_t root_task;
static task_t excp_task;

static unsigned char round_robin_enabled = 0;

void disableRoundRobin(void)
{
    round_robin_enabled = 0;
}

void enableRoundRobin(void)
{
    round_robin_enabled = 1;
}

BOOL roundRobinIsEnabled(void)
{
    return ((BOOL) round_robin_enabled);
}

/*****************************************************************************
** kernelTimeSlice - turns Round-Robin Timeslicing on or off in the scheduler
*****************************************************************************/
STATUS kernelTimeSlice(int ticks_per_quantum) 
{
    task_t *task;
    int sched_policy;
    TRACEF("%i",ticks_per_quantum);

    taskLock();

    /*
     **  Linux doesn't allow the round-robin quantum to be changed, so
     **  we only use the ticks_per_quantum as an on/off value for
     **  round-robin scheduling.
     */
    if (ticks_per_quantum == 0) {
        /*
         **  Ensure Round-Robin Timeslicing is OFF for all tasks, both
         **  existing and yet to be created.
         */
        round_robin_enabled = 0;
        sched_policy = SCHED_FIFO;
    } else {
        /*
         **  Ensure Round-Robin Timeslicing is ON for all tasks, both
         **  existing and yet to be created.
         */
        round_robin_enabled = 1;
        sched_policy = SCHED_RR;
    }

    struct sched_param schedparam;
    //  Change the scheduling policy for all tasks in the task list.
    for (task = task_list; task ; task = task->nxt_task) {

        /*
         **  First set the new scheduling policy attribute.  Since the
         **  max priorities are identical under Linux for both real time
         **  scheduling policies, we know we don't have to change priority.
         */
        pthread_attr_setschedpolicy(&task->attr, sched_policy);

        pthread_attr_getschedparam(&task->attr, &schedparam);
        //  Activate the new scheduling policy
        CHK0(pthread_setschedparam(task->pthrid, sched_policy, &schedparam));
        // { perror("kernelTimeSlice pthread_setschedparam returned error:"); }
    }

    taskUnlock();

    return (OK);
}

/*****************************************************************************
**  system exception task
**
**  In the v2pthreads environment, the exception task serves only to
**  handle watchdog timer functions and to allow self-restarting of other
**  v2pthread tasks.
*****************************************************************************/
static long long    ms_count = 0;
int exception_task(int dummy0, int dummy1, int dummy2, int dummy3,
                   int dummy4, int dummy5, int dummy6, int dummy7, int dummy8, int dummy9)
{
    TRACEF("%x",exception_task);
    struct timespec ts_sys_start = { 0 },
                    ts_start = { 0 },
                    ts_end = { 0 };
    long long       ns_diff = 0;
    __useconds_t    us_v2pt_tick = 0,
                    us_lx_tick = 0,
                    us_delay = 0,
                    us_diff = 0;
    
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    ts_sys_start = ts_start;
    us_v2pt_tick = us_delay = V2PT_TICK * 1000;
    us_lx_tick = 1000000 / sysRealClkRateGet();
    
    while (1) {
        /*
         **  Process system watchdog timers (if any are defined).
         **  NOTE that since ALL timers must be handled during a single
         **  10 msec system clock tick, timers should be used sparingly.
         **  In addition, the timeout functions called by watchdog timers
         **  should be "short and sweet".
         */
        process_timer_list();

        /*
         **  Delay for one timer tick.  Since this is the highest-priority
         **  task in the v2pthreads virtual machine (except for the root task,
         **  which stays blocked almost all the time), any processing done
         **  in this task can impose a heavy load on the remaining tasks.
         **  For this reason, this task and all watchdog timeout functions
         **  should be kept as brief as possible.
         */
        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        ns_diff = (ts_end.tv_sec == ts_start.tv_sec) ?
                   ts_end.tv_nsec - ts_start.tv_nsec :
                   ts_end.tv_nsec + (V2PT_NANOSEC - ts_start.tv_nsec);
        if (ns_diff < 0) {
            ns_diff += V2PT_NANOSEC;
        }
        us_diff = (__useconds_t)(ns_diff / 1000);
        us_delay = (us_diff > us_v2pt_tick) ? us_delay - us_lx_tick
                                            : us_diff;
        ms_count += us_diff / 1000;
        if (us_diff % 1000 >= 500)
        	ms_count++;
        v2pt_TickHandler(ms_count);
#if 0 /* for debug stage */
        printf("DEBUG: ns_diff = %lld, us_delay = %lu, ms_count = %lu\n\r",
                ns_diff, us_delay, ms_count);
#endif
        ts_start = ts_end;
        usleep(us_delay);
    }
    return 0;
}

/*****************************************************************************
**  v2pt_tickGet
**
**  Return system uptime in ticks
*****************************************************************************/
unsigned long v2pt_tickGet(void)
{
    return (unsigned long)(ms_count / V2PT_TICK);
}

/*****************************************************************************
**  v2pt_tickSet
**
**  Set up system uptime in milliseconds for ticks
*****************************************************************************/
void v2pt_tickSet(unsigned long ticks)
{
    ms_count = ticks * V2PT_TICK;
}

/*****************************************************************************
**  v2pthread main program - NOT IN USE
**
**  This function serves as the entry point to the v2pthreads emulation
**  environment.  It serves as the parent process to all v2pthread tasks.
**  This process creates an initialization thread and sets the priority of
**  that thread to the highest allowable value.  This allows the initialization
**  thread to complete its work without being preempted by any of the task
**  threads it creates.
*****************************************************************************/

int v2lin_init(void)
{
    int max_priority;
    TRACEF();
    // Start system root task
    taskInit(&root_task, "tUsrRoot", 0, 0, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    max_priority = sched_get_priority_max(SCHED_FIFO);
    (root_task.prv_priority).sched_priority = max_priority;
    pthread_attr_setschedparam(&(root_task.attr), &(root_task.prv_priority));
    root_task.state = READY;
    root_task.pthrid = pthread_self();
    taskActivate(root_task.taskid);

    // Start system exception task.
    taskInit(&excp_task, "tExcTask", 0, 0, 0, 0, exception_task, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    max_priority = sched_get_priority_max(SCHED_FIFO);
    (excp_task.prv_priority).sched_priority = max_priority - 1;
    pthread_attr_setschedparam(&(excp_task.attr), &(excp_task.prv_priority));

    taskActivate(excp_task.taskid);

    return 0;
}

/*****************************************************************************
* v2pt_TickHandler:
*
* Update emulated agent time in milliseconds
******************************************************************************/
static unsigned long TimeFromStart = 0;
static unsigned long long agnTime = 0;
void v2pt_TickHandler(unsigned long long mseconds)
{
    agnTime = mseconds;
    TimeFromStart = mseconds;              
}

/*****************************************************************************
* v2pt_agnTimeGet:
*
******************************************************************************/
unsigned long long v2pt_agnTimeGet(void)
{
    return agnTime;
}

/*****************************************************************************
* v2pt_TimeFromStartGet:
*
******************************************************************************/
unsigned long v2pt_TimeFromStartGet(void)
{
    return TimeFromStart;
}

/*****************************************************************************
 * v2pthread (v2lin) Init API:
 *
 * The following set of "Init" functions implements an entry point to the
 * v2pthreads emulation environment
 *
 *****************************************************************************/
/*****************************************************************************
* v2pt_Init:	v2pthread main program
*
******************************************************************************/
STATUS v2pt_Init(int ticksPerSecond)
{
    STATUS status = OK;
    
	/*************************************************************************/
	/* set up virtual clock rate for v2lin environment - this must be done   */
    /* first                                                                 */
	/*************************************************************************/
   	status = v2pt_sysClkRateSet(ticksPerSecond);
	
	/*************************************************************************/
	/* init virtual uptime of the system                                     */
	/*************************************************************************/
    tickSet(0);
    
	/*************************************************************************/
	/* start exception task (timers)                                         */
	/*************************************************************************/
   	if (OK == status) {
   		status = v2pt_StartExcTask();
   	}
   	
	/*************************************************************************/
	/* Start system root task                                                */
	/*************************************************************************/
   	if (OK == status) {
   		status = v2pt_StartRootTask();
   	}
    
    return status;
}

/*****************************************************************************
 * v2pt_StartExcTask:
 *
 *****************************************************************************/
static task_t excp_task;
STATUS v2pt_StartExcTask(void)
{
    int max_priority;
    STATUS status = OK;

	/* Start system exception (timers) task */
    if ((status = taskInit(&excp_task,
                           "tExcTask",
                           99, 0, 0, 0,
                           exception_task,
                           0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) != OK) {
        printf("\n\rv2pt_StartExcTask: tExcTask init failed!\n\r");
    }

    if (OK == status) {
        if ((status = taskActivate(excp_task.taskid)) != OK)  {
            printf("\n\rv2pt_StartExcTask: tExcTask activation failed!\n\r");
        }
    }

	if (OK == status) {
		status = v2pt_set_realtime_prio(excp_task.pthrid,
										SCHED_FIFO,
										sched_get_priority_max(SCHED_FIFO) - 1);
	}

#ifdef PTHREAD_NAME_SUPPORTED
    /* pthread names are not yet supported in our CentOS installation */
    if (OK == status) {
        if ((status =
                pthread_setname_np(excp_task.pthrid, excp_task.taskname)) != 0) {
            perror("tExcTask: pthread_setname_np");
        }
    }
#endif /* PTHREAD_NAME_SUPPORTED */
    return status;
}

/*****************************************************************************
 * v2pt_StartRootTask:
 *
 *****************************************************************************/
static task_t root_task;
STATUS v2pt_StartRootTask(void)
{
    STATUS status = OK;
    extern int main_root_task(void);

	/* Start system root task */
	if ((status = taskInit(&root_task,
			               "tUsrRoot",
			               99, 0, 0, 0,
			               main_root_task,
			               0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) != OK) {
        printf("\n\rv2pt_StartRootTask: tUsrRoot init failed!\n\r");
	}

	if (OK == status) {
		if ((status = taskActivate(root_task.taskid)) != OK) {
            printf("\n\rv2pt_StartRootTask: tUsrRoot activation failed!\n\r");
        }
	}
	
	if (OK == status) {
		status = v2pt_set_realtime_prio(root_task.pthrid,
										SCHED_FIFO,
										sched_get_priority_max(SCHED_FIFO) - 2);
	}
	
#ifdef PTHREAD_NAME_SUPPORTED
    /* pthread names are not yet supported in our CentOS installation */
    if (OK == status) {
        if ((status =
                pthread_setname_np(root_task.pthrid, root_task.taskname)) != 0) {
            perror("tUsrRoot: pthread_setname_np");
        }
    }
#endif /* PTHREAD_NAME_SUPPORTED */
    return status;
}

int main_root_task(void)
{
	/*************************/
	/* user root task code   */
	/*************************/

	return OK;
}
