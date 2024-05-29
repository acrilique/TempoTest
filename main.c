#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <soundio/soundio.h>

#include "lib/Beat-and-Tempo-Tracking/BTT.h"

#include "wave.h"

#include "circular_buffer.h"

#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "lib/raygui.h"

#undef RAYGUI_IMPLEMENTATION
#define GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION
#include "lib/gui_window_file_dialog.h"

int nanosleep(const struct timespec *req, struct timespec *rem);

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex2 = PTHREAD_MUTEX_INITIALIZER;
pthread_rwlock_t rwlock1 = PTHREAD_RWLOCK_INITIALIZER;
pthread_t thread1;

BTT *btt;
WAVE wav;
CircularBuffer *cb;
struct SoundIo *soundio;

WAVE_READ_RETURN wv_ret = 0;

float last_tempo = 0;
int stop_reading_flag = 0;
int audio_thead_open = 1; // 1 if closed, 0 if open
bool no_audio_mode = false;
int flag_reinit_analisys = 0;

void *no_audio_thread(void *arg);

void *audio_thread(void *arg);

int max(int a, int b) {
    return a > b ? a : b;
}

static void write_callback(struct SoundIoOutStream *outstream, int frame_count_min, int frame_count_max)
{
    const struct SoundIoChannelLayout *layout = &outstream->layout;
    float float_sample_rate = outstream->sample_rate;
    struct SoundIoChannelArea *areas;
    int frames_left = frame_count_max;
    int err;

    dft_sample_t buffer[frame_count_max];
    float **samples;
    samples = (float **) malloc(frame_count_max * sizeof(float *));
    for (int i = 0; i < frame_count_max; i++) {
        samples[i] = (float *) malloc(2 * sizeof(float));
    }

    wv_ret = wave_read(&wav, frame_count_max, samples);

    if (wv_ret == WAVE_READ_EOF || wv_ret == WAVE_READ_ERROR) {
        wave_close(&wav);
        exit(0);
    }
    
    while (frames_left > 0) {
        int frame_count = frames_left;

        if ((err = soundio_outstream_begin_write(outstream, &areas, &frame_count))) {
            fprintf(stderr, "%s\n", soundio_strerror(err));
            exit(1);
        }

        if (!frame_count)
            break;

        for (int frame = 0; frame < frame_count; frame += 1) {
            buffer[frame] = samples[frame][0];
            pthread_rwlock_wrlock(&rwlock1);
            circular_buffer_push(cb, samples[frame]);
            pthread_rwlock_unlock(&rwlock1);

            for (int channel = 0; channel < layout->channel_count; channel += 1) {
                float *ptr = (float*)(areas[channel].ptr + areas[channel].step * frame);
                *ptr = samples[frame][channel];
            }
        }
        if ((err = soundio_outstream_end_write(outstream))) {
            fprintf(stderr, "%s\n", soundio_strerror(err));
            exit(1);
        }

        btt_process(btt, buffer, frame_count_max);
        pthread_mutex_lock(&mutex1);
        last_tempo = (float) btt_get_tempo_bpm(btt);
        pthread_mutex_unlock(&mutex1);
        frames_left -= frame_count;
    }
}

int main()
{
    int err;
    soundio = soundio_create();
    if (!soundio) {
        fprintf(stderr, "out of memory\n");
        no_audio_mode = true;
    }

    if (!no_audio_mode && (err = soundio_connect(soundio))) {
        fprintf(stderr, "error connecting: %s", soundio_strerror(err));
        no_audio_mode = true;
    }

    if (!no_audio_mode) {
        soundio_flush_events(soundio);
    }
    
    btt = btt_new_default();
    char tempo_string[32];

    wave_init(&wav);

    cb = circular_buffer_new(16000, 2);

    btt_set_tracking_mode(btt, BTT_ONSET_AND_TEMPO_TRACKING);
    btt_set_gaussian_tempo_histogram_decay(btt, 0.999);

    float autocorr_exponent = 0.5;
    char *autocorr_exponent_str = malloc(32);
    snprintf(autocorr_exponent_str, 32, "%f", autocorr_exponent);

    float gaussian_tempo_histogram_decay = 0.999;
    char *gaussian_tempo_histogram_decay_str = malloc(32);
    snprintf(gaussian_tempo_histogram_decay_str, 32, "%f", gaussian_tempo_histogram_decay);

    float gaussian_tempo_histogram_width = 5;
    char *gaussian_tempo_histogram_width_str = malloc(32);
    snprintf(gaussian_tempo_histogram_width_str, 32, "%f", gaussian_tempo_histogram_width);

    float log_gaussian_tempo_weight_mean = 120;
    char *log_gaussian_tempo_weight_mean_str = malloc(32);
    snprintf(log_gaussian_tempo_weight_mean_str, 32, "%f", log_gaussian_tempo_weight_mean);

    float log_gaussian_tempo_weight_width = 75;
    char *log_gaussian_tempo_weight_width_str = malloc(32);
    snprintf(log_gaussian_tempo_weight_width_str, 32, "%f", log_gaussian_tempo_weight_width);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(800, 600, "BTT Test");

    GuiWindowFileDialogState fileDialogState = InitGuiWindowFileDialog(GetWorkingDirectory());
    char fileNameToLoad[512] = { 0 };

    SetTargetFPS(60);
    while(!WindowShouldClose()) {

        if (flag_reinit_analisys) {
            if (!no_audio_mode) {
                audio_thead_open = pthread_create(&thread1, NULL, audio_thread, NULL);
            } else {
                audio_thead_open = pthread_create(&thread1, NULL, no_audio_thread, NULL);
            }
            flag_reinit_analisys = 0;
        }

        if (fileDialogState.SelectFilePressed)
        {
            if (IsFileExtension(fileDialogState.fileNameText, ".wav"))
            {
                if (wav.is_open) {
                    stop_reading_flag = 1;
                    pthread_join(thread1, NULL);
                    wave_close(&wav);
                }
                strncpy(fileNameToLoad, (const char*) fileDialogState.dirPathText, 511);
                strncat(fileNameToLoad, "/", sizeof(fileNameToLoad) - strlen(fileNameToLoad) - 1);
                strncat(fileNameToLoad, (const char*) fileDialogState.fileNameText, 512 - strlen(fileNameToLoad));
                wave_open(&wav, (const char*) fileNameToLoad);
                
                if (!no_audio_mode) {
                    audio_thead_open = pthread_create(&thread1, NULL, audio_thread, NULL);
                } else {
                    audio_thead_open = pthread_create(&thread1, NULL, no_audio_thread, NULL);
                }
            }

            fileDialogState.SelectFilePressed = false;
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);

        if (fileDialogState.windowActive) GuiLock();

        if (cb->head > 0 || cb->tail > 0) {
            pthread_rwlock_rdlock(&rwlock1);
            int step = max(1, (int) cb->size / GetScreenWidth());
            int x = 0;
            for (int i = 0; i < GetScreenWidth(); i++) {
                int index = (i * step) % cb->size;
                DrawLine(x, GetScreenHeight() / 8 - cb->buffer[index][0] * GetScreenHeight() / 8, x + 1, GetScreenHeight() / 8 - cb->buffer[index + step - 1][0] * GetScreenHeight() / 8, RED);
                x++;
            }
            pthread_rwlock_unlock(&rwlock1);
        }
        pthread_mutex_lock(&mutex1);
        int ret = snprintf(tempo_string, 32, "%f", last_tempo);
        pthread_mutex_unlock(&mutex1);
        if (ret < 0 || ret >= 32) {
            DrawText("ERROR", GetScreenWidth() / 4, 2 * GetScreenHeight() / 3, GetScreenWidth() / 10, RED);
        }
        else 
            DrawText((const char *) tempo_string, GetScreenWidth() / 4, 2 * GetScreenHeight() / 3, GetScreenWidth() / 10, GREEN);

        if (GuiButton((Rectangle){ 0, 0, 140, 30 }, GuiIconText(ICON_FILE_OPEN, "Open Wave File"))) fileDialogState.windowActive = true;

        if (GuiButton((Rectangle){ 140, 0, 140, 30 }, "Restart (flush)")) {
            stop_reading_flag = 1;
            pthread_join(thread1, NULL);
            btt_destroy(btt);
            btt = btt_new_default();
            btt_set_tracking_mode(btt, BTT_ONSET_AND_TEMPO_TRACKING);
            btt_set_gaussian_tempo_histogram_decay(btt, gaussian_tempo_histogram_decay);
            btt_set_gaussian_tempo_histogram_width(btt, gaussian_tempo_histogram_width);
            btt_set_log_gaussian_tempo_weight_mean(btt, log_gaussian_tempo_weight_mean);
            btt_set_log_gaussian_tempo_weight_width(btt, log_gaussian_tempo_weight_width);
            if (wav.is_open) {
                wave_close(&wav);
            }            
            wave_open(&wav, (const char *) fileNameToLoad);
            circular_buffer_flush(cb);
            if (!no_audio_mode)
                audio_thead_open = pthread_create(&thread1, NULL, audio_thread, NULL);
            else
                audio_thead_open = pthread_create(&thread1, NULL, no_audio_thread, NULL);
        }

        if (GuiButton((Rectangle){ 280, 0, 140, 30 }, "Restart")) {
            stop_reading_flag = 1;
            pthread_join(thread1, NULL);
            if (wav.is_open) {
                wave_close(&wav);
            }
            wave_open(&wav, (const char *) fileNameToLoad);
            if (!no_audio_mode)
                audio_thead_open = pthread_create(&thread1, NULL, audio_thread, NULL);
            else
                audio_thead_open = pthread_create(&thread1, NULL, no_audio_thread, NULL);
        }
        
        if (GuiSlider((Rectangle){ 180, 150, 140, 20 }, "Autocorrelation Exponent", autocorr_exponent_str, &autocorr_exponent, 0.1, 2.0)) {
            pthread_mutex_lock(&mutex1);
            btt_set_autocorrelation_exponent(btt, autocorr_exponent);
            pthread_mutex_unlock(&mutex1);
            snprintf(autocorr_exponent_str, 32, "%f", autocorr_exponent);
        }

        if (GuiSlider((Rectangle){ 180, 180, 140, 20 }, "Gaussian Tempo Histogram Decay", gaussian_tempo_histogram_decay_str, &gaussian_tempo_histogram_decay, 0.6, 1.0)) {
            pthread_mutex_lock(&mutex1);
            btt_set_gaussian_tempo_histogram_decay(btt, gaussian_tempo_histogram_decay);
            pthread_mutex_unlock(&mutex1);
            snprintf(gaussian_tempo_histogram_decay_str, 32, "%f", gaussian_tempo_histogram_decay);
        }

        if (GuiSlider((Rectangle){ 180, 210, 140, 20 }, "Gaussian Tempo Histogram Width", gaussian_tempo_histogram_width_str, &gaussian_tempo_histogram_width, 0.0, 10.0)) {
            pthread_mutex_lock(&mutex1);
            btt_set_gaussian_tempo_histogram_width(btt, gaussian_tempo_histogram_width);
            pthread_mutex_unlock(&mutex1);
            snprintf(gaussian_tempo_histogram_width_str, 32, "%f", gaussian_tempo_histogram_width);
        }

        if (GuiSlider((Rectangle){ 180, 240, 140, 20 }, "Log Gaussian Tempo Weight Mean", log_gaussian_tempo_weight_mean_str, &log_gaussian_tempo_weight_mean, 0.0, 240.0)) {
            pthread_mutex_lock(&mutex1);
            btt_set_log_gaussian_tempo_weight_mean(btt, log_gaussian_tempo_weight_mean);
            pthread_mutex_unlock(&mutex1);
            snprintf(log_gaussian_tempo_weight_mean_str, 32, "%f", log_gaussian_tempo_weight_mean);
        }

        if (GuiSlider((Rectangle){ 180, 270, 140, 20 }, "Log Gaussian Tempo Weight Width", log_gaussian_tempo_weight_width_str, &log_gaussian_tempo_weight_width, 0.0, 150.0)) {
            pthread_mutex_lock(&mutex1);
            btt_set_log_gaussian_tempo_weight_width(btt, log_gaussian_tempo_weight_width);
            pthread_mutex_unlock(&mutex1);
            snprintf(log_gaussian_tempo_weight_width_str, 32, "%f", log_gaussian_tempo_weight_width);
        }

        GuiUnlock();

        GuiWindowFileDialog(&fileDialogState);

        EndDrawing();
    }

    stop_reading_flag = 1;
    if (!audio_thead_open)
        pthread_join(thread1, NULL); 
    if (wav.is_open) {
        wave_close(&wav);
    }

    soundio_destroy(soundio);
    btt_destroy(btt);
    circular_buffer_free(cb);

    free(autocorr_exponent_str);

    CloseWindow();
    return 0;
}

void *audio_thread(void *arg) {
        int err;
        int default_out_device_index = soundio_default_output_device_index(soundio);
        if (default_out_device_index < 0) {
            fprintf(stderr, "no output device found");
            no_audio_mode = true;
            flag_reinit_analisys = 1;
            return NULL;
        }

        struct SoundIoDevice *device = soundio_get_output_device(soundio, default_out_device_index);
        if (!device) {
            fprintf(stderr, "out of memory");
            no_audio_mode = true;
            flag_reinit_analisys = 1;
            return NULL;
        }

        struct SoundIoOutStream *outstream = soundio_outstream_create(device);
        if (outstream->sample_rate != wav.header->sample_rate) {
            if (soundio_device_supports_sample_rate(device, wav.header->sample_rate)) {
                outstream->sample_rate = wav.header->sample_rate;
            } else {
                // WE SHOULD RESAMPLE
                fprintf(stderr, "sample rate %d is not supported", wav.header->sample_rate);
                no_audio_mode = true;
                flag_reinit_analisys = 1;
                return NULL;
            }
        }
        outstream->format = SoundIoFormatFloat32NE;
        outstream->write_callback = write_callback;

        if ((err = soundio_outstream_open(outstream))) {
            fprintf(stderr, "unable to open device: %s", soundio_strerror(err));
            no_audio_mode = true;
            flag_reinit_analisys = 1;
            return NULL;
        }

        if (!no_audio_mode) {
            if (outstream->layout_error) {
                fprintf(stderr, "unable to set channel layout: %s\n", soundio_strerror(outstream->layout_error));
                no_audio_mode = true;
                flag_reinit_analisys = 1;
                return NULL;
            }
            if ((err = soundio_outstream_start(outstream))) {
                fprintf(stderr, "unable to start device: %s", soundio_strerror(err));
                no_audio_mode = true;
                flag_reinit_analisys = 1;
                return NULL;
            }
        }

        while (wv_ret == WAVE_READ_SUCCESS && !stop_reading_flag) {
            soundio_wait_events(soundio);
        }

        soundio_outstream_destroy(outstream);
        soundio_device_unref(device);

        stop_reading_flag = 0;
        audio_thead_open = 1;

        return NULL;
}

void *no_audio_thread(void* arg) {

    unsigned int buffer_size = 4;
    dft_sample_t buffer[buffer_size];
    float *samples[buffer_size];
    for (unsigned int i = 0; i < buffer_size; i++) {
        samples[i] = (float *) malloc(sizeof(float) * 2);
    }

    int sample_rate = wav.header->sample_rate; 
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = (long) (buffer_size * 1e9 / sample_rate); // Convert sample rate to nanoseconds

    while (wv_ret == WAVE_READ_SUCCESS && !stop_reading_flag) {
        wv_ret = wave_read(&wav, buffer_size, samples);
        for (unsigned int i = 0; i < buffer_size - 1; i++) {
            buffer[i] = samples[i][0];
            pthread_rwlock_wrlock(&rwlock1);
            circular_buffer_push(cb, samples[i]);
            pthread_rwlock_unlock(&rwlock1);
        }
        
        btt_process(btt, buffer, buffer_size);
        pthread_mutex_lock(&mutex1);
        last_tempo = (float) btt_get_tempo_bpm(btt);
        pthread_mutex_unlock(&mutex1);

        // wait, this lets the audio be analyzed at the same speed we would play it
        nanosleep(&ts, NULL);
    }

    if (wv_ret == WAVE_READ_EOF || wv_ret == WAVE_READ_ERROR) {

        wave_close(&wav);
    }

    for (unsigned int i = 0; i < buffer_size; i++) {
        free(samples[i]);
    }
    stop_reading_flag = 0;
    audio_thead_open = 1;

    return NULL;
}