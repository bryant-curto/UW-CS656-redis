#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <linux/fs.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>


#include "my_util.h"
#include "sds.h"
#include "connection.h"
#include "uring.h"
#include "uring_ops.h"

#include <linux/io_uring.h>

//=============================================================================
// Stolen from https://unixism.net/2020/04/io-uring-by-example-part-1-introduction/

#define QUEUE_DEPTH 16384 // This is just a very large number
#define BLOCK_SZ    1024

/* This is x86 specific */
#define read_barrier()  __asm__ __volatile__("":::"memory")
#define write_barrier() __asm__ __volatile__("":::"memory")

// Test configuration variables
char blocking, batch_syscalls;
size_t submitted;
size_t batchNum = 0; // debug logging

// Store members of submission queue ring
struct app_io_sq_ring {
    unsigned *head;
    unsigned *tail;
    unsigned *ring_mask;
    unsigned *ring_entries;
    unsigned *flags;
    unsigned *array;
};

// Store members of completion queue ring
struct app_io_cq_ring {
    unsigned *head;
    unsigned *tail;
    unsigned *ring_mask;
    unsigned *ring_entries;
    struct io_uring_cqe *cqes;
};

// Hold state for using io uring
struct submitter {
    int ring_fd;
    struct app_io_sq_ring sq_ring;
    struct io_uring_sqe *sqes;
    struct app_io_cq_ring cq_ring;
};

struct user_data {
	unsigned char op;
	connection *conn;
	int expected_rval;
	void **to_free;
	unsigned num_to_free;
};

/*
 * This code is written in the days when io_uring-related system calls are not
 * part of standard C libraries. So, we roll our own system call wrapper
 * functions.
 * */

int io_uring_setup(unsigned entries, struct io_uring_params *p) {
    return (int) syscall(__NR_io_uring_setup, entries, p);
}

int io_uring_enter(int ring_fd, unsigned int to_submit,
                          unsigned int min_complete, unsigned int flags)
{
    return (int) syscall(__NR_io_uring_enter, ring_fd, to_submit, min_complete,
                   flags, NULL, 0);
}

/*
 * io_uring requires a lot of setup which looks pretty hairy, but isn't all
 * that difficult to understand. Because of all this boilerplate code,
 * io_uring's author has created liburing, which is relatively easy to use.
 * However, you should take your time and understand this code. It is always
 * good to know how it all works underneath. Apart from bragging rights,
 * it does offer you a certain strange geeky peace.
 * */

int app_setup_uring(struct submitter *s) {
    struct app_io_sq_ring *sring = &s->sq_ring;
    struct app_io_cq_ring *cring = &s->cq_ring;
    struct io_uring_params p;
    void *sq_ptr, *cq_ptr;

    /*
     * We need to pass in the io_uring_params structure to the io_uring_setup()
     * call zeroed out. We could set any flags if we need to, but for this
     * example, we don't.
     * */
    memset(&p, 0, sizeof(p));
    s->ring_fd = io_uring_setup(QUEUE_DEPTH, &p);
    if (s->ring_fd < 0) {
        perror("io_uring_setup");
        return 1;
    }

    /*
     * io_uring communication happens via 2 shared kernel-user space ring buffers,
     * which can be jointly mapped with a single mmap() call in recent kernels. 
     * While the completion queue is directly manipulated, the submission queue 
     * has an indirection array in between. We map that in as well.
     * */

    int sring_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned);
    int cring_sz = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);

    /* In kernel version 5.4 and above, it is possible to map the submission and 
     * completion buffers with a single mmap() call. Rather than check for kernel 
     * versions, the recommended way is to just check the features field of the 
     * io_uring_params structure, which is a bit mask. If the 
     * IORING_FEAT_SINGLE_MMAP is set, then we can do away with the second mmap()
     * call to map the completion ring.
     * */
    if (p.features & IORING_FEAT_SINGLE_MMAP) {
        if (cring_sz > sring_sz) {
            sring_sz = cring_sz;
        }
        cring_sz = sring_sz;
    }

    /* Map in the submission and completion queue ring buffers.
     * Older kernels only map in the submission queue, though.
     * */
    sq_ptr = mmap(0, sring_sz, PROT_READ | PROT_WRITE, 
            MAP_SHARED | MAP_POPULATE,
            s->ring_fd, IORING_OFF_SQ_RING);
    if (sq_ptr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    if (p.features & IORING_FEAT_SINGLE_MMAP) {
        cq_ptr = sq_ptr;
    } else {
        /* Map in the completion queue ring buffer in older kernels separately */
        cq_ptr = mmap(0, cring_sz, PROT_READ | PROT_WRITE, 
                MAP_SHARED | MAP_POPULATE,
                s->ring_fd, IORING_OFF_CQ_RING);
        if (cq_ptr == MAP_FAILED) {
            perror("mmap");
            return 1;
        }
    }
    /* Save useful fields in a global app_io_sq_ring struct for later
     * easy reference */
    sring->head = sq_ptr + p.sq_off.head;
    sring->tail = sq_ptr + p.sq_off.tail;
    sring->ring_mask = sq_ptr + p.sq_off.ring_mask;
    sring->ring_entries = sq_ptr + p.sq_off.ring_entries;
    sring->flags = sq_ptr + p.sq_off.flags;
    sring->array = sq_ptr + p.sq_off.array;

    /* Map in the submission queue entries array */
    s->sqes = mmap(0, p.sq_entries * sizeof(struct io_uring_sqe),
            PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
            s->ring_fd, IORING_OFF_SQES);
    if (s->sqes == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    /* Save useful fields in a global app_io_cq_ring struct for later
     * easy reference */
    cring->head = cq_ptr + p.cq_off.head;
    cring->tail = cq_ptr + p.cq_off.tail;
    cring->ring_mask = cq_ptr + p.cq_off.ring_mask;
    cring->ring_entries = cq_ptr + p.cq_off.ring_entries;
    cring->cqes = cq_ptr + p.cq_off.cqes;

    return 0;
}

/*
 * Read from completion queue.
 * In this function, we read completion events from the completion queue, get
 * the data buffer that will have the file data and print it to the console.
 * */
void read_from_cq(struct submitter *s) {
    struct app_io_cq_ring *cring = &s->cq_ring;
    unsigned head = *cring->head;

    do {
        read_barrier();
        /*
         * Remember, this is a ring buffer. If head == tail, it means that the
         * buffer is empty.
         * */
        if (head == *cring->tail) {
            break;
		}

        /* Get the entry */
        struct io_uring_cqe *cqe = &cring->cqes[head & *s->cq_ring.ring_mask];

		// Handle result based on op type
        struct user_data *udata = (struct user_data*)cqe->user_data;
		if (IORING_OP_WRITEV == udata->op) {
			oprintf("Wrote %s\n", (char *)((struct iovec *)((void *)udata + sizeof(struct user_data)))->iov_base);
			// Make sure we got expected return value
			if (udata->expected_rval != cqe->res) {
				eprintf("Got unexpected rval! expected=%d / got=%d\n", udata->expected_rval, cqe->res);
			}
		} else {
			eprintf("Not implemented\n");
		}

		// Free data
		for (unsigned i = 0; i < udata->num_to_free; ++i) {
			free(udata->to_free[i]);
		}

        head++;
    } while (1);

    *cring->head = head;
    write_barrier();
}

// Submit requests to the submission queue.
void submit_to_sq(struct io_uring_sqe sqe, struct submitter *s) {
    struct app_io_sq_ring *sring = &s->sq_ring;

    /* Add our submission queue entry to the tail of the SQE ring buffer */
	unsigned next_tail, tail;
    next_tail = tail = *sring->tail;
    next_tail++;
    read_barrier();
    unsigned index = tail & *s->sq_ring.ring_mask;

	s->sqes[index] = sqe;

    sring->array[index] = index;
    tail = next_tail;

    /* Update the tail so the kernel can see it. */
    if(*sring->tail != tail) {
        write_barrier(); // This is not in example code but in https://kernel.dk/io_uring.pdf
        *sring->tail = tail;
        write_barrier();
    }

    /*
     * Tell the kernel we have submitted events with the io_uring_enter() system
     * call. We also pass in the IOURING_ENTER_GETEVENTS flag which causes the
     * io_uring_enter() call to wait until min_complete events (the 3rd param)
     * complete.
     * */
    if (!batch_syscalls) {
		unsigned min_complete = (blocking ? 1 : 0);
		unsigned flags = (blocking ? IORING_ENTER_GETEVENTS : 0);
    	int rval =  io_uring_enter(s->ring_fd, 1, min_complete, flags);
		if (1 != rval) {
			eprintf("io_uring_enter failed with rval=%d (errno=%d)\n", rval, errno);
		}
		if (blocking) {
			read_from_cq(s);
		}
	} else {
		++submitted;
	}
}



// Variables for using io uring
struct submitter *submitter = NULL;

void uring_init(char batch, char block) {
	batch_syscalls = batch;
	blocking = block;
	submitted = 0;

	// Actually initialize io uring
	submitter = malloc(sizeof(struct submitter));
	app_setup_uring(submitter);
}

/* Interface to io uring for redis server */
/******************************************/
int uring_connWrite(connection *conn, const void *data, size_t data_len) {
    // Objective is to make code equivalent to return conn->type->write(conn, data, data_len);

	// Perform sanity checks
	if (!submitter) {
		eprintf("io uring not initialized\n");
	}
	//if (conn->type->write != connSocketWrite) {
	//	eprintf("conn->type->write not connSocketWrite\n");
	//}

    void *client = conn->private_data; /* equivalent to connGetPrivateData(conn) */
    oprintf("Clietn %p writing in batch %zu\n", client, batchNum + 1);

	// Figure out how many blocks need to be requested to
	// store data to be written to socket
    size_t bytes_remaining = data_len;
    int blocks = (int) data_len / BLOCK_SZ;
    if (data_len % BLOCK_SZ) {
		blocks++;
	}

    /*
     * For each block of the file we need to read, we allocate an iovec struct
     * which is indexed into the iovecs array. This array is passed in as part
     * of the submission. If you don't understand this, then you need to look
     * up how the readv() and writev() system calls work.
     * */
	void *mem = malloc(sizeof(struct user_data) + blocks * sizeof(struct iovec) + 2 * sizeof(void *));
	struct user_data *udata = mem;
	struct iovec *iovecs = mem + sizeof(struct user_data);
	udata->to_free = mem + sizeof(struct user_data) + blocks * sizeof(struct iovec);
	udata->num_to_free = 2;

	void *bufs;
	if (0 != posix_memalign(&bufs, BLOCK_SZ, BLOCK_SZ * blocks)) {
		eprintf("posix_memalign() failed\n");
	}

	// Make sure that we free this memory IN THIS ORDER
	*(udata->to_free + 0) = bufs;
	*(udata->to_free + 1) = mem;

	for (int i = 0; i < blocks; ++i) {
		iovecs[i].iov_len = (int)(bytes_remaining > BLOCK_SZ ? BLOCK_SZ : bytes_remaining);
        iovecs[i].iov_base = bufs + i * BLOCK_SZ;
		memcpy(iovecs[i].iov_base, data + (i * BLOCK_SZ), iovecs[i].iov_len);

        bytes_remaining -= (size_t)iovecs[i].iov_len;
    }

	udata->op = IORING_OP_WRITEV;
	udata->conn = conn;
	udata->expected_rval = (int)data_len;

	struct io_uring_sqe sqe = {0}; // This resolved EINVAL I was getting
    sqe.opcode = IORING_OP_WRITEV;
    sqe.flags = 0;
	sqe.ioprio = 0;
	sqe.fd = conn->fd;
    sqe.off = 0;
    sqe.addr = (unsigned long) iovecs;
    sqe.len = blocks;
    sqe.user_data = (unsigned long long) udata;


	// Submit entry 
	submit_to_sq(sqe, submitter);

	// Return number of bytes written. This isn't great,
	// as we assume that write completes with writing
	// all data to socket
	return udata->expected_rval;
}

void uring_maybeBulkSubmit(void) {
	if (NULL == submitter) {
		eprintf("io uring not initialized\n");
	}
	read_from_cq(submitter);

	if (batch_syscalls && submitted > 0) {
		oprintf("Batch %zu: submitting %zu ops\n", ++batchNum, submitted);
    	int rval =  io_uring_enter(submitter->ring_fd, submitted, 0, 0);
		if (rval < 0 || submitted != (size_t)rval) {
			eprintf("io_uring_enter failed with rval=%d (errno=%d)\n", rval, errno);
		}
		submitted = 0; // Can't forget about clearing submission count!
	}
}

void uring_processResponses(void) {
	if (NULL == submitter) {
		eprintf("io uring not initialized\n");
	}
	read_from_cq(submitter);
}
