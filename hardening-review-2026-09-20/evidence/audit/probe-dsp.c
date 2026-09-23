#define MINIAUDIO_IMPLEMENTATION
#include "/mnt/d2/agent-workspace/repos/miniaudio/miniaudio.h"
#include <stdio.h>
#include <math.h>

static int gain_probe(unsigned channels, unsigned smooth, unsigned block) {
    ma_gainer gain;
    ma_gainer_config config = ma_gainer_config_init(channels, smooth);
    float input[40*33], output[40*33];
    unsigned i, c, bad = 0;
    if (ma_gainer_init(&config, NULL, &gain) != MA_SUCCESS) return 1;
    for (i = 0; i < 40*33; ++i) input[i] = 1, output[i] = -999;
    ma_gainer_process_pcm_frames(&gain, output, input, smooth);
    ma_gainer_set_gain(&gain, 0);
    ma_gainer_process_pcm_frames(&gain, output, input, block);
    printf("channels=%u smooth=%u block=%u first-channel:", channels, smooth, block);
    for (i = 0; i < block; ++i) {
        float expected = i < smooth ? 1.0f - (float)i/smooth : 0;
        printf(" %.6f", output[i*channels]);
        for (c = 0; c < channels; ++c) if (fabsf(output[i*channels+c]-expected) > 0.00001f) ++bad;
    }
    printf(" mismatching_samples=%u\n", bad);
    ma_gainer_uninit(&gain, NULL);
    return bad != 0;
}

static int tail_probe(void) {
    ma_engine engine;
    ma_sound sound;
    ma_audio_buffer_ref buffer;
    ma_engine_config config = ma_engine_config_init();
    ma_int16 input[3036];
    float output[4096];
    ma_uint64 got=0;
    unsigned i, nonzero=0, last=0;
    ma_result result;
    for (i=0;i<3036;++i) input[i] = 16384;
    config.noDevice = MA_TRUE;
    config.channels = 1;
    config.sampleRate = 22050;
    if (ma_engine_init(&config, &engine) != MA_SUCCESS) return 1;
    if (ma_audio_buffer_ref_init(ma_format_s16, 1, input, 3036, &buffer) != MA_SUCCESS) return 1;
    result = ma_sound_init_from_data_source(&engine, &buffer, MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH, NULL, &sound);
    if (result != MA_SUCCESS) return 1;
    ma_sound_start(&sound);
    result = ma_engine_read_pcm_frames(&engine, output, 4096, &got);
    for (i=0;i<got;++i) if (output[i] != 0) { ++nonzero; last=i; }
    printf("tail: result=%d frames=%llu nonzero=%u last_nonzero=%u expected_nonzero=3036\n", result, (unsigned long long)got, nonzero, last);
    ma_sound_uninit(&sound);
    ma_audio_buffer_ref_uninit(&buffer);
    ma_engine_uninit(&engine);
    return nonzero != 3036;
}

int main(void) {
    int failed=0;
    failed += gain_probe(1, 8, 12);
    failed += gain_probe(2, 8, 7);
    failed += gain_probe(2, 8, 12);
    failed += gain_probe(6, 8, 7);
    failed += gain_probe(8, 8, 12);
    failed += gain_probe(33, 8, 12);
    failed += tail_probe();
    printf("failed_cases=%d\n", failed);
    return failed != 0;
}
