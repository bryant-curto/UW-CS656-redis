#include "atomicvar.h"
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

#define NOT_IMPLEMENTED { fprintf(stderr, "Code path not implemented!\n"); assert(false); exit(-1); }

//#define MY_DEBUG
#if defined(MY_DEBUG)
	#define myprintf(...) \
			{ printf(">>>> "__VA_ARGS__); fflush(stdout); }
#else
	#define myprintf(...) ((void)0)
#endif // NDEBUG

static inline void acquireClient(client *c) {
	unsigned lock_depth;
	pthread_t thread_id;
	atomicGet(c->lock_depth, lock_depth);
	atomicGet(c->thread_id, thread_id);
	myprintf("Thread %lu trying to acquire client %p\n", (unsigned long)pthread_self(), (void *)c);
	if (lock_depth > 0 && pthread_self() == thread_id) {
		lock_depth += 1;
		atomicSet(c->lock_depth, lock_depth);
	} else {
		assert(0 == pthread_spin_lock(&c->mutex));
		atomicGet(c->lock_depth, lock_depth);
		assert(0 == lock_depth);
		lock_depth = 1;
		thread_id = pthread_self();
		atomicSet(c->lock_depth, lock_depth);
		atomicSet(c->thread_id, pthread_self());
	}
	myprintf("Thread %lu acquired client %p (%u)\n", (unsigned long)pthread_self(), (void *)c, lock_depth);

	if (c->is_deleted) {
		fprintf(stderr, "Lock acquired on deleted client!\n");
		exit(-1);
	}
}

static inline void releaseClient(client *c) {
	unsigned lock_depth;
	atomicGet(c->lock_depth, lock_depth);
	myprintf("Thread %lu trying to release client %p (%u)\n", (unsigned long)pthread_self(), (void *)c, lock_depth);

	if (1 == lock_depth) {
		atomicSet(c->lock_depth, 0);
		myprintf("Thread %lu released client %p\n", (unsigned long)pthread_self(), (void *)c);
		assert(0 == pthread_spin_unlock(&c->mutex));
	} else {
		atomicSet(c->lock_depth, lock_depth - 1);
	}
}
