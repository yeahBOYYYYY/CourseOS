#ifndef QUEUE_H
#define QUEUE_H

#include <stddef.h>

void   initQueue(void);
void   destroyQueue(void);
void   enqueue(void *item);
void  *dequeue(void);
size_t visited(void);

#endif