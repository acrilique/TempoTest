#ifndef AUDIO_QUEUE_H
#define AUDIO_QUEUE_H

#include <pthread.h>
#include <stdlib.h>

typedef struct {
    float* data;
    size_t frameCount;
} AudioFrame;

typedef struct {
    AudioFrame* frames;
    int head;
    int tail;
    int capacity;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} AudioQueue;

AudioQueue* createAudioQueue(int capacity);
void destroyAudioQueue(AudioQueue* queue);
void enqueueAudioFrame(AudioQueue* queue, float* data, size_t frameCount);
AudioFrame dequeueAudioFrame(AudioQueue* queue);

#endif // AUDIO_QUEUE_H
