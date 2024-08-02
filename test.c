#include "lib/Beat-and-Tempo-Tracking/BTT.h"
#include <stdio.h>

#define MINIAUDIO_IMPLEMENTATION
#include "lib/miniaudio.h"

double tempo = 0.0;

int main(int argc, char *argv[]) {

    if (argc < 2) {
        printf("Usage: %s <audio file>\n", argv[0]);
        return 1;
    }

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, 44100);
    ma_decoder decoder;
    if (ma_decoder_init_file(argv[1], &config, &decoder) != MA_SUCCESS) {
        printf("Failed to load audio file\n");
        return 1;
    }
    /* instantiate a new object */
    BTT *btt = btt_new_default();

    int buffer_size = 64;
    ma_float pFrames[buffer_size * 2];
    ma_float buffer[buffer_size];

    for (;;) {

        ma_uint64 framesRead;
        ma_result result = ma_decoder_read_pcm_frames(&decoder, pFrames, (ma_uint64) buffer_size, &framesRead);
        if (framesRead < buffer_size) {
            printf("End of file\n");
            ma_decoder_uninit(&decoder);
            break;
        }

        for (int i = 0; i < buffer_size; i++) {
            buffer[i] = pFrames[i * 2];
            printf("%f\n", buffer[i]);
        }

        btt_process(btt, buffer, buffer_size);
        double new_tempo = btt_get_tempo_bpm(btt);
        if (new_tempo != tempo) {
            tempo = new_tempo;
            printf("Tempo: %f\n", tempo);
        }

        ma_sleep(buffer_size * 1000 / 44100);
    }

    return 0;
}
