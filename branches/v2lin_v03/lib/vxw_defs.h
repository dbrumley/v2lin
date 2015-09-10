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

__BEGIN_DECLS
/*
**  Generic data types
*/
#ifndef BOOL
#define BOOL int
#endif
#ifndef STATUS
#define STATUS int
#endif
#ifndef LOCAL
#define LOCAL static
#endif
#ifndef FAST
#define FAST register
#endif
#ifndef IMPORT
#define IMPORT extern
#endif
#ifndef OK
#define OK 0
#endif
#ifndef ERROR
#define ERROR (-1)
#endif
#ifndef TASK_ID
#define TASK_ID int
#endif


/*
**  Miscellaneous error codes
*/
#define TASK_ERRS                       0x00030000
#define MEM_ERRS                        0x00110000
#define MSGQ_ERRS                       0x00410000
#define OBJ_ERRS                        0x003d0000
#define SEM_ERRS                        0x00160000
#define SM_OBJ_ERRS                     0x00580000
#define S_memLib_NOT_ENOUGH_MEMORY      (MEM_ERRS + 1)
#define S_msgQLib_INVALID_MSG_LENGTH    (MSGQ_ERRS + 1)
#define S_msgQLib_INVALID_MSG_COUNT		(MSGQ_ERRS + 2)
#define S_objLib_OBJ_INVALID_ARGUMENT	(OBJ_ERRS + 5)
#define S_objLib_OBJ_DELETED            (OBJ_ERRS + 3)
#define S_objLib_OBJ_ID_ERROR           (OBJ_ERRS + 1)
#define S_objLib_OBJ_TIMEOUT            (OBJ_ERRS + 4)
#define S_objLib_OBJ_UNAVAILABLE        (OBJ_ERRS + 2)
#define S_semLib_INVALID_OPERATION      (SEM_ERRS + 0x00000068)
#define S_smObjLib_NOT_INITIALIZED      (SM_OBJ_ERRS + 1)
#define S_taskLib_ILLEGAL_PRIORITY      (TASK_ERRS + 0x00000065)
#define EV_OBJ_ERRS                     0x00860000
#define S_eventLib_TIMEOUT              (EV_OBJ_ERRS + 1)
#define S_eventLib_NOT_ALL_EVENTS       (EV_OBJ_ERRS + 2)
#define S_eventLib_ZERO_EVENTS          (EV_OBJ_ERRS + 3)
#define S_eventLib_ALREADY_REGISTERED   (EV_OBJ_ERRS + 4)
#define S_eventLib_EVENTSEND_FAILED		(EV_OBJ_ERRS + 5)
#define S_eventLib_TASK_NOT_REGISTERED	(EV_OBJ_ERRS + 6)

/* ---- */


/*
**  Timeout options
*/
#define NO_WAIT                         0
#define WAIT_FOREVER                    -1
/*
**  Message Queue Option Flags
*/
#define MSG_PRI_NORMAL                  0x00
#define MSG_PRI_URGENT                  0x01
#define MSG_Q_FIFO                      0x00
#define MSG_Q_PRIORITY                  0x01
/*
**  Semaphore Option Flags
*/
#define SEM_EMPTY                       0x00
#define SEM_FULL                        0x01
#define SEM_Q_FIFO                      0x00
#define SEM_Q_PRIORITY                  0x01
#define SEM_DELETE_SAFE                 0x04
#define SEM_INVERSION_SAFE              0x08

#define SEM_TYPE_MUTEX					0x00




/*
**  Events Option Flags
*/
#define EVENTS_WAIT_ANY                 0x1
#define EVENTS_WAIT_ALL                 0x0
#define EVENTS_RETURN_ALL               0x2
#define EVENTS_KEEP_UNWANTED		    0x4
#define EVENTS_FETCH                    0x8


#define EVENT_ANY			EVENTS_WAIT_ANY			/* (0x1) */
#define EVENT_ALL			EVENTS_WAIT_ALL			/* (0x0) */
#define EVENT_RETURN_ALL	EVENTS_RETURN_ALL		/* (0x2) */
#define EVENT_KEEP_UNWANTED	EVENTS_KEEP_UNWANTED	/* (0x4) */
#define EVENT_FETCH			EVENTS_FETCH			/* (0x8) */

/*
**  Events options for message queue extension flags
*/
#define EVENTS_SEND_ONCE                0x1
#define EVENTS_ALLOW_OVERWRITE          0x2
#define EVENTS_SEND_IF_FREE             0x4
#define EVENTS_OPTIONS_NONE             0x0

/*
 * New options
 */
/* 
 * msgQOpen options:
 * 	OM_CREATE -	Create a message queue if none is found.
 * 	OM_EXCL - When set jointly with the OM_CREATE flag, create a new message queue without
 * 		trying to open an existing one. The call fails if name causes a name clash. This flag has
 * 		no effect if the flag OM_CREATE is not set.
 * 	OM_DELETE_ON_LAST_CLOSE - Only used when a message queue is created. If set, the message queue is deleted during
 * 		the last msgQClose( ) call, independently of whether msgQUnlink( ) was previously
 * 		called or not.
 */
#include <fcntl.h>
#define	OM_CREATE					O_CREAT  	/* 00000100  - Lunux standart */
#define	OM_EXCL						O_EXCL		/* 00000200  - Lunux standart */
#define	OM_DELETE_ON_LAST_CLOSE		0x00000800	/* 00000400  - re-definition */
		
#include <pthread.h>

__END_DECLS
