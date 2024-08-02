#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>

#include "lib/Beat-and-Tempo-Tracking/BTT.h"
#include "audio_queue.h"
#include "circular_buffer.h"

#define MINIAUDIO_IMPLEMENTATION
#include "lib/miniaudio.h"

#define CIRCULAR_BUFFER_SIZE (44100 * 2)

typedef struct {
    const char* name;
    union {
        void (*double_setter)(BTT*, double);
        void (*int_setter)(BTT*, int);
    } setter;
    int is_int;
} Parameter;

// Define the parameter list
Parameter params[] = {
    {"use_amplitude_normalization", {.int_setter = btt_set_use_amplitude_normalization}, 1},
    {"spectral_compression_gamma", {.double_setter = btt_set_spectral_compression_gamma}, 0},
    {"oss_filter_cutoff", {.double_setter = btt_set_oss_filter_cutoff}, 0},
    {"onset_threshold", {.double_setter = btt_set_onset_threshold}, 0},
    {"onset_threshold_min", {.double_setter = btt_set_onset_threshold_min}, 0},
    {"noise_cancellation_threshold", {.double_setter = btt_set_noise_cancellation_threshold}, 0},
    {"autocorrelation_exponent", {.double_setter = btt_set_autocorrelation_exponent}, 0},
    {"min_tempo", {.double_setter = btt_set_min_tempo}, 0},
    {"max_tempo", {.double_setter = btt_set_max_tempo}, 0},
    {"num_tempo_candidates", {.int_setter = btt_set_num_tempo_candidates}, 1},
    {"gaussian_tempo_histogram_decay", {.double_setter = btt_set_gaussian_tempo_histogram_decay}, 0},
    {"gaussian_tempo_histogram_width", {.double_setter = btt_set_gaussian_tempo_histogram_width}, 0},
    {"log_gaussian_tempo_weight_mean", {.double_setter = btt_set_log_gaussian_tempo_weight_mean}, 0},
    {"log_gaussian_tempo_weight_width", {.double_setter = btt_set_log_gaussian_tempo_weight_width}, 0}
};

void parse_parameters(BTT* btt, int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            char* param = argv[++i];
            char* value_str = strchr(param, '=');
            if (value_str) {
                *value_str = '\0'; // Split the string
                value_str++;
                double value = atof(value_str);

                int num_params = (int) sizeof(params) / sizeof(params[0]);
                for (int j = 0; j < num_params; j++) {
                    if (strcmp(param, params[j].name) == 0) {
                        if (params[j].is_int) {
                            params[j].setter.int_setter(btt, (int)value);
                        } else {
                            params[j].setter.double_setter(btt, value);
                        }
                        break;
                    }
                }
            }
        }
    }
}

typedef struct {
    ma_decoder* decoder;
    CircularBuffer* waveform_buffer;
    AudioQueue* btt_queue;
    BTT* btt;
    pthread_t btt_thread;
    GtkWidget* tempo_label;
    GtkWidget* drawing_area;
    float currentTempo;
    bool isPlaying;
    bool btt_thread_running;
    bool ui_running;
} AudioContext;

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    AudioContext* context = (AudioContext*)pDevice->pUserData;
    ma_decoder* pDecoder = context->decoder;

    if (pDecoder == NULL || !context->isPlaying) {
        return;
    }

    float* output = (float*)pOutput;
    ma_uint64 framesRead;
    ma_decoder_read_pcm_frames(pDecoder, output, frameCount, &framesRead);

    for (int i = 0; i < (int) framesRead; i++) {
        writeToCircularBuffer(context->waveform_buffer, &output[i * pDecoder->outputChannels], 1);
    }

    enqueueAudioFrame(context->btt_queue, output, framesRead);

    (void)pInput;
}

void* btt_processing_thread(void* arg) {
    AudioContext* context = (AudioContext*)arg;

    while (context->btt_thread_running) {
        AudioFrame frame = dequeueAudioFrame(context->btt_queue);
        if (frame.data == NULL) break;  // Exit if we receive a NULL frame
        btt_process(context->btt, frame.data, frame.frameCount);
        double new_tempo = btt_get_tempo_bpm(context->btt);
        if (new_tempo != context->currentTempo)
            context->currentTempo = new_tempo;
        // Don't free frame.data here
    }

    return NULL;
}

static void draw_waveform(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    (void) area;
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
    if (!context->ui_running) return G_SOURCE_REMOVE;
    char tempo_text[32];
    snprintf(tempo_text, sizeof(tempo_text), "Tempo: %.1f BPM", context->currentTempo);
    gtk_label_set_text(GTK_LABEL(context->tempo_label), tempo_text);
    gtk_widget_queue_draw(context->drawing_area);
    return G_SOURCE_CONTINUE;
}

static void activate(GtkApplication* app, gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;
    GtkWidget *window, *grid, *tempo_label, *drawing_area;

    context->ui_running = TRUE;

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "BTT Testing");
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

static void shutdown(GtkApplication* app, gpointer user_data) {
    (void) app;
    AudioContext* context = (AudioContext*)user_data;

    context->isPlaying = false;
    context->btt_thread_running = false;
    context->ui_running = false;

    enqueueAudioFrame(context->btt_queue, NULL, 0);
    pthread_join(context->btt_thread, NULL);

    destroyCircularBuffer(context->waveform_buffer);
    destroyAudioQueue(context->btt_queue);
    btt_destroy(context->btt);

}

int main(int argc, char** argv) {
    ma_result result;
    ma_decoder decoder;
    ma_device_config deviceConfig;
    ma_device device;
    ma_decoder_config decoderConfig;

    AudioContext context;

    if (argc < 2) {
        printf("Usage: %s <audio_file> [-i parameter=initial_value]\n", argv[0]);
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
    btt_set_tracking_mode(context.btt, BTT_ONSET_AND_TEMPO_TRACKING);
    parse_parameters(context.btt, argc, argv);

    context.btt_queue = createAudioQueue(1024);  // Adjust capacity as needed
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

    app = gtk_application_new("com.acrilique.TestBTT", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &context);
    g_signal_connect(app, "shutdown", G_CALLBACK(shutdown), &context);
    status = g_application_run(G_APPLICATION(app), 1, argv);

    if (ma_device_is_started(&device)) ma_device_stop(&device);
    ma_device_uninit(&device);
    ma_decoder_uninit(&decoder);

    g_object_unref(app);

    return status;
}
