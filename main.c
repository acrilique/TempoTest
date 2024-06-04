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
pthread_t analysys_thread, audio_thread;

int stop_reading_flag = 0;
int audio_thread_open = 1; // 1 if closed, 0 if open
int analysys_thread_open = 1; // 1 if closed, 0 if open

struct AnalysysThreadArgs {
    WAVE *wav;
    struct CircularBuffer *cb;
    BTT *btt;
    float last_tempo;
};

struct AudioThreadArgs {
    struct SoundIoDevice *device;
    struct SoundIo *soundio;
    float *samples;
    int sample_rate;
    unsigned long num_samples;
    unsigned long samples_read;
    bool no_audio_mode;
};

void *analysis_thread(void *arg); //takes struct AnalysysThreadArgs *args as argument

void *audio_thread_fn(void *arg); //takes WAVE *wav as argument

int max(int a, int b) {
    return a > b ? a : b;
}

int main()
{
    struct AnalysysThreadArgs *analysys_args = malloc(sizeof(struct AnalysysThreadArgs));

    WAVE *wav = malloc(sizeof(WAVE));
    wave_init(wav);
    BTT *btt = btt_new_default();
    CircularBuffer *cb = circular_buffer_new(16000);

    analysys_args->wav = wav;
    analysys_args->btt = btt;
    analysys_args->cb = cb;
    analysys_args->last_tempo = 0.0;

    struct AudioThreadArgs *audio_args = malloc(sizeof(struct AudioThreadArgs));

    struct SoundIo *soundio = soundio_create();
    if (!soundio) {
        fprintf(stderr, "Soundio: Out of memory\n");
        audio_args->no_audio_mode = true;
    }
    if (soundio_connect(soundio)) {
        fprintf(stderr, "Soundio: Unable to connect\n");
        audio_args->no_audio_mode = true;
    }
    soundio_flush_events(soundio);
    struct SoundIoDevice *device = soundio_get_output_device(soundio, soundio_default_output_device_index(soundio));
    if (!device) {
        fprintf(stderr, "Soundio: Unable to find output device\n");
        audio_args->no_audio_mode = true;
    }

    audio_args->device = device;
    audio_args->soundio = soundio;
    audio_args->samples_read = 0;

    char tempo_string[32];

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

        if (fileDialogState.SelectFilePressed)
        {
            if (IsFileExtension(fileDialogState.fileNameText, ".wav"))
            {
                if (wav->is_open) {
                    if (audio_args->no_audio_mode == false) {
                        // do something to stop the audio playback
                    }
                    stop_reading_flag = 1;
                    pthread_join(analysys_thread, NULL);
                    wave_close(wav);
                }
                strncpy(fileNameToLoad, (const char*) fileDialogState.dirPathText, 511);
                strncat(fileNameToLoad, "/", sizeof(fileNameToLoad) - strlen(fileNameToLoad) - 1);
                strncat(fileNameToLoad, (const char*) fileDialogState.fileNameText, 512 - strlen(fileNameToLoad));
                
                wave_open(wav, (const char *) fileNameToLoad);
                if (!audio_args->no_audio_mode) {
                    if (soundio_device_supports_sample_rate(device, wav->header->sample_rate)) {
                        audio_args->sample_rate = wav->header->sample_rate;
                    } else {
                        int nearest_sr = soundio_device_nearest_sample_rate(device, wav->header->sample_rate);
                        wave_resample(wav, nearest_sr);
                        audio_args->sample_rate = nearest_sr;
                    }
                }
                if (!audio_args->no_audio_mode) {
                    audio_args->samples = wave_copy_samples(wav);
                    audio_args->num_samples = wav->num_samples;
                    if ((audio_thread_open = pthread_create(&audio_thread, NULL, audio_thread_fn, audio_args)) != 0) {
                        fprintf(stderr, "Error creating audio thread\n");
                        audio_args->no_audio_mode = true;
                    }
                }
                if ((analysys_thread_open = pthread_create(&analysys_thread, NULL, analysis_thread, analysys_args)) != 0) {
                    fprintf(stderr, "Error creating analysis thread\n");
                    return 1;
                }
            }

            fileDialogState.SelectFilePressed = false;
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);

        if (fileDialogState.windowActive) GuiLock();

        if (cb->head > 0 || cb->tail > 0) {
            int step = max(1, (int) cb->size / GetScreenWidth());
            int x = 0;
            for (int i = 0; i < GetScreenWidth(); i++) {
                int index = (i * step) % cb->size;
                DrawLine(x, GetScreenHeight() / 8 - cb->buffer[index] * GetScreenHeight() / 8, x + 1, GetScreenHeight() / 8 - cb->buffer[index + step - 1] * GetScreenHeight() / 8, RED);
                x++;
            }
        }
        int ret = snprintf(tempo_string, 32, "%f", analysys_args->last_tempo);
        if (ret < 0 || ret >= 32) {
            DrawText("ERROR", GetScreenWidth() / 4, 2 * GetScreenHeight() / 3, GetScreenWidth() / 10, RED);
        }
        else 
            DrawText((const char *) tempo_string, GetScreenWidth() / 4, 2 * GetScreenHeight() / 3, GetScreenWidth() / 10, GREEN);

        if (GuiButton((Rectangle){ 0, 0, 140, 30 }, GuiIconText(ICON_FILE_OPEN, "Open Wave File"))) fileDialogState.windowActive = true;

        if (GuiButton((Rectangle){ 140, 0, 140, 30 }, "Restart (flush)")) {
            stop_reading_flag = 1;
            pthread_join(audio_thread, NULL);
            if (audio_args->no_audio_mode == false) {
                pthread_join(analysys_thread, NULL);
            }
            
            btt_destroy(btt);
            btt = btt_new_default();
            btt_set_tracking_mode(btt, BTT_ONSET_AND_TEMPO_TRACKING);
            btt_set_gaussian_tempo_histogram_decay(btt, gaussian_tempo_histogram_decay);
            btt_set_gaussian_tempo_histogram_width(btt, gaussian_tempo_histogram_width);
            btt_set_log_gaussian_tempo_weight_mean(btt, log_gaussian_tempo_weight_mean);
            btt_set_log_gaussian_tempo_weight_width(btt, log_gaussian_tempo_weight_width);
            if (wav->is_open) {
                wave_close(wav);
            }            
            wave_open(wav, (const char *) fileNameToLoad);
            circular_buffer_flush(cb);
            
        }

        if (GuiButton((Rectangle){ 280, 0, 140, 30 }, "Restart")) {
            if (audio_thread_open == 0) {
                stop_reading_flag = 1;
                pthread_join(audio_thread, NULL);
            }
            if (analysys_thread_open == 0) {
                stop_reading_flag = 1;
                pthread_join(analysys_thread, NULL);
            }
            if (!audio_args->no_audio_mode)
                if ((audio_thread_open = pthread_create(&audio_thread, NULL, audio_thread_fn, audio_args) == 0)) {
                    fprintf(stderr, "Error creating audio thread\n");
                    audio_args->no_audio_mode = true;
                }
            if ((analysys_thread_open = pthread_create(&analysys_thread, NULL, analysis_thread, analysys_args)) == 0) {
                fprintf(stderr, "Error creating analysis thread\n");
                return 1;
            }
        }
        
        if (GuiSlider((Rectangle){ 180, 150, 140, 20 }, "Autocorrelation Exponent", autocorr_exponent_str, &autocorr_exponent, 0.1, 2.0)) {
            btt_set_autocorrelation_exponent(btt, autocorr_exponent);
            snprintf(autocorr_exponent_str, 32, "%f", autocorr_exponent);
        }

        if (GuiSlider((Rectangle){ 180, 180, 140, 20 }, "Gaussian Tempo Histogram Decay", gaussian_tempo_histogram_decay_str, &gaussian_tempo_histogram_decay, 0.6, 1.0)) {
            btt_set_gaussian_tempo_histogram_decay(btt, gaussian_tempo_histogram_decay);
            snprintf(gaussian_tempo_histogram_decay_str, 32, "%f", gaussian_tempo_histogram_decay);
        }

        if (GuiSlider((Rectangle){ 180, 210, 140, 20 }, "Gaussian Tempo Histogram Width", gaussian_tempo_histogram_width_str, &gaussian_tempo_histogram_width, 0.0, 10.0)) {
            btt_set_gaussian_tempo_histogram_width(btt, gaussian_tempo_histogram_width);
            snprintf(gaussian_tempo_histogram_width_str, 32, "%f", gaussian_tempo_histogram_width);
        }

        if (GuiSlider((Rectangle){ 180, 240, 140, 20 }, "Log Gaussian Tempo Weight Mean", log_gaussian_tempo_weight_mean_str, &log_gaussian_tempo_weight_mean, 0.0, 240.0)) {
            btt_set_log_gaussian_tempo_weight_mean(btt, log_gaussian_tempo_weight_mean);
            snprintf(log_gaussian_tempo_weight_mean_str, 32, "%f", log_gaussian_tempo_weight_mean);
        }

        if (GuiSlider((Rectangle){ 180, 270, 140, 20 }, "Log Gaussian Tempo Weight Width", log_gaussian_tempo_weight_width_str, &log_gaussian_tempo_weight_width, 0.0, 150.0)) {
            btt_set_log_gaussian_tempo_weight_width(btt, log_gaussian_tempo_weight_width);
            snprintf(log_gaussian_tempo_weight_width_str, 32, "%f", log_gaussian_tempo_weight_width);
        }

        GuiUnlock();

        GuiWindowFileDialog(&fileDialogState);

        EndDrawing();
    }

    
    if (!audio_thread_open) {
        stop_reading_flag = 1;
        pthread_join(audio_thread, NULL);
    }
    if (!analysys_thread_open) {
        stop_reading_flag = 1;
        pthread_join(analysys_thread, NULL);
    }
    if (wav->is_open)
        wave_close(wav);

    soundio_device_unref(device);
    soundio_destroy(soundio);
    btt_destroy(btt);
    circular_buffer_free(cb);

    free(autocorr_exponent_str);

    CloseWindow();
    return 0;
}

static void write_callback(struct SoundIoOutStream *outstream, int frame_count_min, int frame_count_max)
{
    struct AudioThreadArgs *args = (struct AudioThreadArgs *) outstream->userdata;
    const struct SoundIoChannelLayout *layout = &outstream->layout;
    struct SoundIoChannelArea *areas;
    int frames_left = frame_count_max;
    float *samples = args->samples;
    unsigned long samples_read = args->samples_read;
    int err;

    while (samples_read < args->num_samples && frames_left > 0 && !stop_reading_flag) {   
        int frame_count = frames_left;
        if ((err = soundio_outstream_begin_write(outstream, &areas, &frame_count))) {
            fprintf(stderr, "%s\n", soundio_strerror(err));
            args->no_audio_mode = true;
            return;
        }
        if (!frame_count)
            break;
        for (int frame = 0; frame < frame_count; frame++) {
            for (int channel = 0; channel < layout->channel_count; channel++) {
                float *ptr = (float *)(areas[channel].ptr + areas[channel].step * frame);
                *ptr = samples[samples_read++];
            }
        }
        if ((err = soundio_outstream_end_write(outstream))) {
            fprintf(stderr, "%s\n", soundio_strerror(err));
            args->no_audio_mode = true;
            return;
        }
        frames_left -= frame_count;
    }

    args->samples_read = samples_read;
}

void *audio_thread_fn(void *arg) {
    struct AudioThreadArgs *args = (struct AudioThreadArgs *) arg;
    int err;
    struct SoundIoOutStream *outstream = soundio_outstream_create(args->device);
    if (!outstream) {
        fprintf(stderr, "Soundio: Out of memory\n");
        args->no_audio_mode = true;
        return NULL;
    }
    outstream->format = SoundIoFormatFloat32NE;
    outstream->write_callback = write_callback;
    outstream->userdata = args;
    outstream->sample_rate = args->sample_rate;

    if ((err = soundio_outstream_open(outstream))) {
        fprintf(stderr, "%s\n", soundio_strerror(err));
        args->no_audio_mode = true;
        return NULL;
    }
    if ((err = soundio_outstream_start(outstream))) {
        fprintf(stderr, "%s\n", soundio_strerror(err));
        args->no_audio_mode = true;
        return NULL;
    }
    while (!stop_reading_flag && args->samples_read < args->num_samples) {
        soundio_wait_events(args->soundio);
    }
    soundio_outstream_destroy(outstream);
    free(args->samples);
    audio_thread_open = 1;
    args->samples_read = 0;
    return NULL;
}

void *analysis_thread(void* arg) {

    struct AnalysysThreadArgs *args = (struct AnalysysThreadArgs *) arg;
    unsigned int buffer_size = 4;
    dft_sample_t buffer[buffer_size];
    unsigned long samples_read = 0;
    unsigned int num_samples = args->wav->num_samples;

    int sample_rate = args->wav->header->sample_rate; 
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = (long) (buffer_size * 1e9 / sample_rate); // Convert sample rate to nanoseconds

    while (!stop_reading_flag && samples_read < num_samples) {
        for (unsigned int i = 0; i < buffer_size - 1; i++) {
            buffer[i] = args->wav->samples[samples_read++ * args->wav->header->channels];
            circular_buffer_push(args->cb, buffer[i]);
        }
        
        btt_process(args->btt, buffer, buffer_size);
        args->last_tempo = (float) btt_get_tempo_bpm(args->btt);

        // wait, this lets the audio be analyzed at the same speed we would play it
        nanosleep(&ts, NULL);
    }

    wave_close(args->wav);

    samples_read = 0;
    stop_reading_flag = 0;
    analysys_thread_open = 1;

    return NULL;
}