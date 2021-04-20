#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include "concurrent-queue.h"

#define INSERTS (1 << 10)

int main() {
	initConcurrency(1);
	initThreadConcurrency();

	for (int type = (int)__MIN; type <= (int)__MAX; type++) {
		printf("Testing Queue Type %d\n", type);
		ConcurrentQueue *queue = conqueueCreate((enum QueueType)type);

		const uint64_t valBase = 2021;
		for (int i = 0; i < INSERTS; i++) {
			uint64_t val = valBase + i;
			//printf("Value enqueued: %lu\n", val);
			assert(0 == conqueueEnqueue(queue, (void *)val));
		}

		uint64_t val;
		for (int i = 0; i < INSERTS; i++) {
			assert(0 == conqueueDequeue(queue, (void **)&val));
			//printf("Value dequeued: %lu\n", val);
		}
		assert(0 != conqueueDequeue(queue, (void **)&val));

		conqueueDestroy(queue);
	}

	printf("Success!\n");
	return 0;
}
