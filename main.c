#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "lib/Beat-and-Tempo-Tracking/BTT.h"

#include "wave.h"

#include "circular_buffer.h"

#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "lib/raylib/examples/shapes/raygui.h"

#undef RAYGUI_IMPLEMENTATION
#define GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION
#include "lib/raygui/examples/custom_file_dialog/gui_window_file_dialog.h"

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex2 = PTHREAD_MUTEX_INITIALIZER;
pthread_t tempo_thread;

WAVE wav;
CircularBuffer *cb;
float last_tempo = 0;
int stop_reading_flag = 0;
int audio_thead_open = 1; // 1 if closed, 0 if open

void audio_thread(BTT *btt);

int max(int a, int b) {
    return a > b ? a : b;
}

int main()
{
    BTT *btt = btt_new_default();
    char tempo_string[32];

    wave_init(&wav);

    cb = circular_buffer_new(16000);

    btt_set_tracking_mode(btt, BTT_ONSET_AND_TEMPO_TRACKING);
    btt_set_gaussian_tempo_histogram_decay(btt, 0.999);

    float autocorr_exponent = 0.5;
    char *autocorr_exponent_str = malloc(32);
    snprintf(autocorr_exponent_str, 32, "%f", autocorr_exponent);

    InitWindow(800, 600, "BTT Test");

    GuiWindowFileDialogState fileDialogState = InitGuiWindowFileDialog(GetWorkingDirectory());
    bool exitWindow = false;

    SetTargetFPS(60);
    while(!WindowShouldClose()) {

        if (fileDialogState.SelectFilePressed)
        {
            if (IsFileExtension(fileDialogState.fileNameText, ".wav"))
            {
                // TODO Close old track if opened, open new track...
                if (wav.is_open) {
                    stop_reading_flag = 1;
                    pthread_join(tempo_thread, NULL);
                    wave_close(&wav);
                }
                char fileNameToLoad[512] = { 0 };
                strncat(fileNameToLoad, (const char*) fileDialogState.dirPathText, 512);
                strncat(fileNameToLoad, "/", 1);
                strncat(fileNameToLoad, (const char*) fileDialogState.fileNameText, 512 - strlen(fileNameToLoad));
                wave_open(&wav, (const char*) fileNameToLoad);
                
                audio_thead_open = pthread_create(&tempo_thread, NULL, (void *) audio_thread, btt);
                
            }

            fileDialogState.SelectFilePressed = false;
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);

        if (fileDialogState.windowActive) GuiLock();

        pthread_mutex_lock(&mutex2);
        int step = max(1, (int) cb->size / GetScreenWidth());
        int x = 0;
        for (int i = 0; i < GetScreenWidth(); i++) {
            int index = (cb->head + i * step) % cb->size;
            DrawLine(x, GetScreenHeight() / 8 - cb->buffer[index] * GetScreenHeight() / 8,
                     x + 1, GetScreenHeight() / 8 - cb->buffer[index + step] * GetScreenHeight() / 8,
                     RED);
            x++;
        }
        pthread_mutex_unlock(&mutex2);

        pthread_mutex_lock(&mutex1);
        int ret = snprintf(tempo_string, 32, "%f", last_tempo);
        pthread_mutex_unlock(&mutex1);
        if (ret < 0 || ret >= 32) {
            DrawText("ERROR", GetScreenWidth() / 4, 2 * GetScreenHeight() / 3, GetScreenWidth() / 10, RED);
        }
        else 
            DrawText((const char *) tempo_string, GetScreenWidth() / 4, 2 * GetScreenHeight() / 3, GetScreenWidth() / 10, GREEN);

        if (GuiButton((Rectangle){ 0, 0, 140, 30 }, GuiIconText(ICON_FILE_OPEN, "Open Wave File"))) fileDialogState.windowActive = true;
        
        if (GuiSlider((Rectangle){ 170, 150, 140, 20 }, "Autocorrelation Exponent", autocorr_exponent_str, &autocorr_exponent, 0.1, 2.0)) {
            pthread_mutex_lock(&mutex1);
            btt_set_autocorrelation_exponent(btt, autocorr_exponent);
            pthread_mutex_unlock(&mutex1);
            snprintf(autocorr_exponent_str, 32, "%f", autocorr_exponent);
        }


        GuiUnlock();

        GuiWindowFileDialog(&fileDialogState);

        EndDrawing();
    }

    stop_reading_flag = 1;
    if (!audio_thead_open)
        pthread_join(tempo_thread, NULL); 
    if (wav.is_open) {
        wave_close(&wav);
    }

    btt_destroy(btt);
    circular_buffer_free(cb);

    free(autocorr_exponent_str);

    CloseWindow();
    return 0;
}

void audio_thread(BTT *btt) {
    unsigned int buffer_size = 4;
    dft_sample_t buffer[buffer_size];
    float *samples[buffer_size];
    for (int i = 0; i < buffer_size; i++) {
        samples[i] = (float *) malloc(sizeof(float) * 2);
    }
    int ret = 0;

    int sample_rate = wav.header->sample_rate; 
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = (long) (buffer_size * 1e9 / sample_rate); // Convert sample rate to nanoseconds

    while (ret == WAVE_READ_SUCCESS && !stop_reading_flag) {
        ret = wave_read(&wav, buffer_size, samples);
        for (int i = 0; i < buffer_size - 1; i++) {
            buffer[i] = samples[i][0];
            pthread_mutex_lock(&mutex2);
            circular_buffer_push(cb, samples[i][0]);
            pthread_mutex_unlock(&mutex2);
        }
        
        btt_process(btt, buffer, buffer_size);
        pthread_mutex_lock(&mutex1);
        last_tempo = (float) btt_get_tempo_bpm(btt);
        pthread_mutex_unlock(&mutex1);

        // Wait for the calculated time interval before processing the next sample
        nanosleep(&ts, NULL);
    }
    for (int i = 0; i < buffer_size; i++) {
        free(samples[i]);
    }
    stop_reading_flag = 0;
    audio_thead_open = 1;
}