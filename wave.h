#ifndef _WAVE_H
#define _WAVE_H

#include <stdio.h>

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
	float *samples;				// Stored samples
	int is_open;
} WAVE;

/**
 * Init WAVE object
 * @param WAVE* wav - pointer to wave object
 **/
int wave_init(struct WAVE *wav);

/**
 * Open wave file pointer and save header data.
 * It also reads the audio samples, it calls
 * wave_read.
 * @param WAVE* wav - pointer to wave object
 * @param char* file_path - path to wave file
 **/
int wave_open(struct WAVE *wav, const char *file_path);

/**
 * Shortcut that allows you to pass a specific desired
 * sample rate for the samples. If it's different than
 * the one in the header, it will resample the audio.
 * It calls wave_open and wave_resample.
 * @param WAVE* wav - pointer to wave object
 * @param char* file_path - path to wave file
 * @param unsigned int desired_sample_rate - desired sample rate
 **/
int wave_open_resample(struct WAVE *wav, const char *file_path, unsigned int desired_sample_rate);

/**
 * Close wave object (release resources)
 * @param WAVE* wav - pointer to wave object
 **/
void wave_close(struct WAVE *wav);

/**
 * Read samples from wave file and store them in the
 * wave object. It will allocate memory for the samples
 * and store them in the samples array.
 * @param WAVE* wav - pointer to wave object
 **/
int wave_read(struct WAVE *wav);

/**
 * Resample audio to a different sample rate. It will
 * allocate memory for the new samples and replace the
 * pointer in the wave object. You will be able to get 
 * samples in the same way after calling this function.
 * @param WAVE* wav - pointer to wave object
 * @param int outSampleRate - desired sample rate
 **/
int wave_resample(struct WAVE *wav, int outSampleRate);

/**
 * Write wave file from wave object
 * @param WAVE* wav - pointer to wave object
 * @param char* file_path - path to wave file
 **/
int wave_write(struct WAVE *wav, const char *file_path);

/**
 * Get a copy of the audio samples from wave object
 * (you're responsible for freeing it). Remember that
 * the samples are interleaved (left channel, then right, then left...).
 * @param WAVE* wav - pointer to wave object
 **/
int wave_copy_samples(struct WAVE *wav, float *buffer);

#endif // _WAVE_H