#define MA_NO_DEVICE_IO
#include "../../miniaudio.c"

#include <math.h>
#include <stdio.h>

#define GAINER_MAX_CHANNELS 33
#define GAINER_MAX_FRAMES   20
#define GAINER_GUARD_VALUE  12345.0f

static int gainer_near(float actual, float expected)
{
    return fabs((double)actual - (double)expected) <= 0.00001;
}

static int gainer_run_ramp(ma_uint32 channels, ma_uint32 smoothFrames, const ma_uint32* pBlocks, ma_uint32 blockCount, ma_bool32 inPlace)
{
    ma_gainer gainer;
    ma_gainer_config config = ma_gainer_config_init(channels, smoothFrames);
    float input[GAINER_MAX_FRAMES * GAINER_MAX_CHANNELS + 2];
    float output[GAINER_MAX_FRAMES * GAINER_MAX_CHANNELS + 2];
    ma_uint32 totalFrames = 0;
    ma_uint32 blockIndex;
    ma_uint32 sampleIndex;
    int failed = 0;

    for (blockIndex = 0; blockIndex < blockCount; blockIndex += 1) {
        totalFrames += pBlocks[blockIndex];
    }
    if (totalFrames > GAINER_MAX_FRAMES || ma_gainer_init(&config, NULL, &gainer) != MA_SUCCESS) {
        return 1;
    }
    for (sampleIndex = 0; sampleIndex < GAINER_MAX_FRAMES * GAINER_MAX_CHANNELS + 2; sampleIndex += 1) {
        input[sampleIndex] = 1.0f;
        output[sampleIndex] = GAINER_GUARD_VALUE;
    }

    if (ma_gainer_process_pcm_frames(&gainer, output, input, 1) != MA_SUCCESS) {
        ma_gainer_uninit(&gainer, NULL);
        return 1;
    }
    ma_gainer_set_gain(&gainer, 0.0f);
    totalFrames = 0;
    for (blockIndex = 0; blockIndex < blockCount; blockIndex += 1) {
        ma_uint32 frameCount = pBlocks[blockIndex];
        float* pOutput = inPlace ? input + totalFrames * channels : output + totalFrames * channels;
        const float* pInput = input + totalFrames * channels;
        if (ma_gainer_process_pcm_frames(&gainer, pOutput, pInput, frameCount) != MA_SUCCESS) {
            ma_gainer_uninit(&gainer, NULL);
            return 1;
        }
        totalFrames += frameCount;
    }

    for (sampleIndex = 0; sampleIndex < totalFrames * channels; sampleIndex += 1) {
        ma_uint32 frameIndex = sampleIndex / channels;
        float expected = smoothFrames == 0 || frameIndex >= smoothFrames ? 0.0f : 1.0f - (float)frameIndex / (float)smoothFrames;
        float actual = inPlace ? input[sampleIndex] : output[sampleIndex];
        if (!gainer_near(actual, expected)) {
            failed = 1;
            break;
        }
    }
    if (!inPlace && (output[totalFrames * channels] != GAINER_GUARD_VALUE || output[totalFrames * channels + 1] != GAINER_GUARD_VALUE)) {
        printf("gainer overwrote output guard: channels=%u smooth=%u\n", channels, smoothFrames);
        failed = 1;
    }
    if (failed) {
        printf("gainer ramp mismatch: channels=%u smooth=%u split=%u inplace=%u sample=%u\n", channels, smoothFrames, blockCount, (unsigned int)inPlace, sampleIndex);
    }

    ma_gainer_uninit(&gainer, NULL);
    return failed;
}

static int gainer_test_null_advance(void)
{
    ma_gainer gainer;
    ma_gainer_config config = ma_gainer_config_init(2, 8);
    float input[4] = {1, 1, 1, 1};
    float output[4] = {0, 0, 0, 0};
    int failed = 0;

    if (ma_gainer_init(&config, NULL, &gainer) != MA_SUCCESS) {
        return 1;
    }
    if (ma_gainer_process_pcm_frames(&gainer, output, input, 1) != MA_SUCCESS) {
        ma_gainer_uninit(&gainer, NULL);
        return 1;
    }
    ma_gainer_set_gain(&gainer, 0);
    if (ma_gainer_process_pcm_frames(&gainer, NULL, NULL, 3) != MA_SUCCESS ||
        ma_gainer_process_pcm_frames(&gainer, output, input, 2) != MA_SUCCESS) {
        ma_gainer_uninit(&gainer, NULL);
        return 1;
    }
    if (!gainer_near(output[0], 0.625f) || !gainer_near(output[2], 0.5f)) {
        printf("null-buffer advancement mismatch: %f %f\n", output[0], output[2]);
        failed = 1;
    }
    ma_gainer_uninit(&gainer, NULL);
    return failed;
}

static int gainer_test_mid_ramp_target(void)
{
    ma_gainer gainer;
    ma_gainer_config config = ma_gainer_config_init(1, 8);
    float input[12];
    float output[12];
    ma_uint32 i;
    int failed = 0;

    for (i = 0; i < 12; i += 1) {
        input[i] = 1;
    }
    if (ma_gainer_init(&config, NULL, &gainer) != MA_SUCCESS) {
        return 1;
    }
    if (ma_gainer_process_pcm_frames(&gainer, output, input, 1) != MA_SUCCESS) {
        ma_gainer_uninit(&gainer, NULL);
        return 1;
    }
    ma_gainer_set_gain(&gainer, 0);
    if (ma_gainer_process_pcm_frames(&gainer, output, input, 4) != MA_SUCCESS) {
        ma_gainer_uninit(&gainer, NULL);
        return 1;
    }
    ma_gainer_set_gain(&gainer, 1);
    if (ma_gainer_process_pcm_frames(&gainer, output + 4, input + 4, 8) != MA_SUCCESS) {
        ma_gainer_uninit(&gainer, NULL);
        return 1;
    }
    for (i = 0; i < 8; i += 1) {
        float expected = 0.5f + 0.5f * (float)i / 8.0f;
        if (!gainer_near(output[4 + i], expected)) {
            printf("mid-ramp target mismatch at %u: %f != %f\n", i, output[4 + i], expected);
            failed = 1;
            break;
        }
    }
    ma_gainer_uninit(&gainer, NULL);
    return failed;
}

int main(void)
{
    static const ma_uint32 oneBlock[] = {12};
    static const ma_uint32 splitBlocks[] = {3, 4, 5};
    static const ma_uint32 channels[] = {1, 2, 6, 8, 33};
    ma_uint32 i;
    int failed = 0;

    for (i = 0; i < ma_countof(channels); i += 1) {
        failed += gainer_run_ramp(channels[i], 8, oneBlock, ma_countof(oneBlock), MA_FALSE);
        failed += gainer_run_ramp(channels[i], 8, splitBlocks, ma_countof(splitBlocks), MA_FALSE);
        failed += gainer_run_ramp(channels[i], 7, oneBlock, ma_countof(oneBlock), MA_TRUE);
    }
    failed += gainer_run_ramp(2, 0, oneBlock, ma_countof(oneBlock), MA_FALSE);
    failed += gainer_run_ramp(2, 1, oneBlock, ma_countof(oneBlock), MA_FALSE);
    failed += gainer_test_null_advance();
    failed += gainer_test_mid_ramp_target();
    return failed != 0;
}
