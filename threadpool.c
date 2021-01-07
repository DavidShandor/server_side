// Name: David Shandor	
// Id: 302902705
#include <pthread.h>
#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include <sysexits.h>
#include <unistd.h>

#ifndef __THREADPOOL
#define __THREADPOOL
#define USAGE "Usage: threadpool <pool-size> <max-number-of-jobs>\n"

// "dispatch_fn" declares a typed function pointer.  A
// variable of type "dispatch_fn" points to a function
// with the following signature:
//
//     int dispatch_function(void *arg);

typedef int (*dispatch_fn)(void *);

/**
 * create_threadpool creates a fixed-sized thread
 * pool.  If the function succeeds, it returns a (non-NULL)
 * "threadpool", else it returns NULL.
 * this function should:
 * 1. input sanity check 
 * 2. initialize the threadpool structure
 * 3. initialized mutex and conditional variables
 * 4. create the threads, the thread init function is do_work and its argument is the initialized threadpool. 
 */
threadpool *create_threadpool(int num_threads_in_pool)
{
	//valid parameters
	if (num_threads_in_pool <= 0 ||
		num_threads_in_pool > MAXT_IN_POOL)
	{
		printf("%s", USAGE);
		exit(EXIT_FAILURE);
	}

	threadpool *pool = (threadpool *)malloc(sizeof(threadpool));
	if (!pool){
		perror("error: malloc()\n");
		return NULL;
	}

	int mtx, cnd1, cnd2, rv;
	pool->num_threads = num_threads_in_pool;
	pool->qsize = 0;
	pool->qhead = NULL;
	pool->qtail = NULL;
	pool->shutdown = 0;
	pool->dont_accept = 0;

	if ((mtx = pthread_mutex_init(&pool->qlock, NULL)) != 0)
	{	
		perror("error: pthread_mutex_init\n");
		free(pool);
		return NULL;
	}
	if ((cnd1 = pthread_cond_init(&pool->q_empty, NULL)) != 0)
	{	
		perror("error: pthread_cond_init\n");
		pthread_mutex_destroy(&pool->qlock);
		free(pool);
		return NULL;
	}

	if ((cnd2 = pthread_cond_init(&pool->q_not_empty, NULL)) != 0)
	{
		perror("error: pthread_cond_init\n");
		pthread_mutex_destroy(&pool->qlock);
		pthread_cond_destroy(&pool->q_empty);
		free(pool);
		return NULL;
	}

	pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * pool->num_threads);
	if (!pool->threads)
	{
		perror("error: malloc()\n");
		pthread_mutex_destroy(&pool->qlock);
		pthread_cond_destroy(&pool->q_empty);
		pthread_cond_destroy(&pool->q_not_empty);
		free(pool);
		return NULL;
	}

	for (size_t i = 0; i < num_threads_in_pool; i++)
	{
		if ((rv = pthread_create(&pool->threads[i], NULL, do_work, pool)) != 0)
		{ // create threads failed- exits threads and free memory.
			for (size_t j = 0; j < i; j++)
				pthread_exit(&pool->threads[j]);
			perror("error: pthread_create\n");
			pthread_cond_destroy(&pool->q_not_empty);
			pthread_cond_destroy(&pool->q_empty);
			pthread_mutex_destroy(&pool->qlock);
			free(pool->threads);
			free(pool);
			return NULL;
		}
	}

	return pool;
}

/**
 * dispatch enter a "job" of type work_t into the queue.
 * when an available thread takes a job from the queue, it will
 * call the function "dispatch_to_here" with argument "arg".
 * this function should:
 * 1. create and init work_t element
 * 2. lock the mutex
 * 3. add the work_t element to the queue
 * 4. unlock mutex
 *
 */
void dispatch(threadpool *from_me, dispatch_fn dispatch_to_here, void *arg)
{	
	if (from_me == NULL || dispatch_to_here == NULL)
		return;
	pthread_mutex_lock(&from_me->qlock);
	
	//destroy start. don't accept more jobs.
	if(from_me->dont_accept == 1){
		pthread_mutex_unlock(&from_me->qlock);
		return;
	}

	work_t *temp = (work_t *)malloc(sizeof(work_t));

	if(!temp){ // release all the memory till that.

		perror("error: malloc\n");
		pthread_mutex_unlock(&from_me->qlock);
		return;
	}
	temp->routine = dispatch_to_here;
	temp->arg = arg;
	temp->next = NULL;


	if (!from_me->qhead)
	{
		from_me->qhead = temp;
		from_me->qtail = temp;
	}
	else{
		from_me->qtail->next = temp;
		from_me->qtail = temp;
	}
	from_me->qsize++;

	pthread_cond_signal(&from_me->q_not_empty); 
	
	pthread_mutex_unlock(&from_me->qlock);

}


/**
 * The work function of the thread
 * this function should:
 * 1. lock mutex
 * 2. if the queue is empty, wait
 * 3. take the first element from the queue (work_t)
 * 4. unlock mutex
 * 5. call the thread routine
 *
 */
void *do_work(void *p)
{
	if (p == NULL)
		return NULL;
	threadpool *pool = (threadpool *)p;
	
	if (pool->shutdown == 1) return NULL;// destroy has start.

	while(1){
		pthread_mutex_lock(&pool->qlock);
		if (pool->shutdown == 1)
		{
			pthread_mutex_unlock(&pool->qlock);
			return NULL;
		}
		if(pool->qsize == 0)
			pthread_cond_wait(&pool->q_not_empty, &pool->qlock);// wait till there is a new job in the queue.
		
		// check if destroy start.
		if(pool->shutdown == 1){
			pthread_mutex_unlock(&pool->qlock);
			return NULL;
		} 
		
		work_t *job = pool->qhead;
		
		pool->qsize--;
		pool->qhead = pool->qhead->next; // move the head to the next job.

		if(pool->qhead == NULL && pool->dont_accept == 1) pthread_cond_signal(&pool->q_empty);
		pthread_mutex_unlock(&pool->qlock);
		job->routine(job->arg);
		free(job);
		
	}
}

/**
 * destroy_threadpool kills the threadpool, causing
 * all threads in it to commit suicide, and then
 * frees all the memory associated with the threadpool.
 */
void destroy_threadpool(threadpool *destroyme)
{
	if (destroyme == NULL)
		return;
	
	pthread_mutex_lock(&destroyme->qlock);
	destroyme->dont_accept = 1;

	if (destroyme->qsize > 0)
		pthread_cond_wait(&destroyme->q_empty, &destroyme->qlock);
	
	destroyme->shutdown = 1;
	pthread_cond_broadcast(&destroyme->q_not_empty);
	
	pthread_mutex_unlock(&destroyme->qlock);

	
	for (size_t i = 0; i < destroyme->num_threads; i++)
		pthread_join(destroyme->threads[i], NULL);

	//start free memory and destroy cond&mutex	
	pthread_cond_destroy(&destroyme->q_empty);
	pthread_cond_destroy(&destroyme->q_not_empty);
	pthread_mutex_destroy(&destroyme->qlock);
	free(destroyme->threads);
	free(destroyme);
}


#endif // __THREADPOOL