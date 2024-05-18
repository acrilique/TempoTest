#include <stdio.h>
#include <stdlib.h>
#include "lib/Beat-and-Tempo-Tracking/BTT.h"
#include "wave.h"

int main()
{
    BTT *btt = btt_new_default();
    WAVE wav;

    int buffer_size = 4;
    dft_sample_t buffer[buffer_size];

    float last_tempo = 0;

    wave_open(&wav, "../../../Music/soundtrack.wav");

    btt_set_tracking_mode(btt, BTT_ONSET_AND_TEMPO_TRACKING);
    btt_set_gaussian_tempo_histogram_decay(btt, 0.999);

    unsigned int num_samples = wav.num_samples;
    unsigned int cont = 0;

    while (num_samples > cont)
    {
    
	float **samples = wave_read(&wav, buffer_size);
	for (int i = 0; i < buffer_size; i++){
	    buffer[i] = samples[0][i];
	    cont = cont + 1;
	}
	    free(samples);
	    btt_process(btt, buffer, buffer_size);
	    float actual_tempo = btt_get_tempo_bpm(btt);
        if (actual_tempo != last_tempo) {
            printf("Tempo changed! (Old: %f, New: %f). Confidence of %f. Samples read: %d\n",last_tempo, actual_tempo, btt_get_tempo_certainty(btt), cont);
            last_tempo = actual_tempo;
        }
    }

    return 0;
}
