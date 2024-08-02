#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <pthread.h>

typedef struct {
    float* buffer;
    int head;
    int tail;
    int size;
    pthread_mutex_t mutex;
} CircularBuffer;

CircularBuffer* createCircularBuffer(int size);
void destroyCircularBuffer(CircularBuffer* cb);
void writeToCircularBuffer(CircularBuffer* cb, float* data, int count);
int readFromCircularBuffer(CircularBuffer* cb, float* data, int count);
int getAvailableData(CircularBuffer* cb);

#endif // CIRCULAR_BUFFER_H
