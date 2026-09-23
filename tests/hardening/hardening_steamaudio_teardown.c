#include <stdint.h>
#include <phonon.h>
#include <stdio.h>
#include <stdlib.h>

static void hardening_detach_before_reader_wait(void* pOutputBus);
static IPLAudioEffectState hardening_binaural_effect_apply(IPLBinauralEffect effect,
    IPLBinauralEffectParams* pParams, IPLAudioBuffer* pInput, IPLAudioBuffer* pOutput);
static void hardening_binaural_effect_release(IPLBinauralEffect* pEffect);

#define MA_STEAMAUDIO_NO_MAIN
#define MA_NODE_DETACH_BEFORE_READER_WAIT_HOOK(pOutputBus) hardening_detach_before_reader_wait((void*)(pOutputBus))
#define iplBinauralEffectApply hardening_binaural_effect_apply
#define iplBinauralEffectRelease hardening_binaural_effect_release
#include "../../examples/engine_steamaudio.c"
#undef iplBinauralEffectApply
#undef iplBinauralEffectRelease
#undef MA_NODE_DETACH_BEFORE_READER_WAIT_HOOK

typedef struct
{
    ma_event applyEntered;
    ma_event allowApply;
    ma_event transition;
    ma_event graphDone;
    ma_node_graph graph;
    ma_steamaudio_binaural_node node;
    ma_allocation_callbacks allocationCallbacks;
    ma_node_output_bus* pOutputBus;
    IPLBinauralEffect effect;
    void* pStaging;
    volatile ma_uint32 applyCount;
    volatile ma_uint32 applyActive;
    volatile ma_uint32 detachReached;
    volatile ma_uint32 readerCountAtDetach;
    volatile ma_uint32 releaseCount;
    volatile ma_uint32 prematureRelease;
    volatile ma_uint32 stagingFreed;
    volatile ma_uint32 prematureStagingFree;
    volatile ma_uint32 uninitFinished;
    ma_result readResult;
    ma_uint64 framesRead;
    float output[512 * 2];
} hardening_teardown_case;

static hardening_teardown_case* g_teardownCase;

typedef struct
{
    ma_node_base baseNode;
    ma_uint32 channels;
} hardening_teardown_source;

static void hardening_source_process(ma_node* pNode, const float** ppFramesIn,
    ma_uint32* pFrameCountIn, float** ppFramesOut, ma_uint32* pFrameCountOut)
{
    hardening_teardown_source* pSource = (hardening_teardown_source*)pNode;
    ma_uint32 sample;
    (void)ppFramesIn;
    for (sample = 0; sample < *pFrameCountOut * pSource->channels; sample += 1) {
        ppFramesOut[0][sample] = (sample == 0) ? 1.0f : 0.0f;
    }
    *pFrameCountIn = 0;
}

static ma_node_vtable g_hardening_source_vtable =
{
    hardening_source_process,
    NULL,
    0,
    1,
    0
};

static void* hardening_teardown_malloc(size_t size, void* pUserData)
{
    (void)pUserData;
    return malloc(size);
}

static void* hardening_teardown_realloc(void* pMemory, size_t size, void* pUserData)
{
    (void)pUserData;
    return realloc(pMemory, size);
}

static void hardening_teardown_free(void* pMemory, void* pUserData)
{
    hardening_teardown_case* pCase = (hardening_teardown_case*)pUserData;
    if (pMemory != NULL && pMemory == pCase->pStaging) {
        if (ma_atomic_load_32(&pCase->pOutputBus->refCount) != 0 ||
            ma_atomic_load_32(&pCase->applyActive) != 0 ||
            ma_atomic_load_32(&pCase->releaseCount) == 0) {
            ma_atomic_store_32(&pCase->prematureStagingFree, 1);
        }
        ma_atomic_store_32(&pCase->stagingFreed, 1);
    }
    free(pMemory);
}

static void hardening_detach_before_reader_wait(void* pOutputBus)
{
    hardening_teardown_case* pCase = g_teardownCase;
    if (pCase != NULL && pOutputBus == pCase->pOutputBus) {
        ma_atomic_store_32(&pCase->readerCountAtDetach,
            ma_atomic_load_32(&pCase->pOutputBus->refCount));
        ma_atomic_store_32(&pCase->detachReached, 1);
        ma_event_signal(&pCase->transition);
    }
}

static IPLAudioEffectState hardening_binaural_effect_apply(IPLBinauralEffect effect,
    IPLBinauralEffectParams* pParams, IPLAudioBuffer* pInput, IPLAudioBuffer* pOutput)
{
    hardening_teardown_case* pCase = g_teardownCase;
    if (pCase != NULL && effect == pCase->effect) {
        if (ma_atomic_fetch_add_32(&pCase->applyCount, 1) == 0) {
            ma_atomic_store_32(&pCase->applyActive, 1);
            ma_event_signal(&pCase->applyEntered);
            ma_event_wait(&pCase->allowApply);
        }
    }
    {
        IPLAudioEffectState state = iplBinauralEffectApply(effect, pParams, pInput, pOutput);
        if (pCase != NULL && effect == pCase->effect) {
            ma_atomic_store_32(&pCase->applyActive, 0);
        }
        return state;
    }
}

static void hardening_binaural_effect_release(IPLBinauralEffect* pEffect)
{
    hardening_teardown_case* pCase = g_teardownCase;
    if (pCase != NULL && pEffect != NULL && *pEffect == pCase->effect) {
        /* A wrong release-before-detach order wakes the controller. Delay the
           actual release until the blocked reader exits so the test can report
           the ordering failure without causing a use-after-free itself. */
        if (ma_atomic_load_32(&pCase->detachReached) == 0 ||
            ma_atomic_load_32(&pCase->pOutputBus->refCount) != 0 ||
            ma_atomic_load_32(&pCase->applyActive) != 0) {
            ma_atomic_store_32(&pCase->prematureRelease, 1);
            ma_event_signal(&pCase->transition);
            ma_event_wait(&pCase->graphDone);
        }
        ma_atomic_fetch_add_32(&pCase->releaseCount, 1);
    }
    iplBinauralEffectRelease(pEffect);
}

static ma_thread_result MA_THREADCALL hardening_graph_reader(void* pData)
{
    hardening_teardown_case* pCase = (hardening_teardown_case*)pData;
    pCase->readResult = ma_node_graph_read_pcm_frames(&pCase->graph,
        pCase->output, 512, &pCase->framesRead);
    ma_event_signal(&pCase->graphDone);
    return (ma_thread_result)0;
}

static ma_thread_result MA_THREADCALL hardening_node_uninit(void* pData)
{
    hardening_teardown_case* pCase = (hardening_teardown_case*)pData;
    ma_steamaudio_binaural_node_uninit(&pCase->node, &pCase->allocationCallbacks);
    ma_atomic_store_32(&pCase->uninitFinished, 1);
    return (ma_thread_result)0;
}

static int hardening_test_active_teardown(ma_uint32 inputChannels)
{
    hardening_teardown_case test;
    IPLContextSettings contextSettings;
    IPLAudioSettings audioSettings;
    IPLHRTFSettings hrtfSettings;
    IPLContext context = NULL;
    IPLHRTF hrtf = NULL;
    ma_node_graph_config graphConfig;
    ma_node_config sourceConfig;
    ma_steamaudio_binaural_node_config nodeConfig;
    hardening_teardown_source source;
    ma_thread readerThread;
    ma_thread uninitThread;
    ma_result result;
    int eventsInitialized = 0;
    int graphInitialized = 0;
    int contextInitialized = 0;
    int hrtfInitialized = 0;
    int nodeInitialized = 0;
    int sourceInitialized = 0;
    int readerStarted = 0;
    int uninitStarted = 0;
    int failed = 0;

    MA_ZERO_OBJECT(&test);
    MA_ZERO_OBJECT(&source);
    source.channels = inputChannels;
    test.allocationCallbacks.pUserData = &test;
    test.allocationCallbacks.onMalloc = hardening_teardown_malloc;
    test.allocationCallbacks.onRealloc = hardening_teardown_realloc;
    test.allocationCallbacks.onFree = hardening_teardown_free;
    if (ma_event_init(&test.applyEntered) != MA_SUCCESS) return 1;
    eventsInitialized = 1;
    if (ma_event_init(&test.allowApply) != MA_SUCCESS) goto cleanup;
    eventsInitialized = 2;
    if (ma_event_init(&test.transition) != MA_SUCCESS) goto cleanup;
    eventsInitialized = 3;
    if (ma_event_init(&test.graphDone) != MA_SUCCESS) goto cleanup;
    eventsInitialized = 4;

    graphConfig = ma_node_graph_config_init(2);
    result = ma_node_graph_init(&graphConfig, &test.allocationCallbacks, &test.graph);
    if (result != MA_SUCCESS) goto cleanup;
    graphInitialized = 1;

    MA_ZERO_OBJECT(&contextSettings);
    contextSettings.version = STEAMAUDIO_VERSION;
    if (iplContextCreate(&contextSettings, &context) != IPL_STATUS_SUCCESS) goto cleanup;
    contextInitialized = 1;
    MA_ZERO_OBJECT(&audioSettings);
    audioSettings.samplingRate = 48000;
    audioSettings.frameSize = 512;
    MA_ZERO_OBJECT(&hrtfSettings);
    hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
    hrtfSettings.volume = 1;
    if (iplHRTFCreate(context, &audioSettings, &hrtfSettings, &hrtf) != IPL_STATUS_SUCCESS) goto cleanup;
    hrtfInitialized = 1;
    nodeConfig = ma_steamaudio_binaural_node_config_init(inputChannels, audioSettings, context, hrtf);
    result = ma_steamaudio_binaural_node_init(&test.graph, &nodeConfig,
        &test.allocationCallbacks, &test.node);
    if (result != MA_SUCCESS) goto cleanup;
    nodeInitialized = 1;
    test.pOutputBus = &test.node.baseNode.pOutputBuses[0];
    test.effect = test.node.iplEffect;
    test.pStaging = test.node._pHeap;
    if (test.effect == NULL || test.pStaging == NULL) goto cleanup;
    sourceConfig = ma_node_config_init();
    sourceConfig.vtable = &g_hardening_source_vtable;
    sourceConfig.pOutputChannels = &inputChannels;
    result = ma_node_init(&test.graph, &sourceConfig, &test.allocationCallbacks,
        &source.baseNode);
    if (result != MA_SUCCESS) goto cleanup;
    sourceInitialized = 1;
    result = ma_node_attach_output_bus(&source, 0, &test.node, 0);
    if (result != MA_SUCCESS) goto cleanup;
    result = ma_node_attach_output_bus(&test.node, 0,
        ma_node_graph_get_endpoint(&test.graph), 0);
    if (result != MA_SUCCESS) goto cleanup;

    g_teardownCase = &test;
    result = ma_thread_create(&readerThread, ma_thread_priority_normal, 0,
        hardening_graph_reader, &test, NULL);
    if (result != MA_SUCCESS) goto cleanup;
    readerStarted = 1;
    ma_event_wait(&test.applyEntered);

    result = ma_thread_create(&uninitThread, ma_thread_priority_normal, 0,
        hardening_node_uninit, &test, NULL);
    if (result != MA_SUCCESS) goto cleanup;
    uninitStarted = 1;
    /* This event is signaled at the graph's reader wait or by an early effect
       release. No scheduler delay is needed to distinguish those paths. */
    ma_event_wait(&test.transition);
    if (ma_atomic_load_32(&test.detachReached) != 1 ||
        ma_atomic_load_32(&test.readerCountAtDetach) == 0 ||
        ma_atomic_load_32(&test.releaseCount) != 0 ||
        ma_atomic_load_32(&test.stagingFreed) != 0 ||
        ma_atomic_load_32(&test.prematureRelease) != 0) {
        printf("%u-channel teardown reached release before retiring its active reader\n", inputChannels);
        failed = 1;
    }
    ma_event_signal(&test.allowApply);
    ma_thread_wait(&readerThread);
    readerStarted = 0;
    ma_thread_wait(&uninitThread);
    uninitStarted = 0;
    nodeInitialized = 0;

    if (test.readResult != MA_SUCCESS || test.framesRead != 512 ||
        ma_atomic_load_32(&test.applyCount) == 0 ||
        ma_atomic_load_32(&test.releaseCount) != 1 ||
        ma_atomic_load_32(&test.stagingFreed) != 1 ||
        ma_atomic_load_32(&test.prematureRelease) != 0 ||
        ma_atomic_load_32(&test.prematureStagingFree) != 0 ||
        ma_atomic_load_32(&test.uninitFinished) != 1 ||
        ma_atomic_load_32(&test.pOutputBus->refCount) != 0) {
        printf("%u-channel teardown did not retire reader before effect/staging release\n", inputChannels);
        failed = 1;
    }

cleanup:
    if (readerStarted) {
        ma_event_signal(&test.allowApply);
        ma_thread_wait(&readerThread);
    }
    if (uninitStarted) ma_thread_wait(&uninitThread);
    g_teardownCase = NULL;
    if (nodeInitialized) ma_steamaudio_binaural_node_uninit(&test.node, &test.allocationCallbacks);
    if (sourceInitialized) ma_node_uninit(&source.baseNode, &test.allocationCallbacks);
    iplHRTFRelease(&hrtf);
    iplContextRelease(&context);
    if (graphInitialized) ma_node_graph_uninit(&test.graph, &test.allocationCallbacks);
    if (eventsInitialized >= 4) ma_event_uninit(&test.graphDone);
    if (eventsInitialized >= 3) ma_event_uninit(&test.transition);
    if (eventsInitialized >= 2) ma_event_uninit(&test.allowApply);
    if (eventsInitialized >= 1) ma_event_uninit(&test.applyEntered);
    if (eventsInitialized != 4 || !graphInitialized || !contextInitialized || !hrtfInitialized) failed = 1;
    return failed;
}

int main(void)
{
    int failures = 0;
    failures += hardening_test_active_teardown(1);
    failures += hardening_test_active_teardown(2);
    if (failures == 0) printf("Steam Audio mono/stereo active-callback teardown passed\n");
    return failures ? 1 : 0;
}
