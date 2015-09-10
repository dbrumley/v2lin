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

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <fcntl.h>           /* For O_* constants */
#include <sys/stat.h>        /* For mode constants */
#include <mqueue.h>
#include "v2lpthread.h"
#include "vxw_hdrs.h"
#include "vxw_defs.h"
#include "internal.h"
#include "v2ldebug.h"

#define MSGQ_PATH_TO     ""
#define MSGQ_NAME_LEN	32
#define MSGQ_PATH_LEN	128

#define SEND  0
#define URGNT 1
#define KILLD 2

typedef struct q_msg
{
	uint msglen;
	char *msgbuf;
} q_msg_t;

/*****************************************************************************
**  Control block for v2pthread queue
**
**  The message list for a queue is organized into an array called an extent.
**  Actual send and fetch operations are done using a queue_head and
**  queue_tail pointer.  These pointers must 'rotate' through the extent to
**  create a logical circular buffer.  A single extra location is added
**  to ensure room for urgent messages even when the queue is 'full' for
**  normal messages.
**
*****************************************************************************/
typedef struct v2pt_mqueue
{
	// Mutex and Condition variable for queue send/pend
	pthread_mutex_t queue_lock;
	pthread_cond_t queue_send;

	// Mutex and Condition variable for queue delete
	pthread_mutex_t qdlet_lock;
	pthread_cond_t qdlet_cmplt;

	// Mutex and Condition variable for queue-full pend 
	pthread_mutex_t qfull_lock;
	pthread_cond_t queue_space;

	//  Pointer to next message pointer to be fetched from queue
	q_msg_t *queue_head;

	// Pointer to last message pointer sent to queue
	q_msg_t *queue_tail;

	int send_type; // Type of send operation last performed on queue

	q_msg_t *first_msg_in_queue;
	q_msg_t *last_msg_in_queue;

	struct v2pt_mqueue *nxt_queue;

	// First task control block in list of tasks waiting to receive a message from queue
	task_t *first_susp;

	// First task control block in list of tasks waiting for space to post messages to queue
	task_t *first_write_susp;

	int msg_count; 		// Total number of messages currently sent to queue
	int msgs_per_queue; // Total (max) messages per queue
	uint msg_len; 		// Maximum size of messages sent to queue
	size_t vmsg_len; 	// sizeof( each element in queue ) used for subscript incr/decr.
	int order; 			// Task pend order (FIFO or Priority) for queue
    // fields for new options, unsupported before 						
	mqd_t 	mq;			// message queue descriptor for use by other message queue functions;
	                    // if mq more than zero we knows that is a interprocess pipe otherwise a local
	int 	mode; 		// in vxWorks may have values: OM_CREATE | OM_EXCL | OM_DELETE_ON_LAST_CLOSE
	char	qname[MSGQ_NAME_LEN];		// name of the named queue
    int eventTask; // Extension library for msgQEvent, that can receive event
                   // if any message is pended to the queue
    unsigned long eventEvents; // Events to send
    unsigned char eventOptions; // Event options
} v2pt_mqueue_t;

static v2pt_mqueue_t *mqueue_list;
static pthread_mutex_t mqueue_list_lock = PTHREAD_MUTEX_INITIALIZER;

int msgQShow(v2pt_mqueue_t *q , V2Lin_PrintCb_T printFunc_p, void *cookie, int mem)
{
	task_t *t; int i;
	
	printFunc_p(cookie, "%x num=%i len=%i: ", q, q->msgs_per_queue, q->msg_len);
	printFunc_p(cookie, "readers: ");
	
	i = 0;
	for (t = q->first_susp; t; t = t->nxt_susp)   {
		printFunc_p(cookie,"%x %s ",t,t->taskname);
		i++;
	
		if (i > 10)  
			break;
	}
	
	i = 0;
	printFunc_p(cookie, "writers: ");
	
	for (t = q->first_write_susp; t; t = t->nxt_susp)  {
		printFunc_p(cookie, "%x %s ", t, t->taskname);
		i++;
		
		if (i > 10)  
			break;
	}
	
	if (mem) {
		int *w;
		
		for (w = (int*)q; w < (int*)(q + 1); w++) 
			printFunc_p(cookie, "%x ",*w);
	}
	
	printFunc_p(cookie, "\r\n");
	
	q = q->nxt_queue;
	
	return 0;
}

int msgQList(V2Lin_PrintCb_T printFunc_p, void *cookie, int mem)
{
	TRACEF();
	
	int c = 0;
	v2pt_mqueue_t * q = mqueue_list;
	
	printFunc_p(cookie, "\r\n");
	
	while (q){
		c++;
		printFunc_p(cookie, "%i ", c);
		msgQShow(q, printFunc_p, cookie, mem);
		q = q->nxt_queue;
	}
	
	return c;
}



/*****************************************************************************
**  mqueue_find_lock - verifies whether the specified queue still exists, and if
**                so, locks exclusive access to the queue for the caller.
*****************************************************************************/
static int mqueue_find_lock(v2pt_mqueue_t * queue)
{
	v2pt_mqueue_t *current_qcb;
	int found_queue;
	found_queue = FALSE;
	pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock, (void *) &mqueue_list_lock);
	pthread_mutex_lock(&mqueue_list_lock);
	for (current_qcb = mqueue_list;
			current_qcb != NULL; current_qcb = current_qcb->nxt_queue) {
		if (current_qcb == queue) {
			/*
			 ** Lock mutex for queue access (it is assumed that a
			 ** 'pthread_cleanup_push()' has already been performed
			 **  by the caller in case of unexpected thread termination.)
			 */
			pthread_mutex_lock(&queue->queue_lock);
			found_queue = TRUE;
			break;
		}
	}
	pthread_cleanup_pop(1);
	return found_queue;
}

/*****************************************************************************
** link_qcb - appends a new queue control block pointer to the mqueue_list
*****************************************************************************/
static void link_qcb(v2pt_mqueue_t * new_mqueue)
{
	v2pt_mqueue_t **i;

	pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock, (void *) &mqueue_list_lock);
	pthread_mutex_lock(&mqueue_list_lock);

	i = &mqueue_list; 
	while (*i) i = & (*i)->nxt_queue; // find tail
	new_mqueue->nxt_queue = NULL;
	*i = new_mqueue;
	TRACEF("add queue cb @ %p ",new_mqueue);
	pthread_mutex_unlock(&mqueue_list_lock);
	pthread_cleanup_pop(0);
}

/*****************************************************************************
** unlink_qcb - removes a queue control block pointer from the mqueue_list
*****************************************************************************/
static v2pt_mqueue_t *unlink_qcb(v2pt_mqueue_t * qid)
{
	v2pt_mqueue_t **i = &mqueue_list;
	pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock, (void *) &mqueue_list_lock);
	pthread_mutex_lock(&mqueue_list_lock);

	while (*i) {
		if ( *i == qid ) {
			TRACEF("%p", qid);
			*i = (*i)->nxt_queue;	// remove
			break;
		}
		i = &((*i)->nxt_queue);
	}
	pthread_cleanup_pop(1);
	return qid;
}

/*****************************************************************************
** urgent_msg_to - sends a message to the front of the specified queue
*****************************************************************************/
static void urgent_msg_to(v2pt_mqueue_t * queue, char *msg, uint msglen)
{
	uint i;
	char *element;
	TRACEF();
	/*
	 **  It is assumed when we enter this function that the queue has space
	 **  to accept the message about to be sent. 
	 **  Pre-decrement the queue_head (fetch) pointer, adjusting for
	 **  possible wrap to the end of the queue;
	 **  (Urgent messages are placed at the queue head so they will be the
	 **  next message fetched from the queue - ahead of any
	 **  previously-queued messages.)
	 */
	element = (char *) queue->queue_head;
	element -= queue->vmsg_len;
	queue->queue_head = (q_msg_t *) element;

	if (queue->queue_head < queue->first_msg_in_queue) {
		/*
		 **  New queue_head pointer underflowed beginning of the extent...
		 **  Wrap the queue_head pointer to the last message address
		 **  in the extent allocated for the queue.
		 */
		queue->queue_head = queue->last_msg_in_queue;
	}
	TRACEF(" new queue_head @ %p", queue->queue_head);

	if (msg != (char *) NULL) {
		element = (char *) &((queue->queue_head)->msgbuf);
		for (i = 0; i < msglen; i++) {
			*(element + i) = *(msg + i);
		}
	}
	(queue->queue_head)->msglen = msglen;

	TRACEF("nsent urgent msg %p len %x to queue_head @ %p", msg, msglen, queue->queue_head);
	queue->msg_count++;

	queue->send_type = URGNT;
}

/*****************************************************************************
** send_msg_to - sends the specified message to the tail of the specified queue
*****************************************************************************/
static void send_msg_to(v2pt_mqueue_t * queue, char *msg, uint msglen)
{
	uint i;
	char *element;
	TRACEF();
	/*
	 **  It is assumed when we enter this function that the queue has space
	 **  to accept the message about to be sent.  Start by sending the
	 **  message.
	 */
	if (msg != (char *) NULL) {
		element = (char *) &((queue->queue_tail)->msgbuf);
		for (i = 0; i < msglen; i++) {
			*(element + i) = *(msg + i);
		}
	}
	queue->queue_tail->msglen = msglen;

	TRACEF("%x len %x to queue_tail @ %p", msg, msglen, queue->queue_tail);

	/*
	 **  Now increment the queue_tail (send) pointer, adjusting for
	 **  possible wrap to the beginning of the queue.
	 */
	element = (char *) queue->queue_tail;
	element += queue->vmsg_len;
	queue->queue_tail = (q_msg_t *) element;

	if (queue->queue_tail > queue->last_msg_in_queue) {
		/*
		 **  Wrap the queue_tail pointer to the first message address
		 **  in the queue.
		 */
		queue->queue_tail = queue->first_msg_in_queue;
	}
	TRACEF(" new queue_tail @ %p", queue->queue_tail);

	queue->msg_count++;
	queue->send_type = SEND;
	pthread_cond_broadcast(&(queue->queue_send));
}

/*****************************************************************************
** notify_if_delete_complete - indicates if all tasks waiting on specified
**                             queue have successfully been awakened. 
*****************************************************************************/
static void notify_if_delete_complete(v2pt_mqueue_t * queue)
{
	/*
	 **  All tasks pending on the specified queue are being awakened...
	 **  If the calling task was the last task pending on the queue,
	 **  signal the deletion-complete condition variable.
	 */
	if (( ! queue->first_susp ) && ( ! queue->first_write_susp )) {
		// Lock mutex for queue delete completion
		pthread_mutex_clean_lock(&(queue->qdlet_lock));

		// Signal the deletion-complete condition variable for the queue
		pthread_cond_broadcast(&queue->qdlet_cmplt);

		// Unlock the queue delete completion mutex. 
		pthread_mutex_unlock(&queue->qdlet_lock);
		pthread_cleanup_pop(0);
	}
}


/*****************************************************************************
** fetch_msg_from - fetches the next message from the specified queue
*****************************************************************************/
static uint fetch_msg_from(v2pt_mqueue_t * queue, char *msg)
{
	char *element;
	uint i;
	uint msglen;

	/*
	 **  It is assumed when we enter this function that the queue contains
	 **  one or more messages to be fetched.
	 **  Fetch the message from the queue_head message location.
	 */
	if (msg != (char *) NULL) {
		element = (char *) &((queue->queue_head)->msgbuf);
		msglen = (queue->queue_head)->msglen;
		for (i = 0; i < msglen; i++) {
			*(msg + i) = *(element + i);
		}
	} else
		msglen = 0;

	TRACEF("fetched msg of len %x from queue_head @ %p", msglen, queue->queue_head);

	/*
	 **  Clear the message from the queue
	 */
	element = (char *) &((queue->queue_head)->msgbuf);
	*element = (char) NULL;
	(queue->queue_head)->msglen = 0;

	/*
	 **  Now increment the queue_head (send) pointer, adjusting for
	 **  possible wrap to the beginning of the queue.
	 */
	element = (char *) queue->queue_head;
	element += queue->vmsg_len;
	queue->queue_head = (q_msg_t *) element;

	if (queue->queue_head > queue->last_msg_in_queue) {
		/*
		 **  New queue_head pointer overflowed end of queue...
		 **  Wrap the queue_head pointer to the first message address
		 **  in the queue.
		 */
		queue->queue_head = queue->first_msg_in_queue;
	}
	TRACEF(" new queue_head @ %p", queue->queue_head);

	queue->msg_count--;

	/*
	 **  Now see if adequate space was freed in the queue and alert any tasks
	 **  waiting for message space if adequate space now exists.
	 */
	if (queue->first_write_susp != (task_t *) NULL) {
		if (queue->msg_count <= (queue->msgs_per_queue - 1)) {

			TRACEF("\r\nqueue @ %p freed msg space for queue list @ %p",
				   queue, &(queue->first_write_susp));
			/*
			 **  Lock mutex for queue space
			 */
			pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock,
								 (void *) &(queue->qfull_lock));
			pthread_mutex_lock(&(queue->qfull_lock));

			/*
			 **  Alert the waiting tasks that message space is available.
			 */
			pthread_cond_broadcast(&(queue->queue_space));

			/*
			 **  Unlock the queue space mutex. 
			 */
			pthread_cleanup_pop(1);
		}
	}
	return (msglen);
}

/*****************************************************************************
** data_extent_for - allocates space for queue data.  Data is allocated in
**                  a block large enough to hold (max_msgs + 1) messages.
*****************************************************************************/
static q_msg_t *data_extent_for(v2pt_mqueue_t * queue)
{
	char *new_extent;
	char *last_msg;
	size_t alloc_size;

	/*
	 **  Calculate the number of bytes of memory needed for this extent.
	 **  Start by calculating the size of each element of the extent array.
	 **  Each (q_msg_t) element will contain an unsigned int byte length followed
	 **  by a character array of queue->msg_len bytes.  First get the size
	 **  of the q_msg_t 'header' excluding the start of the data array.
	 **  Then add the size of the maximum-length message data.
	 */
	queue->vmsg_len = sizeof(q_msg_t) - sizeof(char *);
	queue->vmsg_len += (sizeof(char) * queue->msg_len);

	/*
	 **  The size of each array element is now known...
	 **  Multiply it by the number of elements to get allocation size.
	 */
	alloc_size = queue->vmsg_len * (queue->msgs_per_queue + 1);

	/*
	 **  Now allocate a block of memory to contain the extent.
	 */
	if ((new_extent = (char *) ts_malloc(alloc_size)) != (char *) NULL) {
		/*
		 **  Clear the memory block.  Note that this creates a NULL pointer
		 **  for the nxt_extent link as well as zeroing the message array.
		 */
		bzero((void *) new_extent, (int) alloc_size);

		/*
		 **  Link new data extent into the queue control block
		 */
		last_msg = new_extent + (queue->vmsg_len * queue->msgs_per_queue);
		queue->first_msg_in_queue = (q_msg_t *) new_extent;
		queue->last_msg_in_queue = (q_msg_t *) last_msg;
	}
	TRACEF("new extent @ %p for queue @ %p vmsg_len %x", new_extent, queue, queue->vmsg_len);
	return ((q_msg_t *) new_extent);
}

/*****************************************************************************
** msgQCreate - creates a v2pthread message queue
*****************************************************************************/
MSG_Q_ID msgQCreate(int max_msgs, int msglen, int opt)
{
	v2pt_mqueue_t *queue;
	STATUS error;
	TRACEF("%i %i %x",max_msgs,msglen,opt);
	error = OK;
	queue = (v2pt_mqueue_t *) ts_malloc(sizeof(v2pt_mqueue_t));
	if (!queue) {

		error = S_memLib_NOT_ENOUGH_MEMORY;
		goto exit;
	}
	memset(queue,0,sizeof(*queue));
	queue->msgs_per_queue = max_msgs;

	queue->msg_len = msglen;
	queue->mq = -1;

	if ( data_extent_for(queue) ) {

		/*
		 ** Mutex and Condition variable for queue send/pend
		 */
		pthread_mutex_init(&(queue->queue_lock), (pthread_mutexattr_t *) NULL);
		pthread_cond_init(&(queue->queue_send), (pthread_condattr_t *) NULL);

		/*
		 ** Mutex and Condition variable for queue delete
		 */
		pthread_mutex_init(&(queue->qdlet_lock), (pthread_mutexattr_t *) NULL);
		pthread_cond_init(&(queue->qdlet_cmplt), (pthread_condattr_t *) NULL);

		/*
		 ** Mutex and Condition variable for queue-full pend
		 */
		pthread_mutex_init(&(queue->qfull_lock), (pthread_mutexattr_t *) NULL);
		pthread_cond_init(&(queue->queue_space), (pthread_condattr_t *) NULL);

		/*
		 ** Pointer to next message pointer to be fetched from queue
		 */
		queue->queue_head = queue->first_msg_in_queue;

		/*
		 ** Pointer to last message pointer sent to queue
		 */
		queue->queue_tail = queue->first_msg_in_queue;

		/*
		 ** Type of send operation last performed on queue
		 */
		queue->send_type = SEND;

		queue->first_susp = NULL;

		/*
		 ** First task control block in list of tasks waiting for space to
		 ** post messages to queue
		 */
		queue->first_write_susp = (task_t *) NULL;

		/*
		 ** Total number of messages currently sent to queue
		 */
		queue->msg_count = 0;

		/*
		 ** Task pend order (FIFO or Priority) for queue
		 */
		if (opt & MSG_Q_PRIORITY)
			queue->order = 1;
		else
			queue->order = 0;

        queue->eventTask = -1;

		/*
		 **  If no errors thus far, we have a new queue ready to link into
		 **  the queue list.
		 */
		if (error == OK) {
			link_qcb(queue);
		} else {
			/*
			 **  Oops!  Problem somewhere above.  Release control block
			 **  and data memory and return.
			 */
			ts_free((void *) queue->first_msg_in_queue);
			ts_free((void *) queue);
		}
	} else {
		/*
		 **  No memory for queue data... free queue control block & return
		 */
		ts_free((void *) queue);
		error = S_memLib_NOT_ENOUGH_MEMORY;
	}
exit:
	if (error != OK) {
		errno = (int) error;
		queue = (v2pt_mqueue_t *) NULL;
	}

	return (MSG_Q_ID)queue;
}

/*****************************************************************************
** waiting_on_q_space - returns a nonzero result unless a qualifying event
**                      occurs on the specified queue which should cause the
**                      pended task to be awakened.  The qualifying events
**                      are:
**                          (1) message space is freed in the queue and the 
**                              current task is selected to receive it
**                          (2) the queue is deleted
*****************************************************************************/
static int waiting_on_q_space(v2pt_mqueue_t * queue, struct timespec *timeout, int *retcode)
{
	int result;
	struct timeval now;
	ulong usec;
	TRACEF();
	if (queue->send_type & KILLD) {
		result = 0;
		*retcode = 0;
	} else {
		/*
		 **  Queue still in service... check for message space availability.
		 **  Initially assume no message space available for our task
		 */
		result = 1;

		/*
		 **  Multiple messages removed from the queue may be represented by
		 **  only a single signal to the condition variable, so continue
		 **  checking for a message slot for our task as long as more space
		 **  is available.  Also note that for a 'zero-length' queue, the
		 **  presence of a task waiting on the queue for our message will
		 **  allow our message to be posted to the queue.
		 */
		while ((queue->msg_count <= (queue->msgs_per_queue - 1)) ||
			   ((queue->msgs_per_queue == 0) && queue->first_susp )) {
			// Message slot available... see if it's for our task.
			if (signal_for_my_task(&queue->first_write_susp, queue->order)) {
				/*
				 **  Message slot was destined for our task... waiting is over.
				 */
				result = 0;
				*retcode = 0;
				break;
			} else {
				/*
				 **  Message slot isn't for our task... continue waiting.
				 **  Sleep awhile to allow other tasks ahead of ours in the
				 **  list of tasks waiting on the queue to get their
				 **  messages, bringing our task to the head of the list.
				 */
				pthread_mutex_unlock(&(queue->qfull_lock));
				taskDelay(1);
				pthread_mutex_lock(&(queue->qfull_lock));
			}

			/*
			 **  If a timeout was specified, make sure we respect it and
			 **  exit this loop if it expires.
			 */
			if (timeout != (struct timespec *) NULL) {
				gettimeofday(&now, (struct timezone *) NULL);
				if (timeout->tv_nsec > (now.tv_usec * 1000)) {
					usec = (timeout->tv_nsec - (now.tv_usec * 1000)) / 1000;
					if (timeout->tv_sec < now.tv_sec)
						usec = 0;
					else
						usec += ((timeout->tv_sec - now.tv_sec) * 1000000);
				} else {
					usec = ((timeout->tv_nsec + 1000000000) - (now.tv_usec * 1000)) / 1000;
					if ((timeout->tv_sec - 1) < now.tv_sec)
						usec = 0;
					else
						usec += (((timeout->tv_sec - 1) - now.tv_sec)
								 * 1000000);
				}
				if (usec == 0)
					break;
			}
		}
	}

	return result;
}

/*****************************************************************************
** waitToSend - sends the queue message if sufficient space becomes available
**              within the allotted waiting interval.
*****************************************************************************/
STATUS waitToSend(v2pt_mqueue_t * queue, char *msg, uint msglen, int wait, int pri)
{
	task_t *our_task;
	struct timeval now;
	struct timespec timeout;
	int retcode;
	long sec, usec;
	STATUS error = OK;
	TRACEF();

	if (wait != NO_WAIT) {
		//  Add task for task to list of tasks waiting on queue
		our_task = my_task();
		TRACEV("%x", queue->first_write_susp);

		link_susp_task(&queue->first_write_susp, our_task);

		retcode = 0;

		//  Unlock the queue mutex so other tasks can receive messages. 
		pthread_mutex_unlock(&queue->queue_lock);

		if (wait == WAIT_FOREVER) {
			while (waiting_on_q_space(queue, 0, &retcode)) {
				pthread_cond_wait(&queue->queue_space, &queue->qfull_lock);
			}
		} else {
			/*
			 **  Wait on queue message space with timeout...
			 **  Calculate timeout delay in seconds and microseconds.
			 */
			sec = 0;
			usec = wait * V2PT_TICK * 1000;
			gettimeofday(&now, (struct timezone *) NULL);
			usec += now.tv_usec;
			if (usec > 1000000) {
				sec = usec / 1000000;
				usec = usec % 1000000;
			}
			timeout.tv_sec = now.tv_sec + sec;
			timeout.tv_nsec = usec * 1000;

			/*
			 **  Wait for queue message space for the current task or for the
			 **  timeout to expire.  The loop is required since the task
			 **  may be awakened by signals for messages which are
			 **  not ours, or for signals other than from a message send.
			 */
			while ((waiting_on_q_space(queue, &timeout, &retcode)) && (retcode != ETIMEDOUT)) {
				retcode = pthread_cond_timedwait(&queue->queue_space,
						&queue->qfull_lock, &timeout);
			}
		}

		/*
		 **  Re-lock the queue mutex before manipulating its control block. 
		 */
		pthread_mutex_lock(&(queue->queue_lock));

		/*
		 **  Remove the calling task's task from the pended task list
		 **  for the queue.  Clear our TCB's suspend list pointer in
		 **  case the queue was killed & its ctrl blk deallocated.
		 */
		unlink_susp_task(&(queue->first_write_susp), our_task);
		//our_task->suspend_list = NULL;

		/*
		 **  See if we were awakened due to a msgQDelete on the queue.
		 */
		if (queue->send_type & KILLD) {
			notify_if_delete_complete(queue);
			error = S_objLib_OBJ_DELETED;
			TRACEF("...queue deleted");
		} else {
			/*
			 **  See if we timed out or if we got a message slot
			 */
			if (retcode == ETIMEDOUT) {
				/*
				 **  Timed out without obtaining a message slot
				 */
				error = S_objLib_OBJ_TIMEOUT;
				TRACEF("...timed out");
			} else {
				/*
				 **  A message slot was freed on the queue for this task...
				 */
				TRACEF("...rcvd queue msg space");

				if (pri == MSG_PRI_URGENT) {
					/*
					 **  Stuff the new message onto the front of the queue.
					 */
					urgent_msg_to(queue, msg, msglen);

					/*
					 **  Signal the condition variable for the queue
					 */
					pthread_cond_broadcast(&(queue->queue_send));
				} else
					/*
					 **  Send the new message to the back of the queue.
					 */
					send_msg_to(queue, msg, msglen);
			}
		}
	} else {
		/*
		 **  Queue is full and no waiting allowed... return QUEUE FULL error
		 */
#if 0 /* uncomment for debug */
		our_task = my_task();
		printf("waitToSend:\n\r\t");
		printf("task %s, msg_count %d, msg_per_queue %d\n\r",
				our_task->taskname, queue->msg_count, queue->msgs_per_queue);
#endif
		error = S_objLib_OBJ_UNAVAILABLE;
		TRACEF("WARNING: queue is full");
	}
	return (error);
}

/*****************************************************************************
** msgQSend - posts a message to the tail of a v2pthread queue and awakens the
**            first selected task pended on the queue.
*****************************************************************************/
STATUS msgQSend(v2pt_mqueue_t * queue, char *msg, uint msglen, int wait, int pri)
{
	STATUS error = OK;
    int evTask = -1;
    unsigned long evEvents = 0;


	if (queue->mq < 1) {

		TRACEF("%x %x %x",my_task(),queue,queue->first_susp);
	
		pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock, &queue->queue_lock);
		if (!mqueue_find_lock(queue)) {
			error = S_objLib_OBJ_ID_ERROR;
			goto exit;
		}
		if (msglen > queue->msg_len) {
			error = S_msgQLib_INVALID_MSG_LENGTH;
			goto unlock;
		} 
	
		if (queue->msg_count > queue->msgs_per_queue) { // BUG > ?
			//  Queue is full
			error = waitToSend(queue, msg, msglen, wait, pri);
		} else {
			if (queue->msg_count == queue->msgs_per_queue) {
				// Full
				if ((queue->msgs_per_queue == 0) && queue->first_susp ) {
					//  Special case... Send the new message.
					send_msg_to(queue, msg, msglen);
				} else {
					if (pri == MSG_PRI_URGENT) {
						//  Stuff the new message onto the queue.
						urgent_msg_to(queue, msg, msglen);
						pthread_cond_broadcast(&(queue->queue_send));
					} else
						/*
						 **  Queue is full... if waiting on space is
						 **  allowed, wait until space becomes available
						 **  or the timeout expires.  If space becomes
						 **  available, send the caller's message.
						 */
						error = waitToSend(queue, msg, msglen, wait, pri);
				}
			} else {
				if (pri == MSG_PRI_URGENT) {
					//  Stuff the new message onto the front of the queue.
					urgent_msg_to(queue, msg, msglen);
	
					//  Signal the condition variable for the queue
					pthread_cond_broadcast(&(queue->queue_send));
				} else
					//  Send the new message to the back of the queue.
					send_msg_to(queue, msg, msglen);
			}
		}

        if ((error == OK) && queue->eventTask != -1) {
            evTask = queue->eventTask;
            evEvents = queue->eventEvents;
        }

	unlock:
		pthread_mutex_unlock(&queue->queue_lock);
		/* ensure context switch if the queue is full */
		if ((error == S_objLib_OBJ_UNAVAILABLE) && 
		    (queue->msg_count >= queue->msgs_per_queue))
			taskDelay(1);
	exit: 	{}
		pthread_cleanup_pop(0);
	
		if (error != OK) {
			errno = (int) error;
			error = ERROR;
		}
	} else {
		if ( msglen <= queue->msg_len) {
			// char *msg, uint msglen, int wait, int pri
			TRACEF("to mq_send()");
			if ( mq_send(queue->mq, msg, msglen, pri) ){
				error = errno;
				TRACEF("mq_send(%s) return error(%d): %s", 
					queue->qname, errno, strerror(errno));
			}
            else {
                if (queue->eventTask != -1) {
                   evTask = queue->eventTask;
                   evEvents = queue->eventEvents;
                }
            }
		}
		else {
			error = S_msgQLib_INVALID_MSG_LENGTH;
		}  
		
	}

    if (evTask != -1)
       eventSend(evTask, evEvents);

	return (error);
}

/*****************************************************************************
** delete_mqueue - takes care of destroying the specified queue and freeing
**                any resources allocated for that queue
*****************************************************************************/
static void delete_mqueue(v2pt_mqueue_t * queue)
{
	TRACEF();
	unlink_qcb(queue);
	ts_free(queue->first_msg_in_queue);
	ts_free(queue);
}

#define pthread_cond_broadcast(args) do { TRACEF("pthread_cond_broadcast %x",args);pthread_cond_broadcast(args); } while (0)

/*****************************************************************************
** msgQDelete - removes the specified queue from the queue list and frees
**              the memory allocated for the queue control block and extents.
*****************************************************************************/
STATUS msgQDelete(v2pt_mqueue_t * queue)
{
	STATUS error = OK;
	TRACEF();

	pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock, &queue->queue_lock);
	if ( ! mqueue_find_lock(queue)) {
		error = S_objLib_OBJ_ID_ERROR;
		goto exit;
	}
	queue->send_type = KILLD;

	taskLock();
	if ( queue->first_susp || queue->first_write_susp ) {
		pthread_mutex_clean_lock(&queue->qdlet_lock);

		TRACEF("%x %x", &queue->queue_send, &queue->queue_lock);
		// Signal the condition variable for tasks waiting on messages in the queue
		pthread_cond_broadcast(&queue->queue_send);

		// Unlock the queue send mutex. 
		pthread_mutex_unlock(&queue->queue_lock);

		// Lock mutex for queue space
		pthread_mutex_clean_lock(&queue->qfull_lock);

		// Signal the condition variable for tasks waiting on space to post messages into the queue
		pthread_cond_broadcast(&queue->queue_space);

		// Unlock the queue space mutex. 
		pthread_cleanup_pop(1); // queue->qfull_lock

		/*
		 **  Wait for all pended tasks to receive deletion signal.
		 **  The last task to receive the deletion signal will signal the
		 **  deletion-complete condition variable.
		 */
		// while ( queue->first_susp  && queue->first_write_susp ) { BUG
		while ( queue->first_susp  || queue->first_write_susp ) {
			pthread_cond_wait(&queue->qdlet_cmplt, &queue->qdlet_lock);
		}

		// Unlock the queue delete completion mutex. 
		pthread_cleanup_pop(1);
	} else {
		// Unlock the queue mutex. 
		pthread_mutex_unlock(&(queue->queue_lock));
	}

	/*
	 **  No other tasks are pending on the queue by this point...
	 **  Now physically delete the queue.
	 */
	delete_mqueue(queue);
	taskUnlock();
exit:
	{}
	pthread_cleanup_pop(0);

	if (error != OK) {
		errno = (int) error;
		error = ERROR;
	}

	return error;
}

extern BOOL time_expired(struct timespec *timeout);

/*****************************************************************************
** waiting_on_q_msg - returns a nonzero result unless a qualifying event
**                    occurs on the specified queue which should cause the
**                    pended task to be awakened.  The qualifying events
**                    are:
**                        (1) a message is sent to the queue and the current
**                            task is selected to receive it
**                        (2) the queue is deleted
*****************************************************************************/
static int waiting_on_q_msg(v2pt_mqueue_t * queue, struct timespec *timeout, int *retcode)
{
	int result;
//	struct timeval now;
//	ulong usec;
	TRACEF();

	if (queue->send_type & KILLD) {
		// Queue has been killed... waiting is over.
		TRACEF("KILLED");
		result = 0;
		*retcode = 0;
	} else {
		result = 1;

		/*
		 **  Multiple messages sent to the queue may be represented by only
		 **  a single signal to the condition variable, so continue
		 **  checking for a message for our task as long as more messages
		 **  are available.
		 */
		while (queue->msg_count > 0) {
			TRACEF("%i",queue->msg_count);
			// Message arrived... see if it's for our task.
			if (signal_for_my_task(&queue->first_susp, queue->order)) {
				/*
				 **  Message was  destined for our task... waiting is over.
				 */
				result = 0;
				*retcode = 0;
				break;
			} else {
				/*
				 **  Message isn't for our task... continue waiting.
				 **  Sleep awhile to allow other tasks ahead of ours in the
				 **  list of tasks waiting on the queue to get their
				 **  messages, bringing our task to the head of the list.
				 */
				pthread_mutex_unlock(&queue->queue_lock);
				taskDelay(1);
				pthread_mutex_lock(&queue->queue_lock);
			}

			/*
			 **  If a timeout was specified, make sure we respect it and
			 **  exit this loop if it expires.
			 */

    		if ( timeout && time_expired (timeout))
            {
                *retcode = ETIMEDOUT;
    			break;
            }
            /*
			if (timeout != (struct timespec *) NULL) {
				gettimeofday(&now, (struct timezone *) NULL);
				if (timeout->tv_nsec > (now.tv_usec * 1000)) {
					usec = (timeout->tv_nsec - (now.tv_usec * 1000)) / 1000;
					if (timeout->tv_sec < now.tv_sec)
						usec = 0;
					else
						usec += ((timeout->tv_sec - now.tv_sec) * 1000000);
				} else {
					usec = ((timeout->tv_nsec + 1000000000) - (now.tv_usec * 1000)) / 1000;
					if ((timeout->tv_sec - 1) < now.tv_sec)
						usec = 0;
					else
						usec += (((timeout->tv_sec - 1) - now.tv_sec)
								 * 1000000);
				}
				if (usec == 0)
					break;
            } 
            */ 
		}
	}

	return result;
}

/*****************************************************************************
** msgQReceive - blocks the calling task until a message is available in the
**               specified v2pthread queue.
*****************************************************************************/
int msgQReceive(v2pt_mqueue_t * queue, char *msgbuf, uint buflen, int max_wait)
{
	task_t *our_task;
	struct timeval now;
	struct timespec timeout;
	int retcode;
	int msglen = ERROR;
	long sec, usec;
	STATUS error  = OK;

	TRACEF("queue = 0x%x, mq = 0x%x", queue, queue->mq);

	if ( queue->mq < 1) {

		pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock, &queue->queue_lock);
		if (!mqueue_find_lock(queue)) 
		{
			TRACEF("S_objLib_OBJ_ID_ERROR");
			error = S_objLib_OBJ_ID_ERROR;	/* Invalid queue specified */
			goto exit;
		}
	
		if (buflen < queue->msg_len) {
			TRACEF("S_msgQLib_INVALID_MSG_LENGTH %i %i ", buflen, queue->msg_len);
			error = S_msgQLib_INVALID_MSG_LENGTH;
			goto unlock;
		} 
		// Add task for task to list of tasks waiting on queue
		our_task = my_task();
		TRACEF("%x %x", our_task, queue);
		TRACEF("wait on queue list @ %p", our_task);
	
		link_susp_task(&queue->first_susp, our_task);
		//  If tasks waiting to write to a zero-length queue, notify
		//  waiting task that we're ready to receive a message.
		if ( !queue->msgs_per_queue && queue->first_write_susp ) {
			pthread_mutex_clean_lock(&queue->qfull_lock);
			TRACEF();
			//  Alert the waiting tasks that message space is available.
			pthread_cond_broadcast(&queue->queue_space);
			pthread_cleanup_pop(1);
		}
	
		retcode = 0;
	
		if (max_wait == NO_WAIT) {
			/*
			 **  Caller specified no wait on queue message...
			 **  Check the condition variable with an immediate timeout.
			 */
			gettimeofday(&now, NULL);
			timeout.tv_sec = now.tv_sec;
			timeout.tv_nsec = now.tv_usec * 1000;
			while ( (waiting_on_q_msg(queue, &timeout, &retcode)) && (retcode != ETIMEDOUT))
			{
				if ( (retcode = pthread_cond_timedwait(&queue->queue_send,
						&queue->queue_lock, &timeout)) != 0 )
					break;
			} 
		} else if (max_wait == WAIT_FOREVER) {
			//  Infinite wait was specified... wait without timeout.
			while (waiting_on_q_msg(queue, 0, &retcode))
			{
				if ( pthread_cond_wait(&queue->queue_send, &queue->queue_lock) != 0 )
					break;
			} 
		} else {
			/*
			 **  Wait on queue message arrival with timeout...
			 **  Calculate timeout delay in seconds and microseconds.
			 */
			sec = 0;
			usec = max_wait * V2PT_TICK * 1000;
			gettimeofday(&now, (struct timezone *) NULL);
			usec += now.tv_usec;
			if (usec > 1000000) {
				sec = usec / 1000000;
				usec = usec % 1000000;
			}
			timeout.tv_sec = now.tv_sec + sec;
			timeout.tv_nsec = usec * 1000;
	
			/*
			 **  Wait for a queue message for the current task or for the
			 **  timeout to expire.  The loop is required since the task
			 **  may be awakened by signals for messages which are
			 **  not ours, or for signals other than from a message send.
			 */
			while ( (waiting_on_q_msg(queue, &timeout, &retcode)) && (retcode != ETIMEDOUT) ) 
			{
				TRACEF("pthread_cond_timedwait { %x %x %i", &queue->queue_send, &queue->queue_lock, max_wait);
				retcode = pthread_cond_timedwait(&queue->queue_send, &queue->queue_lock, &timeout);
				TRACEF("pthread_cond_timedwait }");
				if ( retcode != 0 )
					break;
			} 
		}
	
		/*
		 **  Remove the calling task's task from the waiting task list
		 **  for the queue.  Clear our TCB's suspend list pointer in
		 **  case the queue was killed & its ctrl blk deallocated.
		 */
		unlink_susp_task(&(queue->first_susp), our_task);
		//our_task->suspend_list = NULL;
	
		/*
		 **  See if we were awakened due to a msgQDelete on the queue.
		 */
		if (queue->send_type & KILLD) {
			notify_if_delete_complete(queue);
			error = S_objLib_OBJ_DELETED;
			TRACEF("...queue deleted");
		} else if (retcode == ETIMEDOUT) {
			/*
			 **  Timed out without a message
			 */
			if (max_wait == NO_WAIT)
				error = S_objLib_OBJ_UNAVAILABLE;
			else
				error = S_objLib_OBJ_TIMEOUT;
			TRACEF("...timed out");
		} else {
			/*
			 **  A message was sent to the queue for this task...
			 **  Retrieve the message and clear the queue contents.
			 */
			msglen = (int) fetch_msg_from(queue, (char *) msgbuf);
			TRACEF("...rcvd queue msg @ %p", msgbuf);
		}
	
		/*
		 **  Unlock the mutex for the condition variable.
		 */
	unlock:
		pthread_mutex_unlock(&queue->queue_lock);
	exit: {}
		/*
		 **  Clean up the opening pthread_cleanup_push()
		 */
		pthread_cleanup_pop(0);
	
		if (error != OK) {
			errno = error;
			msglen = ERROR;
		}
/* --- New option:  interprocess msg queue --- */
	} else {
		TRACEF("buflen = %d, max_wait = %d",buflen, max_wait);
		if (buflen >= queue->msg_len) {

			if (max_wait == WAIT_FOREVER) {
				if ( 0 > (msglen = mq_receive(queue->mq, msgbuf, buflen, NULL)) ){
					TRACEF("mq_receive(%s): mq 0x%x, msglen %d;  returns error(%d): %s", 
						queue->qname, queue->mq, msglen, errno, strerror(errno));
					errno = S_objLib_OBJ_UNAVAILABLE;
					msglen = ERROR;
				}
			} else {
			
				if (max_wait == NO_WAIT){
					// return immediatly 
					timeout.tv_sec = 0;
					timeout.tv_nsec = 1;
				} else {
					/*
					 **  Wait on queue message arrival with timeout...
					 **  Calculate timeout delay in seconds and microseconds.
					 */
					sec = 0;
					usec = max_wait * V2PT_TICK * 1000;
					gettimeofday(&now, (struct timezone *) NULL);
					usec += now.tv_usec;
					if (usec > 1000000) {
						sec = usec / 1000000;
						usec = usec % 1000000;
					}
					timeout.tv_sec = now.tv_sec + sec;
					timeout.tv_nsec = usec * 1000;
				}
				if ( 0 > (msglen = mq_timedreceive( queue->mq, msgbuf, buflen, NULL /* msg priorety */, &timeout)) ){
					TRACEF("msg queue(%s) %d returns error(%d): %s", 
						queue->qname, queue->mq, errno, strerror(errno));
					msglen = ERROR;
				}
			}
		} else {
			TRACEF("S_msgQLib_INVALID_MSG_LENGTH %i %i ", buflen, queue->msg_len);
			errno = S_msgQLib_INVALID_MSG_LENGTH;
			msglen = ERROR;
		}
	}
	TRACEF("go out msglen = %d", msglen);
	return msglen;
}

/*****************************************************************************
** msgQNumMsgs - returns the number of messages currently posted to the
**               specified queue.
*****************************************************************************/
int msgQNumMsgs(v2pt_mqueue_t * queue)
{
	int num_msgs;

	if ( queue->mq < 1) {
		// copuld be just return queue->msg_count
		pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock, (void *) &(queue->queue_lock));
		if (mqueue_find_lock(queue)) {
			num_msgs = queue->msg_count;
			pthread_mutex_unlock(&(queue->queue_lock));
		} else {
			num_msgs = (int) ERROR;
		}
	
		pthread_cleanup_pop(0);
	} else {
		struct mq_attr attr;
		return ( mq_getattr( queue->mq, &attr) )? ERROR : attr.mq_curmsgs;
	}

	return (num_msgs);
}
 /************************************************************************************
 * Function name: 
 *      MSG_Q_ID msgQOpen(const char *name, int max_msgs, int msglen, int opt, 
 *                       int mode, void *context)
 *
 * Functionality: 
 *      The mq_open() function shall establish the connection between a process 
 *    and a message queue with a message queue descriptor. It shall create an open 
 *    message queue description that refers to the message queue, and a message 
 *    queue descriptor that refers to that open message queue description. The message 
 *    queue descriptor is used by other functions to refer to that message queue.
 * ------------------------------------------------------------------------------
 *		/!| IMPORTANT:
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
 * -------------------------------------------------------------------------------
 * Parameters:
 *      [IN] const char* name - message queue name
 *      [IN] int max_msgs - max messages that can be queued
 *      [IN] int msglen - max bytes in a message
 *      [IN] int opt - message queue options
 *      [IN] int mode - creation mode
 *      [IN] void* context - context value
 *
 * Return value: 
 *      MSG_Q_ID - message Id or NULL
 *
 ************************************************************************************/

MSG_Q_ID msgQOpen ( const char * name, int max_msgs, int msglen, 
					int opt, int mode, void * context )
{
    struct mq_attr attr;
	mode_t permission_mode = 0777;
	v2pt_mqueue_t *queue = NULL;
	STATUS error = OK;
	char full_name[MSGQ_PATH_LEN];

	
	//msglen /= max_msgs;
	//TRACEF("new msg len = %d", msglen);
	if ( name == NULL ) {
		TRACEF("The file name undefined");
		error = S_objLib_OBJ_INVALID_ARGUMENT;
		goto exit;
	} else {
		snprintf(full_name, sizeof(full_name), "%s%s", MSGQ_PATH_TO, name );
		//snprintf(full_name, sizeof(full_name), "%s",name );
	}
	TRACEF("name %s, size: msg(%d) x len(%d); opt - %x", 
				name, max_msgs, msglen, opt);

	if( max_msgs < 1 ) {
		TRACEF("Invalid max msgs %d", max_msgs);
		error = S_msgQLib_INVALID_MSG_COUNT;
		goto exit;
	}
	if( msglen < 0 ) {
		TRACEF("Invalid msg len %d", msglen);
		error = S_msgQLib_INVALID_MSG_LENGTH;
		goto exit;
	}
	if( !(mode & (O_CREAT | O_EXCL | OM_DELETE_ON_LAST_CLOSE))) {
		TRACEF("The creation mode - undefined");
		error = S_objLib_OBJ_INVALID_ARGUMENT;
		goto exit;
	} else
		mode |= O_RDWR;

	queue = (v2pt_mqueue_t *) malloc(sizeof(v2pt_mqueue_t));
	if (!queue) {
		TRACEF("Can't create a queue");
		error = S_memLib_NOT_ENOUGH_MEMORY;
		goto exit;
	}
	memset(queue, 0, sizeof(*queue));
	
	attr.mq_flags = 0;
    queue->msgs_per_queue = attr.mq_maxmsg = max_msgs;	// 10; //
    queue->msg_len = attr.mq_msgsize = msglen;	//1024;//
    attr.mq_curmsgs = 0;
    // if queue->mq more than zero we knows that is a interprocess pipe otherwise alocal
	// Create and open BLOCKED MSG_Q
    if (mode & (O_CREAT | OM_DELETE_ON_LAST_CLOSE)) {
    	mode &= ~((int)(OM_DELETE_ON_LAST_CLOSE));
    }
	if ( 0 > (queue->mq = mq_open( full_name, mode, permission_mode, &attr)) ){
		printf("Can't open Queue %s, return error(%d): %s\n", full_name, errno, strerror(errno));
		free(queue);
		return (MSG_Q_ID) (queue = (v2pt_mqueue_t *) NULL);
	}

	TRACEF("Queue %s id=%d, Attrib: mq_flags=0x%x, mq_maxmsg=%d, mq_msgsize=%d, mq_curmsgs=%d, mode=0x%x", 
		   full_name, queue->mq, attr.mq_flags, attr.mq_maxmsg,  attr.mq_msgsize, attr.mq_curmsgs, mode);

	queue->mode = mode;
	snprintf(queue->qname, sizeof(queue->qname), "%s", name);
	queue->eventTask = -1;

exit:
	if (error != OK) {
		errno = (int) error;
		queue = (v2pt_mqueue_t *) NULL;
	}

	return (MSG_Q_ID)queue;
}
 /************************************************************************************
 * Function name: 
 *      STATUS msgQUnlink(const char *name)
 *
 * Functionality: 
 *      This routine removes a message queue from the name space, and marks it as ready for
 * 	deletion on the last msgQClose( ). In the case where there is no outstanding msgQOpen( )
 * 	call, the message queue is deleted immediately.
 * 	After a message queue is unlinked, subsequent calls to msgQOpen( ) using name will not be
 * 	able to find the message queue, even if it has not been deleted yet. Instead, a new message
 * 	queue could be created if msgQOpen( ) is called with the OM_CREATE flag.
 *  This routine is not ISR callable.
 * Parameters:
 *      [IN] const char* name - name of message queue to unlink
 *
 * Return value: 
 *      STATUS - OK, or ERROR if unsuccessful
 *
 ************************************************************************************/

STATUS msgQUnlink ( const char * name ){

	char full_name[MSGQ_PATH_LEN];

	if ( name == NULL ) {
		TRACEF("The file name undefined");
		return S_objLib_OBJ_INVALID_ARGUMENT;
	}
	snprintf(full_name, sizeof(full_name), "%s%s", MSGQ_PATH_TO, name );
	

	return ( mq_unlink(full_name) )? ERROR : OK;
}

 /************************************************************************************
 * Function name: 
 *      STATUS msgQClose(MSG_Q_ID msgQId)
 *
 * Functionality: 
 *      This routine closes a named message queue and decrements its reference counter. In the
 *	case where the counter becomes zero, the message queue is deleted if:
 *	- It has been already removed from the name space by a call to msgQUnlink( ).
 *	- It was created with the OM_DESTROY_ON_LAST_CALL option.
 *	This routine is not ISR callable.
 * Parameters:
 *      [IN] MSG_Q_ID msgQId - message queue ID to close
 *
 * Return value: 
 *      STATUS - OK, or ERROR if unsuccessful.
 *
 ************************************************************************************/

STATUS msgQClose ( v2pt_mqueue_t * queue ) {
	STATUS error = OK;
	
	if ( queue == (v2pt_mqueue_t *)NULL ) return error;

	if ( mq_close(queue->mq) ){
		TRACEF("Error on close %s queue: %s", queue->qname, strerror(errno));
		error = S_objLib_OBJ_ID_ERROR;
	}
	if ( queue->mode & OM_DELETE_ON_LAST_CLOSE) {
		if ( OK != msgQUnlink ( queue->qname ) )
			return error;
	}

	free(queue);
	queue = (v2pt_mqueue_t *)NULL;

	return error;
}


STATUS msgQEvStart (MSG_Q_ID msgQId, unsigned int events, unsigned char options)
{
  v2pt_mqueue_t *queue = (v2pt_mqueue_t *)msgQId;
  task_t *ownTask = my_task();
  STATUS error = OK;
	
  if ( queue == (v2pt_mqueue_t *)NULL ) {
      error = S_objLib_OBJ_ID_ERROR;
      errno = error;  
      return (ERROR);  
  } 
  
  if (events == 0) {
      error = S_eventLib_ZERO_EVENTS;
      errno = error;  
      return (ERROR);  
  }
  
  if (queue->mq < 1) {
    pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock, &queue->queue_lock); // sas close
	if (TRUE != mqueue_find_lock(queue)) {
        error = S_objLib_OBJ_ID_ERROR;
		goto exit;
	}

    /* 
    * Refuse to register if another task has already done so, but only if 
    * the EVENTS_ALLOW_OVERWRITE option is not in use AND if the task trying 
    * to do the registration is not the one currently registered. 
    */  
    if ((options & EVENTS_ALLOW_OVERWRITE) != EVENTS_ALLOW_OVERWRITE)  
    {  
        if ((queue->eventTask != -1) && (queue->eventTask != ownTask->taskid))
          error = S_eventLib_ALREADY_REGISTERED;
    }  

    if (error == OK) {
      queue->eventTask = ownTask->taskid;
      queue->eventEvents = events;
      queue->eventOptions = options; 
    }
	
	pthread_mutex_unlock(&queue->queue_lock);
exit: {}   
	pthread_cleanup_pop(0);
  }
  else {
    /* 
    * Refuse to register if another task has already done so, but only if 
    * the EVENTS_ALLOW_OVERWRITE option is not in use AND if the task trying 
    * to do the registration is not the one currently registered. 
    */  
    if ((options & EVENTS_ALLOW_OVERWRITE) != EVENTS_ALLOW_OVERWRITE) {
        if ((queue->eventTask != -1) && (queue->eventTask != ownTask->taskid))
          error = S_eventLib_ALREADY_REGISTERED;
    }  

    if (error == OK) {
      queue->eventTask = ownTask->taskid;
      queue->eventEvents = events;
      queue->eventOptions = options; 
    }
  }
  
  if ((error == OK) &&
     ((options & EVENTS_SEND_IF_FREE) == EVENTS_SEND_IF_FREE)) {
    if ( msgQNumMsgs(queue) > 0 )
      if ( eventSend(0, events) != OK )
        error = S_eventLib_EVENTSEND_FAILED;
  }
    
  if (error != OK) {
     errno = (int) error;
	 error = ERROR;
  }
  
  return error;
}

STATUS msgQEvStop  (MSG_Q_ID msgQId)
{
  v2pt_mqueue_t *queue = (v2pt_mqueue_t *)msgQId;
  task_t *ownTask = my_task();
  STATUS error = OK;
	
  if ( queue == (v2pt_mqueue_t *)NULL ) {
      error = S_objLib_OBJ_ID_ERROR;
      errno = error;  
      return (ERROR);  
  } 

  if (queue->mq < 1) {
	pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock, &queue->queue_lock);
	if (!mqueue_find_lock(queue)) {
       error = S_objLib_OBJ_ID_ERROR;
       goto exit;
    }

    if (ownTask->taskid != queue->eventTask)
      error = S_eventLib_TASK_NOT_REGISTERED;
    else
      queue->eventTask = -1;
	
    pthread_mutex_unlock(&queue->queue_lock);
exit: {}   
    pthread_cleanup_pop(0);
  }
  else {
    if (ownTask->taskid != queue->eventTask)
      error = S_eventLib_TASK_NOT_REGISTERED;
    else
      queue->eventTask = -1;
  }
  
  if (error != OK) {
     errno = (int) error;
	 error = ERROR;
  }

  return error;
}
