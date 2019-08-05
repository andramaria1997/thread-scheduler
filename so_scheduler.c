#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#include "scheduler.h"

static void reschedule(so_thread_t *thread)
{
	so_thread_t *previous_thread = NULL;

	if (thread == NULL)
		return;

	pthread_mutex_lock(&(so_scheduler.lock));

	/* update pointer to running thread */
	previous_thread = so_scheduler.running_thread;
	so_scheduler.running_thread = thread;

	/* change thread's state and signal it's execution */
	thread->state = RUN;
	pthread_cond_signal(&(thread->turn));

	if (previous_thread == NULL) {
		pthread_mutex_unlock(&(so_scheduler.lock));
		return;
	}

	/* reset previous's thread time quantum */
	previous_thread->time_quantum = so_scheduler.time_quantum;

	/* if previous thread has not finished it's execution,
	 * wait for being rescheduled
	 */
	if (previous_thread->state != TERM &&
			previous_thread->tid != thread->tid) {
		previous_thread->state = RDY;
		pthread_cond_wait(&(previous_thread->turn),
					&(so_scheduler.lock));
	}

	pthread_mutex_unlock(&(so_scheduler.lock));
}

static void *start_thread(void *thread_info)
{
	so_thread_t *this_thread = (so_thread_t *)thread_info;
	so_thread_t *next_thread = NULL, *child = NULL;
	int i, rc;

	/* if this thread is not scheduled, wait for rescheduling */
	pthread_mutex_lock(&(so_scheduler.lock));
	if (so_scheduler.running_thread != NULL &&
			this_thread->tid != so_scheduler.running_thread->tid) {
		pthread_cond_wait(&(this_thread->turn), &(so_scheduler.lock));
	}
	pthread_mutex_unlock(&(so_scheduler.lock));

	/* call handler */
	(*(this_thread->handler))(this_thread->prio);

	/* finish execution */
	this_thread->state = TERM;
	delete_ready(&(so_scheduler.ready_threads), this_thread);

	/* reschedule another thread */
	next_thread = get_next_ready(&(so_scheduler.ready_threads));
	reschedule(next_thread);

	/* wait for own children to finish execution */
	if (this_thread->children.vector != NULL) {
		for (i = 0 ; i < this_thread->children.last_pos ; i++) {
			child = this_thread->children.vector[i];
			rc = pthread_join(child->tid, NULL);
			DIE(rc < 0, "Join failed!\n");
			rc = pthread_cond_destroy(&(child->turn));
			DIE(rc < 0, "Pthread condition destroy failed!\n");
			free(child);
		}
		free(this_thread->children.vector);
	}

	return thread_info;
}

int so_init(unsigned int time_quantum, unsigned int io)
{
	int rc;

	/* check if parameters are valid */
	if (time_quantum < 1 || io > SO_MAX_NUM_EVENTS || so_scheduler.init)
		return ERROR;

	/* mark scheduler as initalized, set time quantum and
	 * io devices number
	 */
	so_scheduler.init = true;
	so_scheduler.first_thread = NULL;
	so_scheduler.time_quantum = time_quantum;
	so_scheduler.io_devices_num = io;
	so_scheduler.running_thread = NULL;
	rc = pthread_mutex_init(&(so_scheduler.lock), NULL);
	DIE(rc < 0, "Mutex init failed!\n");

	/* initalize ready threads pool and waiting threads pool */
	ready_pool_init(&(so_scheduler.ready_threads));
	waiting_pool_init(&(so_scheduler.waiting_threads));

	return 0;
}

tid_t so_fork(so_handler *func, unsigned int priority)
{
	int rc = 0;
	pthread_t new_tid;
	so_thread_t *new_thread = NULL, *next_thread = NULL;

	/* check if parameters are valid */
	if (func == NULL || priority > SO_MAX_PRIO)
		return INVALID_TID;

	/* create thread */
	new_thread = create_thread(so_scheduler.time_quantum, priority, func);
	rc = pthread_create(&new_tid, NULL, start_thread, (void *)new_thread);
	DIE(rc < 0, "Thread create failed!\n");
	new_thread->tid = new_tid;

	if (so_scheduler.first_thread == NULL)
		so_scheduler.first_thread = new_thread;

	/* insert thread into ready pool */
	insert_ready(&(so_scheduler.ready_threads), new_thread);

	/* if forked thread is not the first thread, mark running thread
	 * as parent and decrease time quantum
	 */
	if (so_scheduler.running_thread != NULL) {
		add_child(so_scheduler.running_thread, new_thread);
		so_scheduler.running_thread->time_quantum--;
	}

	/* reschedule, if necessary */
	if (so_scheduler.running_thread == NULL ||
			priority > so_scheduler.running_thread->prio) {
		reschedule(new_thread);
	}

	if (so_scheduler.running_thread->time_quantum == 0) {
		next_thread = get_next_ready(&(so_scheduler.ready_threads));
		reschedule(next_thread);
	}

	/* return new thread's tid */
	return new_tid;
}

int so_wait(unsigned int io)
{
	so_thread_t *next_thread = NULL;

	/* check if parameters are valid */
	if (io >= so_scheduler.io_devices_num || io < 0)
		return ERROR;

	/* remove running thread from ready pool */
	delete_ready(&(so_scheduler.ready_threads),
		so_scheduler.running_thread);

	/* put thread in waiting pool */
	insert_waiting(&(so_scheduler.waiting_threads), io,
		so_scheduler.running_thread);

	/* reschedule */
	next_thread = get_next_ready(&(so_scheduler.ready_threads));
	reschedule(next_thread);

	return 0;
}

int so_signal(unsigned int io)
{
	so_thread_t *next_thread = NULL;
	int ret = 0;

	/* check if parameters are valid */
	if (io >= so_scheduler.io_devices_num || io < 0)
		return ERROR;

	so_scheduler.running_thread->time_quantum--;

	/* move all event waiting threads from waiting pool to
	 * ready pool
	 */
	ret = move_to_ready(&(so_scheduler.waiting_threads), io,
			&(so_scheduler.ready_threads));

	/* reschedule, if needed */
	if (so_scheduler.running_thread->time_quantum == 0) {
		next_thread = get_next_ready(&(so_scheduler.ready_threads));
		reschedule(next_thread);
	}

	return ret;
}

void so_exec(void)
{
	so_thread_t *next_thread = NULL;

	/* decrease running thread's time quantum (execute) */
	so_scheduler.running_thread->time_quantum--;

	/* if needed, reschedule */
	if (so_scheduler.running_thread->time_quantum == 0) {
		next_thread = get_next_ready(&(so_scheduler.ready_threads));
		reschedule(next_thread);
	}
}

void so_end(void)
{
	int rc;
	so_thread_t *first_thread = so_scheduler.first_thread;

	/* reset scheduler's initalization */
	so_scheduler.init = false;

	/* wait for root thread to finish and free memory */
	if (first_thread != NULL) {
		rc = pthread_join(first_thread->tid, NULL);
		DIE(rc < 0, "Join failed!\n");
		rc = pthread_cond_destroy(&(first_thread->turn));
		DIE(rc < 0, "Pthread condition destroy failed!\n");
		free(first_thread);
	}

	rc = pthread_mutex_destroy(&(so_scheduler.lock));
	DIE(rc < 0, "Mutex destroy failed!\n");
}
