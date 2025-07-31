#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>

#include "threadpool.h"
#include "Spinlock.h"


typedef struct taskex_s {
	void *pnext;
	handler_pt func;
	void *arg;
}taskex_t;

typedef struct task_s {
	void *next;
	handler_pt func;
	void *arg;
} task_t;

typedef struct task_queue_s {
	void *head;
	void **tail;
	int block; 
	spinlock_t lock;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
}task_queue_t;

struct threadpool_s {
	task_queue_t *task_queue;
	atomic_int quit;
	int thrd_count; 
	pthread_t *threads;
};
	
static task_queue_t *
__taskqueue_create(){
	int ret;
	task_queue_t *queue = (task_queue_t *)malloc(sizeof(task_queue_t));
	if (queue) {
		ret = pthread_mutex_init(&queue->mutex, NULL);
		if (ret == 0) {
			ret = pthread_cond_init(&queue->cond, NULL);
			if (ret == 0) {
				spinlock_init(&queue->lock);
				queue->head = NULL;
				queue->tail = &queue->head;
				queue->block = 1;
				return queue;
			}
			pthread_mutex_destroy(&queue->mutex);
		}
		free(queue);		
	}
	return NULL;	
	
}

static void
__nonblock(task_queue_t *queue) {
	pthread_mutex_lock(&queue->mutex);
	queue->block = 0;
	pthread_mutex_unlock(&queue->mutex);
	pthread_cond_broadcast(&queue->cond);
}

static inline void 
__add_task(task_queue_t *queue, task_t *task) {
	void **link = (void **)task;
	*link = NULL;

	spinlock_lock(&queue->lock);
	*queue->tail = link; 
	queue->tail = link;
	spinlock_unlock(&queue->lock);
	pthread_cond_signal(&queue->cond);
}

static inline void *
__pop_task(task_queue_t *queue) {
	spinlock_lock(&queue->lock);
	if (queue->head == NULL) {
		spinlock_unlock(&queue->lock);
		return NULL;
	}

	task_t *task;
	task = queue->head;
	void **link = (void **)task;
	queue->head = *link;

	if (queue->head == NULL) {
		queue->tail = &queue->head;
	}
	spinlock_unlock(&queue->lock);
	return task;
}

static inline void *
__get_task(task_queue_t *queue) {
	task_t *task;
	while ((task = __pop_task(queue)) == NULL) {
		pthread_mutex_lock(&queue->mutex);
		if (queue->block == 0) {
			pthread_mutex_unlock(&queue->mutex);
			return NULL;
		}
		pthread_cond_wait(&queue->cond, &queue->mutex);
		pthread_mutex_unlock(&queue->mutex);
	}
	return task;
}

static void 
__taskqueue_destroy (task_queue_t *queue) {
	task_t *task;
	while ((task = __pop_task(queue))) {
		free(task);
	}
	spinlock_destroy(&queue->lock);
	pthread_cond_destroy(&queue->cond); 
	pthread_mutex_destroy(&queue->mutex);
	free(queue);
}

static void *
__threadpool_worker(void *arg) {
	threadpool_t *pool = (threadpool_t *) arg;
	task_t *task;
	void *ctx;

	while (atomic_load(&pool->quit) == 0) {
		task = (task_t *)__get_task(pool->task_queue);
		if (!task) break;
		handler_pt func = task->func;
		ctx = task->arg;
		free(task);
		func(ctx);
	}
	return NULL;
}

static void
__threads_terminate(threadpool_t *pool) {
	atomic_store(&pool->quit, 1);
	__nonblock(pool->task_queue);
	int i = 0;
	for (i = 0;i < pool->thrd_count;i ++) {
		pthread_join(pool->threads[i], NULL);
	}
}

static int
__threads_create(threadpool_t *pool, size_t thrd_count) {
	pthread_attr_t attr;
	int ret;
	
	ret = pthread_attr_init(&attr);
	
	if (ret == 0) {
		pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * thrd_count);
		if (pool->threads) {
			int i = 0;
			for (; i < thrd_count; i++) {
				if (pthread_create(&pool->threads[i], &attr, __threadpool_worker, (void *)pool) != 0){
					break; 
				}
			}
			pool->thrd_count = i;
			pthread_attr_destroy(&attr);
			if (i == thrd_count) {
				return 0;
			}
			__threads_terminate(pool);
			free (pool->threads);
		}
		ret = -1;
	}
	return ret;
}



void
__threadpool_terminate(threadpool_t * pool) {
	atomic_store(&pool->quit, 1);
	__nonblock(pool->task_queue);
}

threadpool_t *
__threadpool_create(int thrd_count) {
	threadpool_t *pool;

	pool = (threadpool_t *)malloc(sizeof(*pool));
	if (pool) {
		task_queue_t *queue = __taskqueue_create();
		if (queue) {
			pool->task_queue = queue;
			atomic_init(&pool->quit, 0);
			if (__threads_create(pool, thrd_count) == 0) {
				return pool;
			}
			__taskqueue_destroy(queue);
		}
		free(pool);
	}
	return NULL;
}

int
threadpool_post(threadpool_t *pool, handler_pt func, void *arg) {
	if (atomic_load(&pool->quit) == 1) {
		return -1;
	}
	task_t * task = (task_t *)malloc(sizeof(task_t));
	if (!task) return -1;
	task->func = func;
	task->arg = arg;
	__add_task(pool->task_queue, task);
	return 0;
}

void
threadpool_waitdone(threadpool_t *pool) {
	int i;
	for (i = 0; i < pool->thrd_count; i++) {
		pthread_join(pool->threads [i], NULL);
	}
}

