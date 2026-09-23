#include <stdint.h>
#include <phonon.h>
#include <float.h>
#include <stdio.h>
#include <string.h>

static IPLAudioEffectState hardening_capture_apply(IPLBinauralEffect effect,
    IPLBinauralEffectParams* pParams, IPLAudioBuffer* pInput, IPLAudioBuffer* pOutput);

#define MA_STEAMAUDIO_NO_MAIN
#define iplBinauralEffectApply hardening_capture_apply
#include "../../examples/engine_steamaudio.c"
#undef iplBinauralEffectApply

#define HARDENING_FRAME_SIZE 256
#define HARDENING_FIRST_INPUT 123
#define HARDENING_SECOND_INPUT (HARDENING_FRAME_SIZE - HARDENING_FIRST_INPUT - 1)
#define HARDENING_GUARD_VALUE 12345.25f
#define HARDENING_MAX_BLOCKS 8
#define HARDENING_MAX_CALL_FRAMES 512

typedef struct
{
    ma_uint32 calls;
    ma_uint32 badDescriptor;
    float inputs[HARDENING_MAX_BLOCKS][2][HARDENING_FRAME_SIZE];
    float outputs[HARDENING_MAX_BLOCKS][2][HARDENING_FRAME_SIZE];
} hardening_effect_capture;

static hardening_effect_capture g_capture;
static ma_uint32 g_inCallback;
static ma_uint32 g_callbackAllocations;
static ma_uint32 g_callbackFrees;

static void* IPLCALL hardening_ipl_allocate(IPLsize size, IPLsize alignment)
{
    if (g_inCallback) g_callbackAllocations += 1;
    return ma_aligned_malloc((size_t)size, (size_t)alignment, NULL);
}

static void IPLCALL hardening_ipl_free(void* pMemory)
{
    if (g_inCallback) g_callbackFrees += 1;
    ma_aligned_free(pMemory, NULL);
}

static IPLAudioEffectState hardening_capture_apply(IPLBinauralEffect effect,
    IPLBinauralEffectParams* pParams, IPLAudioBuffer* pInput, IPLAudioBuffer* pOutput)
{
    ma_uint32 channel;
    ma_uint32 sample;

    if (pInput->numSamples != HARDENING_FRAME_SIZE ||
        pOutput->numSamples != HARDENING_FRAME_SIZE ||
        pInput->numChannels < 1 || pInput->numChannels > 2 ||
        pOutput->numChannels != 2) {
        g_capture.badDescriptor += 1;
    }
    if (g_capture.calls < HARDENING_MAX_BLOCKS && pInput->numSamples == HARDENING_FRAME_SIZE &&
        pInput->numChannels >= 1 && pInput->numChannels <= 2) {
        for (channel = 0; channel < (ma_uint32)pInput->numChannels; channel += 1) {
            for (sample = 0; sample < HARDENING_FRAME_SIZE; sample += 1) {
                g_capture.inputs[g_capture.calls][channel][sample] = pInput->data[channel][sample];
            }
        }
    }
    {
        IPLAudioEffectState state = iplBinauralEffectApply(effect, pParams, pInput, pOutput);
        if (g_capture.calls < HARDENING_MAX_BLOCKS && pOutput->numSamples == HARDENING_FRAME_SIZE &&
            pOutput->numChannels == 2) {
            for (channel = 0; channel < 2; channel += 1) {
                for (sample = 0; sample < HARDENING_FRAME_SIZE; sample += 1) {
                    g_capture.outputs[g_capture.calls][channel][sample] = pOutput->data[channel][sample];
                }
            }
        }
        g_capture.calls += 1;
        return state;
    }
}

static int hardening_process_expected(ma_steamaudio_binaural_node* pNode,
    const float* pInput, ma_uint32 inputFrames, ma_uint32 expectedConsumed,
    ma_uint32 outputFrames, float* pCapturedOutput)
{
    const float* ppInput[1];
    float* ppOutput[1];
    float guardedOutput[HARDENING_MAX_CALL_FRAMES * 2 + 8];
    ma_uint32 consumed = inputFrames;
    ma_uint32 produced = outputFrames;
    ma_uint32 i;

    if (outputFrames > HARDENING_MAX_CALL_FRAMES) return 1;
    for (i = 0; i < HARDENING_MAX_CALL_FRAMES * 2 + 8; i += 1) {
        guardedOutput[i] = HARDENING_GUARD_VALUE;
    }
    ppInput[0] = pInput;
    ppOutput[0] = guardedOutput + 4;
    g_inCallback = 1;
    ma_steamaudio_binaural_node_process_pcm_frames((ma_node*)pNode,
        (pInput != NULL) ? ppInput : NULL, &consumed, ppOutput, &produced);
    g_inCallback = 0;
    if (consumed != expectedConsumed || produced != outputFrames) {
        printf("frame accounting changed: input %u/%u output %u/%u\n",
            consumed, expectedConsumed, produced, outputFrames);
        return 1;
    }
    for (i = 0; i < 4; i += 1) {
        if (guardedOutput[i] != HARDENING_GUARD_VALUE ||
            guardedOutput[4 + outputFrames * 2 + i] != HARDENING_GUARD_VALUE) {
            printf("partial callback overwrote its output guard\n");
            return 1;
        }
    }
    for (i = 0; i < outputFrames * 2; i += 1) {
        if (guardedOutput[4 + i] != guardedOutput[4 + i] ||
            guardedOutput[4 + i] > FLT_MAX || guardedOutput[4 + i] < -FLT_MAX) {
            printf("partial callback produced a nonfinite sample\n");
            return 1;
        }
    }
    if (pCapturedOutput != NULL) {
        memcpy(pCapturedOutput, guardedOutput + 4, outputFrames * 2 * sizeof(float));
    }
    return 0;
}

static int hardening_process(ma_steamaudio_binaural_node* pNode,
    const float* pInput, ma_uint32 inputFrames, ma_uint32 outputFrames, float* pCapturedOutput)
{
    return hardening_process_expected(pNode, pInput, inputFrames,
        (pInput != NULL) ? inputFrames : 0, outputFrames, pCapturedOutput);
}

static int hardening_test_null_input_gap(ma_uint32 channels)
{
    ma_node_graph graph;
    ma_node_graph_config graphConfig;
    ma_steamaudio_binaural_node node;
    ma_steamaudio_binaural_node_config nodeConfig;
    IPLContextSettings contextSettings;
    IPLAudioSettings audioSettings;
    IPLHRTFSettings hrtfSettings;
    IPLContext context = NULL;
    IPLHRTF hrtf = NULL;
    float first[HARDENING_FIRST_INPUT * 2];
    float second[HARDENING_SECOND_INPUT * 2];
    float extra[(HARDENING_FRAME_SIZE + 1) * 2];
    float poison;
    uint32_t poisonBits = 0x7fc00001u;
    ma_uint32 channel;
    ma_uint32 sample;
    int graphInitialized = 0;
    int nodeInitialized = 0;
    int failed = 0;

    memcpy(&poison, &poisonBits, sizeof(poison));
    MA_ZERO_OBJECT(&g_capture);
    g_callbackAllocations = 0;
    g_callbackFrees = 0;
    graphConfig = ma_node_graph_config_init(2);
    if (ma_node_graph_init(&graphConfig, NULL, &graph) != MA_SUCCESS) return 1;
    graphInitialized = 1;
    MA_ZERO_OBJECT(&contextSettings);
    contextSettings.version = STEAMAUDIO_VERSION;
    contextSettings.allocateCallback = hardening_ipl_allocate;
    contextSettings.freeCallback = hardening_ipl_free;
    if (iplContextCreate(&contextSettings, &context) != IPL_STATUS_SUCCESS) goto cleanup;
    MA_ZERO_OBJECT(&audioSettings);
    audioSettings.samplingRate = 48000;
    audioSettings.frameSize = HARDENING_FRAME_SIZE;
    MA_ZERO_OBJECT(&hrtfSettings);
    hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
    hrtfSettings.volume = 1;
    if (iplHRTFCreate(context, &audioSettings, &hrtfSettings, &hrtf) != IPL_STATUS_SUCCESS) goto cleanup;
    nodeConfig = ma_steamaudio_binaural_node_config_init(channels, audioSettings, context, hrtf);
    if (ma_steamaudio_binaural_node_init(&graph, &nodeConfig, NULL, &node) != MA_SUCCESS) goto cleanup;
    nodeInitialized = 1;

    /* Every unused planar sample is poisoned before any input arrives. */
    for (channel = 0; channel < channels; channel += 1) {
        for (sample = 0; sample < HARDENING_FRAME_SIZE; sample += 1) {
            node.ppBuffersIn[channel][sample] = poison;
        }
    }
    for (sample = 0; sample < HARDENING_FIRST_INPUT; sample += 1) {
        for (channel = 0; channel < channels; channel += 1) {
            first[sample * channels + channel] =
                (float)(1000 * channel + sample + 1) / 1024.0f;
        }
    }
    for (sample = 0; sample < HARDENING_SECOND_INPUT; sample += 1) {
        for (channel = 0; channel < channels; channel += 1) {
            second[sample * channels + channel] =
                (float)(1000 * channel + HARDENING_FIRST_INPUT + sample + 1) / 1024.0f;
        }
    }

    /* Null input from ALLOW_NULL_INPUT means unavailable input, not an EOF. */
    failed += hardening_process(&node, first, HARDENING_FIRST_INPUT, HARDENING_FIRST_INPUT, NULL);
    if (g_capture.calls != 0) {
        printf("%u-channel adapter submitted an incomplete first fragment\n", channels);
        failed = 1;
    }
    failed += hardening_process(&node, NULL, 0, 1, NULL);
    if (g_capture.calls != 0) {
        printf("%u-channel null-input gap prematurely flushed a partial effect frame\n", channels);
        failed = 1;
    }
    failed += hardening_process(&node, second, HARDENING_SECOND_INPUT, HARDENING_SECOND_INPUT, NULL);
    if (g_capture.calls != 1 || g_capture.badDescriptor != 0) {
        printf("%u-channel adapter submitted %u effect frames with %u bad descriptors\n",
            channels, g_capture.calls, g_capture.badDescriptor);
        failed = 1;
    } else {
        for (channel = 0; channel < channels; channel += 1) {
            for (sample = 0; sample < HARDENING_FRAME_SIZE; sample += 1) {
                float expected;
                if (sample < HARDENING_FIRST_INPUT) {
                    expected = first[sample * channels + channel];
                } else if (sample == HARDENING_FIRST_INPUT) {
                    expected = 0;
                } else {
                    expected = second[(sample - HARDENING_FIRST_INPUT - 1) * channels + channel];
                }
                if (g_capture.inputs[0][channel][sample] != expected) {
                    printf("%u-channel effect input differs at channel %u sample %u\n",
                        channels, channel, sample);
                    failed = 1;
                    break;
                }
            }
        }
    }
    if (g_callbackAllocations != 0 || g_callbackFrees != 0) {
        printf("%u-channel callback allocated/freed %u/%u times\n", channels,
            g_callbackAllocations, g_callbackFrees);
        failed = 1;
    }
    for (sample = 0; sample < HARDENING_FRAME_SIZE + 1; sample += 1) {
        for (channel = 0; channel < channels; channel += 1) {
            extra[sample * channels + channel] = (float)(sample + 1 + channel * 7) / 2048.0f;
        }
    }
    failed += hardening_process_expected(&node, extra, HARDENING_FRAME_SIZE + 1,
        HARDENING_FRAME_SIZE, HARDENING_FRAME_SIZE, NULL);
    if (g_capture.calls != 2 || g_capture.badDescriptor != 0) {
        printf("%u-channel input capacity exceeded output capacity incorrectly\n", channels);
        failed = 1;
    } else {
        for (sample = 0; sample < HARDENING_FRAME_SIZE; sample += 1) {
            for (channel = 0; channel < channels; channel += 1) {
                if (g_capture.inputs[1][channel][sample] != extra[sample * channels + channel]) {
                    printf("%u-channel partial input consumption lost sample %u\n", channels, sample);
                    failed = 1;
                    break;
                }
            }
        }
    }
    failed += hardening_process(&node, extra + HARDENING_FRAME_SIZE * channels, 1, 1, NULL);

cleanup:
    if (nodeInitialized) ma_steamaudio_binaural_node_uninit(&node, NULL);
    iplHRTFRelease(&hrtf);
    iplContextRelease(&context);
    if (graphInitialized) ma_node_graph_uninit(&graph, NULL);
    if (!nodeInitialized) failed = 1;
    return failed;
}

static int hardening_test_split_stream_and_seek(ma_uint32 channels)
{
    static const ma_uint32 splits[] = {1, 255, 17, 239, 240, 16, 511, 257};
    enum { totalFrames = 6 * HARDENING_FRAME_SIZE };
    ma_node_graph graph;
    ma_node_graph_config graphConfig;
    ma_steamaudio_binaural_node node;
    ma_steamaudio_binaural_node_config nodeConfig;
    IPLContextSettings contextSettings;
    IPLAudioSettings audioSettings;
    IPLHRTFSettings hrtfSettings;
    IPLContext context = NULL;
    IPLHRTF hrtf = NULL;
    float input[totalFrames * 2];
    float observed[totalFrames * 2];
    float finalInput[123 * 2];
    float finalOutput[HARDENING_FRAME_SIZE * 2];
    float restartedInput[HARDENING_FRAME_SIZE * 2];
    float restartedOutput[HARDENING_FRAME_SIZE * 2];
    ma_uint32 frame;
    ma_uint32 channel;
    ma_uint32 split;
    ma_uint32 offset = 0;
    ma_uint32 tailFrames = 0;
    ma_uint32 tailBound;
    ma_bool32 sawRestartedSignal = MA_FALSE;
    int graphInitialized = 0;
    int nodeInitialized = 0;
    int failed = 0;

    MA_ZERO_OBJECT(&g_capture);
    g_callbackAllocations = 0;
    g_callbackFrees = 0;
    graphConfig = ma_node_graph_config_init(2);
    if (ma_node_graph_init(&graphConfig, NULL, &graph) != MA_SUCCESS) return 1;
    graphInitialized = 1;
    MA_ZERO_OBJECT(&contextSettings);
    contextSettings.version = STEAMAUDIO_VERSION;
    contextSettings.allocateCallback = hardening_ipl_allocate;
    contextSettings.freeCallback = hardening_ipl_free;
    if (iplContextCreate(&contextSettings, &context) != IPL_STATUS_SUCCESS) goto cleanup;
    MA_ZERO_OBJECT(&audioSettings);
    audioSettings.samplingRate = 48000;
    audioSettings.frameSize = HARDENING_FRAME_SIZE;
    MA_ZERO_OBJECT(&hrtfSettings);
    hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
    hrtfSettings.volume = 1;
    if (iplHRTFCreate(context, &audioSettings, &hrtfSettings, &hrtf) != IPL_STATUS_SUCCESS) goto cleanup;
    nodeConfig = ma_steamaudio_binaural_node_config_init(channels, audioSettings, context, hrtf);
    if (ma_steamaudio_binaural_node_init(&graph, &nodeConfig, NULL, &node) != MA_SUCCESS) goto cleanup;
    nodeInitialized = 1;

    for (frame = 0; frame < totalFrames; frame += 1) {
        for (channel = 0; channel < channels; channel += 1) {
            input[frame * channels + channel] =
                (float)((frame % HARDENING_FRAME_SIZE) + 1 + channel * 97) / 4096.0f;
        }
    }
    for (split = 0; split < sizeof(splits) / sizeof(splits[0]); split += 1) {
        failed += hardening_process(&node, input + offset * channels, splits[split],
            splits[split], observed + offset * 2);
        offset += splits[split];
    }
    if (offset != totalFrames || g_capture.calls != 6 || g_capture.badDescriptor != 0) {
        printf("%u-channel split stream had %u input frames, %u effect calls, %u bad descriptors\n",
            channels, offset, g_capture.calls, g_capture.badDescriptor);
        failed = 1;
    }
    if (g_capture.calls == 6) {
        for (frame = 0; frame < totalFrames; frame += 1) {
            ma_uint32 inputBlock = frame / HARDENING_FRAME_SIZE;
            ma_uint32 inputSample = frame % HARDENING_FRAME_SIZE;
            for (channel = 0; channel < channels; channel += 1) {
                if (g_capture.inputs[inputBlock][channel][inputSample] != input[frame * channels + channel]) {
                    printf("%u-channel split input lost frame %u channel %u\n", channels, frame, channel);
                    failed = 1;
                    break;
                }
            }
            for (channel = 0; channel < 2; channel += 1) {
                float expected = 0;
                if (frame >= HARDENING_FRAME_SIZE) {
                    ma_uint32 outputBlock = (frame - HARDENING_FRAME_SIZE) / HARDENING_FRAME_SIZE;
                    ma_uint32 outputSample = frame % HARDENING_FRAME_SIZE;
                    expected = g_capture.outputs[outputBlock][channel][outputSample];
                }
                if (observed[frame * 2 + channel] != expected) {
                    printf("%u-channel split output lost frame %u channel %u\n", channels, frame, channel);
                    failed = 1;
                    break;
                }
            }
        }
    }

    /* A short final input is padded only after an explicit EOF, and its output remains queued. */
    for (frame = 0; frame < 123; frame += 1) {
        for (channel = 0; channel < channels; channel += 1) {
            finalInput[frame * channels + channel] = (float)(frame + channel + 1) / 8192.0f;
        }
    }
    failed += hardening_process(&node, finalInput, 123, 123, finalOutput);
    if (g_capture.calls != 6) {
        printf("%u-channel short final input was processed before EOF\n", channels);
        failed = 1;
    }
    if (ma_steamaudio_binaural_node_end_input(&node) != MA_SUCCESS) {
        printf("%u-channel EOF signal failed\n", channels);
        failed = 1;
    }
    failed += hardening_process(&node, NULL, 0, 133, finalOutput + 123 * 2);
    if (node.inputFrameCount != 123 || g_capture.calls != 6) {
        printf("%u-channel EOF discarded queued output or flushed early\n", channels);
        failed = 1;
    }
    for (frame = 0; frame < HARDENING_FRAME_SIZE; frame += 1) {
        for (channel = 0; channel < 2; channel += 1) {
            if (finalOutput[frame * 2 + channel] != g_capture.outputs[5][channel][frame]) {
                printf("%u-channel EOF lost queued output frame %u channel %u\n", channels, frame, channel);
                failed = 1;
                break;
            }
        }
    }
    failed += hardening_process(&node, NULL, 0, 1, finalOutput);
    if (g_capture.calls != 7 || node.inputFrameCount != 0) {
        printf("%u-channel EOF did not flush exactly one final block\n", channels);
        failed = 1;
    }
    if (g_capture.calls == 7) {
        for (frame = 0; frame < HARDENING_FRAME_SIZE; frame += 1) {
            for (channel = 0; channel < channels; channel += 1) {
                float expected = frame < 123 ? finalInput[frame * channels + channel] : 0;
                if (g_capture.inputs[6][channel][frame] != expected) {
                    printf("%u-channel final block padding wrong at frame %u channel %u\n",
                        channels, frame, channel);
                    failed = 1;
                    break;
                }
            }
        }
    }

    tailBound = node.outputFrameCount +
        ((ma_uint32)iplBinauralEffectGetTailSize(node.iplEffect) + HARDENING_FRAME_SIZE - 1) /
            HARDENING_FRAME_SIZE * HARDENING_FRAME_SIZE + HARDENING_FRAME_SIZE;
    while ((!node.tailComplete || node.outputFrameCount != 0) && tailFrames <= tailBound) {
        failed += hardening_process(&node, NULL, 0, 97, NULL);
        tailFrames += 97;
    }
    if (!node.tailComplete || node.outputFrameCount != 0 || tailFrames > tailBound) {
        printf("%u-channel EOF tail did not terminate within its reported bound\n", channels);
        failed = 1;
    }

    /* A seek discards old pending output and starts with exactly one silent frame. */
    if (ma_steamaudio_binaural_node_reset(&node) != MA_SUCCESS ||
        node.inputFrameCount != 0 || node.outputFrameCount != 0 || node.inputEnded != 0) {
        printf("%u-channel detached seek reset failed\n", channels);
        failed = 1;
    }
    MA_ZERO_MEMORY(restartedInput, sizeof(restartedInput));
    restartedInput[0] = 1;
    if (channels == 2) restartedInput[1] = -0.5f;
    failed += hardening_process(&node, restartedInput, HARDENING_FRAME_SIZE,
        HARDENING_FRAME_SIZE, restartedOutput);
    if (g_capture.calls != 8 || g_capture.badDescriptor != 0) {
        printf("%u-channel seek did not submit one exact effect frame\n", channels);
        failed = 1;
    }
    for (frame = 0; frame < HARDENING_FRAME_SIZE * 2; frame += 1) {
        if (restartedOutput[frame] != 0) {
            printf("%u-channel seek leaked old output at sample %u\n", channels, frame);
            failed = 1;
            break;
        }
    }
    MA_ZERO_MEMORY(restartedInput, sizeof(restartedInput));
    failed += hardening_process(&node, restartedInput, HARDENING_FRAME_SIZE,
        HARDENING_FRAME_SIZE, restartedOutput);
    for (frame = 0; frame < HARDENING_FRAME_SIZE; frame += 1) {
        for (channel = 0; channel < 2; channel += 1) {
            if (restartedOutput[frame * 2 + channel] != g_capture.outputs[7][channel][frame]) {
                printf("%u-channel seek lost restarted output frame %u channel %u\n",
                    channels, frame, channel);
                failed = 1;
                break;
            }
            if (restartedOutput[frame * 2 + channel] > 0.000001f ||
                restartedOutput[frame * 2 + channel] < -0.000001f) sawRestartedSignal = MA_TRUE;
        }
    }
    if (!sawRestartedSignal) {
        printf("%u-channel seek impulse produced finite silence\n", channels);
        failed = 1;
    }
    if (g_callbackAllocations != 0 || g_callbackFrees != 0) {
        printf("%u-channel callback allocated/freed %u/%u times\n", channels,
            g_callbackAllocations, g_callbackFrees);
        failed = 1;
    }

cleanup:
    if (nodeInitialized) ma_steamaudio_binaural_node_uninit(&node, NULL);
    iplHRTFRelease(&hrtf);
    iplContextRelease(&context);
    if (graphInitialized) ma_node_graph_uninit(&graph, NULL);
    if (!nodeInitialized) failed = 1;
    return failed;
}

static int hardening_test_offline_graph(ma_uint32 channels, ma_uint32 graphPeriod, ma_bool32 scheduled)
{
    static const ma_uint32 requests[] = {1, 17, 255, 256, 257, 511};
    ma_node_graph graph;
    ma_node_graph_config graphConfig;
    ma_steamaudio_binaural_node node;
    ma_steamaudio_binaural_node_config nodeConfig;
    ma_waveform waveform;
    ma_waveform_config waveformConfig;
    ma_data_source_node source;
    ma_data_source_node_config sourceConfig;
    IPLContextSettings contextSettings;
    IPLAudioSettings audioSettings;
    IPLHRTFSettings hrtfSettings;
    IPLContext context = NULL;
    IPLHRTF hrtf = NULL;
    float output[HARDENING_MAX_CALL_FRAMES * 2];
    float poison;
    uint32_t poisonBits = 0x7fc00001u;
    ma_uint32 request;
    ma_uint32 sample;
    ma_uint32 channel;
    ma_uint32 captured;
    ma_bool32 sawSignal = MA_FALSE;
    int graphInitialized = 0;
    int nodeInitialized = 0;
    int waveformInitialized = 0;
    int sourceInitialized = 0;
    int failed = 0;

    memcpy(&poison, &poisonBits, sizeof(poison));
    MA_ZERO_OBJECT(&g_capture);
    g_callbackAllocations = 0;
    g_callbackFrees = 0;
    graphConfig = ma_node_graph_config_init(2);
    graphConfig.processingSizeInFrames = graphPeriod;
    if (ma_node_graph_init(&graphConfig, NULL, &graph) != MA_SUCCESS) return 1;
    graphInitialized = 1;
    MA_ZERO_OBJECT(&contextSettings);
    contextSettings.version = STEAMAUDIO_VERSION;
    contextSettings.allocateCallback = hardening_ipl_allocate;
    contextSettings.freeCallback = hardening_ipl_free;
    if (iplContextCreate(&contextSettings, &context) != IPL_STATUS_SUCCESS) goto cleanup;
    MA_ZERO_OBJECT(&audioSettings);
    audioSettings.samplingRate = 48000;
    audioSettings.frameSize = HARDENING_FRAME_SIZE;
    MA_ZERO_OBJECT(&hrtfSettings);
    hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
    hrtfSettings.volume = 1;
    if (iplHRTFCreate(context, &audioSettings, &hrtfSettings, &hrtf) != IPL_STATUS_SUCCESS) goto cleanup;
    nodeConfig = ma_steamaudio_binaural_node_config_init(channels, audioSettings, context, hrtf);
    if (ma_steamaudio_binaural_node_init(&graph, &nodeConfig, NULL, &node) != MA_SUCCESS) goto cleanup;
    nodeInitialized = 1;
    for (channel = 0; channel < channels; channel += 1) {
        for (sample = 0; sample < HARDENING_FRAME_SIZE; sample += 1) {
            node.ppBuffersIn[channel][sample] = poison;
        }
    }
    for (channel = 0; channel < 2; channel += 1) {
        for (sample = 0; sample < HARDENING_FRAME_SIZE; sample += 1) {
            node.ppBuffersOut[channel][sample] = poison;
        }
    }
    waveformConfig = ma_waveform_config_init(ma_format_f32, channels, 48000,
        ma_waveform_type_sine, 0.125, 440);
    if (ma_waveform_init(&waveformConfig, &waveform) != MA_SUCCESS) goto cleanup;
    waveformInitialized = 1;
    sourceConfig = ma_data_source_node_config_init(&waveform);
    if (ma_data_source_node_init(&graph, &sourceConfig, NULL, &source) != MA_SUCCESS) goto cleanup;
    sourceInitialized = 1;
    if (ma_node_attach_output_bus(&source, 0, &node, 0) != MA_SUCCESS ||
        ma_node_attach_output_bus(&node, 0, ma_node_graph_get_endpoint(&graph), 0) != MA_SUCCESS) goto cleanup;
    if (ma_steamaudio_binaural_node_reset(&node) != MA_INVALID_OPERATION) {
        printf("%u-channel %u-period graph allowed reset while attached\n", channels, graphPeriod);
        failed = 1;
    }
    if (scheduled) {
        if (ma_node_set_state_time(&node, ma_node_state_started, 1) != MA_SUCCESS ||
            ma_node_set_state_time(&node, ma_node_state_stopped, 1100) != MA_SUCCESS) goto cleanup;
    }
    for (request = 0; request < sizeof(requests) / sizeof(requests[0]); request += 1) {
        ma_uint64 read = 0;
        for (sample = 0; sample < requests[request] * 2; sample += 1) output[sample] = poison;
        g_inCallback = 1;
        if (ma_node_graph_read_pcm_frames(&graph, output, requests[request], &read) != MA_SUCCESS) {
            g_inCallback = 0;
            printf("%u-channel %u-period graph read failed\n", channels, graphPeriod);
            failed = 1;
            break;
        }
        g_inCallback = 0;
        if (read != requests[request]) {
            printf("%u-channel %u-period graph returned %llu/%u frames\n", channels,
                graphPeriod, (unsigned long long)read, requests[request]);
            failed = 1;
        }
        for (sample = 0; sample < requests[request] * 2; sample += 1) {
            if (output[sample] != output[sample] || output[sample] > FLT_MAX || output[sample] < -FLT_MAX) {
                printf("%u-channel %u-period graph produced nonfinite output\n", channels, graphPeriod);
                failed = 1;
                break;
            }
            if (output[sample] > 0.000001f || output[sample] < -0.000001f) sawSignal = MA_TRUE;
        }
    }
    if (g_capture.calls == 0 || g_capture.badDescriptor != 0 || !sawSignal) {
        printf("%u-channel %u-period scheduled=%u graph had calls=%u bad=%u signal=%u\n",
            channels, graphPeriod, scheduled, g_capture.calls, g_capture.badDescriptor, sawSignal);
        failed = 1;
    }
    captured = g_capture.calls < HARDENING_MAX_BLOCKS ? g_capture.calls : HARDENING_MAX_BLOCKS;
    for (request = 0; request < captured; request += 1) {
        for (channel = 0; channel < channels; channel += 1) {
            for (sample = 0; sample < HARDENING_FRAME_SIZE; sample += 1) {
                float value = g_capture.inputs[request][channel][sample];
                if (value != value || value > FLT_MAX || value < -FLT_MAX) {
                    printf("%u-channel %u-period graph submitted poisoned input\n", channels, graphPeriod);
                    failed = 1;
                    break;
                }
            }
        }
    }
    if (g_callbackAllocations != 0 || g_callbackFrees != 0) {
        printf("%u-channel %u-period graph callback allocated/freed %u/%u times\n",
            channels, graphPeriod, g_callbackAllocations, g_callbackFrees);
        failed = 1;
    }

cleanup:
    if (nodeInitialized) ma_steamaudio_binaural_node_uninit(&node, NULL);
    if (sourceInitialized) ma_data_source_node_uninit(&source, NULL);
    if (waveformInitialized) ma_waveform_uninit(&waveform);
    iplHRTFRelease(&hrtf);
    iplContextRelease(&context);
    if (graphInitialized) ma_node_graph_uninit(&graph, NULL);
    if (!nodeInitialized || !sourceInitialized) failed = 1;
    return failed;
}

int main(void)
{
    int failures = 0;
    failures += hardening_test_null_input_gap(1);
    failures += hardening_test_null_input_gap(2);
    failures += hardening_test_split_stream_and_seek(1);
    failures += hardening_test_split_stream_and_seek(2);
    failures += hardening_test_offline_graph(1, 240, MA_FALSE);
    failures += hardening_test_offline_graph(2, 240, MA_FALSE);
    failures += hardening_test_offline_graph(1, 0, MA_FALSE);
    failures += hardening_test_offline_graph(2, 0, MA_FALSE);
    failures += hardening_test_offline_graph(1, 256, MA_FALSE);
    failures += hardening_test_offline_graph(2, 256, MA_FALSE);
    failures += hardening_test_offline_graph(1, 240, MA_TRUE);
    failures += hardening_test_offline_graph(2, 240, MA_TRUE);
    failures += hardening_test_offline_graph(1, 256, MA_TRUE);
    failures += hardening_test_offline_graph(2, 256, MA_TRUE);
    if (failures == 0) printf("Steam Audio mono/stereo null-input frame accounting passed\n");
    return failures ? 1 : 0;
}
