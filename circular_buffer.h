/* A circular buffer. We only push to it. */


#ifndef _CIRCULAR_BUFFER_H
#define _CIRCULAR_BUFFER_H

#include <stdlib.h>

typedef struct CircularBuffer {
    float *buffer;
    unsigned int size;
    unsigned int head;
    unsigned int tail;
} CircularBuffer;

CircularBuffer *circular_buffer_new(unsigned int size) {
    CircularBuffer *cb = (CircularBuffer *) malloc(sizeof(CircularBuffer));
    cb->buffer = (float *) malloc(sizeof(float) * size);
    cb->size = size;
    cb->head = 0;
    cb->tail = 0;
    return cb;
}

void circular_buffer_free(struct CircularBuffer *cb) {
    free(cb->buffer);
    free(cb);
}

void circular_buffer_push(struct CircularBuffer *cb, float value) {
    cb->buffer[cb->head] = value;
    cb->head = (cb->head + 1) % cb->size;
    if (cb->head == cb->tail) {
        cb->tail = (cb->tail + 1) % cb->size;
    }
}

#endif // _CIRCULAR_BUFFER_H