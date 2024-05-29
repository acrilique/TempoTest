#ifndef _WAVE_H
#define _WAVE_H

#include <stdio.h>

// WAVE file header format

enum _WAVE_READ_RETURN {
	WAVE_READ_SUCCESS = 0,
	WAVE_READ_EOF = -1, 
	WAVE_READ_ERROR = -2
};

typedef int WAVE_READ_RETURN;

struct _WAVE_READ_RETURN_DESC {
	int code;
	char *message;
};
extern struct _WAVE_READ_RETURN_DESC WAVE_READ_RETURN_DESC[];

struct HEADER {
	unsigned char riff[4];						// RIFF string
	unsigned int overall_size	;				// overall size of file in bytes
	unsigned char wave[4];						// WAVE string
	unsigned char fmt_chunk_marker[4];			// fmt string with trailing null char
	unsigned int length_of_fmt;					// length of the format data
	unsigned int format_type;					// format type. 1-PCM, 3- IEEE float, 6 - 8bit A law, 7 - 8bit mu law
	unsigned int channels;						// no.of channels
	unsigned int sample_rate;					// sampling rate (blocks per second)
	unsigned int byterate;						// SampleRate * NumChannels * BitsPerSample/8
	unsigned int block_align;					// NumChannels * BitsPerSample/8
	unsigned int bits_per_sample;				// bits per sample, 8- 8bits, 16- 16 bits etc
	unsigned char data_chunk_header [4];		// DATA string or FLLR string
	unsigned int data_size;						// NumSamples * NumChannels * BitsPerSample/8 - size of the next chunk that will be read
};

typedef struct WAVE {
	struct HEADER *header;						// HEADER struct
	FILE *ptr;												// Struct to hold pointer to a FILE
	unsigned long num_samples;				// Number of samples in the audio file (a sample includes left and right channel)
	float **resampled_samples;				// Resampled samples
	int is_open;
} WAVE;

int wave_init(struct WAVE *wav);

int wave_open(struct WAVE *wav, const char *file_path);

void wave_close(struct WAVE *wav);

int wave_read(struct WAVE *wav, unsigned int num_samples, float **samples);

unsigned long wave_resample(struct WAVE *wav, float *output, int inSampleRate, int outSampleRate, unsigned long inputSize, unsigned int channels);

#endif // _WAVE_H