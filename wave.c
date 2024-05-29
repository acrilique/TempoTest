/**
 * wave.c - Read a wave file and extract floating point samples
 *
 * @author Lluc Simó Margalef
 **/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "wave.h"

struct _WAVE_READ_RETURN_DESC WAVE_READ_RETURN_DESC[] = {
		{ WAVE_READ_SUCCESS, "We managed to read all the samples requested, without errors. (Could be END OF FILE)." },
		{ WAVE_READ_EOF, "Unexpected END OF FILE. We read less samples than requested." },
		{ WAVE_READ_ERROR, "Memory allocation error or error in the header of the .wav file." }
};

unsigned int le2int16bit(unsigned char *bytes) {
	return bytes[0] | (bytes[1] << 8);
}

unsigned int le2int32bit(unsigned char *bytes) {
	return bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
}

/**
 * Init WAVE object
 * @param struct WAVE wav* - pointer to wave object
 **/
int wave_init(struct WAVE *wav){
	wav->is_open = 0;
	return 0;
}

/**
 * Open wave file pointer and read its header
 * until the beggining of the data section
 * (ready to read samples).
 * @param struct WAVE wav* - pointer to wave object
 * @param char *file_path - path to wave file
 **/
int wave_open(struct WAVE *wav, const char *file_path){

	unsigned char buffer4[4];
	unsigned char buffer2[2];

	wav->header = (struct HEADER*) malloc(sizeof(struct HEADER)); /* Allocation to save header data. */
	if (wav->header == NULL) {
		printf("Error in malloc\n");
		return 1;
	}

	wav->ptr = fopen(file_path, "rb"); /* Open file in binary mode */
	if (wav->ptr == NULL) {
		printf("Error opening file: %s\n", file_path);
		return 1;
	}
	

	size_t read = fread(wav->header->riff, sizeof(wav->header->riff), 1, wav->ptr); /* Read "RIFF" */

	read = fread(buffer4, sizeof(buffer4), 1, wav->ptr); /* ChunkSize represents the size of the entire file minus 8 bytes. */
	wav->header->overall_size = le2int32bit(buffer4); /* Those 8 bytes are th size of the RIFF header + this field. */

	read = fread(wav->header->wave, sizeof(wav->header->wave), 1, wav->ptr); /* Read "WAVE" */

	read = fread(wav->header->fmt_chunk_marker, sizeof(wav->header->fmt_chunk_marker), 1, wav->ptr); /* Read "fmt " */

	read = fread(buffer4, sizeof(buffer4), 1, wav->ptr); /* Length of format chunk */
	wav->header->length_of_fmt = le2int32bit(buffer4); /* 16: PCM, 18: Non-PCM, 40: Extensible */

	read = fread(buffer2, sizeof(buffer2), 1, wav->ptr); /* Format type. 0x0001: PCM, 0x0003: IEEE float */
	wav->header->format_type = le2int16bit(buffer2); /* 0x0006: A-law, 0x0007: mu-law, 0xFFFE: Extensible */

	read = fread(buffer2, sizeof(buffer2), 1, wav->ptr); /* Number of channels */
	wav->header->channels = le2int16bit(buffer2);

	read = fread(buffer4, sizeof(buffer4), 1, wav->ptr); /* Sample rate */
	wav->header->sample_rate = le2int32bit(buffer4);

	read = fread(buffer4, sizeof(buffer4), 1, wav->ptr); /* Average bytes per second */
	wav->header->byterate = le2int32bit(buffer4); /* Samplerate x bytes per sample x Num. channels */

	read = fread(buffer2, sizeof(buffer2), 1, wav->ptr); /* Block align */
	wav->header->block_align = le2int16bit(buffer2); /* Bytes per sample x Num. channels */

	/* Now we will check if this file is PCM or not, and parse the extra fields if necessary. */
	if (wav->header->length_of_fmt > 16) {
		read = fread(buffer2, sizeof(buffer2), 1, wav->ptr); 
	
		if (wav->header->length_of_fmt > 18) { /* Assume it has length 40. */
			
		}
	} 


	read = fread(buffer2, sizeof(buffer2), 1, wav->ptr);
	wav->header->bits_per_sample = le2int16bit(buffer2);

	read = fread(wav->header->data_chunk_header, sizeof(wav->header->data_chunk_header), 1, wav->ptr);

	read = fread(buffer4, sizeof(buffer4), 1, wav->ptr);
	wav->header->data_size = le2int32bit(buffer4);

	wav->num_samples = (8 * (wav->header->data_size - 8)) / (wav->header->channels * wav->header->bits_per_sample) - 8;

	wav->is_open = 1;

	return 0;
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
 * and return an array of samples. Samples should be
 * pre-allocated like this:
 * float *samples[num_samples];
 * for (int i = 0; i < num_samples; i++) {
 * 	samples[i] = (float *) malloc(sizeof(float) * num_channels);
 * }
 * @param wav WAVE *wav - pointer to wave object
 * @param num_samples unsigned int - number of samples to read
 * @param samples float ** - two dimensional array of float samples (allocated!)
 * @return int - indicates how the read ended. For the return codes, check WAVE_READ_RETURN_DESC
 **/
int wave_read(struct WAVE *wav, unsigned int num_samples, float **samples) {

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

		for (i = 0; i < num_samples; i++) {

			size_t read = fread(data_buffer, sizeof(data_buffer), 1, wav->ptr);
			if (read == 1) {
			
				// dump the data read
				unsigned int xchannels = 0;
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
					samples[i][xchannels] = sample;

					// ensure value is in range
					if (data_in_channel < low_limit) data_in_channel = low_limit;
					else if (data_in_channel > high_limit) data_in_channel = high_limit;
				}
			}
			else {
				//printf("Error reading file. %u samples requested. %d samples read.\n", num_samples, i);
				break;
			}

		} 
	} else {
		// printf("Error in size of sample\n");
		return WAVE_READ_ERROR;
	}
	if (i<num_samples) {
		return WAVE_READ_EOF;
	}
	return WAVE_READ_SUCCESS;
}

unsigned long wave_resample(const float *input, float *output, int inSampleRate, int outSampleRate, unsigned long inputSize, unsigned int channels) {

  if (input == NULL)
    return 0;
  unsigned long outputSize = (unsigned long) (inputSize * (double) outSampleRate / (double) inSampleRate);
  outputSize -= outputSize % channels;
  if (output == NULL)
    return outputSize;
  double stepDist = ((double) inSampleRate / (double) outSampleRate);
  const unsigned long fixedFraction = (1LL << 32);
  const double normFixed = (1.0 / (1LL << 32));
  unsigned long step = ((unsigned long) (stepDist * fixedFraction + 0.5));
  unsigned long curOffset = 0;
  for (unsigned int i = 0; i < outputSize; i += 1) {
    for (unsigned int c = 0; c < channels; c += 1) {
      *output++ = (float) (input[c] + (input[c + channels] - input[c]) * ((double) (curOffset >> 32) + ((curOffset & (fixedFraction - 1)) * normFixed)));
		}
		curOffset += step;
		input += (curOffset >> 32) * channels;
		curOffset &= (fixedFraction - 1);
	}
	return outputSize;
}