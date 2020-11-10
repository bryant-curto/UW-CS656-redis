
#ifndef __URING_OPS__
#define __URING_OPS__

#include <stddef.h>

struct connection;
int uring_connWrite(connection *conn, const void *data, size_t data_len);

#endif //__URING_OPS__
