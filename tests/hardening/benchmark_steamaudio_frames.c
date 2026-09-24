#include <math.h>
#include <phonon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static IPLAudioEffectState benchmark_effect_apply(IPLBinauralEffect effect,
    IPLBinauralEffectParams* pParams, IPLAudioBuffer* pInput, IPLAudioBuffer* pOutput);

/* Time the adapter's frame staging, not HRTF convolution. The real effect is
   still created at node initialization, outside the timed callback. */
#define MA_STEAMAUDIO_NO_MAIN
#define iplBinauralEffectApply benchmark_effect_apply
#include "../../examples/engine_steamaudio.c"
#undef iplBinauralEffectApply

#define BENCH_FRAME_SIZE 256
#define BENCH_WARMUP_FRAMES 1024
#define BENCH_FRAMES_PER_TRIAL 4194304
#define BENCH_TRIALS 7

static ma_uint32 g_inputChannels;
static ma_uint32 g_effectCalls;
static ma_uint32 g_badDescriptor;
static ma_uint32 g_inCallback;
static ma_uint32 g_sdkCallbackAllocations;
static ma_uint32 g_miniaudioCallbackAllocations;
static volatile double g_checksumSink;

static void* IPLCALL benchmark_sdk_alloc(IPLsize size, IPLsize alignment)
{
    if (g_inCallback) g_sdkCallbackAllocations += 1;
    return ma_aligned_malloc((size_t)size, (size_t)alignment, NULL);
}

static void IPLCALL benchmark_sdk_free(void* pMemory)
{
    if (g_inCallback) g_sdkCallbackAllocations += 1;
    ma_aligned_free(pMemory, NULL);
}

static void* benchmark_ma_malloc(size_t size, void* pUserData)
{
    (void)pUserData;
    if (g_inCallback) g_miniaudioCallbackAllocations += 1;
    return malloc(size);
}

static void* benchmark_ma_realloc(void* p, size_t size, void* pUserData)
{
    (void)pUserData;
    if (g_inCallback) g_miniaudioCallbackAllocations += 1;
    return realloc(p, size);
}

static void benchmark_ma_free(void* p, void* pUserData)
{
    (void)pUserData;
    if (g_inCallback) g_miniaudioCallbackAllocations += 1;
    free(p);
}

static IPLAudioEffectState benchmark_effect_apply(IPLBinauralEffect effect,
    IPLBinauralEffectParams* pParams, IPLAudioBuffer* pInput, IPLAudioBuffer* pOutput)
{
    ma_uint32 sample;
    (void)effect;
    (void)pParams;
    if (pInput->numSamples != BENCH_FRAME_SIZE || pOutput->numSamples != BENCH_FRAME_SIZE ||
        pInput->numChannels != (IPLint32)g_inputChannels || pOutput->numChannels != 2) {
        g_badDescriptor += 1;
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
    for (sample = 0; sample < BENCH_FRAME_SIZE; sample += 1) {
        pOutput->data[0][sample] = pInput->data[0][sample];
        pOutput->data[1][sample] = pInput->data[g_inputChannels - 1][sample];
    }
    g_effectCalls += 1;
    return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
}

static float benchmark_input_value(ma_uint64 frame, ma_uint32 channel)
{
    return (float)((frame % 4096) + 1) / 4096.0f + (channel ? 0.5f : 0.0f);
}

static ma_uint32 benchmark_request(ma_uint32 pattern, ma_uint32 ordinal)
{
    static const ma_uint32 fragments[] = { 1, 17, 255, 256, 257, 511 };
    if (pattern == 0) return 256;
    if (pattern == 1) return 240;
    return fragments[ordinal % (sizeof(fragments) / sizeof(fragments[0]))];
}

static const char* benchmark_pattern_name(ma_uint32 pattern)
{
    if (pattern == 0) return "fixed256";
    if (pattern == 1) return "period240";
    return "fragmented";
}

static int benchmark_compare_double(const void* a, const void* b)
{
    double x = *(const double*)a;
    double y = *(const double*)b;
    return (x > y) - (x < y);
}

static int benchmark_run(ma_uint32 channels, ma_uint32 pattern)
{
    ma_node_graph graph;
    ma_node_graph_config graphConfig;
    ma_steamaudio_binaural_node node;
    ma_steamaudio_binaural_node_config nodeConfig;
    ma_allocation_callbacks callbacks;
    IPLContextSettings contextSettings;
    IPLAudioSettings audioSettings;
    IPLHRTFSettings hrtfSettings;
    IPLContext context = NULL;
    IPLHRTF hrtf = NULL;
    float warmup[BENCH_FRAME_SIZE * 2];
    float warmupOutput[BENCH_FRAME_SIZE * 2];
    float* input = NULL;
    float* output = NULL;
    double nanosPerFrame[BENCH_TRIALS];
    double nanosPerCallback[BENCH_TRIALS];
    double lastChecksum = 0.0;
    ma_uint32 lastCallbacksMade = 0;
    ma_uint32 sample;
    ma_uint32 channel;
    ma_uint32 trial;
    int graphInitialized = 0;
    int nodeInitialized = 0;
    int failed = 1;

    g_inputChannels = channels;
    g_effectCalls = 0;
    g_badDescriptor = 0;
    g_sdkCallbackAllocations = 0;
    g_miniaudioCallbackAllocations = 0;
    MA_ZERO_OBJECT(&callbacks);
    callbacks.onMalloc = benchmark_ma_malloc;
    callbacks.onRealloc = benchmark_ma_realloc;
    callbacks.onFree = benchmark_ma_free;
    graphConfig = ma_node_graph_config_init(2);
    if (ma_node_graph_init(&graphConfig, &callbacks, &graph) != MA_SUCCESS) goto cleanup;
    graphInitialized = 1;

    MA_ZERO_OBJECT(&contextSettings);
    contextSettings.version = STEAMAUDIO_VERSION;
    contextSettings.allocateCallback = benchmark_sdk_alloc;
    contextSettings.freeCallback = benchmark_sdk_free;
    if (iplContextCreate(&contextSettings, &context) != IPL_STATUS_SUCCESS) goto cleanup;
    MA_ZERO_OBJECT(&audioSettings);
    audioSettings.samplingRate = 48000;
    audioSettings.frameSize = BENCH_FRAME_SIZE;
    MA_ZERO_OBJECT(&hrtfSettings);
    hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
    hrtfSettings.volume = 1;
    if (iplHRTFCreate(context, &audioSettings, &hrtfSettings, &hrtf) != IPL_STATUS_SUCCESS) goto cleanup;
    nodeConfig = ma_steamaudio_binaural_node_config_init(channels, audioSettings, context, hrtf);
    if (ma_steamaudio_binaural_node_init(&graph, &nodeConfig, &callbacks, &node) != MA_SUCCESS) goto cleanup;
    nodeInitialized = 1;

    input = (float*)malloc((size_t)BENCH_FRAMES_PER_TRIAL * channels * sizeof(float));
    output = (float*)malloc((size_t)BENCH_FRAMES_PER_TRIAL * 2 * sizeof(float));
    if (input == NULL || output == NULL) goto cleanup;

    for (sample = 0; sample < BENCH_WARMUP_FRAMES; sample += BENCH_FRAME_SIZE) {
        const float* ppInput[1];
        float* ppOutput[1];
        ma_uint32 consumed = BENCH_FRAME_SIZE;
        ma_uint32 produced = BENCH_FRAME_SIZE;
        for (channel = 0; channel < channels; channel += 1) {
            ma_uint32 i;
            for (i = 0; i < BENCH_FRAME_SIZE; i += 1)
                warmup[i * channels + channel] = benchmark_input_value(sample + i, channel);
        }
        ppInput[0] = warmup;
        ppOutput[0] = warmupOutput;
        g_inCallback = 1;
        ma_steamaudio_binaural_node_process_pcm_frames((ma_node*)&node,
            ppInput, &consumed, ppOutput, &produced);
        g_inCallback = 0;
        if (consumed != BENCH_FRAME_SIZE || produced != BENCH_FRAME_SIZE) goto cleanup;
    }

    for (trial = 0; trial < BENCH_TRIALS; trial += 1) {
        ma_uint64 baseFrame = BENCH_WARMUP_FRAMES + (ma_uint64)trial * BENCH_FRAMES_PER_TRIAL;
        ma_uint32 offset = 0;
        ma_uint32 callbacksMade = 0;
        ma_uint32 effectCallsBefore = g_effectCalls;
        ma_timer timer;
        double elapsed;
        double checksum = 0.0;

        for (sample = 0; sample < BENCH_FRAMES_PER_TRIAL; sample += 1) {
            for (channel = 0; channel < channels; channel += 1)
                input[sample * channels + channel] = benchmark_input_value(baseFrame + sample, channel);
        }

        ma_timer_init(&timer);
        while (offset < BENCH_FRAMES_PER_TRIAL) {
            const float* ppInput[1];
            float* ppOutput[1];
            ma_uint32 request = benchmark_request(pattern, callbacksMade);
            ma_uint32 consumed;
            ma_uint32 produced;
            if (request > BENCH_FRAMES_PER_TRIAL - offset)
                request = BENCH_FRAMES_PER_TRIAL - offset;
            consumed = request;
            produced = request;
            ppInput[0] = input + offset * channels;
            ppOutput[0] = output + offset * 2;
            g_inCallback = 1;
            ma_steamaudio_binaural_node_process_pcm_frames((ma_node*)&node,
                ppInput, &consumed, ppOutput, &produced);
            g_inCallback = 0;
            if (consumed != request || produced != request) {
                fprintf(stderr, "FIFO frame accounting failed: channels=%u pattern=%s trial=%u.\n",
                    channels, benchmark_pattern_name(pattern), trial);
                goto cleanup;
            }
            offset += request;
            callbacksMade += 1;
        }
        elapsed = ma_timer_get_time_in_seconds(&timer);

        if (g_effectCalls - effectCallsBefore != BENCH_FRAMES_PER_TRIAL / BENCH_FRAME_SIZE ||
            g_badDescriptor != 0 || g_sdkCallbackAllocations != 0 ||
            g_miniaudioCallbackAllocations != 0) {
            fprintf(stderr, "FIFO effect descriptor, call count, or callback allocation failed: channels=%u pattern=%s trial=%u.\n",
                channels, benchmark_pattern_name(pattern), trial);
            goto cleanup;
        }
        for (sample = 0; sample < BENCH_FRAMES_PER_TRIAL; sample += 1) {
            ma_uint64 sourceFrame = baseFrame + sample - BENCH_FRAME_SIZE;
            for (channel = 0; channel < 2; channel += 1) {
                float observed = output[sample * 2 + channel];
                float expected = benchmark_input_value(sourceFrame,
                    channel && channels == 2 ? 1 : 0);
                if (!isfinite(observed) || observed != expected) {
                    fprintf(stderr, "FIFO output continuity failed: channels=%u pattern=%s trial=%u sample=%u channel=%u.\n",
                        channels, benchmark_pattern_name(pattern), trial, sample, channel);
                    goto cleanup;
                }
                checksum += observed;
            }
        }
        if (!isfinite(checksum) || checksum == 0.0 || elapsed <= 0.0) goto cleanup;
        g_checksumSink = checksum;
        lastChecksum = checksum;
        lastCallbacksMade = callbacksMade;
        nanosPerFrame[trial] = elapsed * 1e9 / BENCH_FRAMES_PER_TRIAL;
        nanosPerCallback[trial] = elapsed * 1e9 / callbacksMade;
        printf("sample,%u,%s,%u,%u,%u,%u,%.3f,%.3f,%.9f\n",
            channels, benchmark_pattern_name(pattern), trial, callbacksMade,
            BENCH_FRAMES_PER_TRIAL, g_effectCalls - effectCallsBefore,
            nanosPerFrame[trial], nanosPerCallback[trial], checksum);
    }

    qsort(nanosPerFrame, BENCH_TRIALS, sizeof(double), benchmark_compare_double);
    qsort(nanosPerCallback, BENCH_TRIALS, sizeof(double), benchmark_compare_double);
    printf("median,%u,%s,-1,%u,%u,%u,%.3f,%.3f,%.9f\n",
        channels, benchmark_pattern_name(pattern), lastCallbacksMade, BENCH_FRAMES_PER_TRIAL,
        BENCH_FRAMES_PER_TRIAL / BENCH_FRAME_SIZE, nanosPerFrame[BENCH_TRIALS / 2],
        nanosPerCallback[BENCH_TRIALS / 2], lastChecksum);
    failed = 0;

cleanup:
    free(input);
    free(output);
    if (nodeInitialized) ma_steamaudio_binaural_node_uninit(&node, &callbacks);
    iplHRTFRelease(&hrtf);
    iplContextRelease(&context);
    if (graphInitialized) ma_node_graph_uninit(&graph, &callbacks);
    return failed;
}

int main(void)
{
    ma_uint32 channels;
    ma_uint32 pattern;
    printf("# frame_size=256,warmup_frames=1024,frames_per_trial=262144,trials=7,effect=identity_stub\n");
    printf("phase,channels,pattern,trial,callbacks,frames,effect_calls,ns_per_frame,ns_per_callback,checksum\n");
    for (channels = 1; channels <= 2; channels += 1) {
        for (pattern = 0; pattern < 3; pattern += 1) {
            if (benchmark_run(channels, pattern) != 0) return 1;
        }
    }
    return 0;
}
