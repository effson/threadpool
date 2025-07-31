#ifndef _THREADPOOL_H
#define _THREADPOOL_H

typedef struct threadpool_s threadpool_t;

typedef void (*handler_pt)(void *);

#ifdef __cplusplus
extern "C"
{
#endif

threadpool_t *threadpool_create(int thrd_count);
void threadpool_terminate(threadpool_t *pool);
int threadpool_post(threadpool_t *pool, handler_pt func, void *arg);
void threadpool_waitdone(threadpool_t *pool);

#ifdef __cplusplus
}
#endif

#endif
