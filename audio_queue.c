#include "audio_queue.h"
#include <string.h>

AudioQueue* createAudioQueue(int capacity) {
    AudioQueue* queue = (AudioQueue*)malloc(sizeof(AudioQueue));
    queue->frames = (AudioFrame*)calloc(capacity, sizeof(AudioFrame));
    queue->head = queue->tail = 0;
    queue->capacity = capacity;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
    return queue;
}

void destroyAudioQueue(AudioQueue* queue) {
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
    for (int i = 0; i < queue->capacity; i++) {
        free(queue->frames[i].data);
    }
    free(queue->frames);
    free(queue);
}

void enqueueAudioFrame(AudioQueue* queue, float* data, size_t frameCount) {
    pthread_mutex_lock(&queue->mutex);

    int next = (queue->tail + 1) % queue->capacity;
    if (next == queue->head) {
        // Queue is full, wait for space
        while (next == queue->head) {
            pthread_cond_wait(&queue->cond, &queue->mutex);
            next = (queue->tail + 1) % queue->capacity;
        }
    }

    queue->frames[queue->tail].data = malloc(frameCount * sizeof(float));
    memcpy(queue->frames[queue->tail].data, data, frameCount * sizeof(float));
    queue->frames[queue->tail].frameCount = frameCount;

    queue->tail = next;

    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}

AudioFrame dequeueAudioFrame(AudioQueue* queue) {
    pthread_mutex_lock(&queue->mutex);

    while (queue->head == queue->tail) {
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }

    AudioFrame frame = queue->frames[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;

    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);

    return frame;
}

void clearAudioQueue(AudioQueue* queue) {
    pthread_mutex_lock(&queue->mutex);
    
    // Free any allocated frame data between head and tail
    int current = queue->head;
    while (current != queue->tail) {
        free(queue->frames[current].data);
        queue->frames[current].data = NULL;
        queue->frames[current].frameCount = 0;
        current = (current + 1) % queue->capacity;
    }
    
    // Reset queue state
    queue->head = queue->tail = 0;
    
    pthread_cond_broadcast(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}
