#include <lazygsf.h>
#include <psflib.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* demo program that renders a GSF file into a WAV */

#define SAMPLE_RATE 48000
#define BUFFER_SIZE 1024

int16_t buffer[BUFFER_SIZE * 2];
uint8_t packed[BUFFER_SIZE * 4];

static unsigned int length = 0;
static unsigned int fade = 0;

/* global variable for the current pcm frame */
uint64_t frame_no = 0;

/* global variable - total pcm frames */
uint64_t frame_total = 0;

/* global variable - frame to start fading on */
uint64_t frame_fade = 0;

static int
write_wav_header(FILE *f);

static void
pack_frames(void);

static void
pack_int16le(uint8_t *d, int16_t n);

static void
pack_uint16le(uint8_t *d, uint16_t n);

static void
pack_uint32le(uint8_t *d, uint32_t n);

static const char * const gsf2wav_separators = "\\/:|";

static void *
gsf2wav_fopen(void *userdata, const char *filename) {
    (void)userdata;
    return fopen(filename,"rb");
}

static size_t
gsf2wav_fread(void *buffer, size_t size, size_t count, void *handle) {
    return fread(buffer,size,count,(FILE *)handle);
}

static int
gsf2wav_fseek(void *handle, int64_t offset, int whence) {
    return fseek((FILE *)handle,offset,whence);
}

static int
gsf2wav_fclose(void *handle) {
    return fclose((FILE *)handle);
}

static long
gsf2wav_ftell(void *handle) {
    return ftell((FILE *)handle);
}

/**
 * parses H:M:S into milliseconds
 */
static unsigned int
gsf2wav_parse_time(const char *ts)
{
	unsigned int i = 0;
	unsigned int t = 0;
	unsigned int c = 0;
	unsigned int m = 1000;
	for(i=0;i<strlen(ts);i++)
	{
		if(ts[i] == ':')
		{
			t *= 60;
			t += c*60;
			c = 0;
		}
		else if(ts[i] == '.') {
			m = 1;
			t += c;
			c = 0;
		}
		else
		{
			if(ts[i] < 48 || ts[i] > 57)
			{
				return 0;
			}
			c *= 10;
			c += (ts[i] - 48) * m;
		}
	}
	return c + t;
}

static int
gsf2wav_tag_handler(void *ctx, const char *name, const char *value) {
    printf("tag: %s=%s\n",name,value);

    if(strcmp(name,"length") == 0) {
        length = gsf2wav_parse_time(value);
    } else if(strcmp(name,"fade") == 0) {
        fade = gsf2wav_parse_time(value);
    }

    return 0;
}


static const psf_file_callbacks gsf2wav_psf_stdio = {
    gsf2wav_separators,
    NULL,
    gsf2wav_fopen,
    gsf2wav_fread,
    gsf2wav_fseek,
    gsf2wav_fclose,
    gsf2wav_ftell,
};

static void
apply_fade(void) {
    uint64_t i = 0;
    float fade;
    for(i=0;i<BUFFER_SIZE;i++) {
        if(frame_no + i > frame_total) {
            buffer[(i*2)+0] = 0;
            buffer[(i*2)+1] = 0;
        }
        if(frame_no + i > frame_fade) {
            fade = ((float)(frame_total - frame_no - i)) / ((float)(frame_total - frame_fade));
            fade *= fade;
            buffer[(i*2) + 0] *= fade;
            buffer[(i*2) + 1] *= fade;
        }
    }
}

static int gsf2wav_load(void *context, const uint8_t *exe, size_t exe_size, const uint8_t *reserved, size_t reserved_size) {
    (void)reserved;
    (void)reserved_size;
    return gsf_upload_section((gsf_state_t *)context,exe,exe_size);
}

int main(int argc, const char *argv[]) {
    gsf_state_t *gsf = NULL;
    FILE *out = NULL;

    if(argc < 3) {
        printf("Usage: %s /path/to/minigsf /path/to/output.wav\n",
        argv[0]);
        return 1;
    }

    gsf_init();

    gsf = malloc(gsf_get_state_size());
    if(gsf == NULL) abort();
    gsf_clear(gsf);

    if(psf_load(argv[1],
        &gsf2wav_psf_stdio,0x22,gsf2wav_load,gsf,
        gsf2wav_tag_handler,NULL,0,
        NULL,NULL) <= 0) {
        free(gsf);
        return 1;
    }
    gsf_set_sample_rate(gsf,SAMPLE_RATE);

    out = fopen(argv[2],"wb");
    if(out == NULL) {
        gsf_shutdown(gsf);
        free(gsf);
        return 1;
    }

    frame_total = length;
    frame_fade = frame_total;
    frame_total += fade;

    frame_fade *= SAMPLE_RATE;
    frame_fade /= 1000;

    frame_total *= SAMPLE_RATE;
    frame_total /= 1000;

    write_wav_header(out);

    for(frame_no=0;frame_no<frame_total; frame_no += BUFFER_SIZE) {
        gsf_render(gsf,buffer,BUFFER_SIZE);
        apply_fade();
        pack_frames();
        fwrite(packed,4, BUFFER_SIZE, out);
    }

    gsf_shutdown(gsf);
    free(gsf);
    fclose(out);
    return 0;
}

static int write_wav_header(FILE *f) {
    unsigned int data_size = (unsigned int)frame_total * 4;
    uint8_t tmp[4];
    if(fwrite("RIFF",1,4,f) != 4) return 1;
    pack_uint32le(tmp, 4 + ( 8 + data_size ) + (8 + 40) );
    if(fwrite(tmp,1,4,f) != 4) return 1;

    if(fwrite("WAVE",1,4,f) != 4) return 1;
    if(fwrite("fmt ",1,4,f) != 4) return 1;

    /* fmtSize
     * 16 = standard wave
     * 40 = extensible
     */
    pack_uint32le(tmp,16);
    if(fwrite(tmp,1,4,f) != 4) return 1;

    /* audioFormat:
     * 1 = PCM
     * 3 = float
     * 6 = alaw
     * 7 = ulaw
     * 0xfffe = extensible */
    pack_uint16le(tmp,1);
    if(fwrite(tmp,1,2,f) != 2) return 1;

    /* numChannels */
    pack_uint16le(tmp,2);
    if(fwrite(tmp,1,2,f) != 2) return 1;

    /* sampleRate */
    pack_uint32le(tmp,SAMPLE_RATE);
    if(fwrite(tmp,1,4,f) != 4) return 1;

    /* dataRate (bytes per second) */
    pack_uint32le(tmp,SAMPLE_RATE * 4);
    if(fwrite(tmp,1,4,f) != 4) return 1;

    /* block alignment (channels * sample size) */
    pack_uint16le(tmp,4);
    if(fwrite(tmp,1,2,f) != 2) return 1;

    /* bits per sample */
    pack_uint16le(tmp,16);
    if(fwrite(tmp,1,2,f) != 2) return 1;

    if(fwrite("data",1,4,f) != 4) return 1;

    pack_uint32le(tmp,data_size);
    if(fwrite(tmp,1,4,f) != 4) return 1;

    return 0;
}

static void pack_frames(void) {
    unsigned int i = 0;
    while(i < BUFFER_SIZE) {
        pack_int16le(&packed[(i*4)+0],buffer[(i*2)+0]);
        pack_int16le(&packed[(i*4)+2],buffer[(i*2)+1]);
        i++;
    }
}

static void pack_int16le(uint8_t *d, int16_t n) {
    d[0] = (uint8_t)((uint16_t) n      );
    d[1] = (uint8_t)((uint16_t) n >> 8 );
}

static void pack_uint16le(uint8_t *d, uint16_t n) {
    d[0] = (uint8_t)((uint16_t) n      );
    d[1] = (uint8_t)((uint16_t) n >> 8 );
}

static void pack_uint32le(uint8_t *d, uint32_t n) {
    d[0] = (uint8_t)(n      );
    d[1] = (uint8_t)(n >> 8 );
    d[2] = (uint8_t)(n >> 16);
    d[3] = (uint8_t)(n >> 24);
}
