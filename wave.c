/**
 * wave.c - Read a wave file and extract floating point samples
 *
 * @author Lluc Simó Margalef
 **/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "wave.h"

/**
 * Open wave file, read header and
 * open file pointer.
 * @param struct WAVE wav* - pointer to wave object
 * @param char *file_path - path to wave file
 **/
void wave_open(struct WAVE *wav, const char *file_path){

	unsigned char buffer4[4];
	unsigned char buffer2[2];

	// allocate memory for wave header
	wav->header = (struct HEADER*) malloc(sizeof(struct HEADER));
	if (wav->header == NULL) {
		printf("Error in malloc\n");
		exit(1);
	}
	// open file
	wav->ptr = fopen(file_path, "rb");
	if (wav->ptr == NULL) {
		printf("Error opening file\n");
		exit(1);
	}
	
	size_t read = fread(wav->header->riff, sizeof(wav->header->riff), 1, wav->ptr);

	read = fread(buffer4, sizeof(buffer4), 1, wav->ptr);

	// convert little endian to big endian 4 byte int
	wav->header->overall_size = buffer4[0] |
	(buffer4[1]<<8) |
	(buffer4[2]<<16) |
	(buffer4[3]<<24);

	read = fread(wav->header->wave, sizeof(wav->header->wave), 1, wav->ptr);

	read = fread(wav->header->fmt_chunk_marker, sizeof(wav->header->fmt_chunk_marker), 1, wav->ptr);

	read = fread(buffer4, sizeof(buffer4), 1, wav->ptr);

	// convert little endian to big endian 4 byte integer
	wav->header->length_of_fmt = buffer4[0] |
	(buffer4[1] << 8) |
	(buffer4[2] << 16) |
	(buffer4[3] << 24);

	read = fread(buffer2, sizeof(buffer2), 1, wav->ptr);

	wav->header->format_type = buffer2[0] | (buffer2[1] << 8);

	read = fread(buffer2, sizeof(buffer2), 1, wav->ptr);

	wav->header->channels = buffer2[0] | (buffer2[1] << 8);

	read = fread(buffer4, sizeof(buffer4), 1, wav->ptr);

	wav->header->sample_rate = buffer4[0] |
	(buffer4[1] << 8) |
	(buffer4[2] << 16) |
	(buffer4[3] << 24);

	read = fread(buffer4, sizeof(buffer4), 1, wav->ptr);

	wav->header->byterate = buffer4[0] |
	(buffer4[1] << 8) |
	(buffer4[2] << 16) |
	(buffer4[3] << 24);

	read = fread(buffer2, sizeof(buffer2), 1, wav->ptr);

	wav->header->block_align = buffer2[0] |
	(buffer2[1] << 8);

	read = fread(buffer2, sizeof(buffer2), 1, wav->ptr);

	wav->header->bits_per_sample = buffer2[0] |
	(buffer2[1] << 8);

	read = fread(wav->header->data_chunk_header, sizeof(wav->header->data_chunk_header), 1, wav->ptr);

	read = fread(buffer4, sizeof(buffer4), 1, wav->ptr);

	wav->header->data_size = buffer4[0] |
	(buffer4[1] << 8) |
	(buffer4[2] << 16) |
	(buffer4[3] << 24 );

	wav->num_samples = (8 * (wav->header->data_size - 8)) / (wav->header->channels * wav->header->bits_per_sample) - 8;

}

/**
 * Close wave file (release resources)
 * @param struct WAVE wav - wave object
 **/
void wave_close(struct WAVE *wav) {
	fclose(wav->ptr);
	free(wav->header);
}

/**
 * Read certain amount of samples from wave file
 * and return an array of samples
 * @param struct WAVE wav - wave object
 * @param num_samples - number of samples to read
 * @return samples - two dimensional array of float samples between -1 and 1
 **/
float** wave_read(struct WAVE *wav, unsigned int num_samples) {
	float **samples;
	samples = (float**) malloc(sizeof(float*) * num_samples);
	if (samples == NULL) {
		printf("Error in malloc\n");
		exit(1);
	}
	long size_of_each_sample = (wav->header->channels * wav->header->bits_per_sample) / 8;
	long i =0;
	char data_buffer[size_of_each_sample];
	int  size_is_correct = 1;

	// make sure that the bytes-per-sample is completely divisible by num.of channels
	long bytes_in_each_channel = (size_of_each_sample / wav->header->channels);
	if ((bytes_in_each_channel  * wav->header->channels) != size_of_each_sample) {
		size_is_correct = 0;
	}

	if (size_is_correct) { 
				// the valid amplitude range for values based on the bits per sample
		long low_limit = 0l;
		long high_limit = 0l;

		switch (wav->header->bits_per_sample) {
			case 8:
				low_limit = -128;
				high_limit = 127;
				break;
			case 16:
				low_limit = -32768;
				high_limit = 32767;
				break;
			case 32:
				low_limit = -2147483648;
				high_limit = 2147483647;
				break;
		}					

		for (i =1; i <= num_samples; i++) {
			samples[i-1] = (float*) malloc(sizeof(float) * wav->header->channels);
			if (samples[i-1] == NULL) {
					printf("Error in malloc\n");
					exit(1);
			}
			size_t read = fread(data_buffer, sizeof(data_buffer), 1, wav->ptr);
			if (read == 1) {
			
				// dump the data read
				unsigned int  xchannels = 0;
				int data_in_channel = 0;
				int offset = 0; // move the offset for every iteration in the loop below
				for (xchannels = 0; xchannels < wav->header->channels; xchannels ++ ) {
					// convert data from little endian to big endian based on bytes in each channel sample
					if (bytes_in_each_channel == 4) {
						data_in_channel = (data_buffer[offset] & 0x00ff) | 
											((data_buffer[offset + 1] & 0x00ff) <<8) | 
											((data_buffer[offset + 2] & 0x00ff) <<16) | 
											(data_buffer[offset + 3]<<24);
					}
					else if (bytes_in_each_channel == 2) {
						data_in_channel = (data_buffer[offset] & 0x00ff) | (data_buffer[offset + 1] << 8);
					}
					else if (bytes_in_each_channel == 1) {
						data_in_channel = data_buffer[offset] & 0x00ff;
						data_in_channel -= 128; //in wave, 8-bit are unsigned, so shifting to signed
					}

					offset += bytes_in_each_channel;		
					float sample = (float) data_in_channel / high_limit;
					samples[i-1][xchannels] = sample;

					// check if value was in range
					if (data_in_channel < low_limit || data_in_channel > high_limit)
						printf("**value out of range\n");
				}
			}
			else {
				// printf("Error reading file. %d bytes. %d samples read.\n", read, i);
				break;
			}

		} 
	} else {
		printf("Error in size of sample\n");
	}
	if (i<num_samples) {
		printf("End of file: %d samples requested, %ld samples read (A difference of %ld).\n", num_samples, i, num_samples - i);
	}

	return samples;
}
