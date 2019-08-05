#ifndef SCHEDULER_H_
#define SCHEDULER_H_

#include <stdbool.h>

#include "utils.h"
#include "so_scheduler.h"
#include "so_thread_module.h"

/*
 * scheduler's structure:
 */
typedef struct scheduler {
	/* initalizing flag (true if initialised, false otherwise) */
	bool init;
	/* root of threads tree */
	so_thread_t *first_thread;
	/* pointer to currently running thread's info */
	so_thread_t *running_thread;
	/* pool of threads in ready state */
	ready_pool_t ready_threads;
	/* pool of threads in waiting state */
	waiting_pool_t waiting_threads;
	/* lock for synchronizing concurrent scheduler access */
	pthread_mutex_t lock;
	/* time quantum */
	unsigned int time_quantum;
	/* number of I/O devices */
	unsigned int io_devices_num;
} scheduler_t;

/*
 * preempts previous thread and signals another thread's execution
 * + thread: pointer to so_thread_t object to be rescheduled
 */
static void reschedule(so_thread_t *thread);

/*
 * function executed by all created threads
 */
static void *start_thread(void *thread_info);

static scheduler_t so_scheduler = (scheduler_t) {.init = false};

#endif
