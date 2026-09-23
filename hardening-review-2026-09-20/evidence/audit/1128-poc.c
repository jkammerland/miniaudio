#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <crash_input>\n", argv[0]);
        return 2;
    }

    FILE *fp = fopen(argv[1], "rb");
    if (!fp) {
        perror("fopen");
        return 2;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(fp);
        return 2;
    }
    long file_size = ftell(fp);
    if (file_size < 0) {
        perror("ftell");
        fclose(fp);
        return 2;
    }
    rewind(fp);

    size_t size = (size_t)file_size;
    uint8_t *buf = (uint8_t *)malloc(size);
    if (!buf) {
        fclose(fp);
        return 2;
    }
    if (fread(buf, 1, size, fp) != size) {
        perror("fread");
        free(buf);
        fclose(fp);
        return 2;
    }
    fclose(fp);

    ma_decoder decoder;
    ma_result result = ma_decoder_init_memory(buf, size, NULL, &decoder);
    if (result == MA_SUCCESS) {
        uint8_t temp[4096];
        ma_decoder_read_pcm_frames(&decoder, temp, 100, NULL);
        ma_decoder_uninit(&decoder);
    }

    free(buf);
    return 0;
}
