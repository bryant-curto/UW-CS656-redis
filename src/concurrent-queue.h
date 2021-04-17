
#ifndef __CONCURRENT_DS__
#define __CONCURRENT_DS__

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct ConcurrentQueue ConcurrentQueue;

enum QueueType {
	__MIN = 0,
	LOCK_BASED_SINGLY_LINKED_LIST_QUEUE = 0,
	MS_QUEUE = 1,
	TIMESTAMPED_QUEUE = 2,

	// Relaxed Queues
	LOCALLY_LINEARIZABLE_K_QUEUE = 3,
	__MAX = 3
};

void initConcurrency(size_t numThreads);
void initThreadConcurrency(void);

ConcurrentQueue *conqueueCreate(enum QueueType type);

void conqueueDestroy(ConcurrentQueue *queue);

// NOTE: enqueue and dequeue return 0 on success
int conqueueEnqueue(ConcurrentQueue *queue, void *item);
int conqueueDequeue(ConcurrentQueue *queue, void **item);

#ifdef __cplusplus
}
#endif

#endif // __CONCURRENT_DS__
