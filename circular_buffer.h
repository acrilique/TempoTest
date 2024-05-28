/* A circular buffer. We only push to it. */


#ifndef _CIRCULAR_BUFFER_H
#define _CIRCULAR_BUFFER_H

#include <stdlib.h>

typedef struct CircularBuffer {
    float **buffer;
    unsigned int size;
    unsigned int channels;
    unsigned int head;
    unsigned int tail;
} CircularBuffer;

CircularBuffer *circular_buffer_new(unsigned int size, unsigned int channels) {
    CircularBuffer *cb = (CircularBuffer *) malloc(sizeof(CircularBuffer));
    cb->buffer = (float **) malloc(size * sizeof(float *));
    for (unsigned int i = 0; i < size; i++) {
        cb->buffer[i] = (float *) malloc(channels * sizeof(float));
        for (unsigned int j = 0; j < channels; j++) {
            cb->buffer[i][j] = 0.0f;
        }
    }
    cb->size = size;
    cb->channels = channels;
    cb->head = 0;
    cb->tail = 0;
    return cb;
}

void circular_buffer_free(struct CircularBuffer *cb) {
    for (unsigned int i = 0; i < cb->size; i++) {
        free(cb->buffer[i]);
    }
    free(cb->buffer);
    free(cb);
}

void circular_buffer_push(struct CircularBuffer *cb, float *values) {
    for (unsigned int i = 0; i < cb->channels; i++) {
        cb->buffer[cb->head][i] = values[i];
    }
    cb->head = (cb->head + 1) % cb->size;
    if (cb->head == cb->tail) {
        cb->tail = (cb->tail + 1) % cb->size;
    }
}

void circular_buffer_flush(struct CircularBuffer *cb) {
    for (unsigned int i = 0; i < cb->size; i++) {
        for (unsigned int j = 0; j < cb->channels; j++) {
            cb->buffer[i][j] = 0.0f;
        }
    }
    cb->head = 0;
    cb->tail = 0;
}

#endif // _CIRCULAR_BUFFER_H