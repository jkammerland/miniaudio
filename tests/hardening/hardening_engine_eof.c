#define MA_NO_DEVICE_IO
#include "../../miniaudio.c"

#include <math.h>
#include <stdio.h>

#define EOF_TEST_SOURCE_FRAMES 3036
#define EOF_TEST_RENDER_FRAMES 4096
#define EOF_TEST_MAX_BOUNDARY_FRAMES 513
#define EOF_TEST_MAX_READ_FRAMES 257

typedef struct
{
    ma_uint32 count;
    ma_bool32 firedWhilePlaying;
} engine_eof_end_state;

typedef struct
{
    ma_engine engine;
    ma_audio_buffer buffer;
    ma_sound sound;
    ma_int16 inputS16[EOF_TEST_MAX_BOUNDARY_FRAMES];
    float inputF32[EOF_TEST_MAX_BOUNDARY_FRAMES];
    engine_eof_end_state endState;
    ma_format format;
    ma_uint32 sourceFrames;
    ma_bool32 engineInitialized;
    ma_bool32 bufferInitialized;
    ma_bool32 soundInitialized;
} engine_eof_fixture;

static void engine_eof_end_callback(void* pUserData, ma_sound* pSound)
{
    engine_eof_end_state* pState = (engine_eof_end_state*)pUserData;
    pState->count += 1;
    if (ma_sound_is_playing(pSound)) {
        pState->firedWhilePlaying = MA_TRUE;
    }
}

static float engine_eof_pattern_value(ma_uint32 frameIndex)
{
    return (float)((frameIndex % 31) + 1) / 64.0f;
}

static ma_result engine_eof_fixture_init(
    engine_eof_fixture* pFixture,
    ma_format format,
    ma_uint32 sourceFrames,
    ma_uint32 sourceSampleRate,
    ma_uint32 engineSampleRate,
    ma_uint32 periodSizeInFrames,
    ma_bool32 constantInput)
{
    ma_engine_config engineConfig;
    ma_audio_buffer_config bufferConfig;
    ma_sound_config soundConfig;
    ma_result result;
    ma_uint32 i;

    if (pFixture == NULL || sourceFrames == 0 || sourceFrames > EOF_TEST_MAX_BOUNDARY_FRAMES) {
        return MA_INVALID_ARGS;
    }

    MA_ZERO_OBJECT(pFixture);
    pFixture->format = format;
    pFixture->sourceFrames = sourceFrames;
    for (i = 0; i < sourceFrames; i += 1) {
        float value = constantInput ? 0.5f : engine_eof_pattern_value(i);
        pFixture->inputF32[i] = value;
        pFixture->inputS16[i] = (ma_int16)(value * 32768.0f);
    }

    engineConfig = ma_engine_config_init();
    engineConfig.noDevice = MA_TRUE;
    engineConfig.channels = 1;
    engineConfig.sampleRate = engineSampleRate;
    engineConfig.periodSizeInFrames = periodSizeInFrames;
    result = ma_engine_init(&engineConfig, &pFixture->engine);
    if (result != MA_SUCCESS) {
        return result;
    }
    pFixture->engineInitialized = MA_TRUE;

    bufferConfig = ma_audio_buffer_config_init(
        format,
        1,
        sourceFrames,
        format == ma_format_s16 ? (void*)pFixture->inputS16 : (void*)pFixture->inputF32,
        NULL);
    bufferConfig.sampleRate = sourceSampleRate;
    result = ma_audio_buffer_init(&bufferConfig, &pFixture->buffer);
    if (result != MA_SUCCESS) {
        return result;
    }
    pFixture->bufferInitialized = MA_TRUE;

    soundConfig = ma_sound_config_init_2(&pFixture->engine);
    soundConfig.pDataSource = (ma_data_source*)&pFixture->buffer;
    soundConfig.flags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;
    soundConfig.endCallback = engine_eof_end_callback;
    soundConfig.pEndCallbackUserData = &pFixture->endState;
    result = ma_sound_init_ex(&pFixture->engine, &soundConfig, &pFixture->sound);
    if (result != MA_SUCCESS) {
        return result;
    }
    pFixture->soundInitialized = MA_TRUE;

    return ma_sound_start(&pFixture->sound);
}

static void engine_eof_fixture_uninit(engine_eof_fixture* pFixture)
{
    if (pFixture->soundInitialized) {
        ma_sound_uninit(&pFixture->sound);
    }
    if (pFixture->bufferInitialized) {
        ma_audio_buffer_uninit(&pFixture->buffer);
    }
    if (pFixture->engineInitialized) {
        ma_engine_uninit(&pFixture->engine);
    }
}

static int engine_eof_read(engine_eof_fixture* pFixture, float* pOutput, ma_uint32 frameCount, const char* pLabel)
{
    ma_uint64 framesRead;
    ma_result result;

    framesRead = 0;
    result = ma_engine_read_pcm_frames(&pFixture->engine, pOutput, frameCount, &framesRead);
    if (result != MA_SUCCESS || framesRead != frameCount) {
        printf("%s render failed: result=%d frames=%u:%08u expected=%u\n",
            pLabel,
            result,
            (ma_uint32)(framesRead >> 32),
            (ma_uint32)framesRead,
            frameCount);
        return 1;
    }

    return 0;
}

static int engine_eof_check_end_state(engine_eof_fixture* pFixture, ma_uint32 expectedCount, const char* pLabel)
{
    if (pFixture->endState.count != expectedCount || pFixture->endState.firedWhilePlaying) {
        printf("%s end state was count=%u while-playing=%u, expected count=%u\n",
            pLabel,
            pFixture->endState.count,
            pFixture->endState.firedWhilePlaying,
            expectedCount);
        return 1;
    }

    return 0;
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
    engine_eof_end_state endState;
    ma_uint64 framesRead = 0;
    ma_uint32 i;
    ma_result result;
    int failed = 0;

    MA_ZERO_OBJECT(&endState);
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
    soundConfig.pEndCallbackUserData = &endState;
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
    if (endState.count != 1 || endState.firedWhilePlaying) {
        printf("engine end callback state for format %d was count=%u while-playing=%u\n", format, endState.count, endState.firedWhilePlaying);
        failed = 1;
    }

    ma_sound_uninit(&sound);
    ma_audio_buffer_ref_uninit(&buffer);
    ma_engine_uninit(&engine);
    return failed;
}

static int engine_eof_test_split_cache_boundary(ma_format format, ma_uint32 sourceFrames)
{
    static const ma_uint32 chunkSizes[] = {1, 257, 31, 223};
    engine_eof_fixture fixture;
    float output[EOF_TEST_MAX_READ_FRAMES];
    ma_uint32 offset;
    ma_uint32 chunkIndex;
    ma_uint32 i;
    int failed;
    ma_result result;

    failed = 0;
    result = engine_eof_fixture_init(&fixture, format, sourceFrames, 22050, 22050, 0, MA_FALSE);
    if (result != MA_SUCCESS) {
        printf("split cache-boundary setup failed for format=%d frames=%u: %d\n", format, sourceFrames, result);
        engine_eof_fixture_uninit(&fixture);
        return 1;
    }

    offset = 0;
    chunkIndex = 0;
    while (offset < sourceFrames) {
        ma_uint32 frameCount = chunkSizes[chunkIndex % ma_countof(chunkSizes)];
        if (frameCount > sourceFrames - offset) {
            frameCount = sourceFrames - offset;
        }

        failed += engine_eof_read(&fixture, output, frameCount, "split cache-boundary");
        for (i = 0; i < frameCount; i += 1) {
            float expected = engine_eof_pattern_value(offset + i);
            if (fabs((double)output[i] - (double)expected) > 0.00001) {
                printf("split cache-boundary mismatch format=%d source=%u frame=%u: %f != %f\n",
                    format,
                    sourceFrames,
                    offset + i,
                    output[i],
                    expected);
                failed = 1;
                break;
            }
        }
        failed += engine_eof_check_end_state(&fixture, 0, "split cache-boundary before drain");
        offset += frameCount;
        chunkIndex += 1;
    }

    failed += engine_eof_read(&fixture, output, 8, "split cache-boundary post-drain");
    for (i = 0; i < 8; i += 1) {
        if (output[i] != 0) {
            printf("split cache-boundary emitted post-end sample %f for format=%d source=%u\n", output[i], format, sourceFrames);
            failed = 1;
            break;
        }
    }
    failed += engine_eof_check_end_state(&fixture, 0, "split cache-boundary EOF detection");

    failed += engine_eof_read(&fixture, output, 8, "split cache-boundary notification step");
    failed += engine_eof_check_end_state(&fixture, 1, "split cache-boundary notification step");
    failed += engine_eof_read(&fixture, output, 8, "split cache-boundary repeated post-drain");
    failed += engine_eof_check_end_state(&fixture, 1, "split cache-boundary repeated post-drain");
    engine_eof_fixture_uninit(&fixture);
    return failed;
}

static int engine_eof_test_period_boundary(ma_format format, ma_uint32 sourceFrames)
{
    const ma_uint32 periodSize = 64;
    engine_eof_fixture fixture;
    float output[64];
    ma_uint32 offset;
    ma_uint32 i;
    int failed;
    ma_result result;

    failed = 0;
    result = engine_eof_fixture_init(&fixture, format, sourceFrames, 22050, 22050, periodSize, MA_FALSE);
    if (result != MA_SUCCESS) {
        printf("period-boundary setup failed for format=%d frames=%u: %d\n", format, sourceFrames, result);
        engine_eof_fixture_uninit(&fixture);
        return 1;
    }

    offset = 0;
    while (offset < sourceFrames) {
        ma_uint32 audioFramesThisPeriod = sourceFrames - offset;
        if (audioFramesThisPeriod > periodSize) {
            audioFramesThisPeriod = periodSize;
        }
        if (offset + periodSize < sourceFrames) {
            failed += engine_eof_check_end_state(&fixture, 0, "period-boundary before final period");
        }
        failed += engine_eof_read(&fixture, output, periodSize, "period-boundary");
        for (i = 0; i < periodSize; i += 1) {
            float expected = i < audioFramesThisPeriod ? engine_eof_pattern_value(offset + i) : 0;
            if (fabs((double)output[i] - (double)expected) > 0.00001) {
                printf("period-boundary mismatch format=%d source=%u output-frame=%u: %f != %f\n",
                    format,
                    sourceFrames,
                    offset + i,
                    output[i],
                    expected);
                failed = 1;
                break;
            }
        }
        offset += audioFramesThisPeriod;
    }

    if ((sourceFrames % periodSize) == 0) {
        failed += engine_eof_check_end_state(&fixture, 0, "period-boundary exact drain");
        failed += engine_eof_read(&fixture, output, periodSize, "period-boundary EOF detection");
        failed += engine_eof_check_end_state(&fixture, 0, "period-boundary EOF detection");
        failed += engine_eof_read(&fixture, output, periodSize, "period-boundary notification step");
    } else {
        failed += engine_eof_check_end_state(&fixture, 1, "period-boundary partial final period");
        failed += engine_eof_read(&fixture, output, periodSize, "period-boundary post-drain");
    }
    failed += engine_eof_check_end_state(&fixture, 1, "period-boundary notified");
    failed += engine_eof_read(&fixture, output, periodSize, "period-boundary repeated post-drain");
    failed += engine_eof_check_end_state(&fixture, 1, "period-boundary repeated post-drain");
    engine_eof_fixture_uninit(&fixture);
    return failed;
}

static int engine_eof_test_resampling_tail(ma_format format)
{
    const ma_uint32 sourceFrames = 257;
    const ma_uint32 expectedOutputFrames = sourceFrames * 2;
    static const ma_uint32 chunkSizes[] = {127, 257, 63, 67};
    engine_eof_fixture fixture;
    float output[EOF_TEST_MAX_READ_FRAMES];
    ma_uint32 offset;
    ma_uint32 chunkIndex;
    ma_uint32 i;
    int failed;
    ma_result result;

    failed = 0;
    result = engine_eof_fixture_init(&fixture, format, sourceFrames, 24000, 48000, 0, MA_TRUE);
    if (result != MA_SUCCESS) {
        printf("resampling-tail setup failed for format=%d: %d\n", format, result);
        engine_eof_fixture_uninit(&fixture);
        return 1;
    }

    offset = 0;
    chunkIndex = 0;
    while (offset < expectedOutputFrames) {
        ma_uint32 frameCount = chunkSizes[chunkIndex % ma_countof(chunkSizes)];
        if (frameCount > expectedOutputFrames - offset) {
            frameCount = expectedOutputFrames - offset;
        }
        failed += engine_eof_read(&fixture, output, frameCount, "resampling tail");
        for (i = 0; i < frameCount; i += 1) {
            ma_uint32 outputFrameIndex = offset + i;
            /* A 2:1 linear conversion of a 0.5 step starts from the resampler's zero history. */
            float expected = outputFrameIndex == 0 ? 0.0f : (outputFrameIndex == 1 ? 0.25f : 0.5f);
            if (fabs((double)output[i] - (double)expected) > 0.00001) {
                printf("resampling-tail mismatch format=%d frame=%u: %f != %f\n", format, outputFrameIndex, output[i], expected);
                failed = 1;
                break;
            }
        }
        failed += engine_eof_check_end_state(&fixture, 0, "resampling tail before drain");
        offset += frameCount;
        chunkIndex += 1;
    }

    if (fixture.buffer.ref.cursor != sourceFrames) {
        printf("resampling tail consumed %u:%08u source frames, expected %u\n",
            (ma_uint32)(fixture.buffer.ref.cursor >> 32),
            (ma_uint32)fixture.buffer.ref.cursor,
            sourceFrames);
        failed = 1;
    }
    failed += engine_eof_read(&fixture, output, 8, "resampling tail post-drain");
    for (i = 0; i < 8; i += 1) {
        if (output[i] != 0) {
            printf("resampling tail emitted post-end sample %f for format=%d\n", output[i], format);
            failed = 1;
            break;
        }
    }
    failed += engine_eof_check_end_state(&fixture, 0, "resampling tail EOF detection");
    failed += engine_eof_read(&fixture, output, 8, "resampling tail notification step");
    failed += engine_eof_check_end_state(&fixture, 1, "resampling tail notification step");
    failed += engine_eof_read(&fixture, output, 8, "resampling tail repeated post-drain");
    failed += engine_eof_check_end_state(&fixture, 1, "resampling tail repeated post-drain");
    engine_eof_fixture_uninit(&fixture);
    return failed;
}

static int engine_eof_test_loop_transition(ma_format format)
{
    engine_eof_fixture fixture;
    float output[8];
    ma_uint32 i;
    int failed;
    ma_result result;

    failed = 0;
    result = engine_eof_fixture_init(&fixture, format, 3, 22050, 22050, 1, MA_FALSE);
    if (result != MA_SUCCESS) {
        printf("loop transition setup failed for format=%d: %d\n", format, result);
        engine_eof_fixture_uninit(&fixture);
        return 1;
    }

    ma_sound_set_looping(&fixture.sound, MA_TRUE);
    failed += engine_eof_read(&fixture, output, 8, "loop transition looping");
    for (i = 0; i < 8; i += 1) {
        float expected = engine_eof_pattern_value(i % 3);
        if (fabs((double)output[i] - (double)expected) > 0.00001) {
            printf("loop transition mismatch format=%d frame=%u: %f != %f\n", format, i, output[i], expected);
            failed = 1;
            break;
        }
    }
    failed += engine_eof_check_end_state(&fixture, 0, "loop transition while looping");

    ma_sound_set_looping(&fixture.sound, MA_FALSE);
    if (fixture.buffer.ref.cursor != 2) {
        printf("loop transition cursor for format=%d was %u:%08u, expected 2\n",
            format,
            (ma_uint32)(fixture.buffer.ref.cursor >> 32),
            (ma_uint32)fixture.buffer.ref.cursor);
        failed = 1;
    }
    failed += engine_eof_read(&fixture, output, 1, "loop transition final frame");
    if (fabs((double)output[0] - (double)engine_eof_pattern_value(2)) > 0.00001) {
        printf("loop transition lost final frame for format=%d: %f\n", format, output[0]);
        failed = 1;
    }
    failed += engine_eof_check_end_state(&fixture, 0, "loop transition final frame");

    failed += engine_eof_read(&fixture, output, 1, "loop transition post-drain");
    if (output[0] != 0) {
        printf("loop transition emitted post-end sample %f for format=%d\n", output[0], format);
        failed = 1;
    }
    failed += engine_eof_check_end_state(&fixture, 0, "loop transition EOF detection");
    failed += engine_eof_read(&fixture, output, 1, "loop transition notification step");
    failed += engine_eof_check_end_state(&fixture, 1, "loop transition notification step");
    failed += engine_eof_read(&fixture, output, 1, "loop transition repeated post-drain");
    failed += engine_eof_check_end_state(&fixture, 1, "loop transition repeated post-drain");

    engine_eof_fixture_uninit(&fixture);
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += engine_eof_test_format(ma_format_s16);
    failed += engine_eof_test_format(ma_format_f32);
    failed += engine_eof_test_split_cache_boundary(ma_format_s16, 511);
    failed += engine_eof_test_split_cache_boundary(ma_format_s16, 512);
    failed += engine_eof_test_split_cache_boundary(ma_format_s16, 513);
    failed += engine_eof_test_split_cache_boundary(ma_format_f32, 511);
    failed += engine_eof_test_split_cache_boundary(ma_format_f32, 512);
    failed += engine_eof_test_split_cache_boundary(ma_format_f32, 513);
    failed += engine_eof_test_period_boundary(ma_format_s16, 63);
    failed += engine_eof_test_period_boundary(ma_format_s16, 64);
    failed += engine_eof_test_period_boundary(ma_format_s16, 65);
    failed += engine_eof_test_period_boundary(ma_format_f32, 63);
    failed += engine_eof_test_period_boundary(ma_format_f32, 64);
    failed += engine_eof_test_period_boundary(ma_format_f32, 65);
    failed += engine_eof_test_resampling_tail(ma_format_s16);
    failed += engine_eof_test_resampling_tail(ma_format_f32);
    failed += engine_eof_test_loop_transition(ma_format_s16);
    failed += engine_eof_test_loop_transition(ma_format_f32);
    return failed != 0;
}
