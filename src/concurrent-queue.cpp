
#include <stdio.h>
#include <atomic>
#include "concurrent-queue.h"
#include <datastructures/lockbased_queue.h>
#include <datastructures/ms_queue.h>
#include <datastructures/ts_queue.h>
#include <datastructures/unboundedsize_kfifo.h>
//#include <datastructures/wf_queue_ppopp12.h>
//#include <datastructures/random_dequeue_queue.h>
//#include <datastructures/segment_queue.h>

#include <datastructures/balancer_partrr.h>
#include <datastructures/distributed_data_structure.h>
#include <datastructures/ts_timestamp.h>
#include <datastructures/ts_queue_buffer.h>

static bool initialized = false;
static thread_local bool initializedThread = false;
static uint64_t numThreads;
static std::atomic<uint64_t> threadsRegistered(0);

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

	threadsRegistered++;
	if (threadsRegistered > numThreads) {
		fprintf(stderr, "More threads registered than expected\n");
		exit(-1);
	}
}

template <typename T, class P, class B>
struct DistributedQueue : public scal::DistributedDataStructure<T, P, B> {
	using DDS = scal::DistributedDataStructure<T, P, B>;
	using DDS::DistributedDataStructure;

	bool enqueue(T item) {
		return DDS::put(item);
	}

	bool dequeue(T *item) {
		return DDS::get(item);
	}
};

using MyLockBasedQueue = LockBasedQueue<void *>;
using MyMSQueue = scal::MSQueue<void *>;
//using MyWaitfreeQueue = void; //WaitfreeQueue<void *>;
//using MyRandomDequeueQueue = void; //scal::RandomDequeueQueue<void *>;
//using MySegmentQueue = void; //scal::SegmentQueue<void *>;
using MybRRDistributedQueue_Balancer = scal::BalancerPartitionedRoundRobin;
using MybRRDistributedQueue = DistributedQueue<void *, MyMSQueue, MybRRDistributedQueue_Balancer>;
using MyTSQueue = TSQueue<void *, TSQueueBuffer<void *, HardwareIntervalTimestamp>, HardwareIntervalTimestamp>;
using MyUnboundedSizeKFifo = scal::UnboundedSizeKFifo<void *>;


static ConcurrentQueue *createMyLockBasedQueue(QueueType type) {
	// https://github.com/cksystemsgroup/scal/blob/fa2208a97a77d65f4e90f85fef3404c27c1f2ac2/src/datastructures/lockbased_queue.h
	auto queue = new MyLockBasedQueue(0 /* dequeue_mode: use default dequeuing strategy */,
									  0 /* dequeue_timeout: don't delay if no element to dequeue */);
	return new ConcurrentQueue(queue, type);
}

static ConcurrentQueue *createMyMSQueue(QueueType type) {
	// https://github.com/cksystemsgroup/scal/blob/fa2208a97a77d65f4e90f85fef3404c27c1f2ac2/src/datastructures/ms_queue.h
	auto queue = new MyMSQueue();
	return new ConcurrentQueue(queue, type);
}

//static ConcurrentQueue *createMyWaitfreeQueue(QueueType type) {
//	// https://github.com/cksystemsgroup/scal/blob/fa2208a97a77d65f4e90f85fef3404c27c1f2ac2/src/datastructures/wf_queue_ppopp12.h
//	auto queue = new MyWaitfreeQueue(numThreads,
//										/* max_retries */,
//										/* helping_delay */);
//	return new ConcurrentQueue(queue, type);
//}
//
//static ConcurrentQueue *createMyRandomDequeueQueue(QueueType type) {
//	// https://github.com/cksystemsgroup/scal/blob/fa2208a97a77d65f4e90f85fef3404c27c1f2ac2/src/datastructures/random_dequeue_queue.h
//	auto queue = new MyRandomDequeueQueue( /* quasi_factor */,
//											/* max_retries */);
//	return new ConcurrentQueue(queue, type);
//}
//
//static ConcurrentQueue *createMySegmentQueue(QueueType type) {
//	// https://github.com/cksystemsgroup/scal/blob/fa2208a97a77d65f4e90f85fef3404c27c1f2ac2/src/datastructures/segment_queue.h
//	auto queue = new MySegmentQueue( /* s */);
//	return new ConcurrentQueue(queue, type);
//}

static ConcurrentQueue *createMybRRDistributedQueue(QueueType type) {
	// https://github.com/cksystemsgroup/scal/blob/fa2208a97a77d65f4e90f85fef3404c27c1f2ac2/src/datastructures/distributed_data_structure.h
	// https://github.com/cksystemsgroup/scal/blob/fa2208a97a77d65f4e90f85fef3404c27c1f2ac2/src/datastructures/balancer_partrr.h
	// https://github.com/cksystemsgroup/scal/blob/fa2208a97a77d65f4e90f85fef3404c27c1f2ac2/src/datastructures/ms_queue.h
	const uint64_t numParitalQueues = 32; // Used 80 in paper, presumably because testing with 80 threads. My system will have at most 32
	// TODO: Correctly free balancer
	auto balancer = new MybRRDistributedQueue_Balancer(1 /* partitions */,
													   numParitalQueues /* num_queues */);
	auto queue = new MybRRDistributedQueue(numParitalQueues /* num_data_structures */,
										   numThreads /* num_threads */,
										   balancer /* balancer */);
	return new ConcurrentQueue(queue, type);
}

static ConcurrentQueue *createMyTSQueue(QueueType type) {
	// https://github.com/cksystemsgroup/scal/blob/fa2208a97a77d65f4e90f85fef3404c27c1f2ac2/src/datastructures/ts_queue.h
	auto queue = new MyTSQueue(numThreads,
							   0 /* don't wait if no element to dequeue */);
	return new ConcurrentQueue(queue, type);
}

static ConcurrentQueue *createMyUnboundedSizeKFifo(QueueType type) {
	// https://github.com/cksystemsgroup/scal/blob/fa2208a97a77d65f4e90f85fef3404c27c1f2ac2/src/datastructures/unboundedsize_kfifo.h
	auto queue = new MyUnboundedSizeKFifo(numThreads);
	return new ConcurrentQueue(queue, type);
}

#define MATCH_APPLY(op, type, ...)                                                  \
{                                                                                   \
	switch (type) {                                                                 \
	case LOCK_BASED_SINGLY_LINKED_LIST_QUEUE:                                       \
		op(MyLockBasedQueue, LOCK_BASED_SINGLY_LINKED_LIST_QUEUE, ##__VA_ARGS__)    \
		break;                                                                      \
	case MICHAEL_SCOTT_QUEUE:                                                       \
		op(MyMSQueue, MICHAEL_SCOTT_QUEUE, ##__VA_ARGS__)                           \
		break;                                                                      \
/*
	case WAITFREE_QUEUE:                                                            \
		op(MyWaitfreeQueue, WAITFREE_QUEUE, ##__VA_ARGS__)                          \
		break;                                                                      \
	case RANDOM_DEQUEUE_QUEUE:                                                      \
		op(MyRandomDequeueQueue, RANDOM_DEQUEUE_QUEUE, ##__VA_ARGS__)               \
		break;                                                                      \
	case SEGMENT_QUEUE:                                                             \
		op(MySegmentQueue, SEGMENT_QUEUE, ##__VA_ARGS__)                            \
		break;                                                                      \
*/\
	case BRR_DISTRIBUTED_QUEUE:                                                     \
		op(MybRRDistributedQueue, BRR_DISTRIBUTED_QUEUE, ##__VA_ARGS__)             \
		break;                                                                      \
	case TIMESTAMPED_QUEUE:                                                         \
		op(MyTSQueue, TIMESTAMPED_QUEUE, ##__VA_ARGS__)                             \
		break;                                                                      \
	case LOCALLY_LINEARIZABLE_K_QUEUE:                                              \
		op(MyUnboundedSizeKFifo, LOCALLY_LINEARIZABLE_K_QUEUE, ##__VA_ARGS__)       \
		break;                                                                      \
	default:                                                                        \
		fprintf(stderr, "Unrecognized queue type %d!\n", (int)type);                \
		exit(-1);                                                                   \
		break;                                                                      \
	};                                                                              \
}

#define PTRCAST(type, typeenumval, ptr, op) \
		{ op(reinterpret_cast<type *>(ptr)) };

#define QUEUE_MATCH_APPLY(op, queue, ...)	\
	MATCH_APPLY(PTRCAST, queue->type, queue->queue, op, ##__VA_ARGS__)


#define CREATE(type, typeenumval) { return create##type(typeenumval); }
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
	validateThreadInitialized();
	QUEUE_MATCH_APPLY(ENQUEUE, queue);
}

#define DEQUEUE(queue_ptr) \
	{ return (queue_ptr->dequeue(item) ? 0 : -1); }
int conqueueDequeue(ConcurrentQueue *queue, void **item) {
	validateThreadInitialized();
	QUEUE_MATCH_APPLY(DEQUEUE, queue);
}
