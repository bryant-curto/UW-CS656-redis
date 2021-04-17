
#include <stdio.h>
#include "concurrent-queue.h"
#include <datastructures/lockbased_queue.h>
#include <datastructures/ms_queue.h>
#include <datastructures/ts_queue.h>
#include <datastructures/unboundedsize_kfifo.h>

#include <datastructures/ts_timestamp.h>
#include <datastructures/ts_queue_buffer.h>

static bool initialized = false;
static thread_local bool initializedThread = false;
static uint64_t numThreads;

struct ConcurrentQueue {
	template <typename T>
	ConcurrentQueue(T *queue, QueueType type)
	: queue(reinterpret_cast<void *>(queue))
	, type(type)
	{}

	void *queue;
	const QueueType type;
};

static inline void validateInitialized(bool thread, bool invert) {
	if (!(invert ^ (thread ? initializedThread : initialized))) {
		fprintf(stderr, "%s state %s initialized!\n", (thread ? "Thread" : "Global"), (invert ? "already" : "not"));
		assert(false);
		exit(-1);
	}
}

static inline void validateGlobalInitialized(bool invert=false) {
	validateInitialized(false /*thread*/, invert);
}

static inline void validateThreadInitialized(bool invert=false) {
	validateInitialized(true /*thread*/, invert);
}

void initConcurrency(uint64_t numThreads) {
	validateGlobalInitialized(true /*invert*/);

	scal::ThreadContext::prepare(numThreads + 1);
	scal::ThreadContext::assign_context();
	initialized = true;
	::numThreads = numThreads;
}

void initThreadConcurrency(void) {
	validateGlobalInitialized();
	validateThreadInitialized(true /*invert*/);

	// Ripped from scal/src/benchmark/prodcon/prodcon.cc
	size_t tlsize = scal::HumanSizeToPages("1G", 2 /* lenth of string */);
	scal::ThreadLocalAllocator::Get().Init(tlsize, true);

	initializedThread = true;
}

using MyLockBasedQueue = LockBasedQueue<void *>;
using MyMSQueue = scal::MSQueue<void *>;
using MyTSQueue = TSQueue<void *, TSQueueBuffer<void *, HardwareIntervalTimestamp>, HardwareIntervalTimestamp>;
using MyUnboundedSizeKFifo = scal::UnboundedSizeKFifo<void *>;


ConcurrentQueue *createMyLockBasedQueue(void) {
	// https://github.com/cksystemsgroup/scal/blob/fa2208a97a77d65f4e90f85fef3404c27c1f2ac2/src/datastructures/lockbased_queue.h
	auto queue = new MyLockBasedQueue(0 /* use default dequeuing strategy */,
									  0 /* don't delay if no element to dequeue */);
	return new ConcurrentQueue(queue, LOCK_BASED_SINGLY_LINKED_LIST_QUEUE);
}

ConcurrentQueue *createMyMSQueue(void) {
	// https://github.com/cksystemsgroup/scal/blob/fa2208a97a77d65f4e90f85fef3404c27c1f2ac2/src/datastructures/ms_queue.h
	auto queue = new MyMSQueue();
	return new ConcurrentQueue(queue, MS_QUEUE);
}

ConcurrentQueue *createMyTSQueue(void) {
	// https://github.com/cksystemsgroup/scal/blob/fa2208a97a77d65f4e90f85fef3404c27c1f2ac2/src/datastructures/ts_queue.h
	auto queue = new MyTSQueue(numThreads, 0 /* don't wait if no element to dequeue */);
	return new ConcurrentQueue(queue, TIMESTAMPED_QUEUE);
}

ConcurrentQueue *createMyUnboundedSizeKFifo(void) {
	// https://github.com/cksystemsgroup/scal/blob/fa2208a97a77d65f4e90f85fef3404c27c1f2ac2/src/datastructures/unboundedsize_kfifo.h
	auto queue = new MyUnboundedSizeKFifo(numThreads);
	return new ConcurrentQueue(queue, MS_QUEUE);
}

#define MATCH_APPLY(op, type, ...)										\
{																		\
	switch (type) {														\
	case LOCK_BASED_SINGLY_LINKED_LIST_QUEUE:							\
		op(MyLockBasedQueue, ##__VA_ARGS__)								\
		break;															\
	case MS_QUEUE:														\
		op(MyMSQueue, ##__VA_ARGS__)									\
		break;															\
	case TIMESTAMPED_QUEUE:												\
		op(MyMSQueue, ##__VA_ARGS__)									\
		break;															\
	case LOCALLY_LINEARIZABLE_K_QUEUE:									\
		op(MyUnboundedSizeKFifo, ##__VA_ARGS__)							\
		break;															\
	default:															\
		fprintf(stderr, "Unrecognized queue type %d!\n", (int)type);	\
		exit(-1);														\
		break;															\
	};																	\
}

#define PTRCAST(type, ptr, op) \
		{ op(reinterpret_cast<type *>(ptr)) }

#define QUEUE_MATCH_APPLY(op, queue, ...)	\
	MATCH_APPLY(PTRCAST, queue->type, queue->queue, op, ##__VA_ARGS__)


#define CREATE(type) { return create##type(); }
ConcurrentQueue *conqueueCreate(QueueType type) {
	validateGlobalInitialized();
	validateThreadInitialized();

	MATCH_APPLY(CREATE, type);
}

#define DELETE(queue_ptr) { delete queue_ptr; }
void conqueueDestroy(ConcurrentQueue *queue) {
	validateGlobalInitialized();
	validateThreadInitialized();
	QUEUE_MATCH_APPLY(DELETE, queue);
	delete queue;
}

#define ENQUEUE(queue_ptr) \
	{ return (queue_ptr->enqueue(item) ? 0 : -1); }
int conqueueEnqueue(ConcurrentQueue *queue, void *item) {
#ifdef MY_DEBUG
	validateThreadInitialized();
#endif
	QUEUE_MATCH_APPLY(ENQUEUE, queue);
}

#define DEQUEUE(queue_ptr) \
	{ return (queue_ptr->dequeue(item) ? 0 : -1); }
int conqueueDequeue(ConcurrentQueue *queue, void **item) {
#ifdef MY_DEBUG
	validateThreadInitialized();
#endif
	QUEUE_MATCH_APPLY(DEQUEUE, queue);
}
