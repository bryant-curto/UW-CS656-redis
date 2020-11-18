
#ifndef __URING_H__
#define __URING_H__

void uring_init(char batch, char block);
void uring_endOfProcessingLoop(void);
void uring_startOfProcessingLoop(void);

#endif // __URING_H__
