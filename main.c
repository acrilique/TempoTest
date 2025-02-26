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

#define CIRCULAR_BUFFER_SIZE (44100 * 4)

typedef struct _Parameter{
    const char* name;
    union _setter{
        void (*double_setter)(BTT*, double);
        void (*int_setter)(BTT*, int);
    } setter;
    int is_int;
} Parameter;

typedef struct {
    ma_decoder decoder;
    ma_device device;
    ma_decoder_config decoderConfig;
    ma_device_config deviceConfig;
    CircularBuffer* waveform_buffer;
    AudioQueue* btt_queue;
    BTT* btt;
    pthread_t btt_thread;
    GtkWidget* tempo_label;
    GtkWidget* drawing_area;
    GtkWidget *spectral_compression_gamma_label, *oss_filter_cutoff_label, *onset_threshold_label,
            *onset_threshold_min_label, *noise_cancellation_threshold_label, *autocorrelation_exponent_label,
            *min_tempo_label, *max_tempo_label, *num_tempo_candidates_label,
            *gaussian_tempo_histogram_decay_label, *gaussian_tempo_histogram_width_label,
            *log_gaussian_tempo_weight_mean_label, *log_gaussian_tempo_weight_width_label;
    float currentTempo;
    bool isPlaying;
    bool btt_thread_running;
    bool ui_running;
    char* audioFilePath;
} AudioContext;

Parameter params[] = {
    // Onset detection parameters
    {"use_amplitude_normalization", {.int_setter = btt_set_use_amplitude_normalization}, 1},
    {"spectral_compression_gamma", {.double_setter = btt_set_spectral_compression_gamma}, 0},
    {"oss_filter_cutoff", {.double_setter = btt_set_oss_filter_cutoff}, 0},
    {"onset_threshold", {.double_setter = btt_set_onset_threshold}, 0},
    {"onset_threshold_min", {.double_setter = btt_set_onset_threshold_min}, 0},
    {"noise_cancellation_threshold", {.double_setter = btt_set_noise_cancellation_threshold}, 0},

    // Tempo estimation parameters
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
                *value_str = '\0';
                value_str++;
                double value = atof(value_str);
                bool invalid_parameter = TRUE;

                int num_params = (int) sizeof(params) / sizeof(params[0]);
                for (int j = 0; j < num_params; j++) {
                    if (strcmp(param, params[j].name) == 0) {
                        invalid_parameter = FALSE;
                        if (params[j].is_int) {
                            params[j].setter.int_setter(btt, (int)value);
                        } else {
                            params[j].setter.double_setter(btt, value);
                        }
                        break;
                    }
                }
                if (invalid_parameter) printf("Invalid parameter: %s\n", argv[i]);
            }
        }
    }
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    AudioContext* context = (AudioContext*)pDevice->pUserData;

    if (!context->isPlaying) {
        return;
    }

    float* output = (float*)pOutput;
    ma_uint64 framesRead;
    ma_decoder_read_pcm_frames(&context->decoder, output, frameCount, &framesRead);

    for (int i = 0; i < (int) framesRead; i++) {
        writeToCircularBuffer(context->waveform_buffer, &output[i * context->decoder.outputChannels], 1);
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

/**
 * PARAMETER CALLBACKS
 */
void use_amplitude_normalization_togglebutton_toggled(GtkToggleButton *self, gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;
    gboolean active = gtk_toggle_button_get_active(self);
    btt_set_use_amplitude_normalization(context->btt, (int)active);
    gtk_button_set_label(GTK_BUTTON(self), active ? "ON" : "OFF");
}

void spectral_compression_gamma_scale_value_changed(GtkScale *self, gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;
    double value = gtk_range_get_value(GTK_RANGE(self));
    btt_set_spectral_compression_gamma(context->btt, value);
    const char *str = g_strdup_printf("Spectral Compression Gamma: %.2f", value);
    gtk_label_set_text(GTK_LABEL(context->spectral_compression_gamma_label), str);
}

void oss_filter_cutoff_scale_value_changed(GtkScale *self, gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;
    double value = gtk_range_get_value(GTK_RANGE(self));
    btt_set_oss_filter_cutoff(context->btt, value);
    const char *str = g_strdup_printf("OSS Filter Cutoff: %.2f", value);
    gtk_label_set_text(GTK_LABEL(context->oss_filter_cutoff_label), str);
}

void onset_threshold_scale_value_changed(GtkScale *self, gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;
    double value = gtk_range_get_value(GTK_RANGE(self));
    btt_set_onset_threshold(context->btt, value);
    const char *str = g_strdup_printf("Onset Threshold: %.2f", value);
    gtk_label_set_text(GTK_LABEL(context->onset_threshold_label), str);
}

void onset_threshold_min_scale_value_changed(GtkScale *self, gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;
    double value = gtk_range_get_value(GTK_RANGE(self));
    btt_set_onset_threshold_min(context->btt, value);
    const char *str = g_strdup_printf("Onset Threshold Min: %.2f", value);
    gtk_label_set_text(GTK_LABEL(context->onset_threshold_min_label), str);
}

void noise_cancellation_threshold_scale_value_changed(GtkScale *self, gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;
    double value = gtk_range_get_value(GTK_RANGE(self));
    btt_set_noise_cancellation_threshold(context->btt, value);
    const char *str = g_strdup_printf("Noise Cancellation Threshold: %.2f", value);
    gtk_label_set_text(GTK_LABEL(context->noise_cancellation_threshold_label), str);
}

void min_tempo_scale_value_changed(GtkScale *self, gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;
    double value = gtk_range_get_value(GTK_RANGE(self));
    btt_set_min_tempo(context->btt, value);
    const char *str = g_strdup_printf("Min Tempo: %.2f", value);
    gtk_label_set_text(GTK_LABEL(context->min_tempo_label), str);
}

void max_tempo_scale_value_changed(GtkScale *self, gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;
    double value = gtk_range_get_value(GTK_RANGE(self));
    btt_set_max_tempo(context->btt, value);
    const char *str = g_strdup_printf("Max Tempo: %.2f", value);
    gtk_label_set_text(GTK_LABEL(context->max_tempo_label), str);
}

void num_tempo_candidates_spinbutton_value_changed(GtkSpinButton *self, gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;
    btt_set_num_tempo_candidates(context->btt, gtk_spin_button_get_value_as_int(self));
}

void gaussian_tempo_histogram_decay_scale_value_changed(GtkScale *self, gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;
    double value = gtk_range_get_value(GTK_RANGE(self));
    btt_set_gaussian_tempo_histogram_decay(context->btt, value);
    const char *str = g_strdup_printf("Gaussian Tempo Histogram Decay: %.2f", value);
    gtk_label_set_text(GTK_LABEL(context->gaussian_tempo_histogram_decay_label), str);
}

void gaussian_tempo_histogram_width_scale_value_changed(GtkScale *self, gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;
    double value = gtk_range_get_value(GTK_RANGE(self));
    btt_set_gaussian_tempo_histogram_width(context->btt, value);
    const char *str = g_strdup_printf("Gaussian Tempo Histogram Width: %.2f", value);
    gtk_label_set_text(GTK_LABEL(context->gaussian_tempo_histogram_width_label), str);
}

void autocorrelation_exponent_scale_value_changed(GtkScale *self, gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;
    double value = gtk_range_get_value(GTK_RANGE(self));
    btt_set_autocorrelation_exponent(context->btt, value);
    const char *str = g_strdup_printf("Autocorrelation Exponent: %.2f", value);
    gtk_label_set_text(GTK_LABEL(context->autocorrelation_exponent_label), str);
}

void log_gaussian_tempo_weight_mean_scale_value_changed(GtkScale *self, gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;
    double value = gtk_range_get_value(GTK_RANGE(self));
    btt_set_log_gaussian_tempo_weight_mean(context->btt, value);
    const char *str = g_strdup_printf("Log Gaussian Tempo Weight Mean: %.2f", value);
    gtk_label_set_text(GTK_LABEL(context->log_gaussian_tempo_weight_mean_label), str);
}

void log_gaussian_tempo_weight_width_scale_value_changed(GtkScale *self, gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;
    double value = gtk_range_get_value(GTK_RANGE(self));
    btt_set_log_gaussian_tempo_weight_width(context->btt, value);
    const char *str = g_strdup_printf("Log Gaussian Tempo Weight Width: %.2f", value);
    gtk_label_set_text(GTK_LABEL(context->log_gaussian_tempo_weight_width_label), str);
}
/*
 * END OF PARAMETER CALLBACKS
 **/

static void draw_waveform(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    (void) area;
    AudioContext* context = (AudioContext*)user_data;

    // Only draw waveform if we have an audio file loaded
    if (!context->isPlaying) {
        return;
    }

    float data[CIRCULAR_BUFFER_SIZE];
    int count = readFromCircularBuffer(context->waveform_buffer, data, CIRCULAR_BUFFER_SIZE);
    
    // Check if we have any data to draw
    if (count <= 0) {
        return;
    }

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 1.0);

    int samplesPerPixel = count / width;
    if (samplesPerPixel < 1) samplesPerPixel = 1;

    double centerY = height / 2.0;

    // Draw all vertical lines in a single path
    cairo_new_path(cr);
    for (int x = 0; x < width; x++) {
        int startIdx = x * samplesPerPixel;
        int endIdx = startIdx + samplesPerPixel;
        if (endIdx > count) endIdx = count;
        
        if (startIdx >= count) break;

        // Find min and max values for this pixel column
        float minVal = data[startIdx];
        float maxVal = data[startIdx];

        for (int i = startIdx + 1; i < endIdx; i++) {
            float sample = data[i];
            if (sample < minVal) minVal = sample;
            if (sample > maxVal) maxVal = sample;
        }

        cairo_move_to(cr, x, centerY - minVal * centerY);
        cairo_line_to(cr, x, centerY - maxVal * centerY);
    }
    cairo_stroke(cr);
}

static void on_widget_destroy(gpointer data, GObject *where_the_object_was) {
    (void) where_the_object_was;
    GtkWidget **widget_pointer = (GtkWidget **)data;
    *widget_pointer = NULL;
}

static gboolean update_ui_idle(gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;

    // Check again if the UI is still running
    if (!context->ui_running) {
        return G_SOURCE_REMOVE;
    }

    // Safely update UI elements
    if (context->tempo_label && GTK_IS_LABEL(context->tempo_label)) {
        char tempo_text[32];
        snprintf(tempo_text, sizeof(tempo_text), "Tempo: %.1f BPM", context->currentTempo);
        gtk_label_set_text(GTK_LABEL(context->tempo_label), tempo_text);
    }

    if (context->drawing_area && GTK_IS_WIDGET(context->drawing_area)) {
        gtk_widget_queue_draw(context->drawing_area);
    }

    return G_SOURCE_REMOVE;
}

static gboolean update_ui(gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;

    // Check if the UI is still running
    if (!context->ui_running) {
        return G_SOURCE_REMOVE;
    }

    char tempo_text[32];
    snprintf(tempo_text, sizeof(tempo_text), "Tempo: %.1f BPM", context->currentTempo);

    // Use g_idle_add to perform UI updates
    g_idle_add(update_ui_idle, context);

    return G_SOURCE_CONTINUE;
}

static void app_shutdown(GtkApplication* app, gpointer user_data) {
    (void) app;
    AudioContext* context = (AudioContext*)user_data;

    context->isPlaying = false;
    context->btt_thread_running = false;

    enqueueAudioFrame(context->btt_queue, NULL, 0);
    pthread_join(context->btt_thread, NULL);

    if (ma_device_is_started(&context->device)) {
        ma_device_stop(&context->device);
    }
    ma_device_uninit(&context->device);
    ma_decoder_uninit(&context->decoder);

    destroyCircularBuffer(context->waveform_buffer);
    destroyAudioQueue(context->btt_queue);
    btt_destroy(context->btt);
    free(context->audioFilePath);
}

static bool init_miniaudio(AudioContext* context) {
    if (!context->audioFilePath) {
        return false;
    }

    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 2, 44100);
    ma_result result = ma_decoder_init_file(context->audioFilePath, &decoderConfig, &context->decoder);
    if (result != MA_SUCCESS) {
        printf("Could not load file: %s\n", context->audioFilePath);
        return false;
    }

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = context->decoder.outputFormat;
    deviceConfig.playback.channels = context->decoder.outputChannels;
    deviceConfig.sampleRate = context->decoder.outputSampleRate;
    deviceConfig.dataCallback = data_callback;
    deviceConfig.pUserData = context;

    if (ma_device_init(NULL, &deviceConfig, &context->device) != MA_SUCCESS) {
        printf("Failed to open playback device.\n");
        ma_decoder_uninit(&context->decoder);
        return false;
    }

    if (ma_device_start(&context->device) != MA_SUCCESS) {
        printf("Failed to start playback device.\n");
        ma_device_uninit(&context->device);
        ma_decoder_uninit(&context->decoder);
        return false;
    }

    return true;
}

static void reinitialize_audio(AudioContext* context, const char* new_file_path) {
    // Stop current playback and processing
    context->isPlaying = false;
    context->btt_thread_running = false;
    
    // Stop and clean up current audio
    if (ma_device_is_started(&context->device)) {
        ma_device_stop(&context->device);
    }
    ma_device_uninit(&context->device);
    ma_decoder_uninit(&context->decoder);

    // Update file path
    free(context->audioFilePath);
    context->audioFilePath = strdup(new_file_path);

    // Reset buffers
    clearCircularBuffer(context->waveform_buffer);
    clearAudioQueue(context->btt_queue);

    if (!init_miniaudio(context)) {
        return;
    }

    // Restart processing
    context->btt_thread_running = true;
    context->isPlaying = true;
}

static void on_file_dialog_response(GObject* source_object, GAsyncResult* result, gpointer user_data) {
    GtkFileDialog* file_dialog = GTK_FILE_DIALOG(source_object);
    AudioContext* context = (AudioContext*)user_data;
    GError* error = NULL;
    
    GFile* file = gtk_file_dialog_open_finish(file_dialog, result, &error);
    if (file == NULL) {
        if (error != NULL) {
            g_warning("Error opening file: %s", error->message);
            g_error_free(error);
        }
        return;
    }

    char* file_path = g_file_get_path(file);
    g_object_unref(file);

    if (file_path != NULL) {
        reinitialize_audio(context, file_path);
        g_free(file_path);
    }
}

static void on_open_button_clicked(GtkButton* button, gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;
    GtkWindow* window = GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(button)));
    
    GtkFileDialog* file_dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(file_dialog, "Open Audio File");
    
    // Set up file filters
    GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    
    GtkFileFilter* audio_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(audio_filter, "Audio Files");
    gtk_file_filter_add_pattern(audio_filter, "*.wav");
    gtk_file_filter_add_pattern(audio_filter, "*.mp3");
    g_list_store_append(filters, audio_filter);
    
    GtkFileFilter* all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, "All Files");
    gtk_file_filter_add_pattern(all_filter, "*");
    g_list_store_append(filters, all_filter);
    
    gtk_file_dialog_set_filters(file_dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(file_dialog, audio_filter);
    
    g_object_unref(audio_filter);
    g_object_unref(all_filter);
    g_object_unref(filters);
    
    gtk_file_dialog_open(file_dialog, window, NULL, on_file_dialog_response, context);
    g_object_unref(file_dialog);
}

/* UI size levels */
typedef enum {
    UI_SIZE_XS = 0,
    UI_SIZE_S,
    UI_SIZE_M,
    UI_SIZE_L,
    UI_SIZE_XL,
    UI_SIZE_COUNT
} UISize;

static const char* UI_SIZE_CLASS_NAMES[] = {
    "ui-size-xs",
    "ui-size-s",
    "ui-size-m",
    "ui-size-l",
    "ui-size-xl"
};

/* Function to change UI size */
static void change_ui_size(GtkWidget *grid, UISize new_size) {
    // Remove all size classes first
    for (int i = 0; i < UI_SIZE_COUNT; i++) {
        gtk_widget_remove_css_class(grid, UI_SIZE_CLASS_NAMES[i]);
    }
    
    // Add the new size class
    gtk_widget_add_css_class(grid, UI_SIZE_CLASS_NAMES[new_size]);
}

/* Size button click handlers */
static void on_size_decrease_clicked(GtkButton *button, gpointer user_data) {
    (void)button; // Mark parameter as intentionally unused
    GtkWidget *grid = GTK_WIDGET(user_data);
    
    if (grid == NULL) {
        g_warning("Grid is NULL in on_size_decrease_clicked");
        return;
    }
    
    // Find current size class
    UISize current_size = UI_SIZE_M; // Default to medium if no class found
    for (int i = 0; i < UI_SIZE_COUNT; i++) {
        if (gtk_widget_has_css_class(grid, UI_SIZE_CLASS_NAMES[i])) {
            current_size = i;
            break;
        }
    }
    
    // Decrease size (but not below XS)
    if (current_size > UI_SIZE_XS) {
        change_ui_size(grid, current_size - 1);
    }
}

static void on_size_increase_clicked(GtkButton *button, gpointer user_data) {
    (void)button; // Mark parameter as intentionally unused
    GtkWidget *grid = GTK_WIDGET(user_data);
    
    if (grid == NULL) {
        g_warning("Grid is NULL in on_size_increase_clicked");
        return;
    }
    
    // Find current size class
    UISize current_size = UI_SIZE_M; // Default to medium if no class found
    for (int i = 0; i < UI_SIZE_COUNT; i++) {
        if (gtk_widget_has_css_class(grid, UI_SIZE_CLASS_NAMES[i])) {
            current_size = i;
            break;
        }
    }
    
    // Increase size (but not above XL)
    if (current_size < UI_SIZE_XL) {
        change_ui_size(grid, current_size + 1);
    }
}

/* Key press event handler for window */
static gboolean on_key_press(GtkEventController *controller, guint keyval, guint keycode, 
                            GdkModifierType state, gpointer user_data) {
    GtkWidget *grid = GTK_WIDGET(user_data);
    
    // Check for Ctrl key being pressed
    if (state & GDK_CONTROL_MASK) {
        // Check for + key (plus or equal sign with shift)
        if (keyval == GDK_KEY_plus || keyval == GDK_KEY_equal) {
            on_size_increase_clicked(NULL, grid);
            return TRUE;
        }
        // Check for - key
        else if (keyval == GDK_KEY_minus) {
            on_size_decrease_clicked(NULL, grid);
            return TRUE;
        }
    }
    
    return FALSE; // Let other handlers process the event
}

static void activate(GtkApplication* app, gpointer user_data) {
    AudioContext* context = (AudioContext*)user_data;
    GtkWidget *window, *grid, *tempo_label, *drawing_area, *header_bar, *scrolled_window;
    
    // Load CSS provider
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(provider, "style.css");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    GtkWidget *use_amplitude_normalization_togglebutton, *spectral_compression_gamma_scale,
            *oss_filter_cutoff_scale, *onset_threshold_scale, *onset_threshold_min_scale,
            *noise_cancellation_threshold_scale, *autocorrelation_exponent_scale,
            *min_tempo_scale, *max_tempo_scale, *num_tempo_candidates_spinbutton,
            *gaussian_tempo_histogram_decay_scale, *gaussian_tempo_histogram_width_scale,
            *log_gaussian_tempo_weight_mean_scale, *log_gaussian_tempo_weight_width_scale;

    GtkWidget *spectral_compression_gamma_box,
            *oss_filter_cutoff_box, *onset_threshold_box, *onset_threshold_min_box,
            *noise_cancellation_threshold_box, *autocorrelation_exponent_box,
            *min_tempo_box, *max_tempo_box, *num_tempo_candidates_box,
            *gaussian_tempo_histogram_decay_box, *gaussian_tempo_histogram_width_box,
            *log_gaussian_tempo_weight_mean_box, *log_gaussian_tempo_weight_width_box;

    // Only initialize miniaudio if we have an audio file
    if (context->audioFilePath && !init_miniaudio(context)) {
        return;
    }

    context->ui_running = TRUE;

    window = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 400);
    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);

    header_bar = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header_bar), TRUE);

    GtkWidget* open_button = gtk_button_new_with_label("Open");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header_bar), open_button);
    g_signal_connect(open_button, "clicked", G_CALLBACK(on_open_button_clicked), context);
    
    // Create size buttons but don't connect signals yet (grid not created)
    GtkWidget* size_decrease_button = gtk_button_new_with_label("-");
    gtk_widget_add_css_class(size_decrease_button, "size-button");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header_bar), size_decrease_button);
    
    GtkWidget* size_increase_button = gtk_button_new_with_label("+");
    gtk_widget_add_css_class(size_increase_button, "size-button");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header_bar), size_increase_button);

    scrolled_window = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scrolled_window, TRUE);
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_window_set_child(GTK_WINDOW(window), scrolled_window);

    grid = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE); // Allow rows to have different heights
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);     // Reduced spacing
    gtk_grid_set_row_spacing(GTK_GRID(grid), 20);        // Reduced spacing
    gtk_widget_set_hexpand(grid, TRUE);
    gtk_widget_set_vexpand(grid, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), grid);
    
    // Set initial UI size to medium
    change_ui_size(grid, UI_SIZE_M);
    
    // Now connect size button signals since grid is created
    g_signal_connect(size_decrease_button, "clicked", G_CALLBACK(on_size_decrease_clicked), grid);
    g_signal_connect(size_increase_button, "clicked", G_CALLBACK(on_size_increase_clicked), grid);

    tempo_label = gtk_label_new("Tempo: 0 BPM");
    gtk_widget_add_css_class(tempo_label, "tempo-display");
    gtk_grid_attach(GTK_GRID(grid), tempo_label, 0, 0, 10, 1);

    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(drawing_area, TRUE);
    gtk_widget_set_vexpand(drawing_area, TRUE);
    gtk_widget_set_size_request(drawing_area, 200, 100); // Minimum size instead of fixed size
    gtk_widget_add_css_class(drawing_area, "drawing-area");
    gtk_grid_attach(GTK_GRID(grid), drawing_area, 0, 1, 10, 2);

    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area),
                                   (GtkDrawingAreaDrawFunc)draw_waveform,
                                   context, NULL);

    /**
        * PARAMETER CONTROLS, the 6 on the left are for onset detection, while the 8 on the right are for tempo detection.
    */
    GtkWidget* amplitude_normalization_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget* amplitude_normalization_label = gtk_label_new("Amplitude Normalization");
    gboolean initial_state = (gboolean)btt_get_use_amplitude_normalization(context->btt);
    use_amplitude_normalization_togglebutton = gtk_toggle_button_new_with_label(initial_state ? "ON" : "OFF");
    gtk_widget_add_css_class(use_amplitude_normalization_togglebutton, "checkbutton");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(use_amplitude_normalization_togglebutton), initial_state);
    g_signal_connect(use_amplitude_normalization_togglebutton, "toggled", G_CALLBACK(use_amplitude_normalization_togglebutton_toggled), context);
    
    gtk_box_append(GTK_BOX(amplitude_normalization_box), amplitude_normalization_label);
    gtk_box_append(GTK_BOX(amplitude_normalization_box), use_amplitude_normalization_togglebutton);
    gtk_grid_attach(GTK_GRID(grid), amplitude_normalization_box, 0, 3, 2, 2);

    double spectral_compression_gamma = btt_get_spectral_compression_gamma(context->btt);
    const char *spectral_compression_gamma_text = g_strdup_printf("Spectral Compression Gamma: %.2f", spectral_compression_gamma);
    context->spectral_compression_gamma_label = gtk_label_new(spectral_compression_gamma_text);

    spectral_compression_gamma_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.01);
    gtk_range_set_value(GTK_RANGE(spectral_compression_gamma_scale), spectral_compression_gamma);
    g_signal_connect(spectral_compression_gamma_scale, "value-changed", G_CALLBACK(spectral_compression_gamma_scale_value_changed), context);

    spectral_compression_gamma_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(spectral_compression_gamma_box), context->spectral_compression_gamma_label);
    gtk_box_append(GTK_BOX(spectral_compression_gamma_box), spectral_compression_gamma_scale);
    gtk_grid_attach(GTK_GRID(grid), spectral_compression_gamma_box, 2, 3, 2, 2);

    double oss_filter_cutoff = btt_get_oss_filter_cutoff(context->btt);
    const char *oss_filter_cutoff_text = g_strdup_printf("OSS Filter Cutoff: %.2f", oss_filter_cutoff);
    context->oss_filter_cutoff_label = gtk_label_new(oss_filter_cutoff_text);

    oss_filter_cutoff_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1.0, 100.0, 0.01);
    gtk_range_set_value(GTK_RANGE(oss_filter_cutoff_scale), oss_filter_cutoff);
    g_signal_connect(oss_filter_cutoff_scale, "value-changed", G_CALLBACK(oss_filter_cutoff_scale_value_changed), context);

    oss_filter_cutoff_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(oss_filter_cutoff_box), context->oss_filter_cutoff_label);
    gtk_box_append(GTK_BOX(oss_filter_cutoff_box), oss_filter_cutoff_scale);
    gtk_grid_attach(GTK_GRID(grid), oss_filter_cutoff_box, 0, 5, 2, 2);

    double onset_threshold = btt_get_onset_threshold(context->btt);
    const char *onset_threshold_text = g_strdup_printf("Onset Threshold: %.2f", onset_threshold);
    context->onset_threshold_label = gtk_label_new(onset_threshold_text);

    onset_threshold_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.01);
    gtk_range_set_value(GTK_RANGE(onset_threshold_scale), onset_threshold);
    g_signal_connect(onset_threshold_scale, "value-changed", G_CALLBACK(onset_threshold_scale_value_changed), context);

    onset_threshold_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(onset_threshold_box), context->onset_threshold_label);
    gtk_box_append(GTK_BOX(onset_threshold_box), onset_threshold_scale);
    gtk_grid_attach(GTK_GRID(grid), onset_threshold_box, 2, 5, 2, 1);

    double onset_threshold_min = btt_get_onset_threshold_min(context->btt);
    const char *onset_threshold_min_text = g_strdup_printf("Onset Threshold Min: %.2f", onset_threshold_min);
    context->onset_threshold_min_label = gtk_label_new(onset_threshold_min_text);

    onset_threshold_min_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 20.0, 0.01);
    gtk_range_set_value(GTK_RANGE(onset_threshold_min_scale), onset_threshold_min);
    g_signal_connect(onset_threshold_min_scale, "value-changed", G_CALLBACK(onset_threshold_min_scale_value_changed), context);

    onset_threshold_min_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(onset_threshold_min_box), context->onset_threshold_min_label);
    gtk_box_append(GTK_BOX(onset_threshold_min_box), onset_threshold_min_scale);
    gtk_grid_attach(GTK_GRID(grid), onset_threshold_min_box, 0, 7, 2, 2);

    double noise_cancellation_threshold = btt_get_noise_cancellation_threshold(context->btt);
    const char *noise_cancellation_threshold_text = g_strdup_printf("Noise Cancellation Threshold: %.2f", noise_cancellation_threshold);
    context->noise_cancellation_threshold_label = gtk_label_new(noise_cancellation_threshold_text);

    noise_cancellation_threshold_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -100.0, 0.0, 0.01);
    gtk_range_set_value(GTK_RANGE(noise_cancellation_threshold_scale), noise_cancellation_threshold);
    g_signal_connect(noise_cancellation_threshold_scale, "value-changed", G_CALLBACK(noise_cancellation_threshold_scale_value_changed), context);

    noise_cancellation_threshold_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(noise_cancellation_threshold_box), context->noise_cancellation_threshold_label);
    gtk_box_append(GTK_BOX(noise_cancellation_threshold_box), noise_cancellation_threshold_scale);
    gtk_grid_attach(GTK_GRID(grid), noise_cancellation_threshold_box, 2, 7, 2, 2);

    double min_tempo = btt_get_min_tempo(context->btt);
    const char *min_tempo_text = g_strdup_printf("Min Tempo: %.2f", min_tempo);
    context->min_tempo_label = gtk_label_new(min_tempo_text);

    min_tempo_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 40.0, 200.0, 0.01);
    gtk_range_set_value(GTK_RANGE(min_tempo_scale), min_tempo);
    g_signal_connect(min_tempo_scale, "value-changed", G_CALLBACK(min_tempo_scale_value_changed), context);

    min_tempo_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(min_tempo_box), context->min_tempo_label);
    gtk_box_append(GTK_BOX(min_tempo_box), min_tempo_scale);
    gtk_grid_attach(GTK_GRID(grid), min_tempo_box, 4, 3, 2, 2);

    double max_tempo = btt_get_max_tempo(context->btt);
    const char *max_tempo_text = g_strdup_printf("Max Tempo: %.2f", max_tempo);
    context->max_tempo_label = gtk_label_new(max_tempo_text);

    max_tempo_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 40.0, 200.0, 0.01);
    gtk_range_set_value(GTK_RANGE(max_tempo_scale), max_tempo);
    g_signal_connect(max_tempo_scale, "value-changed", G_CALLBACK(max_tempo_scale_value_changed), context);

    max_tempo_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(max_tempo_box), context->max_tempo_label);
    gtk_box_append(GTK_BOX(max_tempo_box), max_tempo_scale);
    gtk_grid_attach(GTK_GRID(grid), max_tempo_box, 6, 3, 2, 2);

    context->num_tempo_candidates_label = gtk_label_new("Num Tempo Candidates");
    num_tempo_candidates_spinbutton = gtk_spin_button_new_with_range(1, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(num_tempo_candidates_spinbutton), btt_get_num_tempo_candidates(context->btt));
    g_signal_connect(num_tempo_candidates_spinbutton, "value-changed", G_CALLBACK(num_tempo_candidates_spinbutton_value_changed), context);

    num_tempo_candidates_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(num_tempo_candidates_box), context->num_tempo_candidates_label);
    gtk_box_append(GTK_BOX(num_tempo_candidates_box), num_tempo_candidates_spinbutton);
    gtk_grid_attach(GTK_GRID(grid), num_tempo_candidates_box, 8, 3, 2, 2);

    double gaussian_tempo_histogram_decay = btt_get_gaussian_tempo_histogram_decay(context->btt);
    const char *gaussian_tempo_histogram_decay_text = g_strdup_printf("Gaussian Tempo Histogram Decay: %.2f", gaussian_tempo_histogram_decay);
    context->gaussian_tempo_histogram_decay_label = gtk_label_new(gaussian_tempo_histogram_decay_text);

    gaussian_tempo_histogram_decay_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.01);
    gtk_range_set_value(GTK_RANGE(gaussian_tempo_histogram_decay_scale), gaussian_tempo_histogram_decay);
    g_signal_connect(gaussian_tempo_histogram_decay_scale, "value-changed", G_CALLBACK(gaussian_tempo_histogram_decay_scale_value_changed), context);

    gaussian_tempo_histogram_decay_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(gaussian_tempo_histogram_decay_box), context->gaussian_tempo_histogram_decay_label);
    gtk_box_append(GTK_BOX(gaussian_tempo_histogram_decay_box), gaussian_tempo_histogram_decay_scale);
    gtk_grid_attach(GTK_GRID(grid), gaussian_tempo_histogram_decay_box, 4, 5, 2, 2);

    double gaussian_tempo_histogram_width = btt_get_gaussian_tempo_histogram_width(context->btt);
    const char *gaussian_tempo_histogram_width_text = g_strdup_printf("Gaussian Tempo Histogram Width: %.2f", gaussian_tempo_histogram_width);
    context->gaussian_tempo_histogram_width_label = gtk_label_new(gaussian_tempo_histogram_width_text);

    gaussian_tempo_histogram_width_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 10.0, 0.01);
    gtk_range_set_value(GTK_RANGE(gaussian_tempo_histogram_width_scale), gaussian_tempo_histogram_width);
    g_signal_connect(gaussian_tempo_histogram_width_scale, "value-changed", G_CALLBACK(gaussian_tempo_histogram_width_scale_value_changed), context);

    gaussian_tempo_histogram_width_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(gaussian_tempo_histogram_width_box), context->gaussian_tempo_histogram_width_label);
    gtk_box_append(GTK_BOX(gaussian_tempo_histogram_width_box), gaussian_tempo_histogram_width_scale);
    gtk_grid_attach(GTK_GRID(grid), gaussian_tempo_histogram_width_box, 6, 5, 2, 2);

    double autocorrelation_exponent = btt_get_autocorrelation_exponent(context->btt);
    const char *autocorrelation_exponent_text = g_strdup_printf("Autocorrelation Exponent: %.2f", autocorrelation_exponent);
    context->autocorrelation_exponent_label = gtk_label_new(autocorrelation_exponent_text);

    autocorrelation_exponent_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.01);
    gtk_range_set_value(GTK_RANGE(autocorrelation_exponent_scale), autocorrelation_exponent);
    g_signal_connect(autocorrelation_exponent_scale, "value-changed", G_CALLBACK(autocorrelation_exponent_scale_value_changed), context);

    autocorrelation_exponent_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(autocorrelation_exponent_box), context->autocorrelation_exponent_label);
    gtk_box_append(GTK_BOX(autocorrelation_exponent_box), autocorrelation_exponent_scale);
    gtk_grid_attach(GTK_GRID(grid), autocorrelation_exponent_box, 8, 5, 2, 2);

    double log_gaussian_tempo_weight_mean = btt_get_log_gaussian_tempo_weight_mean(context->btt);
    const char *log_gaussian_tempo_weight_mean_text = g_strdup_printf("Log Gaussian Tempo Weight Mean: %.2f", log_gaussian_tempo_weight_mean);
    context->log_gaussian_tempo_weight_mean_label = gtk_label_new(log_gaussian_tempo_weight_mean_text);

    log_gaussian_tempo_weight_mean_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 40.0, 200.0, 0.01);
    gtk_range_set_value(GTK_RANGE(log_gaussian_tempo_weight_mean_scale), log_gaussian_tempo_weight_mean);
    g_signal_connect(log_gaussian_tempo_weight_mean_scale, "value-changed", G_CALLBACK(log_gaussian_tempo_weight_mean_scale_value_changed), context);

    log_gaussian_tempo_weight_mean_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(log_gaussian_tempo_weight_mean_box), context->log_gaussian_tempo_weight_mean_label);
    gtk_box_append(GTK_BOX(log_gaussian_tempo_weight_mean_box), log_gaussian_tempo_weight_mean_scale);
    gtk_grid_attach(GTK_GRID(grid), log_gaussian_tempo_weight_mean_box, 5, 7, 2, 2);

    double log_gaussian_tempo_weight_width = btt_get_log_gaussian_tempo_weight_width(context->btt);
    const char *log_gaussian_tempo_weight_width_text = g_strdup_printf("Log Gaussian Tempo Weight Width: %.2f", log_gaussian_tempo_weight_width);
    context->log_gaussian_tempo_weight_width_label = gtk_label_new(log_gaussian_tempo_weight_width_text);

    log_gaussian_tempo_weight_width_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 150.0, 0.01);
    gtk_range_set_value(GTK_RANGE(log_gaussian_tempo_weight_width_scale), log_gaussian_tempo_weight_width);
    g_signal_connect(log_gaussian_tempo_weight_width_scale, "value-changed", G_CALLBACK(log_gaussian_tempo_weight_width_scale_value_changed), context);

    log_gaussian_tempo_weight_width_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(log_gaussian_tempo_weight_width_box), context->log_gaussian_tempo_weight_width_label);
    gtk_box_append(GTK_BOX(log_gaussian_tempo_weight_width_box), log_gaussian_tempo_weight_width_scale);
    gtk_grid_attach(GTK_GRID(grid), log_gaussian_tempo_weight_width_box, 7, 7, 2, 2);

    context->tempo_label = tempo_label;
    context->drawing_area = drawing_area;

    g_object_weak_ref(G_OBJECT(context->tempo_label), on_widget_destroy, &context->tempo_label);
    g_object_weak_ref(G_OBJECT(context->drawing_area), on_widget_destroy, &context->drawing_area);
    g_timeout_add(25, update_ui, context);
    
    GtkEventController *key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_press), grid);
    gtk_widget_add_controller(window, key_controller);

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char** argv) {
    AudioContext context = {0};
    
    if (argc >= 2) {
        context.audioFilePath = strdup(argv[1]);
    } else {
        context.audioFilePath = NULL;
    }
    context.waveform_buffer = createCircularBuffer(CIRCULAR_BUFFER_SIZE);
    context.isPlaying = context.audioFilePath != NULL;
    context.currentTempo = 0;

    context.btt = btt_new_default();
    btt_set_tracking_mode(context.btt, BTT_ONSET_AND_TEMPO_TRACKING);
    parse_parameters(context.btt, argc, argv);

    context.btt_queue = createAudioQueue(1024);  // Adjust capacity as needed
    context.btt_thread_running = true;

    if (pthread_create(&context.btt_thread, NULL, btt_processing_thread, &context) != 0) {
        printf("Failed to create BTT processing thread.\n");
        destroyCircularBuffer(context.waveform_buffer);
        destroyAudioQueue(context.btt_queue);
        free(context.audioFilePath);
        return -3;
    }

    GtkApplication *app;
    int status;

    app = gtk_application_new("com.acrilique.TestBTT", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &context);
    g_signal_connect(app, "shutdown", G_CALLBACK(app_shutdown), &context);
    status = g_application_run(G_APPLICATION(app), 1, argv);

    g_object_unref(app);

    return status;
}
