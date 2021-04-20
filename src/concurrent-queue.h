
#ifndef __CONCURRENT_DS__
#define __CONCURRENT_DS__

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct ConcurrentQueue ConcurrentQueue;

enum QueueType {
	__MIN = 0,
	MICHAEL_SCOTT_QUEUE = 0,
	BRR_DISTRIBUTED_QUEUE = 1,
	LOCK_BASED_SINGLY_LINKED_LIST_QUEUE = 2,
	TIMESTAMPED_QUEUE = 3,
	LOCALLY_LINEARIZABLE_K_QUEUE = 4,
	__MAX = 4,

	// Not implemented
	WAITFREE_QUEUE = 5,
	RANDOM_DEQUEUE_QUEUE = 6,
	SEGMENT_QUEUE = 7
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
