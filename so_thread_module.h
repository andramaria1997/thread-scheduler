#ifndef SO_THREAD_MODULE_H_
#define SO_THREAD_MODULE_H_

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "utils.h"
#include "so_scheduler.h"


#define INIT_NUM_THREADS 10

/* thread's possible states: */
#define NEW	10
#define RUN	11
#define RDY	12
#define WAIT	13
#define TERM	14


typedef struct so_thread so_thread_t;

/*
 * thread list structure:
 */
typedef struct thread_list {
	/* so_thread_t pointer vector */
	so_thread_t **vector;
	/* last unused position in vector */
	unsigned int last_pos;
	/* size of vector */
	unsigned int size;
} thread_list_t;

/*
 * thread's structure:
 */
typedef struct so_thread {
	/* thread's id */
	pthread_t tid;
	/* list of children */
	thread_list_t children;
	/* thread's priority */
	unsigned int prio;
	/* running time left quantum */
	unsigned int time_quantum;
	/* index in corresponding wait or ready bucket */
	unsigned int index_in_bucket;
	/* thread's state (NEW, RDY, RUN, WAIT, TERM) */
	unsigned int state;
	/* thread's handler */
	so_handler *handler;
	/* condition for scheduling */
	pthread_cond_t turn;
} so_thread_t;

/*
 * ready threads' pool structure:
 */
typedef struct ready_pool {
	/* priority thread buckets */
	thread_list_t buckets[SO_MAX_PRIO + 1];
	/* current Round Robin thread index for each bucket */
	unsigned int running_thread[SO_MAX_PRIO + 1];
} ready_pool_t;

/*
 * waiting threads' pool structure:
 */
typedef struct waiting_pool {
	/* event thread buckets */
	thread_list_t buckets[SO_MAX_NUM_EVENTS];
} waiting_pool_t;

/*
 * creates a new so_thread_t object and returns it
 * + time_quantum: thread's time quantum
 * + priority: thread's priority
 * + func: thread's tasks to execute
 */
so_thread_t *create_thread(unsigned int time_quantum, unsigned int priority,
				so_handler *func);

/*
 * inserts child thread into parent's children thread list
 * + parent: pointer to parent thread's info
 * + child: pointer to child thread's info
 */
void add_child(so_thread_t *parent, so_thread_t *child);

/*
 * initializes a ready thread pool
 * + pool: pointer to ready threads pool
 */
void ready_pool_init(ready_pool_t *pool);

/*
 * initializes a waiting thread pool
 * + pool: pointer to waiting threads pool
 */
void waiting_pool_init(waiting_pool_t *pool);

/*
 * insert a so_thread_t into ready pool
 * + pool: pointer to ready threads pool
 * + thread: pointer to inserted thread
 */
void insert_ready(ready_pool_t *pool, so_thread_t *thread);

/*
 * insert a so_thread_t into waiting pool
 * + pool: pointer to waiting threads pool
 * + thread: pointer to inserted thread
 */
void insert_waiting(waiting_pool_t *pool, unsigned int io,
			so_thread_t *thread);

/*
 * delete so_thread_t from ready pool
 * + pool: pointer to ready threads pool
 * + thread: pointer to deleted thread
 */
void delete_ready(ready_pool_t *pool, so_thread_t *thread);

/*
 * returns next ready thread from pool after Round Robin algorithm
 * + pool: pointer to ready threads pool
 */
so_thread_t *get_next_ready(ready_pool_t *pool);

/*
 * move the whole corresponding event bucket from waiting pool
 * to ready pool
 * + waiting: pointer to waiting threads pool
 * + io: event's identifier
 * + ready: pointer to ready threads pool
 */
int move_to_ready(waiting_pool_t *waiting, unsigned int io,
			ready_pool_t *ready);

#endif
