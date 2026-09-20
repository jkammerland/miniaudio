#define MA_NO_DEVICE_IO
#include "../../miniaudio.c"

#include <math.h>
#include <stdio.h>

#define EOF_TEST_SOURCE_FRAMES 3036
#define EOF_TEST_RENDER_FRAMES 4096

static void engine_eof_end_callback(void* pUserData, ma_sound* pSound)
{
    ma_uint32* pCount = (ma_uint32*)pUserData;
    (void)pSound;
    *pCount += 1;
}

static int engine_eof_test_format(ma_format format)
{
    ma_engine_config engineConfig = ma_engine_config_init();
    ma_engine engine;
    ma_audio_buffer_ref buffer;
    ma_sound_config soundConfig;
    ma_sound sound;
    ma_int16 inputS16[EOF_TEST_SOURCE_FRAMES];
    float inputF32[EOF_TEST_SOURCE_FRAMES];
    float output[EOF_TEST_RENDER_FRAMES];
    ma_uint32 endCount = 0;
    ma_uint64 framesRead = 0;
    ma_uint32 i;
    ma_result result;
    int failed = 0;

    for (i = 0; i < EOF_TEST_SOURCE_FRAMES; i += 1) {
        inputS16[i] = 16384;
        inputF32[i] = 0.5f;
    }
    for (i = 0; i < EOF_TEST_RENDER_FRAMES; i += 1) {
        output[i] = -1;
    }

    engineConfig.noDevice = MA_TRUE;
    engineConfig.channels = 1;
    engineConfig.sampleRate = 22050;
    if (ma_engine_init(&engineConfig, &engine) != MA_SUCCESS) {
        return 1;
    }
    result = ma_audio_buffer_ref_init(format, 1, format == ma_format_s16 ? (void*)inputS16 : (void*)inputF32, EOF_TEST_SOURCE_FRAMES, &buffer);
    if (result != MA_SUCCESS) {
        ma_engine_uninit(&engine);
        return 1;
    }

    soundConfig = ma_sound_config_init_2(&engine);
    soundConfig.pDataSource = &buffer;
    soundConfig.flags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;
    soundConfig.endCallback = engine_eof_end_callback;
    soundConfig.pEndCallbackUserData = &endCount;
    result = ma_sound_init_ex(&engine, &soundConfig, &sound);
    if (result != MA_SUCCESS) {
        ma_audio_buffer_ref_uninit(&buffer);
        ma_engine_uninit(&engine);
        return 1;
    }

    ma_sound_start(&sound);
    result = ma_engine_read_pcm_frames(&engine, output, EOF_TEST_RENDER_FRAMES, &framesRead);
    if (result != MA_SUCCESS || framesRead != EOF_TEST_RENDER_FRAMES) {
        printf("engine render failed for format %d: result=%d frames=%u:%08u\n", format, result, (ma_uint32)(framesRead >> 32), (ma_uint32)framesRead);
        failed = 1;
    }
    for (i = 0; i < EOF_TEST_RENDER_FRAMES; i += 1) {
        float expected = i < EOF_TEST_SOURCE_FRAMES ? 0.5f : 0.0f;
        if (fabs((double)output[i] - (double)expected) > 0.00001) {
            printf("engine EOF mismatch for format %d at frame %u: %f != %f\n", format, i, output[i], expected);
            failed = 1;
            break;
        }
    }

    ma_engine_read_pcm_frames(&engine, output, 64, &framesRead);
    if (endCount != 1) {
        printf("engine end callback count for format %d was %u\n", format, endCount);
        failed = 1;
    }

    ma_sound_uninit(&sound);
    ma_audio_buffer_ref_uninit(&buffer);
    ma_engine_uninit(&engine);
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += engine_eof_test_format(ma_format_s16);
    failed += engine_eof_test_format(ma_format_f32);
    return failed != 0;
}
