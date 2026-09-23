static void hardening_direction_publish_after_write_hook(void);
static void hardening_direction_consume_after_load_hook(void);
static void hardening_direction_consume_after_exchange_hook(void);

#define MA_STEAMAUDIO_NO_MAIN
#define MA_STEAMAUDIO_DIRECTION_PUBLISH_AFTER_WRITE() hardening_direction_publish_after_write_hook()
#define MA_STEAMAUDIO_DIRECTION_CONSUME_AFTER_LOAD() hardening_direction_consume_after_load_hook()
#define MA_STEAMAUDIO_DIRECTION_CONSUME_AFTER_EXCHANGE() hardening_direction_consume_after_exchange_hook()
#include "../../examples/engine_steamaudio.c"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>

#define HARDENING_FRAME_SIZE             512
#define HARDENING_GUARD_FRAME_COUNT      4
#define HARDENING_GUARD_VALUE            12345.25f

#define HARDENING_DIRECTION_HOOK_DISABLED          0
#define HARDENING_DIRECTION_HOOK_PUBLISH_WRITE     1
#define HARDENING_DIRECTION_HOOK_CONSUME_LOAD      2
#define HARDENING_DIRECTION_HOOK_CONSUME_EXCHANGE  3

typedef struct
{
    size_t allocationCallCount;
    size_t liveAllocationCount;
} hardening_allocator;

typedef struct
{
    float prefix[HARDENING_GUARD_FRAME_COUNT];
    float frames[HARDENING_FRAME_SIZE * 2];
    float suffix[HARDENING_GUARD_FRAME_COUNT];
} hardening_guarded_buffer;

typedef struct
{
    ma_steamaudio_direction_state* pState;
    ma_vec3f direction;
    ma_bool32 publish;
} hardening_direction_thread_data;

static ma_event g_directionHookEnteredEvent;
static ma_event g_directionHookReleaseEvent;
static volatile ma_uint32 g_directionHookMode;

static void hardening_direction_hook_wait(ma_uint32 mode)
{
    if (ma_atomic_load_32(&g_directionHookMode) == mode) {
        ma_event_signal(&g_directionHookEnteredEvent);
        ma_event_wait(&g_directionHookReleaseEvent);
    }
}

static void hardening_direction_publish_after_write_hook(void)
{
    hardening_direction_hook_wait(HARDENING_DIRECTION_HOOK_PUBLISH_WRITE);
}

static void hardening_direction_consume_after_load_hook(void)
{
    hardening_direction_hook_wait(HARDENING_DIRECTION_HOOK_CONSUME_LOAD);
}

static void hardening_direction_consume_after_exchange_hook(void)
{
    hardening_direction_hook_wait(HARDENING_DIRECTION_HOOK_CONSUME_EXCHANGE);
}

static void* hardening_malloc(size_t size, void* pUserData)
{
    hardening_allocator* pAllocator = (hardening_allocator*)pUserData;
    void* pAllocation;

    pAllocator->allocationCallCount += 1;
    pAllocation = malloc(size);
    if (pAllocation != NULL) {
        pAllocator->liveAllocationCount += 1;
    }
    return pAllocation;
}

static void* hardening_realloc(void* pMemory, size_t size, void* pUserData)
{
    hardening_allocator* pAllocator = (hardening_allocator*)pUserData;
    void* pAllocation;

    pAllocator->allocationCallCount += 1;
    if (pMemory == NULL) {
        pAllocation = malloc(size);
        if (pAllocation != NULL) {
            pAllocator->liveAllocationCount += 1;
        }
        return pAllocation;
    }
    if (size == 0) {
        free(pMemory);
        pAllocator->liveAllocationCount -= 1;
        return NULL;
    }

    return realloc(pMemory, size);
}

static void hardening_free(void* pMemory, void* pUserData)
{
    hardening_allocator* pAllocator = (hardening_allocator*)pUserData;

    if (pMemory != NULL) {
        free(pMemory);
        pAllocator->liveAllocationCount -= 1;
    }
}

static void hardening_guarded_buffer_init(hardening_guarded_buffer* pBuffer)
{
    ma_uint32 i;

    for (i = 0; i < HARDENING_GUARD_FRAME_COUNT; i += 1) {
        pBuffer->prefix[i] = HARDENING_GUARD_VALUE;
        pBuffer->suffix[i] = HARDENING_GUARD_VALUE;
    }
    MA_ZERO_MEMORY(pBuffer->frames, sizeof(pBuffer->frames));
}

static int hardening_guarded_buffer_check(const hardening_guarded_buffer* pBuffer, const char* pCase)
{
    ma_uint32 i;

    for (i = 0; i < HARDENING_GUARD_FRAME_COUNT; i += 1) {
        if (pBuffer->prefix[i] != HARDENING_GUARD_VALUE || pBuffer->suffix[i] != HARDENING_GUARD_VALUE) {
            printf("%s overwrote an output guard\n", pCase);
            return 1;
        }
    }
    return 0;
}

static int hardening_frames_are_finite(const float* pFrames, ma_uint32 frameCount, ma_uint32 channels, const char* pCase)
{
    ma_uint32 i;

    for (i = 0; i < frameCount * channels; i += 1) {
        if (pFrames[i] != pFrames[i] || pFrames[i] > FLT_MAX || pFrames[i] < -FLT_MAX) {
            printf("%s produced a non-finite sample at %u\n", pCase, i);
            return 0;
        }
    }
    return 1;
}

static int hardening_frames_are_silent(const float* pFrames, ma_uint32 frameCount, ma_uint32 channels)
{
    ma_uint32 i;

    for (i = 0; i < frameCount * channels; i += 1) {
        if (pFrames[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static int hardening_frames_have_signal(const float* pFrames, ma_uint32 frameCount, ma_uint32 channels)
{
    ma_uint32 i;

    for (i = 0; i < frameCount * channels; i += 1) {
        if (pFrames[i] > 0.000001f || pFrames[i] < -0.000001f) {
            return 1;
        }
    }
    return 0;
}

static int hardening_direction_equals(ma_vec3f direction, float x, float y, float z)
{
    return direction.x == x && direction.y == y && direction.z == z;
}

static ma_thread_result MA_THREADCALL hardening_direction_thread(void* pData)
{
    hardening_direction_thread_data* pThreadData = (hardening_direction_thread_data*)pData;

    if (pThreadData->publish) {
        ma_steamaudio_direction_state_publish(pThreadData->pState, pThreadData->direction);
    } else {
        pThreadData->direction = ma_steamaudio_direction_state_consume(pThreadData->pState);
    }
    return (ma_thread_result)0;
}

static ma_vec3f hardening_direction(float x, float y, float z)
{
    ma_vec3f direction;
    direction.x = x;
    direction.y = y;
    direction.z = z;
    return direction;
}

static int hardening_test_direction_snapshot(void)
{
    hardening_direction_thread_data threadData;
    ma_steamaudio_direction_state state;
    ma_vec3f direction;
    ma_thread thread;
    ma_result result;
    int failed = 0;

    MA_ZERO_OBJECT(&threadData);
    direction = hardening_direction(0, 0, -1);
    ma_steamaudio_direction_state_init(&state, direction);
    threadData.pState = &state;

    result = ma_event_init(&g_directionHookEnteredEvent);
    if (result != MA_SUCCESS) {
        printf("direction entered-event initialization failed: %d\n", result);
        return 1;
    }
    result = ma_event_init(&g_directionHookReleaseEvent);
    if (result != MA_SUCCESS) {
        printf("direction release-event initialization failed: %d\n", result);
        ma_event_uninit(&g_directionHookEnteredEvent);
        return 1;
    }

    /* A written back slot must not become visible until the producer publishes it. */
    threadData.publish = MA_TRUE;
    threadData.direction = hardening_direction(1, 0, 0);
    ma_atomic_store_32(&g_directionHookMode, HARDENING_DIRECTION_HOOK_PUBLISH_WRITE);
    result = ma_thread_create(&thread, ma_thread_priority_normal, 0, hardening_direction_thread, &threadData, NULL);
    if (result != MA_SUCCESS) {
        printf("direction publisher creation failed: %d\n", result);
        failed = 1;
        goto cleanup;
    }
    ma_event_wait(&g_directionHookEnteredEvent);
    direction = ma_steamaudio_direction_state_consume(&state);
    if (!hardening_direction_equals(direction, 0, 0, -1)) {
        printf("direction snapshot exposed an unpublished back slot\n");
        failed = 1;
    }
    ma_event_signal(&g_directionHookReleaseEvent);
    ma_thread_wait(&thread);
    ma_atomic_store_32(&g_directionHookMode, HARDENING_DIRECTION_HOOK_DISABLED);
    direction = ma_steamaudio_direction_state_consume(&state);
    if (!hardening_direction_equals(direction, 1, 0, 0)) {
        printf("direction snapshot did not expose a completed publication\n");
        failed = 1;
    }

    /* If a newer publication wins before the exchange, the consumer adopts the newest slot. */
    ma_steamaudio_direction_state_publish(&state, hardening_direction(0, 1, 0));
    threadData.publish = MA_FALSE;
    ma_atomic_store_32(&g_directionHookMode, HARDENING_DIRECTION_HOOK_CONSUME_LOAD);
    result = ma_thread_create(&thread, ma_thread_priority_normal, 0, hardening_direction_thread, &threadData, NULL);
    if (result != MA_SUCCESS) {
        printf("direction consumer creation failed: %d\n", result);
        failed = 1;
        goto cleanup;
    }
    ma_event_wait(&g_directionHookEnteredEvent);
    ma_steamaudio_direction_state_publish(&state, hardening_direction(0, 0, 1));
    ma_event_signal(&g_directionHookReleaseEvent);
    ma_thread_wait(&thread);
    ma_atomic_store_32(&g_directionHookMode, HARDENING_DIRECTION_HOOK_DISABLED);
    if (!hardening_direction_equals(threadData.direction, 0, 0, 1)) {
        printf("direction snapshot failed to coalesce a newer publication\n");
        failed = 1;
    }

    /* Once exchanged, the front slot remains stable while the producer reuses the other two. */
    ma_steamaudio_direction_state_publish(&state, hardening_direction(-1, 0, 0));
    threadData.publish = MA_FALSE;
    ma_atomic_store_32(&g_directionHookMode, HARDENING_DIRECTION_HOOK_CONSUME_EXCHANGE);
    result = ma_thread_create(&thread, ma_thread_priority_normal, 0, hardening_direction_thread, &threadData, NULL);
    if (result != MA_SUCCESS) {
        printf("direction post-exchange consumer creation failed: %d\n", result);
        failed = 1;
        goto cleanup;
    }
    ma_event_wait(&g_directionHookEnteredEvent);
    ma_steamaudio_direction_state_publish(&state, hardening_direction(0, -1, 0));
    ma_steamaudio_direction_state_publish(&state, hardening_direction(0, 0, -2));
    ma_event_signal(&g_directionHookReleaseEvent);
    ma_thread_wait(&thread);
    ma_atomic_store_32(&g_directionHookMode, HARDENING_DIRECTION_HOOK_DISABLED);
    if (!hardening_direction_equals(threadData.direction, -1, 0, 0)) {
        printf("direction producer overwrote the active front slot\n");
        failed = 1;
    }
    direction = ma_steamaudio_direction_state_consume(&state);
    if (!hardening_direction_equals(direction, 0, 0, -2)) {
        printf("direction snapshot did not retain the newest coalesced value\n");
        failed = 1;
    }

cleanup:
    ma_atomic_store_32(&g_directionHookMode, HARDENING_DIRECTION_HOOK_DISABLED);
    ma_event_uninit(&g_directionHookReleaseEvent);
    ma_event_uninit(&g_directionHookEnteredEvent);
    return failed;
}

static int hardening_process(
    ma_steamaudio_binaural_node* pNode,
    const float* pFramesIn,
    ma_uint32 inputFrameCount,
    float* pFramesOut,
    ma_uint32 outputFrameCount,
    const char* pCase)
{
    const float* ppFramesIn[1];
    float* ppFramesOut[1];
    ma_uint32 consumedFrameCount = inputFrameCount;
    ma_uint32 producedFrameCount = outputFrameCount;

    ppFramesIn[0] = pFramesIn;
    ppFramesOut[0] = pFramesOut;
    ma_steamaudio_binaural_node_process_pcm_frames(
        (ma_node*)pNode,
        (pFramesIn != NULL) ? ppFramesIn : NULL,
        &consumedFrameCount,
        ppFramesOut,
        &producedFrameCount);

    if (consumedFrameCount != ((pFramesIn != NULL) ? inputFrameCount : 0) || producedFrameCount != outputFrameCount) {
        printf("%s consumed/produced %u/%u frames, expected %u/%u\n",
            pCase, consumedFrameCount, producedFrameCount,
            (pFramesIn != NULL) ? inputFrameCount : 0, outputFrameCount);
        return 1;
    }
    if (!hardening_frames_are_finite(pFramesOut, outputFrameCount, 2, pCase)) {
        return 1;
    }
    return 0;
}

static int hardening_expect_direction(const ma_steamaudio_binaural_node* pNode, float x, float y, float z, const char* pCase)
{
    ma_vec3f direction = pNode->directionState.snapshots[pNode->directionState.frontIndex];

    if (direction.x != x || direction.y != y || direction.z != z) {
        printf("%s adopted direction %f, %f, %f; expected %f, %f, %f\n",
            pCase, direction.x, direction.y, direction.z, x, y, z);
        return 1;
    }
    return 0;
}

static int hardening_test_real_sdk(void)
{
    hardening_allocator allocator;
    ma_allocation_callbacks allocationCallbacks;
    ma_node_graph_config graphConfig;
    ma_node_graph graph;
    IPLContextSettings contextSettings;
    IPLAudioSettings audioSettings;
    IPLHRTFSettings hrtfSettings;
    IPLContext context = NULL;
    IPLHRTF hrtf = NULL;
    ma_steamaudio_binaural_node_config monoConfig;
    ma_steamaudio_binaural_node_config stereoConfig;
    ma_steamaudio_binaural_node monoNode;
    ma_steamaudio_binaural_node stereoNode;
    ma_steamaudio_binaural_node invalidNode;
    hardening_guarded_buffer output;
    float monoInput[HARDENING_FRAME_SIZE];
    float stereoInput[HARDENING_FRAME_SIZE * 2];
    float partialInput[123];
    size_t allocationCallsBeforeProcessing;
    ma_uint32 i;
    ma_uint32 remainingTailFrames;
    ma_uint32 renderedTailFrames;
    ma_uint32 tailBound;
    ma_result result;
    int failed = 0;
    int monoInitialized = 0;
    int stereoInitialized = 0;
    int graphInitialized = 0;
    int sawSignal = 0;

    MA_ZERO_OBJECT(&allocator);
    allocationCallbacks.pUserData = &allocator;
    allocationCallbacks.onMalloc = hardening_malloc;
    allocationCallbacks.onRealloc = hardening_realloc;
    allocationCallbacks.onFree = hardening_free;

    graphConfig = ma_node_graph_config_init(2);
    result = ma_node_graph_init(&graphConfig, &allocationCallbacks, &graph);
    if (result != MA_SUCCESS) {
        printf("node graph initialization failed: %d\n", result);
        return 1;
    }
    graphInitialized = 1;

    MA_ZERO_OBJECT(&contextSettings);
    contextSettings.version = STEAMAUDIO_VERSION;
    if (iplContextCreate(&contextSettings, &context) != IPL_STATUS_SUCCESS) {
        printf("Steam Audio context initialization failed\n");
        failed = 1;
        goto cleanup;
    }

    MA_ZERO_OBJECT(&audioSettings);
    audioSettings.samplingRate = 48000;
    audioSettings.frameSize = HARDENING_FRAME_SIZE;
    MA_ZERO_OBJECT(&hrtfSettings);
    hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
    hrtfSettings.volume = 1;
    if (iplHRTFCreate(context, &audioSettings, &hrtfSettings, &hrtf) != IPL_STATUS_SUCCESS) {
        printf("Steam Audio HRTF initialization failed\n");
        failed = 1;
        goto cleanup;
    }

    monoConfig = ma_steamaudio_binaural_node_config_init(1, audioSettings, context, hrtf);
    monoConfig.nodeConfig.initialState = ma_node_state_stopped;
    stereoConfig = ma_steamaudio_binaural_node_config_init(2, audioSettings, context, hrtf);

    monoConfig.iplAudioSettings.frameSize = 0;
    if (ma_steamaudio_binaural_node_init(&graph, &monoConfig, &allocationCallbacks, &invalidNode) != MA_INVALID_ARGS) {
        printf("zero Steam Audio frame size was accepted\n");
        failed = 1;
        goto cleanup;
    }
    monoConfig.iplAudioSettings = audioSettings;
    monoConfig.iplAudioSettings.samplingRate = -1;
    if (ma_steamaudio_binaural_node_init(&graph, &monoConfig, &allocationCallbacks, &invalidNode) != MA_INVALID_ARGS) {
        printf("negative Steam Audio sample rate was accepted\n");
        failed = 1;
        goto cleanup;
    }
    monoConfig.iplAudioSettings = audioSettings;

    result = ma_steamaudio_binaural_node_init(&graph, &monoConfig, &allocationCallbacks, &monoNode);
    if (result != MA_SUCCESS) {
        printf("mono Steam Audio node initialization failed: %d\n", result);
        failed = 1;
        goto cleanup;
    }
    monoInitialized = 1;
    if (ma_node_get_state(&monoNode) != ma_node_state_stopped) {
        printf("Steam Audio node ignored its base node configuration\n");
        failed = 1;
        goto cleanup;
    }

    result = ma_steamaudio_binaural_node_init(&graph, &stereoConfig, &allocationCallbacks, &stereoNode);
    if (result != MA_SUCCESS) {
        printf("stereo Steam Audio node initialization failed: %d\n", result);
        failed = 1;
        goto cleanup;
    }
    stereoInitialized = 1;

    /* The nodes must remain usable after the caller releases both dependencies. */
    iplHRTFRelease(&hrtf);
    iplContextRelease(&context);
    if (hrtf != NULL || context != NULL) {
        printf("Steam Audio did not clear released caller handles\n");
        failed = 1;
        goto cleanup;
    }
    allocationCallsBeforeProcessing = allocator.allocationCallCount;

    MA_ZERO_MEMORY(monoInput, sizeof(monoInput));
    monoInput[0] = 1;
    hardening_guarded_buffer_init(&output);
    ma_steamaudio_binaural_node_set_direction(&monoNode, 1, 0, 0);
    failed += hardening_process(&monoNode, monoInput, HARDENING_FRAME_SIZE, output.frames, HARDENING_FRAME_SIZE, "mono first block");
    failed += hardening_guarded_buffer_check(&output, "mono first block");
    if (!hardening_frames_are_silent(output.frames, HARDENING_FRAME_SIZE, 2)) {
        printf("mono first block did not preserve the documented one-block latency\n");
        failed = 1;
    }
    failed += hardening_expect_direction(&monoNode, 1, 0, 0, "mono first block");

    MA_ZERO_MEMORY(monoInput, sizeof(monoInput));
    hardening_guarded_buffer_init(&output);
    ma_steamaudio_binaural_node_set_direction(&monoNode, 0, 1, 0);
    failed += hardening_process(&monoNode, monoInput, 257, output.frames, 257, "mono split block first half");
    sawSignal |= hardening_frames_have_signal(output.frames, 257, 2);
    failed += hardening_expect_direction(&monoNode, 1, 0, 0, "mono split block first half");
    failed += hardening_guarded_buffer_check(&output, "mono split block first half");

    hardening_guarded_buffer_init(&output);
    ma_steamaudio_binaural_node_set_direction(&monoNode, 0, 0, 1);
    failed += hardening_process(&monoNode, monoInput + 257, HARDENING_FRAME_SIZE - 257, output.frames, HARDENING_FRAME_SIZE - 257, "mono split block second half");
    sawSignal |= hardening_frames_have_signal(output.frames, HARDENING_FRAME_SIZE - 257, 2);
    failed += hardening_expect_direction(&monoNode, 0, 0, 1, "mono split block second half");
    failed += hardening_guarded_buffer_check(&output, "mono split block second half");
    if (!sawSignal) {
        printf("real Steam Audio processing produced no signal for an impulse\n");
        failed = 1;
    }

    for (i = 0; i < 123; i += 1) {
        partialInput[i] = (i == 0) ? 0.5f : 0;
    }
    hardening_guarded_buffer_init(&output);
    ma_steamaudio_binaural_node_set_direction(&monoNode, -1, 0, 0);
    failed += hardening_process(&monoNode, partialInput, 123, output.frames, 123, "mono partial final block");
    failed += hardening_guarded_buffer_check(&output, "mono partial final block");
    failed += hardening_expect_direction(&monoNode, 0, 0, 1, "mono partial final block");

    if (ma_steamaudio_binaural_node_end_input(&monoNode) != MA_SUCCESS) {
        printf("mono end-of-input signal failed\n");
        failed = 1;
    }
    remainingTailFrames = monoNode.outputFrameCount;
    hardening_guarded_buffer_init(&output);
    failed += hardening_process(&monoNode, NULL, 0, output.frames, remainingTailFrames, "mono pending output drain");
    failed += hardening_guarded_buffer_check(&output, "mono pending output drain");
    if (monoNode.outputFrameCount != 0 || monoNode.inputFrameCount != 123) {
        printf("pending output drain changed the staged short input\n");
        failed = 1;
    }

    hardening_guarded_buffer_init(&output);
    failed += hardening_process(&monoNode, NULL, 0, output.frames, 1, "mono partial flush");
    failed += hardening_guarded_buffer_check(&output, "mono partial flush");
    failed += hardening_expect_direction(&monoNode, -1, 0, 0, "mono partial flush");
    if (monoNode.inputFrameCount != 0 || monoNode.outputFrameCount != HARDENING_FRAME_SIZE - 1) {
        printf("short final input was not zero-padded into one fixed Steam Audio block\n");
        failed = 1;
    }

    remainingTailFrames = (ma_uint32)iplBinauralEffectGetTailSize(monoNode.iplEffect);
    tailBound = monoNode.outputFrameCount + ((remainingTailFrames + HARDENING_FRAME_SIZE - 1) / HARDENING_FRAME_SIZE) * HARDENING_FRAME_SIZE + 97;
    renderedTailFrames = 0;
    while ((!monoNode.tailComplete || monoNode.outputFrameCount != 0) && renderedTailFrames <= tailBound) {
        hardening_guarded_buffer_init(&output);
        failed += hardening_process(&monoNode, NULL, 0, output.frames, 97, "mono tail drain");
        failed += hardening_guarded_buffer_check(&output, "mono tail drain");
        renderedTailFrames += 97;
    }
    if (!monoNode.tailComplete || monoNode.outputFrameCount != 0 || renderedTailFrames > tailBound) {
        printf("Steam Audio tail did not terminate within its reported bound\n");
        failed = 1;
    }

    hardening_guarded_buffer_init(&output);
    for (i = 0; i < 31 * 2; i += 1) {
        output.frames[i] = 1;
    }
    failed += hardening_process(&monoNode, NULL, 0, output.frames, 31, "mono post-tail silence");
    failed += hardening_guarded_buffer_check(&output, "mono post-tail silence");
    if (!hardening_frames_are_silent(output.frames, 31, 2)) {
        printf("Steam Audio node emitted data after its tail completed\n");
        failed = 1;
    }

    MA_ZERO_MEMORY(stereoInput, sizeof(stereoInput));
    stereoInput[0] = 1;
    stereoInput[1] = -0.5f;
    hardening_guarded_buffer_init(&output);
    failed += hardening_process(&stereoNode, stereoInput, HARDENING_FRAME_SIZE, output.frames, HARDENING_FRAME_SIZE, "stereo first block");
    failed += hardening_guarded_buffer_check(&output, "stereo first block");
    if (!hardening_frames_are_silent(output.frames, HARDENING_FRAME_SIZE, 2)) {
        printf("stereo first block did not preserve the documented one-block latency\n");
        failed = 1;
    }
    MA_ZERO_MEMORY(stereoInput, sizeof(stereoInput));
    hardening_guarded_buffer_init(&output);
    failed += hardening_process(&stereoNode, stereoInput, HARDENING_FRAME_SIZE, output.frames, HARDENING_FRAME_SIZE, "stereo second block");
    failed += hardening_guarded_buffer_check(&output, "stereo second block");
    if (!hardening_frames_have_signal(output.frames, HARDENING_FRAME_SIZE, 2)) {
        printf("stereo Steam Audio processing produced no signal for an impulse\n");
        failed = 1;
    }

    hardening_guarded_buffer_init(&output);
    failed += hardening_process(&stereoNode, stereoInput, 17, output.frames, 17, "allocation probe");
    if (allocator.allocationCallCount != allocationCallsBeforeProcessing) {
        printf("Steam Audio node allocated from the miniaudio allocator during processing\n");
        failed = 1;
    }

cleanup:
    if (stereoInitialized) {
        ma_steamaudio_binaural_node_uninit(&stereoNode, &allocationCallbacks);
    }
    if (monoInitialized) {
        ma_steamaudio_binaural_node_uninit(&monoNode, &allocationCallbacks);
    }
    iplHRTFRelease(&hrtf);
    iplContextRelease(&context);
    if (graphInitialized) {
        ma_node_graph_uninit(&graph, &allocationCallbacks);
    }
    if (allocator.liveAllocationCount != 0) {
        printf("Steam Audio hardening test leaked %lu miniaudio allocations\n", (unsigned long)allocator.liveAllocationCount);
        failed = 1;
    }
    return failed;
}

int main(void)
{
    int failed = 0;

    failed += hardening_test_direction_snapshot();
    failed += hardening_test_real_sdk();
    if (failed != 0) {
        return 1;
    }

    printf("Steam Audio hardening regression tests passed\n");
    return 0;
}
