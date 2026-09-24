#define MA_STEAMAUDIO_NO_MAIN
#define MA_STEAMAUDIO_TEST_DRY_WET_PARSER
static void drywet_after_load_hook(void);
#define MA_STEAMAUDIO_DRY_WET_AFTER_LOAD() drywet_after_load_hook()
#include "../../examples/engine_steamaudio.c"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define DRYWET_FRAME_SIZE 64
#define DRYWET_TOLERANCE 0.00002f

static ma_uint32 g_inCallback;
static ma_uint32 g_sdkCallbackAllocations;
static ma_uint32 g_miniaudioCallbackAllocations;
static ma_event g_dryWetLoadedEvent;
static ma_event g_dryWetReleaseEvent;
static volatile ma_uint32 g_pauseAfterLoad;

static void drywet_after_load_hook(void)
{
    if (ma_atomic_load_32(&g_pauseAfterLoad)) {
        ma_event_signal(&g_dryWetLoadedEvent);
        ma_event_wait(&g_dryWetReleaseEvent);
    }
}

static void* IPLCALL drywet_sdk_alloc(IPLsize size, IPLsize alignment)
{
    if (g_inCallback) g_sdkCallbackAllocations += 1;
    return ma_aligned_malloc((size_t)size, (size_t)alignment, NULL);
}

static void IPLCALL drywet_sdk_free(void* pMemory)
{
    if (g_inCallback) g_sdkCallbackAllocations += 1;
    ma_aligned_free(pMemory, NULL);
}

static void* drywet_ma_malloc(size_t size, void* pUserData)
{
    (void)pUserData;
    if (g_inCallback) g_miniaudioCallbackAllocations += 1;
    return malloc(size);
}

static void* drywet_ma_realloc(void* p, size_t size, void* pUserData)
{
    (void)pUserData;
    if (g_inCallback) g_miniaudioCallbackAllocations += 1;
    return realloc(p, size);
}

static void drywet_ma_free(void* p, void* pUserData)
{
    (void)pUserData;
    if (g_inCallback) g_miniaudioCallbackAllocations += 1;
    free(p);
}

static int drywet_near(float observed, float expected)
{
    return observed == observed && observed <= FLT_MAX && observed >= -FLT_MAX &&
        fabsf(observed - expected) <= DRYWET_TOLERANCE;
}

static int drywet_process(ma_steamaudio_binaural_node* pNode, const float* pInput,
    ma_uint32 framesIn, float* pOutput, ma_uint32 framesOut)
{
    const float* ppInput[] = { pInput };
    float* ppOutput[] = { pOutput };
    ma_uint32 consumed = framesIn;
    ma_uint32 produced = framesOut;
    g_inCallback = 1;
    ma_steamaudio_binaural_node_process_pcm_frames((ma_node*)pNode,
        pInput != NULL ? ppInput : NULL, &consumed, ppOutput, &produced);
    g_inCallback = 0;
    if (consumed != (pInput != NULL ? framesIn : 0) || produced != framesOut) {
        fprintf(stderr, "dry/wet frame accounting: consumed=%u produced=%u expected=%u/%u\n",
            consumed, produced, pInput != NULL ? framesIn : 0, framesOut);
        return 1;
    }
    return 0;
}

static float drywet_input(int signal, int sample, int channel)
{
    float mono;
    if (signal == 0) {
        mono = sample == 0 ? 1.0f : (sample == 7 ? -0.25f : 0.0f);
    } else {
        mono = 0.1f + 0.35f * sinf(2.0f * 3.14159265358979323846f *
            440.0f * (float)sample / 48000.0f);
    }
    return channel == 0 ? mono : -0.5f * mono;
}

static int drywet_case(ma_node_graph* pGraph, IPLContext context, IPLHRTF hrtf,
    const ma_allocation_callbacks* pCallbacks, ma_uint32 channels,
    IPLHRTFInterpolation interpolation, IPLVector3 direction, int signal, float wet)
{
    IPLAudioSettings audioSettings = { 48000, DRYWET_FRAME_SIZE };
    IPLBinauralEffectSettings effectSettings = { hrtf };
    IPLBinauralEffect reference = NULL;
    IPLBinauralEffectParams params;
    ma_steamaudio_binaural_node_config config;
    ma_steamaudio_binaural_node node;
    float input[DRYWET_FRAME_SIZE * 2];
    float referenceInput[2][DRYWET_FRAME_SIZE];
    float* referenceInputChannels[2] = { referenceInput[0], referenceInput[1] };
    float referenceOutput[2][DRYWET_FRAME_SIZE] = {{0}};
    float* referenceOutputChannels[2] = { referenceOutput[0], referenceOutput[1] };
    float startup[DRYWET_FRAME_SIZE * 2];
    float output[DRYWET_FRAME_SIZE * 2];
    IPLAudioBuffer inputBuffer;
    IPLAudioBuffer outputBuffer;
    double wetEnergy = 0.0;
    int nodeInitialized = 0;
    int failed = 1;
    int sample;
    int channel;

    config = ma_steamaudio_binaural_node_config_init(channels, audioSettings, context, hrtf);
    config.dryWet = wet;
    config.interpolation = interpolation;
    if (ma_steamaudio_binaural_node_init(pGraph, &config, pCallbacks, &node) != MA_SUCCESS)
        goto cleanup;
    nodeInitialized = 1;
    if (ma_steamaudio_binaural_node_set_dry_wet(&node, -0.01f) != MA_INVALID_ARGS ||
        ma_steamaudio_binaural_node_set_dry_wet(&node, 1.01f) != MA_INVALID_ARGS ||
        ma_steamaudio_binaural_node_set_dry_wet(&node, NAN) != MA_INVALID_ARGS ||
        ma_steamaudio_binaural_node_set_dry_wet(&node, INFINITY) != MA_INVALID_ARGS)
        goto cleanup;
    if (ma_steamaudio_binaural_node_set_direction(&node,
        direction.x, direction.y, direction.z) != MA_SUCCESS)
        goto cleanup;
    if (iplBinauralEffectCreate(context, &audioSettings, &effectSettings,
        &reference) != IPL_STATUS_SUCCESS)
        goto cleanup;

    for (sample = 0; sample < DRYWET_FRAME_SIZE; sample += 1) {
        for (channel = 0; channel < (int)channels; channel += 1) {
            float value = drywet_input(signal, sample, channel);
            input[sample * channels + channel] = value;
            referenceInput[channel][sample] = value;
        }
    }
    inputBuffer.numChannels = channels;
    inputBuffer.numSamples = DRYWET_FRAME_SIZE;
    inputBuffer.data = referenceInputChannels;
    outputBuffer.numChannels = 2;
    outputBuffer.numSamples = DRYWET_FRAME_SIZE;
    outputBuffer.data = referenceOutputChannels;
    MA_ZERO_OBJECT(&params);
    params.direction = direction;
    params.interpolation = interpolation;
    params.spatialBlend = 1.0f;
    params.hrtf = hrtf;
    iplBinauralEffectApply(reference, &params, &inputBuffer, &outputBuffer);

    if (drywet_process(&node, input, 17, startup, 17) ||
        drywet_process(&node, input + 17 * channels, DRYWET_FRAME_SIZE - 17,
            startup + 17 * 2, DRYWET_FRAME_SIZE - 17))
        goto cleanup;
    for (sample = 0; sample < DRYWET_FRAME_SIZE * 2; sample += 1)
        if (startup[sample] != 0.0f) goto cleanup;
    if (drywet_process(&node, NULL, 0, output, 17) ||
        drywet_process(&node, NULL, 0, output + 17 * 2, DRYWET_FRAME_SIZE - 17))
        goto cleanup;

    for (sample = 0; sample < DRYWET_FRAME_SIZE; sample += 1) {
        for (channel = 0; channel < 2; channel += 1) {
            float dry = input[sample * channels + (channels == 1 ? 0 : channel)];
            float expected = (1.0f - wet) * dry + wet * referenceOutput[channel][sample];
            float actual = output[sample * 2 + channel];
            wetEnergy += fabsf(referenceOutput[channel][sample]);
            if (!drywet_near(actual, expected) ||
                (wet == 0.0f && actual != dry) ||
                (wet == 1.0f && actual != referenceOutput[channel][sample])) {
                fprintf(stderr, "dry/wet mismatch: channels=%u interpolation=%d signal=%d wet=%f sample=%d ear=%d actual=%f expected=%f\n",
                    channels, interpolation, signal, wet, sample, channel, actual, expected);
                goto cleanup;
            }
        }
    }
    if (wetEnergy <= 0.001 || g_sdkCallbackAllocations != 0 ||
        g_miniaudioCallbackAllocations != 0) {
        fprintf(stderr, "dry/wet case was silent or allocated in callback\n");
        goto cleanup;
    }
    failed = 0;

cleanup:
    iplBinauralEffectRelease(&reference);
    if (nodeInitialized) ma_steamaudio_binaural_node_uninit(&node, pCallbacks);
    return failed;
}

static int drywet_transition_and_tail(ma_node_graph* pGraph, IPLContext context,
    IPLHRTF hrtf, const ma_allocation_callbacks* pCallbacks)
{
    IPLAudioSettings audioSettings = { 48000, DRYWET_FRAME_SIZE };
    IPLBinauralEffectSettings effectSettings = { hrtf };
    IPLBinauralEffect reference = NULL;
    IPLBinauralEffectParams params;
    ma_steamaudio_binaural_node_config config;
    ma_steamaudio_binaural_node node;
    float input[DRYWET_FRAME_SIZE];
    float referenceOutput[2][DRYWET_FRAME_SIZE] = {{0}};
    float* inputChannel[] = { input };
    float* outputChannels[] = { referenceOutput[0], referenceOutput[1] };
    float output[DRYWET_FRAME_SIZE * 2];
    IPLAudioBuffer inputBuffer = { 1, DRYWET_FRAME_SIZE, inputChannel };
    IPLAudioBuffer outputBuffer = { 2, DRYWET_FRAME_SIZE, outputChannels };
    int nodeInitialized = 0;
    int failed = 1;
    int sample;
    int ear;
    double wetDifference = 0.0;
    double tailEnergy = 0.0;

    config = ma_steamaudio_binaural_node_config_init(1, audioSettings, context, hrtf);
    config.dryWet = 0.0f;
    if (ma_steamaudio_binaural_node_init(pGraph, &config, pCallbacks, &node) != MA_SUCCESS)
        goto cleanup;
    nodeInitialized = 1;
    if (iplBinauralEffectCreate(context, &audioSettings, &effectSettings,
        &reference) != IPL_STATUS_SUCCESS)
        goto cleanup;
    for (sample = 0; sample < DRYWET_FRAME_SIZE; sample += 1)
        input[sample] = 0.1f + 0.35f * sinf(2.0f * 3.14159265358979323846f *
            440.0f * (float)sample / 48000.0f);
    MA_ZERO_OBJECT(&params);
    params.direction.z = -1.0f;
    params.interpolation = IPL_HRTFINTERPOLATION_NEAREST;
    params.spatialBlend = 1.0f;
    params.hrtf = hrtf;
    iplBinauralEffectApply(reference, &params, &inputBuffer, &outputBuffer);
    if (drywet_process(&node, input, DRYWET_FRAME_SIZE, output, DRYWET_FRAME_SIZE))
        goto cleanup;
    if (ma_steamaudio_binaural_node_set_dry_wet(&node, 1.0f) != MA_SUCCESS)
        goto cleanup;
    iplBinauralEffectApply(reference, &params, &inputBuffer, &outputBuffer);
    if (drywet_process(&node, input, DRYWET_FRAME_SIZE, output, DRYWET_FRAME_SIZE))
        goto cleanup;
    if (ma_steamaudio_binaural_node_end_input(&node) != MA_SUCCESS)
        goto cleanup;
    for (sample = 0; sample < DRYWET_FRAME_SIZE; sample += 1)
        for (ear = 0; ear < 2; ear += 1)
            if (output[sample * 2 + ear] != input[sample]) goto cleanup;
    if (drywet_process(&node, NULL, 0, output, DRYWET_FRAME_SIZE))
        goto cleanup;
    for (sample = 0; sample < DRYWET_FRAME_SIZE; sample += 1) {
        float wet = (float)(sample + 1) / (float)DRYWET_FRAME_SIZE;
        for (ear = 0; ear < 2; ear += 1) {
            float expected = (1.0f - wet) * input[sample] +
                wet * referenceOutput[ear][sample];
            wetDifference += fabsf(referenceOutput[ear][sample] - input[sample]);
            if (!drywet_near(output[sample * 2 + ear], expected)) {
                fprintf(stderr, "dry/wet transition mismatch at sample=%d ear=%d\n", sample, ear);
                goto cleanup;
            }
        }
    }
    if (wetDifference <= 0.001 || iplBinauralEffectGetTailSize(reference) <= 0) {
        fprintf(stderr, "dry/wet transition or reference tail was vacuous\n");
        goto cleanup;
    }
    if (ma_steamaudio_binaural_node_set_dry_wet(&node, 0.0f) != MA_SUCCESS)
        goto cleanup;
    if (drywet_process(&node, NULL, 0, output, DRYWET_FRAME_SIZE))
        goto cleanup;
    iplBinauralEffectGetTail(reference, &outputBuffer);
    for (sample = 0; sample < DRYWET_FRAME_SIZE; sample += 1) {
        float wet = 1.0f - (float)(sample + 1) / (float)DRYWET_FRAME_SIZE;
        for (ear = 0; ear < 2; ear += 1) {
            float expected = wet * referenceOutput[ear][sample];
            tailEnergy += fabsf(referenceOutput[ear][sample]);
            if (!drywet_near(output[sample * 2 + ear], expected)) {
                fprintf(stderr, "dry/wet tail mismatch at sample=%d ear=%d\n", sample, ear);
                goto cleanup;
            }
        }
    }
    if (tailEnergy <= 0.001) {
        fprintf(stderr, "dry/wet reference tail was silent\n");
        goto cleanup;
    }
    if (ma_steamaudio_binaural_node_set_dry_wet(&node, 0.5f) != MA_SUCCESS ||
        ma_steamaudio_binaural_node_reset(&node) != MA_SUCCESS)
        goto cleanup;
    iplBinauralEffectReset(reference);
    iplBinauralEffectApply(reference, &params, &inputBuffer, &outputBuffer);
    if (drywet_process(&node, input, DRYWET_FRAME_SIZE, output, DRYWET_FRAME_SIZE))
        goto cleanup;
    for (sample = 0; sample < DRYWET_FRAME_SIZE * 2; sample += 1)
        if (output[sample] != 0.0f) goto cleanup;
    if (drywet_process(&node, NULL, 0, output, DRYWET_FRAME_SIZE))
        goto cleanup;
    for (sample = 0; sample < DRYWET_FRAME_SIZE; sample += 1) {
        for (ear = 0; ear < 2; ear += 1) {
            float expected = 0.5f * input[sample] + 0.5f * referenceOutput[ear][sample];
            if (!drywet_near(output[sample * 2 + ear], expected)) {
                fprintf(stderr, "dry/wet reset did not resume at the published target\n");
                goto cleanup;
            }
        }
    }
    if (g_sdkCallbackAllocations != 0 || g_miniaudioCallbackAllocations != 0)
        goto cleanup;
    failed = 0;

cleanup:
    iplBinauralEffectRelease(&reference);
    if (nodeInitialized) ma_steamaudio_binaural_node_uninit(&node, pCallbacks);
    return failed;
}

typedef struct
{
    ma_steamaudio_binaural_node* pNode;
    ma_result result;
} drywet_publisher_data;

static ma_thread_result MA_THREADCALL drywet_publish_during_mix(void* pData)
{
    drywet_publisher_data* pPublisher = (drywet_publisher_data*)pData;
    ma_event_wait(&g_dryWetLoadedEvent);
    pPublisher->result = ma_steamaudio_binaural_node_set_dry_wet(pPublisher->pNode, 1.0f);
    ma_event_signal(&g_dryWetReleaseEvent);
    return (ma_thread_result)0;
}

static int drywet_concurrent_publication(ma_node_graph* pGraph, IPLContext context,
    IPLHRTF hrtf, const ma_allocation_callbacks* pCallbacks)
{
    IPLAudioSettings audioSettings = {48000, DRYWET_FRAME_SIZE};
    ma_steamaudio_binaural_node_config config =
        ma_steamaudio_binaural_node_config_init(2, audioSettings, context, hrtf);
    ma_steamaudio_binaural_node node;
    drywet_publisher_data publisher;
    ma_thread thread;
    int nodeInitialized = 0;
    int loadedEventInitialized = 0;
    int releaseEventInitialized = 0;
    int failed = 1;
    int sample;
    int ear;

    config.dryWet = 0.5f;
    if (ma_steamaudio_binaural_node_init(pGraph, &config, pCallbacks, &node) != MA_SUCCESS)
        goto cleanup;
    nodeInitialized = 1;
    if (ma_event_init(&g_dryWetLoadedEvent) != MA_SUCCESS) goto cleanup;
    loadedEventInitialized = 1;
    if (ma_event_init(&g_dryWetReleaseEvent) != MA_SUCCESS) goto cleanup;
    releaseEventInitialized = 1;

    publisher.pNode = &node;
    publisher.result = MA_ERROR;
    for (sample = 0; sample < DRYWET_FRAME_SIZE; sample += 1) {
        for (ear = 0; ear < 2; ear += 1) {
            node.ppBuffersIn[ear][sample] = 0.2f + 0.01f * (float)sample + (float)ear;
            node.ppBuffersOut[ear][sample] = -0.5f - 0.02f * (float)sample - (float)ear;
        }
    }
    if (ma_thread_create(&thread, ma_thread_priority_normal, 0,
        drywet_publish_during_mix, &publisher, NULL) != MA_SUCCESS)
        goto cleanup;
    ma_atomic_store_32(&g_pauseAfterLoad, 1);
    g_inCallback = 1;
    ma_steamaudio_binaural_node_mix_output(&node, MA_FALSE);
    g_inCallback = 0;
    ma_atomic_store_32(&g_pauseAfterLoad, 0);
    ma_thread_wait(&thread);
    if (publisher.result != MA_SUCCESS || node.appliedDryWet != 0.5f) {
        fprintf(stderr, "dry/wet publication changed the active block\n");
        goto cleanup;
    }
    for (sample = 0; sample < DRYWET_FRAME_SIZE; sample += 1) {
        for (ear = 0; ear < 2; ear += 1) {
            float dry = 0.2f + 0.01f * (float)sample + (float)ear;
            float wetOutput = -0.5f - 0.02f * (float)sample - (float)ear;
            if (!drywet_near(node.ppBuffersOut[ear][sample], 0.5f * dry + 0.5f * wetOutput)) {
                fprintf(stderr, "dry/wet publication tore the active block at %d/%d\n", sample, ear);
                goto cleanup;
            }
            node.ppBuffersOut[ear][sample] = wetOutput;
        }
    }
    g_inCallback = 1;
    ma_steamaudio_binaural_node_mix_output(&node, MA_FALSE);
    g_inCallback = 0;
    for (sample = 0; sample < DRYWET_FRAME_SIZE; sample += 1) {
        float blend = 0.5f + 0.5f * (float)(sample + 1) / (float)DRYWET_FRAME_SIZE;
        for (ear = 0; ear < 2; ear += 1) {
            float dry = 0.2f + 0.01f * (float)sample + (float)ear;
            float wetOutput = -0.5f - 0.02f * (float)sample - (float)ear;
            if (!drywet_near(node.ppBuffersOut[ear][sample],
                (1.0f - blend) * dry + blend * wetOutput)) {
                fprintf(stderr, "dry/wet publication did not apply next block at %d/%d\n", sample, ear);
                goto cleanup;
            }
        }
    }
    if (g_sdkCallbackAllocations != 0 || g_miniaudioCallbackAllocations != 0)
        goto cleanup;
    failed = 0;

cleanup:
    ma_atomic_store_32(&g_pauseAfterLoad, 0);
    if (releaseEventInitialized) ma_event_uninit(&g_dryWetReleaseEvent);
    if (loadedEventInitialized) ma_event_uninit(&g_dryWetLoadedEvent);
    if (nodeInitialized) ma_steamaudio_binaural_node_uninit(&node, pCallbacks);
    return failed;
}

static int drywet_reject_config(ma_node_graph* pGraph,
    const ma_steamaudio_binaural_node_config* pConfig,
    const ma_allocation_callbacks* pCallbacks)
{
    ma_steamaudio_binaural_node node;
    ma_result result = ma_steamaudio_binaural_node_init(pGraph, pConfig, pCallbacks, &node);
    if (result == MA_SUCCESS) ma_steamaudio_binaural_node_uninit(&node, pCallbacks);
    return result != MA_INVALID_ARGS;
}

int main(void)
{
    static const IPLVector3 directions[] = {
        {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}
    };
    static const float blends[] = {0.0f, 0.5f, 1.0f};
    ma_node_graph graph;
    ma_node_graph_config graphConfig = ma_node_graph_config_init(2);
    ma_allocation_callbacks callbacks;
    IPLContextSettings contextSettings;
    IPLAudioSettings audioSettings = {48000, DRYWET_FRAME_SIZE};
    IPLHRTFSettings hrtfSettings;
    IPLContext context = NULL;
    IPLHRTF hrtf = NULL;
    ma_uint32 channels;
    ma_uint32 interpolation;
    ma_uint32 direction;
    ma_uint32 signal;
    ma_uint32 blend;
    float parsedWet = -1.0f;
    int failures = 0;

    if (ma_steamaudio_parse_dry_wet("0", &parsedWet) != MA_SUCCESS || parsedWet != 0.0f ||
        ma_steamaudio_parse_dry_wet("0.5", &parsedWet) != MA_SUCCESS || parsedWet != 0.5f ||
        ma_steamaudio_parse_dry_wet("1", &parsedWet) != MA_SUCCESS || parsedWet != 1.0f ||
        ma_steamaudio_parse_dry_wet("-0.1", &parsedWet) != MA_INVALID_ARGS ||
        ma_steamaudio_parse_dry_wet("1.1", &parsedWet) != MA_INVALID_ARGS ||
        ma_steamaudio_parse_dry_wet("nan", &parsedWet) != MA_INVALID_ARGS ||
        ma_steamaudio_parse_dry_wet("0.5garbage", &parsedWet) != MA_INVALID_ARGS)
        return 1;

    MA_ZERO_OBJECT(&callbacks);
    callbacks.onMalloc = drywet_ma_malloc;
    callbacks.onRealloc = drywet_ma_realloc;
    callbacks.onFree = drywet_ma_free;
    if (ma_node_graph_init(&graphConfig, &callbacks, &graph) != MA_SUCCESS) return 1;
    MA_ZERO_OBJECT(&contextSettings);
    contextSettings.version = STEAMAUDIO_VERSION;
    contextSettings.allocateCallback = drywet_sdk_alloc;
    contextSettings.freeCallback = drywet_sdk_free;
    if (iplContextCreate(&contextSettings, &context) != IPL_STATUS_SUCCESS) {
        failures = 1;
        goto cleanup;
    }
    MA_ZERO_OBJECT(&hrtfSettings);
    hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
    hrtfSettings.volume = 1.0f;
    if (iplHRTFCreate(context, &audioSettings, &hrtfSettings, &hrtf) != IPL_STATUS_SUCCESS) {
        failures = 1;
        goto cleanup;
    }
    {
        ma_steamaudio_binaural_node_config config =
            ma_steamaudio_binaural_node_config_init(1, audioSettings, context, hrtf);
        if (config.dryWet != 1.0f || config.interpolation != IPL_HRTFINTERPOLATION_NEAREST)
            failures += 1;
        config.dryWet = -0.01f;
        failures += drywet_reject_config(&graph, &config, &callbacks);
        config.dryWet = 1.01f;
        failures += drywet_reject_config(&graph, &config, &callbacks);
        config.dryWet = NAN;
        failures += drywet_reject_config(&graph, &config, &callbacks);
        config.dryWet = 1.0f;
        config.interpolation = (IPLHRTFInterpolation)999;
        failures += drywet_reject_config(&graph, &config, &callbacks);
        if (ma_steamaudio_binaural_node_set_dry_wet(NULL, 0.5f) != MA_INVALID_ARGS)
            failures += 1;
    }

    for (channels = 1; channels <= 2; channels += 1)
        for (interpolation = 0; interpolation < 2; interpolation += 1)
            for (direction = 0; direction < sizeof(directions) / sizeof(directions[0]); direction += 1)
                for (signal = 0; signal < 2; signal += 1)
                    for (blend = 0; blend < sizeof(blends) / sizeof(blends[0]); blend += 1)
                        failures += drywet_case(&graph, context, hrtf, &callbacks,
                            channels, interpolation == 0 ? IPL_HRTFINTERPOLATION_NEAREST :
                            IPL_HRTFINTERPOLATION_BILINEAR, directions[direction], signal,
                            blends[blend]);
    failures += drywet_transition_and_tail(&graph, context, hrtf, &callbacks);
    failures += drywet_concurrent_publication(&graph, context, hrtf, &callbacks);

cleanup:
    iplHRTFRelease(&hrtf);
    iplContextRelease(&context);
    ma_node_graph_uninit(&graph, &callbacks);
    if (failures != 0) fprintf(stderr, "%d dry/wet cases failed\n", failures);
    return failures != 0;
}
