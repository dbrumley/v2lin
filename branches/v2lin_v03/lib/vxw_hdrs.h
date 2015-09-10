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

#ifndef __VXW_HDRS_H
#define __VXW_HDRS_H

//#include <features.h>
#include <sys/types.h>
#include "vxw_defs.h"
#include "v2lpthread.h"



__BEGIN_DECLS

/*
**  Object Compatibility data types
**
**  These data types are defined here to provide source code compatibility with
**  existing vxWorks code.  NOTE that they do not necessarily correspond to the
**  actual Wind River definitions for the defined types, nor to the actual
**  types of the objects they reference as defined in the v2pthreads
**  environment.  THIS MAY CAUSE LINKER ERRORS IF TYPE-SAFE LINKAGE IS USED!
**  It works okay with standard ANSII C-style linkage, however. 
*/

/*****************************************************************************
**  Control block for v2pthread watchdog timer
**
**  These watchdog timers provide a means of executing delayed or cyclic
**  functions.  They are inherently 'one-shot' timers.  For cyclic operation,
**  the timeout handler function must call wdStart to restart the timer.
**  In the v2pthreads environment, these timers execute from the
**  context of the system exception task rather tha the timer interrupt.
*****************************************************************************/
typedef struct v2pt_mqueue * MSG_Q_ID;
typedef struct v2lsem *SEM_ID;
typedef struct v2pt_wdog *WDOG_ID;
typedef int SEM_B_STATE;
/* typedef void *FUNCPTR; */

typedef int (*FUNCPTR)();
typedef void (*V2Lin_PrintCb_T)(void *cookie, char *fmt, ...);

/* This function must be called ASAP in main() */
/* NOTE: we use v2pt_Init() instead            */
extern int v2lin_init(void);
extern int v2pt_Init(int);

/*
**  Round-Robin Scheduling Control
**
**  The following three functions are unique to v2pthreads. 
**  They are used to manipulate a global system setting which affects all
**  tasks spawned or initialized after the round-robin control call is made.
**  They have no effect on tasks spawned or initialized prior to the call.
**  Round-Robin scheduling causes tasks at the same priority level to be
**  scheduled on a 'time-sliced' basis within that priority level, so that
**  all tasks at a given priority level get an equal opportunity to execute.
**  Round-robin scheduling is TURNED OFF by default.
*/

extern void disableRoundRobin(void);
extern void enableRoundRobin(void);
extern BOOL roundRobinIsEnabled(void);
extern STATUS kernelTimeSlice(int ticks_per_quantum);

/*
**  taskLib Function Prototypes
*/

/* 
 * Currently the taskSpawn does not support any options. 
 * For this all options defines like zero(0)
 */ 
#define VX_FP_TASK	0
extern int taskSpawn(char *name, int pri, int opts, int stksize, FUNCPTR entry, int arg1,
					 int arg2, int arg3, int arg4, int arg5, int arg6, int arg7, int arg8,
					 int arg9, int arg10);

extern STATUS taskInit(WIND_TCB * task, char *name, int pri,
					   int opts, char *pstack, int stksize, FUNCPTR entry, int arg1, int arg2,
					   int arg3, int arg4, int arg5, int arg6, int arg7, int arg8, int arg9,
					   int arg10);

STATUS taskActivate(int taskId);
STATUS taskDelete(int taskId);
STATUS taskDeleteForce(int taskId);
STATUS taskSuspend(int taskId);
STATUS taskResume(int taskId);
STATUS taskRestart(int taskId);
STATUS taskPrioritySet(int taskId, int priority);
STATUS taskPriorityGet(int taskId, int *priority);
STATUS taskLock(void);
STATUS taskUnlock(void);
STATUS taskSafe(void);
STATUS taskUnsafe(void);
STATUS taskDelay(int ticks_to_wait);
char *taskName(int taskId);
int taskNameToId(char *task_name);
STATUS taskIdVerify(int taskId);
int taskIdSelf(void);
int taskIdDefault(int taskId);
BOOL taskIsReady(int taskId);
BOOL taskIsSuspended(int taskId);
WIND_TCB *taskTcb(int taskId);
int taskIdListGet(int list[], int maxIds);
STATUS taskWait(int tid); /* New vxWorks functionality :-) */

/*
**  msgQLib Function Prototypes
*/
extern MSG_Q_ID msgQCreate(int max_msgs, int msglen, int opt);
extern STATUS msgQDelete(MSG_Q_ID queue);
extern STATUS msgQSend(MSG_Q_ID queue, char *msg, uint msglen, int wait, int pri);
extern int msgQReceive(MSG_Q_ID queue, char *msgbuf, uint buflen, int max_wait);
extern int msgQNumMsgs(MSG_Q_ID queue);

/* New vxWorks functionality - communication between processes */
/*
 * --------------------------------------------------------------------------
 *		              /!|  I M P O R T A N T :
 * --------------------------------------------------------------------------
 *       Using functions: 
 *			- mq_open()
 *			- mq_unlink()
 *			- mq_close()
 *			- mq_send() / mq_timesend()
 *			- mq_receive() /  / mq_timereceive()
 *			- mq_getattr() / mq_setattr()
 *		needed the Kernel and glibc support and configuration:
 *		POSIX message queues have been supported on Linux since kernel 2.6.6. 
 *		Glibc support has been provided since version 2.3.4. 
 *		Support for POSIX message queues is configurable via the CONFIG_POSIX_MQUEUE 
 *		kernel configuration option
 * --------------------------------------------------------------------------
 */
extern MSG_Q_ID msgQOpen ( const char * name, int max_msgs, int msglen, 
	   int opt, int mode, void * context );	
extern STATUS msgQClose ( MSG_Q_ID msgQId );
extern STATUS msgQUnlink ( const char * name );

/*
**  semLib Function Prototypes
*/
extern STATUS semGive(SEM_ID semaphore);
extern STATUS semTake(SEM_ID semaphore, int max_wait);
extern STATUS semFlush(SEM_ID semaphore);
extern STATUS semDelete(SEM_ID semaphore);
extern SEM_ID semBCreate(int opt, SEM_B_STATE initial_state);
extern SEM_ID semCCreate(int opt, int initial_count);
extern SEM_ID semMCreate(int opt);
extern STATUS semMGiveForce(SEM_ID semaphore);

/*
**  wdLib Function Prototypes
*/
extern STATUS wdCancel(WDOG_ID wdId);
extern WDOG_ID wdCreate(void);
extern STATUS wdDelete(WDOG_ID wdId);
extern STATUS wdStart(WDOG_ID wdId, int delay, void (*f) (int), int parm);

/* additinal utility functions */

int msgQList(V2Lin_PrintCb_T printFunc_p, void *cookie, int mem);
int taskList(V2Lin_PrintCb_T printFunc_p, void *cookie, int mem);
int semList(V2Lin_PrintCb_T printFunc_p, void *cookie, int mem);
int wdogShow(V2Lin_PrintCb_T printFunc_p, void *cookie);
char * VxWorksError(STATUS status);


STATUS eventReceive(unsigned int events, unsigned char flags, int timeOut, unsigned int * events_r);
STATUS eventSend(int taskId, unsigned long events);
STATUS eventClear(void);

STATUS msgQEvStart (MSG_Q_ID msgQId, unsigned int events, unsigned char options);
STATUS msgQEvStop  (MSG_Q_ID msgQId);


int sysClkRateGet(void);
int exception_task(int dummy0, int dummy1, int dummy2, int dummy3,
                   int dummy4, int dummy5, int dummy6, int dummy7, int dummy8, int dummy9);

int sysRealClkRateGet(void);

__END_DECLS

#endif
