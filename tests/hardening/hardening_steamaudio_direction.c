#include <stdint.h>
#include <phonon.h>
#include <float.h>
#include <stdio.h>

static IPLAudioEffectState hardening_capture_apply(IPLBinauralEffect effect,
    IPLBinauralEffectParams* pParams, IPLAudioBuffer* pInput, IPLAudioBuffer* pOutput);

#define MA_STEAMAUDIO_NO_MAIN
/* The reviewed baseline predates MA_STEAMAUDIO_NO_MAIN. Rename only its demo
   entry point so this same regression can run against that exact source. */
#define main hardening_example_main
#define iplBinauralEffectApply hardening_capture_apply
#include "../../examples/engine_steamaudio.c"
#undef iplBinauralEffectApply
#undef main

#define HARDENING_DIRECTION_FRAMES 512
#define HARDENING_DIRECTION_READS 96
#define HARDENING_DIRECTION_UPDATES 4096

typedef struct
{
    ma_node_base baseNode;
} hardening_direction_source;

typedef struct
{
    ma_steamaudio_binaural_node* pNode;
    volatile ma_uint32 setterFailures;
    volatile ma_uint32 publishedCount;
} hardening_direction_publisher;

typedef struct
{
    ma_node_graph* pGraph;
    ma_uint32 failures;
    ma_uint32 signalBlocks;
} hardening_direction_reader;

static ma_event g_firstApplyEntered;
static ma_event g_resumeFirstApply;
static volatile ma_uint32 g_applyCount;
static volatile ma_uint32 g_tornCount;
static ma_vec3f g_firstDirection;
static ma_vec3f g_lastDirection;

/* These three distinct, non-axis directions are independent test input. */
static const ma_vec3f g_directions[3] = {
    { 0.36f, 0.48f, 0.8f },
    { 0.8f, 0.36f, 0.48f },
    { 0.48f, 0.8f, 0.36f }
};

static ma_bool32 hardening_direction_equals(ma_vec3f actual, ma_vec3f expected)
{
    return actual.x == expected.x && actual.y == expected.y && actual.z == expected.z;
}

static IPLAudioEffectState hardening_capture_apply(IPLBinauralEffect effect,
    IPLBinauralEffectParams* pParams, IPLAudioBuffer* pInput, IPLAudioBuffer* pOutput)
{
    ma_vec3f direction;
    ma_uint32 index;
    ma_uint32 i;
    ma_bool32 valid = MA_FALSE;

    direction.x = pParams->direction.x;
    direction.y = pParams->direction.y;
    direction.z = pParams->direction.z;
    for (i = 0; i < 3; i += 1) {
        valid |= hardening_direction_equals(direction, g_directions[i]);
    }
    if (!valid) {
        ma_atomic_fetch_add_32(&g_tornCount, 1);
    }

    g_lastDirection = direction;
    index = ma_atomic_fetch_add_32(&g_applyCount, 1);
    if (index == 0) {
        g_firstDirection = direction;
        ma_event_signal(&g_firstApplyEntered);
        ma_event_wait(&g_resumeFirstApply);
    }
    return iplBinauralEffectApply(effect, pParams, pInput, pOutput);
}

static void hardening_source_process(ma_node* pNode, const float** ppFramesIn,
    ma_uint32* pFrameCountIn, float** ppFramesOut, ma_uint32* pFrameCountOut)
{
    ma_uint32 i;
    (void)pNode;
    (void)ppFramesIn;
    for (i = 0; i < *pFrameCountOut; i += 1) {
        ppFramesOut[0][i] = (i == 0) ? 1.0f : 0.0f;
    }
    if (pFrameCountIn != NULL) *pFrameCountIn = 0;
}

static ma_node_vtable g_sourceVTable = {
    hardening_source_process, NULL, 0, 1, 0
};

static ma_thread_result MA_THREADCALL hardening_publish_directions(void* pData)
{
    hardening_direction_publisher* pPublisher = (hardening_direction_publisher*)pData;
    ma_uint32 i;

    ma_event_wait(&g_firstApplyEntered);
    for (i = 0; i < HARDENING_DIRECTION_UPDATES; i += 1) {
        const ma_vec3f* pDirection = &g_directions[i % 3];
        if (ma_steamaudio_binaural_node_set_direction(pPublisher->pNode,
                pDirection->x, pDirection->y, pDirection->z) != MA_SUCCESS) {
            ma_atomic_fetch_add_32(&pPublisher->setterFailures, 1);
        } else {
            ma_atomic_fetch_add_32(&pPublisher->publishedCount, 1);
        }
        if (i == 1023) ma_event_signal(&g_resumeFirstApply);
    }
    return (ma_thread_result)0;
}

static ma_thread_result MA_THREADCALL hardening_read_graph(void* pData)
{
    hardening_direction_reader* pReader = (hardening_direction_reader*)pData;
    float output[HARDENING_DIRECTION_FRAMES * 2];
    ma_uint64 framesRead;
    ma_uint32 block;
    ma_uint32 sample;
    ma_bool32 hasSignal;

    for (block = 0; block < HARDENING_DIRECTION_READS; block += 1) {
        framesRead = 0;
        if (ma_node_graph_read_pcm_frames(pReader->pGraph, output,
                HARDENING_DIRECTION_FRAMES, &framesRead) != MA_SUCCESS ||
            framesRead != HARDENING_DIRECTION_FRAMES) {
            pReader->failures += 1;
            break;
        }
        hasSignal = MA_FALSE;
        for (sample = 0; sample < HARDENING_DIRECTION_FRAMES * 2; sample += 1) {
            if (output[sample] != output[sample] ||
                output[sample] > FLT_MAX || output[sample] < -FLT_MAX) {
                pReader->failures += 1;
                break;
            }
            if (output[sample] > 0.000001f || output[sample] < -0.000001f) {
                hasSignal = MA_TRUE;
            }
        }
        if (sample != HARDENING_DIRECTION_FRAMES * 2) break;
        pReader->signalBlocks += hasSignal;
    }
    return (ma_thread_result)0;
}

static int hardening_test_graph_directions(void)
{
    ma_node_graph graph;
    ma_node_graph_config graphConfig;
    ma_node_config sourceConfig;
    hardening_direction_source source;
    ma_steamaudio_binaural_node node;
    ma_steamaudio_binaural_node_config nodeConfig;
    hardening_direction_publisher publisher;
    hardening_direction_reader reader;
    ma_thread publisherThread;
    ma_thread readerThread;
    IPLContextSettings contextSettings;
    IPLAudioSettings audioSettings;
    IPLHRTFSettings hrtfSettings;
    IPLContext context = NULL;
    IPLHRTF hrtf = NULL;
    ma_uint32 channels = 1;
    ma_uint32 i;
    ma_uint32 applyCountBeforeStop;
    ma_uint64 framesRead;
    float output[HARDENING_DIRECTION_FRAMES * 2];
    int eventsInitialized = 0;
    int graphInitialized = 0;
    int nodeInitialized = 0;
    int sourceInitialized = 0;
    int publisherStarted = 0;
    int readerStarted = 0;
    int concurrentRunStarted = 0;
    int failed = 0;

    MA_ZERO_OBJECT(&graph);
    MA_ZERO_OBJECT(&source);
    MA_ZERO_OBJECT(&node);
    MA_ZERO_OBJECT(&publisher);
    MA_ZERO_OBJECT(&reader);
    ma_atomic_store_32(&g_applyCount, 0);
    ma_atomic_store_32(&g_tornCount, 0);
    if (ma_event_init(&g_firstApplyEntered) != MA_SUCCESS) return 1;
    eventsInitialized = 1;
    if (ma_event_init(&g_resumeFirstApply) != MA_SUCCESS) goto cleanup;
    eventsInitialized = 2;

    graphConfig = ma_node_graph_config_init(2);
    if (ma_node_graph_init(&graphConfig, NULL, &graph) != MA_SUCCESS) goto cleanup;
    graphInitialized = 1;
    MA_ZERO_OBJECT(&contextSettings);
    contextSettings.version = STEAMAUDIO_VERSION;
    if (iplContextCreate(&contextSettings, &context) != IPL_STATUS_SUCCESS) goto cleanup;
    MA_ZERO_OBJECT(&audioSettings);
    audioSettings.samplingRate = 48000;
    audioSettings.frameSize = HARDENING_DIRECTION_FRAMES;
    MA_ZERO_OBJECT(&hrtfSettings);
    hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
    hrtfSettings.volume = 1;
    if (iplHRTFCreate(context, &audioSettings, &hrtfSettings, &hrtf) != IPL_STATUS_SUCCESS) goto cleanup;

    nodeConfig = ma_steamaudio_binaural_node_config_init(1, audioSettings, context, hrtf);
    nodeConfig.nodeConfig.initialState = ma_node_state_stopped;
    if (ma_steamaudio_binaural_node_init(&graph, &nodeConfig, NULL, &node) != MA_SUCCESS) goto cleanup;
    nodeInitialized = 1;
    sourceConfig = ma_node_config_init();
    sourceConfig.vtable = &g_sourceVTable;
    sourceConfig.pOutputChannels = &channels;
    if (ma_node_init(&graph, &sourceConfig, NULL, &source.baseNode) != MA_SUCCESS) goto cleanup;
    sourceInitialized = 1;
    if (ma_node_attach_output_bus(&source, 0, &node, 0) != MA_SUCCESS ||
        ma_node_attach_output_bus(&node, 0, ma_node_graph_get_endpoint(&graph), 0) != MA_SUCCESS) goto cleanup;

    /* A stopped node coalesces many publications into the latest full tuple. */
    for (i = 0; i < 64; i += 1) {
        const ma_vec3f* pDirection = &g_directions[i % 3];
        if (ma_steamaudio_binaural_node_set_direction(&node,
                pDirection->x, pDirection->y, pDirection->z) != MA_SUCCESS) goto cleanup;
    }
    if (ma_node_set_state(&node, ma_node_state_started) != MA_SUCCESS) goto cleanup;

    publisher.pNode = &node;
    reader.pGraph = &graph;
    if (ma_thread_create(&publisherThread, ma_thread_priority_normal, 0,
            hardening_publish_directions, &publisher, NULL) != MA_SUCCESS) goto cleanup;
    publisherStarted = 1;
    if (ma_thread_create(&readerThread, ma_thread_priority_normal, 0,
            hardening_read_graph, &reader, NULL) != MA_SUCCESS) goto cleanup;
    readerStarted = 1;
    concurrentRunStarted = 1;
    ma_thread_wait(&readerThread);
    readerStarted = 0;
    if (ma_atomic_load_32(&g_applyCount) == 0) ma_event_signal(&g_firstApplyEntered);
    ma_thread_wait(&publisherThread);
    publisherStarted = 0;

    if (!hardening_direction_equals(g_firstDirection, g_directions[63 % 3]) ||
        ma_atomic_load_32(&g_applyCount) < HARDENING_DIRECTION_READS ||
        ma_atomic_load_32(&g_tornCount) != 0 ||
        ma_atomic_load_32(&publisher.setterFailures) != 0 ||
        ma_atomic_load_32(&publisher.publishedCount) != HARDENING_DIRECTION_UPDATES ||
        reader.failures != 0 || reader.signalBlocks == 0) {
        printf("concurrent graph direction publication failed: apply=%u torn=%u setter=%u published=%u reader=%u signal=%u\n",
            ma_atomic_load_32(&g_applyCount), ma_atomic_load_32(&g_tornCount),
            ma_atomic_load_32(&publisher.setterFailures),
            ma_atomic_load_32(&publisher.publishedCount), reader.failures, reader.signalBlocks);
        failed = 1;
    }

    applyCountBeforeStop = ma_atomic_load_32(&g_applyCount);
    if (ma_node_set_state(&node, ma_node_state_stopped) != MA_SUCCESS ||
        ma_steamaudio_binaural_node_set_direction(&node,
            g_directions[1].x, g_directions[1].y, g_directions[1].z) != MA_SUCCESS) {
        failed = 1;
        goto cleanup;
    }
    framesRead = 0;
    if (ma_node_graph_read_pcm_frames(&graph, output,
            HARDENING_DIRECTION_FRAMES, &framesRead) != MA_SUCCESS ||
        framesRead != HARDENING_DIRECTION_FRAMES ||
        ma_atomic_load_32(&g_applyCount) != applyCountBeforeStop) {
        printf("stopped Steam Audio node processed an effect block\n");
        failed = 1;
    }
    if (ma_node_set_state(&node, ma_node_state_started) != MA_SUCCESS) {
        failed = 1;
        goto cleanup;
    }
    framesRead = 0;
    if (ma_node_graph_read_pcm_frames(&graph, output,
            HARDENING_DIRECTION_FRAMES, &framesRead) != MA_SUCCESS ||
        framesRead != HARDENING_DIRECTION_FRAMES ||
        ma_atomic_load_32(&g_applyCount) <= applyCountBeforeStop ||
        !hardening_direction_equals(g_lastDirection, g_directions[1])) {
        printf("restarted Steam Audio node missed its stopped-state direction\n");
        failed = 1;
    }

cleanup:
    if (readerStarted) {
        ma_event_signal(&g_firstApplyEntered);
        ma_event_signal(&g_resumeFirstApply);
        ma_thread_wait(&readerThread);
    }
    if (publisherStarted) {
        ma_event_signal(&g_firstApplyEntered);
        ma_thread_wait(&publisherThread);
    }
    if (nodeInitialized) ma_steamaudio_binaural_node_uninit(&node, NULL);
    if (sourceInitialized) ma_node_uninit(&source.baseNode, NULL);
    iplHRTFRelease(&hrtf);
    iplContextRelease(&context);
    if (graphInitialized) ma_node_graph_uninit(&graph, NULL);
    if (eventsInitialized >= 2) ma_event_uninit(&g_resumeFirstApply);
    if (eventsInitialized >= 1) ma_event_uninit(&g_firstApplyEntered);
    if (eventsInitialized != 2 || !graphInitialized || !nodeInitialized ||
        !sourceInitialized || !concurrentRunStarted) failed = 1;
    return failed;
}

int main(void)
{
    if (hardening_test_graph_directions() != 0) return 1;
    printf("Steam Audio direction graph publication passed\n");
    return 0;
}
