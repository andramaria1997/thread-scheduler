#include "so_thread_module.h"

so_thread_t *create_thread(unsigned int time_quantum, unsigned int priority,
				so_handler *func)
{
	int rc;

	/* allocate space and initalize attributes for so_thread_t object */
	so_thread_t *new_thread = calloc(1, sizeof(so_thread_t));
	DIE(new_thread == NULL, "Calloc failed!\n");
	new_thread->handler = func;
	new_thread->prio = priority;
	new_thread->time_quantum = time_quantum;
	new_thread->children.vector = NULL;
	new_thread->children.size = 0;
	new_thread->children.last_pos = 0;
	new_thread->state = NEW;
	rc = pthread_cond_init(&(new_thread->turn), NULL);
	DIE(rc < 0, "Pthread condition init failed!\n");

	/* return pointer to so_thread_t object */
	return new_thread;
}

void init_list(thread_list_t *list)
{
	/* allocate space, set size and last position */
	list->vector = calloc(sizeof(so_thread_t *), INIT_NUM_THREADS);
	DIE(list->vector == NULL, "Calloc failed!\n");
	list->size = INIT_NUM_THREADS;
	list->last_pos = 0;
}

static unsigned int insert_list(thread_list_t *list, so_thread_t *thread)
{
	unsigned int size = 0, max_index = 0;
	so_thread_t **tmp = NULL;

	size = list->size;
	max_index = list->last_pos;

	/* if bucket is full, double size */
	if (size == max_index) {
		size *= 2;
		list->size = size;
		tmp = realloc(list->vector, sizeof(so_thread_t *) * size);
		DIE(tmp == NULL, "Realloc failed!\n");
		list->vector = tmp;
	}

	list->vector[max_index] = thread;

	return list->last_pos++;
}

void add_child(so_thread_t *parent, so_thread_t *child)
{
	if (parent->children.size == 0)
		init_list(&(parent->children));

	insert_list(&(parent->children), child);
}

void ready_pool_init(ready_pool_t *pool)
{
	int i;

	for (i = 0 ; i <= SO_MAX_PRIO ; i++) {
		init_list(&(pool->buckets[i]));
		pool->running_thread[i] = 0;
	}
}

void waiting_pool_init(waiting_pool_t *pool)
{
	int i;

	for (i = 0 ; i < SO_MAX_NUM_EVENTS ; i++)
		init_list(&(pool->buckets[i]));
}

void insert_ready(ready_pool_t *pool, so_thread_t *thread)
{
	unsigned int index;

	/* insert thread into bucket */
	index = insert_list(&(pool->buckets[thread->prio]), thread);

	/* set index and state */
	thread->index_in_bucket = index;
	thread->state = RDY;
}

void insert_waiting(waiting_pool_t *pool, unsigned int io, so_thread_t *thread)
{
	unsigned int index;

	/* insert thread in bucket */
	index = insert_list(&(pool->buckets[io]), thread);

	/* mark index and state */
	thread->index_in_bucket = index;
	thread->state = WAIT;
}

void delete_ready(ready_pool_t *pool, so_thread_t *thread)
{
	unsigned int max_index = 0, i = 0;
	unsigned int index = thread->index_in_bucket;
	so_thread_t **bucket = pool->buckets[thread->prio].vector;

	max_index = --(pool->buckets[thread->prio].last_pos);

	/* shift al threads to left in bucket */
	for (i = index + 1 ; i <= max_index ; i++) {
		bucket[i - 1] = bucket[i];
		bucket[i]->index_in_bucket--;
	}

	pool->running_thread[thread->prio]--;
}

so_thread_t *get_next_ready(ready_pool_t *pool)
{
	int bucket = SO_MAX_PRIO;
	unsigned int next = 0;

	/* search for max priority non-empty bucket */
	while ((bucket >= 0) && (pool->buckets[bucket].last_pos == 0))
		bucket--;

	if (bucket < 0)
		return NULL;

	/* get next thread from that bucket */
	next = (pool->running_thread[bucket] + 1) % pool->buckets[bucket].last_pos;
	pool->running_thread[bucket] = next;

	/* return pointer to that thread */
	return pool->buckets[bucket].vector[next];
}

int move_to_ready(waiting_pool_t *waiting_pool, unsigned int io,
			ready_pool_t *ready_pool)
{
	int i, max = waiting_pool->buckets[io].last_pos;

	/* all threads from io waiting bucket are move to ready pool
	 * one by one (insert in ready, then delete from waiting)
	 */
	for (i = 0 ; i < max ; i++) {
		insert_ready(ready_pool, waiting_pool->buckets[io].vector[i]);
		waiting_pool->buckets[io].vector[i]->state = RDY;
		waiting_pool->buckets[io].vector[i] = NULL;
	}

	/* bucket is now empty */
	waiting_pool->buckets[io].last_pos = 0;

	return max;
}
