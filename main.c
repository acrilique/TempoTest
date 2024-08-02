#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>

#include "lib/Beat-and-Tempo-Tracking/BTT.h"
#include "lib/Beat-and-Tempo-Tracking/src/DFT.h"

#define MINIAUDIO_IMPLEMENTATION
#include "lib/miniaudio.h"

#define CIRCULAR_BUFFER_SIZE (44100 * 2)

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

typedef struct {
    float* buffer;
    int head;
    int tail;
    int size;
    pthread_mutex_t mutex;
} CircularBuffer;

typedef struct {
    ma_decoder* decoder;
    CircularBuffer* waveform_buffer;
    bool isPlaying;
    float currentTempo;
    GtkWidget* tempo_label;
    GtkWidget* drawing_area;
    BTT* btt;
    AudioQueue* btt_queue;
    pthread_t btt_thread;
    bool btt_thread_running;
} AudioContext;

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

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    AudioContext* context = (AudioContext*)pDevice->pUserData;
    ma_decoder* pDecoder = context->decoder;

    if (pDecoder == NULL) {
        return;
    }

    float* output = (float*)pOutput;
    ma_uint64 framesRead;
    ma_decoder_read_pcm_frames(pDecoder, output, frameCount, &framesRead);

    for (int i = 0; i < framesRead; i++) {
        writeToCircularBuffer(context->waveform_buffer, &output[i * pDecoder->outputChannels], 1);
    }

    enqueueAudioFrame(context->btt_queue, output, framesRead);

    (void)pInput;
}

void* btt_processing_thread(void* arg) {
    AudioContext* context = (AudioContext*)arg;

    while (context->btt_thread_running) {
        AudioFrame frame = dequeueAudioFrame(context->btt_queue);
        btt_process(context->btt, frame.data, frame.frameCount);
        double new_tempo = btt_get_tempo_bpm(context->btt);
        if (new_tempo != context->currentTempo)
            context->currentTempo = new_tempo;
        free(frame.data);
    }

    return NULL;
}

static void draw_waveform(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;

    float data[CIRCULAR_BUFFER_SIZE];
    int count = readFromCircularBuffer(context->waveform_buffer, data, CIRCULAR_BUFFER_SIZE);

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 1.0);

    int step = count / width;
    if (step < 1) step = 1;

    cairo_move_to(cr, 0, height / 2);

    for (int i = 0; i < width; i++) {
        int index = (i * step) % count;
        float sample = data[index];
        cairo_line_to(cr, i, height / 2 - sample * height / 2);
    }

    cairo_stroke(cr);

}

static gboolean update_ui(gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;
    char tempo_text[32];
    snprintf(tempo_text, sizeof(tempo_text), "Tempo: %.1f BPM", context->currentTempo);
    gtk_label_set_text(GTK_LABEL(context->tempo_label), tempo_text);
    gtk_widget_queue_draw(context->drawing_area);
    return G_SOURCE_CONTINUE;
}

static void activate(GtkApplication* app, gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;
    GtkWidget *window, *grid, *tempo_label, *drawing_area;

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Waveform");
    gtk_window_set_default_size(GTK_WINDOW(window), 300, 150);  // Set initial size, but allow resizing

    grid = gtk_grid_new();
    gtk_window_set_child(GTK_WINDOW(window), grid);

    tempo_label = gtk_label_new("Tempo: 0 BPM");
    gtk_grid_attach(GTK_GRID(grid), tempo_label, 0, 0, 1, 1);

    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_vexpand(drawing_area, TRUE);
    gtk_widget_set_hexpand(drawing_area, TRUE);
    gtk_grid_attach(GTK_GRID(grid), drawing_area, 0, 1, 1, 1);

    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area),
                                   (GtkDrawingAreaDrawFunc)draw_waveform,
                                   context, NULL);

    context->tempo_label = tempo_label;
    context->drawing_area = drawing_area;

    g_timeout_add(100, update_ui, context);

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char** argv) {
    ma_result result;
    ma_decoder decoder;
    ma_device_config deviceConfig;
    ma_device device;
    ma_decoder_config decoderConfig;

    AudioContext context;

    pthread_t processingThreadId;

    if (argc < 2) {
        printf("Usage: %s <audio_file>\n", argv[0]);
        return -1;
    }

    decoderConfig = ma_decoder_config_init(ma_format_f32, 2, 44100);

    result = ma_decoder_init_file(argv[1], &decoderConfig, &decoder);
    if (result != MA_SUCCESS) {
        printf("Could not load file: %s\n", argv[1]);
        return -2;
    }

    context.decoder = &decoder;
    context.waveform_buffer = createCircularBuffer(CIRCULAR_BUFFER_SIZE);
    context.isPlaying = true;
    context.currentTempo = 0;

    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = decoder.outputFormat;
    deviceConfig.playback.channels = decoder.outputChannels;
    deviceConfig.sampleRate = decoder.outputSampleRate;
    deviceConfig.dataCallback = data_callback;
    deviceConfig.pUserData = &context;

    context.btt = btt_new_default();
    context.btt_queue = createAudioQueue(100);  // Adjust capacity as needed
    context.btt_thread_running = true;

    if (pthread_create(&context.btt_thread, NULL, btt_processing_thread, &context) != 0) {
        printf("Failed to create BTT processing thread.\n");
        ma_decoder_uninit(&decoder);
        destroyCircularBuffer(context.waveform_buffer);
        destroyAudioQueue(context.btt_queue);
        return -3;
    }

    if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
        printf("Failed to open playback device.\n");
        ma_decoder_uninit(&decoder);
        return -3;
    }

    if (ma_device_start(&device) != MA_SUCCESS) {
        printf("Failed to start playback device.\n");
        ma_device_uninit(&device);
        ma_decoder_uninit(&decoder);
        return -4;
    }

    GtkApplication *app;
    int status;

    app = gtk_application_new("com.example.GtkApplication", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &context);
    status = g_application_run(G_APPLICATION(app), 1, argv);
    g_object_unref(app);

    context.isPlaying = false;
    context.btt_thread_running = false;
    pthread_join(context.btt_thread, NULL);

    ma_device_uninit(&device);
    ma_decoder_uninit(&decoder);
    destroyCircularBuffer(context.waveform_buffer);
    destroyAudioQueue(context.btt_queue);

    return status;
}
