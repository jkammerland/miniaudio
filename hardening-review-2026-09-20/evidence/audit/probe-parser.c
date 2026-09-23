#define MINIAUDIO_IMPLEMENTATION
#include "/mnt/d2/agent-workspace/repos/miniaudio/miniaudio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { ma_decoder memory; unsigned seeks; } probe;
static ma_result read_probe(ma_decoder* decoder, void* out, size_t n, size_t* read) {
    probe* p = (probe*)decoder->pUserData;
    return ma_decoder__on_read_memory(&p->memory, out, n, read);
}
static ma_result seek_probe(ma_decoder* decoder, ma_int64 n, ma_seek_origin origin) {
    probe* p = (probe*)decoder->pUserData;
    ++p->seeks;
    if (p->seeks > 32) return MA_BAD_SEEK;
    fprintf(stderr, "seek=%u pos=%zu offset=%lld origin=%d\n", p->seeks, p->memory.data.memory.currentReadPos, (long long)n, (int)origin);
    return ma_decoder__on_seek_memory(&p->memory, n, origin);
}
int main(int argc, char** argv) {
    FILE* file;
    unsigned char* data;
    long n;
    ma_decoder decoder;
    ma_dr_wav wav;
    ma_result result;
    probe p;
    if (argc != 3) return 2;
    file = fopen(argv[2], "rb"); if (!file) return 3;
    fseek(file, 0, SEEK_END); n = ftell(file); rewind(file);
    if (n < 0 || n > 1024*1024) return 4;
    data = malloc((size_t)n); if (!data) return 5;
    if (fread(data, 1, (size_t)n, file) != (size_t)n) return 6;
    fclose(file);
    memset(&p, 0, sizeof(p));
    p.memory.data.memory.pData = data;
    p.memory.data.memory.dataSize = (size_t)n;
    if (strcmp(argv[1], "bounded") == 0) result = ma_decoder_init(read_probe, seek_probe, &p, NULL, &decoder);
    else if (strcmp(argv[1], "file") == 0) result = ma_decoder_init_file(argv[2], NULL, &decoder);
    else if (strcmp(argv[1], "memory") == 0) result = ma_decoder_init_memory(data, (size_t)n, NULL, &decoder);
    else if (strcmp(argv[1], "direct") == 0) {
        int ok = ma_dr_wav_init_memory(&wav, data, (size_t)n, NULL);
        if (ok) ma_dr_wav_uninit(&wav);
        printf("direct init=%d\n", ok); free(data); return 0;
    } else return 7;
    printf("result=%d seeks=%u\n", result, p.seeks);
    if (result == MA_SUCCESS) ma_decoder_uninit(&decoder);
    free(data); return 0;
}
