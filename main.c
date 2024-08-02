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
} AudioContext;

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
    float* buffer[framesRead];

    for (int i = 0; i < framesRead; i++) {
        // Assume interleaved samples. We only want the first channel written to the buffers.
        writeToCircularBuffer(context->waveform_buffer, &output[i * pDecoder->outputChannels], 1);
        buffer[i] = &output[i * pDecoder->outputChannels];
    }

    btt_process(context->btt, output, framesRead);
    double new_tempo = btt_get_tempo_bpm(context->btt);
    if (new_tempo != context->currentTempo)
        context->currentTempo = new_tempo;

    (void)pInput;
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

    ma_device_uninit(&device);
    ma_decoder_uninit(&decoder);
    destroyCircularBuffer(context.waveform_buffer);

    return status;
}
