#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "lib/Beat-and-Tempo-Tracking/BTT.h"

#include "wave.h"

#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "../../lib/raylib/examples/shapes/raygui.h"

#undef RAYGUI_IMPLEMENTATION            // Avoid including raygui implementation again
#define GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION
#include "lib/raygui/examples/custom_file_dialog/gui_window_file_dialog.h"

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex2 = PTHREAD_MUTEX_INITIALIZER;
pthread_t tempo_thread;

WAVE wav;
float last_tempo = 0;
int stop_reading_flag = 0;

void update_tempo(BTT *btt);

int main()
{
    BTT *btt = btt_new_default();
    char tempo_string[32];

    wave_init(&wav);

    btt_set_tracking_mode(btt, BTT_ONSET_AND_TEMPO_TRACKING);
    btt_set_gaussian_tempo_histogram_decay(btt, 0.999);

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
                
                pthread_create(&tempo_thread, NULL, (void *) update_tempo, btt);
                
            } else {
                DrawText("File not supported", 10, 10, 20, RED);
            }

            fileDialogState.SelectFilePressed = false;
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);

        pthread_mutex_lock(&mutex1);
        int ret = snprintf(tempo_string, 32, "%f", last_tempo);
        pthread_mutex_unlock(&mutex1);
        
        if (ret < 0 || ret >= 32) {
            DrawText("ERROR", GetScreenWidth() / 4, 2 * GetScreenHeight() / 3, GetScreenWidth() / 10, RED);
        }
        else 
            DrawText((const char *) tempo_string, GetScreenWidth() / 4, 2 * GetScreenHeight() / 3, GetScreenWidth() / 10, GREEN);

        if (fileDialogState.windowActive) GuiLock();

        if (GuiButton((Rectangle){ 0, 0, 140, 30 }, GuiIconText(ICON_FILE_OPEN, "Open Wave File"))) fileDialogState.windowActive = true;

        GuiUnlock();

        GuiWindowFileDialog(&fileDialogState);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}

void update_tempo(BTT *btt) {

    unsigned int buffer_size = 4;
    dft_sample_t buffer[buffer_size];
    float *samples[buffer_size];
    for (int i = 0; i < buffer_size; i++) {
        samples[i] = (float *) malloc(sizeof(float) * 2);
    }
    int ret = 0;
    while (ret == WAVE_READ_SUCCESS && !stop_reading_flag) {
        ret = wave_read(&wav, buffer_size, samples);
        for (int i = 0; i < buffer_size - 1; i++) {
            buffer[i] = samples[i][0];
        }
        btt_process(btt, buffer, buffer_size);
        pthread_mutex_lock(&mutex1);
        last_tempo = (float) btt_get_tempo_bpm(btt);
        pthread_mutex_unlock(&mutex1);
    }
    stop_reading_flag = 0;
}