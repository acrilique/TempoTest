#include "circular_buffer.h"
#include <stdlib.h>
#include <string.h>

CircularBuffer* createCircularBuffer(int size) {
    CircularBuffer* cb = (CircularBuffer*)malloc(sizeof(CircularBuffer));
    cb->buffer = (float*)calloc(size, sizeof(float));
    cb->head = 0;
    cb->tail = 0;
    cb->size = size;
    pthread_mutex_init(&cb->mutex, NULL);
    return cb;
}

void destroyCircularBuffer(CircularBuffer* cb) {
    pthread_mutex_destroy(&cb->mutex);
    free(cb->buffer);
    free(cb);
}

void writeToCircularBuffer(CircularBuffer* cb, float* data, int count) {
    pthread_mutex_lock(&cb->mutex);
    for (int i = 0; i < count; i++) {
        cb->buffer[cb->head] = data[i];
        cb->head = (cb->head + 1) % cb->size;
        if (cb->head == cb->tail) {
            cb->tail = (cb->tail + 1) % cb->size;
        }
    }
    pthread_mutex_unlock(&cb->mutex);
}

int readFromCircularBuffer(CircularBuffer* cb, float* data, int count) {
    pthread_mutex_lock(&cb->mutex);
    int available = (cb->head - cb->tail + cb->size) % cb->size;
    int toRead = (count < available) ? count : available;

    int readIndex = (cb->head - toRead + cb->size) % cb->size;
    for (int i = 0; i < toRead; i++) {
        data[i] = cb->buffer[readIndex];
        readIndex = (readIndex + 1) % cb->size;
    }
    pthread_mutex_unlock(&cb->mutex);

    return toRead;
}

int getAvailableData(CircularBuffer* cb) {
    pthread_mutex_lock(&cb->mutex);
    int available = (cb->head - cb->tail + cb->size) % cb->size;
    pthread_mutex_unlock(&cb->mutex);
    return available;
}

void clearCircularBuffer(CircularBuffer* cb) {
    pthread_mutex_lock(&cb->mutex);
    memset(cb->buffer, 0, cb->size * sizeof(float));
    cb->head = 0;
    cb->tail = 0;
    pthread_mutex_unlock(&cb->mutex);
}
