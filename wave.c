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

void int2le16bit(unsigned char *bytes, unsigned int value) {
	bytes[0] = value & 0xff;
	bytes[1] = (value >> 8) & 0xff;
	return;
}

void int2le32bit(unsigned char *bytes, unsigned int value) {
	bytes[0] = value & 0xff;
	bytes[1] = (value >> 8) & 0xff;
	bytes[2] = (value >> 16) & 0xff;
	bytes[3] = (value >> 24) & 0xff;
	return;
}

int wave_init(struct WAVE *wav){
	wav = (struct WAVE*) malloc(sizeof(struct WAVE));
	wav->is_open = 0;
	return 0;
}

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

	read = fread(buffer2, 2, 1, wav->ptr); /* Format type. 0x0001: PCM, 0x0003: IEEE float */
	wav->header->format_type = le2int16bit(buffer2); /* 0x0006: A-law, 0x0007: mu-law, 0xFFFE: Extensible */

	read = fread(buffer2, 2, 1, wav->ptr); /* Number of channels */
	wav->header->channels = le2int16bit(buffer2);

	read = fread(buffer4, sizeof(buffer4), 1, wav->ptr); /* Sample rate */
	wav->header->sample_rate = le2int32bit(buffer4);

	read = fread(buffer4, sizeof(buffer4), 1, wav->ptr); /* Average bytes per second */
	wav->header->byterate = le2int32bit(buffer4); /* Samplerate x bytes per sample x Num. channels */

	read = fread(buffer2, 2, 1, wav->ptr); /* Block align */
	wav->header->block_align = le2int16bit(buffer2); /* Bytes per sample x Num. channels */

	/* Now we will check if this file is PCM or not, and parse the extra fields if necessary. */
	if (wav->header->length_of_fmt > 16) {
		read = fread(buffer2, 2, 1, wav->ptr); 
	
		if (wav->header->length_of_fmt > 18) { /* Assume it has length 40. */
			
		}
	} 

	read = fread(buffer2, 2, 1, wav->ptr);
	wav->header->bits_per_sample = le2int16bit(buffer2);

	read = fread(wav->header->data_chunk_header, sizeof(wav->header->data_chunk_header), 1, wav->ptr);

	read = fread(buffer4, sizeof(buffer4), 1, wav->ptr);
	wav->header->data_size = le2int32bit(buffer4);

	wav->num_samples = (8 * (wav->header->data_size - 8)) / (wav->header->channels * wav->header->bits_per_sample) - 8;

	wav->is_open = 1;

	wave_read(wav);
	
	return 0;
}

int wave_open_resample(struct WAVE *wav, const char *file_path, unsigned int desired_sample_rate) {
	wave_open(wav, file_path);

	if (desired_sample_rate != wav->header->sample_rate)
		wave_resample(wav, desired_sample_rate);

	return 0;
}

void wave_close(struct WAVE *wav) {
	fclose(wav->ptr);
	free(wav->header);
	free(wav->samples);
	wav->is_open = 0;
}

int wave_read(struct WAVE *wav) {

	wav->samples = (float *) malloc(sizeof(float) * wav->num_samples * wav->header->channels);
	if (wav->samples == NULL) {
		return WAVE_READ_ERROR;
	}

	long i =0;
	int  size_is_correct = 1;
	unsigned long num_samples = wav->num_samples;
	unsigned int channels = wav->header->channels;
	long size_of_each_sample = (channels * wav->header->bits_per_sample) / 8;
	char data_buffer[size_of_each_sample];

	// make sure that the bytes-per-sample is completely divisible by num.of channels
	long bytes_in_each_channel = (size_of_each_sample / channels);
	if ((bytes_in_each_channel  * channels) != size_of_each_sample) {
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
				for (xchannels = 0; xchannels < channels; xchannels ++ ) {
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
					// ensure value is in range
					if (data_in_channel < low_limit) data_in_channel = low_limit;
					else if (data_in_channel > high_limit) data_in_channel = high_limit;
					
					float sample = (float) data_in_channel / (high_limit + 1);
					wav->samples[i * channels + xchannels] = sample;

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

int wave_resample(struct WAVE *wav, int outSampleRate) {
    double ratio = (double)outSampleRate / wav->header->sample_rate;
    unsigned long new_num_samples = (unsigned long)(wav->num_samples * ratio);
    float *output = (float *)malloc(sizeof(float) * new_num_samples * wav->header->channels);

    for (unsigned long i = 0; i < new_num_samples; i++) {
        double src_index = i / ratio; 
        unsigned long index_part = (unsigned long)src_index;
        double frac_part = src_index - index_part;

        for (unsigned int c = 0; c < wav->header->channels; c++) {
            // Handle potential out-of-bounds access at the very end
            if (index_part + 1 >= wav->num_samples) {
                output[i * wav->header->channels + c] = wav->samples[(wav->num_samples - 1) * wav->header->channels + c]; 
            } else {
                float sample1 = wav->samples[index_part * wav->header->channels + c];
                float sample2 = wav->samples[(index_part + 1) * wav->header->channels + c];
                // Linear interpolation
                output[i * wav->header->channels + c] = sample1 + (sample2 - sample1) * frac_part;
            }
        }
    }

    wav->num_samples = new_num_samples;
    wav->header->sample_rate = outSampleRate;

    free(wav->samples);
    wav->samples = output;

    return 0;
}

int wave_write(struct WAVE *wav, const char *file_path) {
	FILE *file = fopen(file_path, "wb");
	if (file == NULL) {
		return 1;
	}
	unsigned char buffer1[4];
	unsigned char buffer2[2];

	fwrite(wav->header->riff, sizeof(wav->header->riff), 1, file);
	int2le32bit(buffer1, wav->header->overall_size);
	fwrite(buffer1, 4, 1, file);
	fwrite(wav->header->wave, sizeof(wav->header->wave), 1, file);
	fwrite(wav->header->fmt_chunk_marker, sizeof(wav->header->fmt_chunk_marker), 1, file);
	int2le32bit(buffer1, wav->header->length_of_fmt);
	fwrite(buffer1, 4, 1, file);
	int2le16bit(buffer2, wav->header->format_type);
	fwrite(buffer2, 2, 1, file);
	int2le16bit(buffer2, wav->header->channels);
	fwrite(buffer2, 2, 1, file);
	int2le32bit(buffer1, wav->header->sample_rate);
	fwrite(buffer1, 4, 1, file);
	int2le32bit(buffer1, wav->header->byterate);
	fwrite(buffer1, 4, 1, file);
	int2le16bit(buffer2, wav->header->block_align);
	fwrite(buffer2, 2, 1, file);
	int2le16bit(buffer2, wav->header->bits_per_sample);
	fwrite(buffer2, 2, 1, file);
	fwrite(wav->header->data_chunk_header, sizeof(wav->header->data_chunk_header), 1, file);
	int2le32bit(buffer1, wav->header->data_size);
	fwrite(buffer1, 4, 1, file);
	
	for (unsigned long i = 0; i < wav->num_samples; i += 1) {
		for (unsigned int c = 0; c < wav->header->channels; c += 1) {
			if (wav->header->bits_per_sample == 8) {
				buffer1[0] = (char) ((wav->samples[i * wav->header->channels + c] + 1.0) * 128);
				fwrite(buffer1, 1, 1, file);
			} else if (wav->header->bits_per_sample == 16) {
				int2le16bit(buffer2, (unsigned int) ((wav->samples[i * wav->header->channels + c]) * 32768 - 1.0));
				fwrite(buffer2, 2, 1, file);
			} else if (wav->header->bits_per_sample == 32) {
				int2le32bit(buffer1, (unsigned int) ((wav->samples[i * wav->header->channels + c]) * 2147483648 - 1.0));
				fwrite(buffer1, 4, 1, file);
			}
		}
	}

	fclose(file);
	return 0;
}

int wave_copy_samples(struct WAVE *wav, float *buffer) {
	if (buffer == NULL) {
		return WAVE_READ_ERROR;
	}
	for (unsigned long i = 0; i < wav->num_samples * wav->header->channels; i += 1) {
		buffer[i] = wav->samples[i];
	}
	return WAVE_READ_SUCCESS;
}