#include "../../miniaudio.h"

static ma_result hardening_wasapi_stop(ma_device* pDevice);
static ma_result hardening_wasapi_start(ma_device* pDevice);
static ma_result hardening_wasapi_reinit(ma_device* pDevice, ma_device_type deviceType);
static ma_bool32 hardening_wasapi_endpoint_is_active(void* pState, ma_device_type deviceType);
static void hardening_wasapi_notify_rerouted(ma_device* pDevice);
static ma_result hardening_wasapi_reinit_init_internal(ma_context* pContext, ma_device_type deviceType, const void* pDeviceID, void* pData);
static ma_int32 hardening_wasapi_reinit_set_event_handle(void* pAudioClient, void* hEvent);
static ma_int32 hardening_wasapi_reinit_get_buffer_size(void* pAudioClient, ma_uint32* pBufferSize);
static ma_result hardening_wasapi_reinit_release_audio_client(ma_context* pContext, void* pAudioClient, void* pRenderClient, void* pCaptureClient, ma_event* pEvent);

#define MA_WASAPI_DEVICE_STOP_INTERNAL(pDevice)                 hardening_wasapi_stop((pDevice))
#define MA_WASAPI_DEVICE_START_INTERNAL(pDevice)                hardening_wasapi_start((pDevice))
#define MA_WASAPI_DEVICE_REINIT(pDevice, deviceType)            hardening_wasapi_reinit((pDevice), (deviceType))
#define MA_WASAPI_DEVICE_ENDPOINT_IS_ACTIVE(pState, deviceType) hardening_wasapi_endpoint_is_active((pState), (deviceType))
#define MA_WASAPI_DEVICE_NOTIFY_REROUTED(pDevice)               hardening_wasapi_notify_rerouted((pDevice))
#define MA_WASAPI_REINIT_DEVICE_INIT_INTERNAL(pContext, deviceType, pDeviceID, pData) hardening_wasapi_reinit_init_internal((pContext), (deviceType), (const void*)(pDeviceID), (void*)(pData))
#define MA_WASAPI_REINIT_SET_EVENT_HANDLE(pAudioClient, hEvent) ((HRESULT)hardening_wasapi_reinit_set_event_handle((void*)(pAudioClient), (void*)(hEvent)))
#define MA_WASAPI_REINIT_GET_BUFFER_SIZE(pAudioClient, pBufferSize) ((HRESULT)hardening_wasapi_reinit_get_buffer_size((void*)(pAudioClient), (pBufferSize)))
#define MA_WASAPI_REINIT_RELEASE_AUDIO_CLIENT(pContext, pAudioClient, pRenderClient, pCaptureClient, pEvent) hardening_wasapi_reinit_release_audio_client((pContext), (void*)(pAudioClient), (void*)(pRenderClient), (void*)(pCaptureClient), (pEvent))
#define MINIAUDIO_IMPLEMENTATION
#include "../../miniaudio.h"

#include <stdio.h>

typedef enum
{
    hardening_trace_stop,
    hardening_trace_reinit_playback,
    hardening_trace_reinit_capture,
    hardening_trace_start,
    hardening_trace_rerouted,
    hardening_trace_public_started,
    hardening_trace_public_stopped
} hardening_trace_event;

typedef struct
{
    hardening_trace_event events[32];
    ma_uint32 count;
} hardening_trace;

typedef struct
{
    ma_result stopResult;
    ma_result startResult;
    ma_result playbackReinitResult;
    ma_result captureReinitResult;
    ma_bool32 playbackEndpointActive;
    ma_bool32 captureEndpointActive;
    ma_bool32 blockReinit;
    ma_event* pReinitEnteredEvent;
    ma_event* pReinitContinueEvent;
} hardening_hooks;

typedef struct
{
    ma_context context;
    ma_log log;
    ma_device device;
    ma_device_notification_state__wasapi state;
} hardening_reroute_fixture;

static hardening_trace g_trace;
static hardening_hooks g_hooks;

typedef struct
{
    ma_result initResult;
    ma_int32 setEventResult;
    ma_int32 getBufferSizeResult;
    ma_result oldReleaseResult;
    ma_result candidateReleaseResult;
    ma_uint32 initCount;
    ma_uint32 setEventCount;
    ma_uint32 getBufferSizeCount;
    ma_uint32 oldReleaseCount;
    ma_uint32 candidateReleaseCount;
    ma_uint32 unexpectedReleaseCount;
} hardening_reinit_hooks;

static hardening_reinit_hooks g_reinitHooks;
static ma_uint8 g_oldAudioClientMarker;
static ma_uint8 g_oldRenderClientMarker;
static ma_uint8 g_candidateAudioClientMarker;
static ma_uint8 g_candidateRenderClientMarker;

static ma_result hardening_wasapi_reinit_init_internal(ma_context* pContext, ma_device_type deviceType, const void* pDeviceID, void* pData)
{
    ma_device_init_internal_data__wasapi* pInternalData = (ma_device_init_internal_data__wasapi*)pData;

    (void)pContext;
    (void)pDeviceID;
    g_reinitHooks.initCount += 1;
    if (g_reinitHooks.initResult != MA_SUCCESS) {
        return g_reinitHooks.initResult;
    }

    pInternalData->pAudioClient = (ma_IAudioClient*)&g_candidateAudioClientMarker;
    pInternalData->pRenderClient = (deviceType == ma_device_type_playback) ? (ma_IAudioRenderClient*)&g_candidateRenderClientMarker : NULL;
    pInternalData->pCaptureClient = NULL;
    pInternalData->formatOut = ma_format_f32;
    pInternalData->channelsOut = 2;
    pInternalData->sampleRateOut = 44100;
    ma_channel_map_init_standard(ma_standard_channel_map_default, pInternalData->channelMapOut, MA_MAX_CHANNELS, 2);
    pInternalData->periodSizeInFramesOut = 64;
    pInternalData->periodsOut = 2;
    ma_strcpy_s(pInternalData->deviceName, sizeof(pInternalData->deviceName), "hardening candidate");
    MA_ZERO_OBJECT(&pInternalData->id);
    return MA_SUCCESS;
}

static ma_int32 hardening_wasapi_reinit_set_event_handle(void* pAudioClient, void* hEvent)
{
    (void)hEvent;
    if (pAudioClient != &g_candidateAudioClientMarker) {
        return (ma_int32)E_FAIL;
    }
    g_reinitHooks.setEventCount += 1;
    return g_reinitHooks.setEventResult;
}

static ma_int32 hardening_wasapi_reinit_get_buffer_size(void* pAudioClient, ma_uint32* pBufferSize)
{
    if (pAudioClient != &g_candidateAudioClientMarker) {
        return (ma_int32)E_FAIL;
    }
    g_reinitHooks.getBufferSizeCount += 1;
    if (g_reinitHooks.getBufferSizeResult == (ma_int32)S_OK) {
        *pBufferSize = 128;
    }
    return g_reinitHooks.getBufferSizeResult;
}

static ma_result hardening_wasapi_reinit_release_audio_client(ma_context* pContext, void* pAudioClient, void* pRenderClient, void* pCaptureClient, ma_event* pEvent)
{
    (void)pContext;
    (void)pCaptureClient;
    (void)pEvent;

    if (pAudioClient == &g_oldAudioClientMarker && pRenderClient == &g_oldRenderClientMarker) {
        g_reinitHooks.oldReleaseCount += 1;
        return g_reinitHooks.oldReleaseResult;
    }
    if (pAudioClient == &g_candidateAudioClientMarker && pRenderClient == &g_candidateRenderClientMarker) {
        g_reinitHooks.candidateReleaseCount += 1;
        return g_reinitHooks.candidateReleaseResult;
    }

    g_reinitHooks.unexpectedReleaseCount += 1;
    return MA_ERROR;
}

static void hardening_trace_reset(void)
{
    MA_ZERO_OBJECT(&g_trace);
}

static void hardening_trace_append(hardening_trace_event event)
{
    if (g_trace.count < ma_countof(g_trace.events)) {
        g_trace.events[g_trace.count] = event;
        g_trace.count += 1;
    }
}

static const char* hardening_trace_event_name(hardening_trace_event event)
{
    switch (event)
    {
        case hardening_trace_stop:            return "stop";
        case hardening_trace_reinit_playback: return "reinit-playback";
        case hardening_trace_reinit_capture:  return "reinit-capture";
        case hardening_trace_start:           return "start";
        case hardening_trace_rerouted:        return "rerouted";
        case hardening_trace_public_started:  return "public-started";
        case hardening_trace_public_stopped:  return "public-stopped";
        default:                              return "unknown";
    }
}

static int hardening_expect_trace(const hardening_trace_event* pExpected, ma_uint32 expectedCount, const char* pCase)
{
    ma_uint32 i;

    if (g_trace.count != expectedCount) {
        printf("%s: trace count=%u expected=%u\n", pCase, g_trace.count, expectedCount);
        for (i = 0; i < g_trace.count; i += 1) {
            printf("  actual[%u]=%s\n", i, hardening_trace_event_name(g_trace.events[i]));
        }
        return 1;
    }

    for (i = 0; i < expectedCount; i += 1) {
        if (g_trace.events[i] != pExpected[i]) {
            printf("%s: trace[%u]=%s expected=%s\n", pCase, i,
                hardening_trace_event_name(g_trace.events[i]),
                hardening_trace_event_name(pExpected[i]));
            return 1;
        }
    }

    return 0;
}

static ma_result hardening_wasapi_stop(ma_device* pDevice)
{
    ma_bool32 suppressed;

    hardening_trace_append(hardening_trace_stop);
    suppressed = ma_device__consume_internal_notification_suppression(pDevice, ma_device_notification_type_stopped);
    if (g_hooks.stopResult == MA_SUCCESS) {
        ma_device__set_state(pDevice, ma_device_state_stopped);
        if (suppressed == MA_FALSE) {
            hardening_trace_append(hardening_trace_public_stopped);
        }
    }

    return g_hooks.stopResult;
}

static ma_result hardening_wasapi_start(ma_device* pDevice)
{
    ma_bool32 suppressed;

    hardening_trace_append(hardening_trace_start);
    suppressed = ma_device__consume_internal_notification_suppression(pDevice, ma_device_notification_type_started);
    if (g_hooks.startResult == MA_SUCCESS) {
        ma_device__set_state(pDevice, ma_device_state_started);
        if (suppressed == MA_FALSE) {
            hardening_trace_append(hardening_trace_public_started);
        }
    } else {
        ma_device__set_state(pDevice, ma_device_state_stopped);
    }

    return g_hooks.startResult;
}

static ma_result hardening_wasapi_reinit(ma_device* pDevice, ma_device_type deviceType)
{
    (void)pDevice;

    if (deviceType == ma_device_type_playback) {
        hardening_trace_append(hardening_trace_reinit_playback);
    } else {
        hardening_trace_append(hardening_trace_reinit_capture);
    }

    if (g_hooks.blockReinit) {
        ma_event_signal(g_hooks.pReinitEnteredEvent);
        ma_event_wait(g_hooks.pReinitContinueEvent);
    }

    if (deviceType == ma_device_type_playback) {
        return g_hooks.playbackReinitResult;
    }
    return g_hooks.captureReinitResult;
}

static ma_bool32 hardening_wasapi_endpoint_is_active(void* pState, ma_device_type deviceType)
{
    (void)pState;
    if (deviceType == ma_device_type_playback) {
        return g_hooks.playbackEndpointActive;
    }
    return g_hooks.captureEndpointActive;
}

static void hardening_wasapi_notify_rerouted(ma_device* pDevice)
{
    (void)pDevice;
    hardening_trace_append(hardening_trace_rerouted);
}

static ma_result hardening_reroute_fixture_init(hardening_reroute_fixture* pFixture, ma_device_type deviceType, ma_device_state deviceState, ma_bool32 requestedStarted)
{
    ma_result result;

    MA_ZERO_OBJECT(pFixture);
    MA_ZERO_OBJECT(&g_hooks);
    g_hooks.stopResult             = MA_SUCCESS;
    g_hooks.startResult            = MA_SUCCESS;
    g_hooks.playbackReinitResult   = MA_SUCCESS;
    g_hooks.captureReinitResult    = MA_SUCCESS;
    g_hooks.playbackEndpointActive = MA_TRUE;
    g_hooks.captureEndpointActive  = MA_TRUE;
    hardening_trace_reset();

    result = ma_log_init(NULL, &pFixture->log);
    if (result != MA_SUCCESS) {
        return result;
    }

    result = ma_mutex_init(&pFixture->state.operationLock);
    if (result != MA_SUCCESS) {
        ma_log_uninit(&pFixture->log);
        return result;
    }

    result = ma_mutex_init(&pFixture->device.wasapi.rerouteLock);
    if (result != MA_SUCCESS) {
        ma_mutex_uninit(&pFixture->state.operationLock);
        ma_log_uninit(&pFixture->log);
        return result;
    }

    pFixture->context.backend = ma_backend_wasapi;
    pFixture->context.pLog = &pFixture->log;
    pFixture->device.pContext = &pFixture->context;
    pFixture->device.type = deviceType;
    pFixture->device.playback.shareMode = ma_share_mode_shared;
    pFixture->device.capture.shareMode = ma_share_mode_shared;
    pFixture->device.wasapi.allowPlaybackAutoStreamRouting = MA_TRUE;
    pFixture->device.wasapi.allowCaptureAutoStreamRouting = MA_TRUE;
    pFixture->device.wasapi.pDeviceEnumerator = (ma_ptr)&pFixture->state;
    pFixture->state.pDevice = &pFixture->device;
    ma_atomic_store_32(&pFixture->state.acceptingNotifications, MA_TRUE);
    ma_atomic_store_32(&pFixture->state.requestedStarted, requestedStarted);
    ma_atomic_store_32(&pFixture->state.suppressStartedNotification, MA_FALSE);
    ma_atomic_store_32(&pFixture->state.suppressStoppedNotification, MA_FALSE);
    ma_device__set_state(&pFixture->device, deviceState);

    return MA_SUCCESS;
}

static void hardening_reroute_fixture_uninit(hardening_reroute_fixture* pFixture)
{
    ma_mutex_uninit(&pFixture->device.wasapi.rerouteLock);
    ma_mutex_uninit(&pFixture->state.operationLock);
    ma_log_uninit(&pFixture->log);
}

static int hardening_test_reroute_success(void)
{
    hardening_reroute_fixture fixture;
    const hardening_trace_event expected[] = {
        hardening_trace_stop,
        hardening_trace_reinit_playback,
        hardening_trace_start,
        hardening_trace_rerouted
    };
    ma_result result;
    int failed = 0;

    result = hardening_reroute_fixture_init(&fixture, ma_device_type_playback, ma_device_state_started, MA_TRUE);
    if (result != MA_SUCCESS) {
        return 1;
    }

    result = ma_device_process_notifications__wasapi(&fixture.state, MA_WASAPI_NOTIFICATION_OP_RENDER);
    if (result != MA_SUCCESS || ma_device_get_state(&fixture.device) != ma_device_state_started || fixture.device.wasapi.isDetachedPlayback) {
        printf("successful reroute: result=%d state=%d detached=%d\n", result, ma_device_get_state(&fixture.device), fixture.device.wasapi.isDetachedPlayback);
        failed = 1;
    }
    failed += hardening_expect_trace(expected, ma_countof(expected), "successful reroute");

    hardening_reroute_fixture_uninit(&fixture);
    return failed;
}

static int hardening_test_reroute_failure(void)
{
    hardening_reroute_fixture fixture;
    const hardening_trace_event expected[] = {
        hardening_trace_stop,
        hardening_trace_reinit_playback
    };
    ma_result result;
    int failed = 0;

    result = hardening_reroute_fixture_init(&fixture, ma_device_type_playback, ma_device_state_started, MA_TRUE);
    if (result != MA_SUCCESS) {
        return 1;
    }
    g_hooks.playbackReinitResult = MA_OUT_OF_MEMORY;

    result = ma_device_process_notifications__wasapi(&fixture.state, MA_WASAPI_NOTIFICATION_OP_RENDER);
    if (result != MA_OUT_OF_MEMORY || ma_device_get_state(&fixture.device) != ma_device_state_stopped || !fixture.device.wasapi.isDetachedPlayback) {
        printf("failed reroute: result=%d state=%d detached=%d\n", result, ma_device_get_state(&fixture.device), fixture.device.wasapi.isDetachedPlayback);
        failed = 1;
    }
    failed += hardening_expect_trace(expected, ma_countof(expected), "failed reroute");

    hardening_reroute_fixture_uninit(&fixture);
    return failed;
}

static int hardening_test_restart_failure(void)
{
    hardening_reroute_fixture fixture;
    const hardening_trace_event expected[] = {
        hardening_trace_stop,
        hardening_trace_reinit_playback,
        hardening_trace_start,
        hardening_trace_rerouted
    };
    ma_result result;
    int failed = 0;

    result = hardening_reroute_fixture_init(&fixture, ma_device_type_playback, ma_device_state_started, MA_TRUE);
    if (result != MA_SUCCESS) {
        return 1;
    }
    g_hooks.startResult = MA_ERROR;

    result = ma_device_process_notifications__wasapi(&fixture.state, MA_WASAPI_NOTIFICATION_OP_RENDER);
    if (result != MA_ERROR || ma_device_get_state(&fixture.device) != ma_device_state_stopped ||
        ma_atomic_load_32(&fixture.state.suppressStartedNotification) != MA_FALSE ||
        ma_atomic_load_32(&fixture.state.suppressStoppedNotification) != MA_FALSE) {
        printf("failed restart: result=%d state=%d suppress=%u/%u\n", result, ma_device_get_state(&fixture.device),
            ma_atomic_load_32(&fixture.state.suppressStartedNotification),
            ma_atomic_load_32(&fixture.state.suppressStoppedNotification));
        failed = 1;
    }
    failed += hardening_expect_trace(expected, ma_countof(expected), "failed restart");

    hardening_reroute_fixture_uninit(&fixture);
    return failed;
}

static int hardening_test_stopped_device_stays_stopped(void)
{
    hardening_reroute_fixture fixture;
    const hardening_trace_event expected[] = {
        hardening_trace_reinit_playback,
        hardening_trace_rerouted
    };
    ma_result result;
    int failed = 0;

    result = hardening_reroute_fixture_init(&fixture, ma_device_type_playback, ma_device_state_stopped, MA_FALSE);
    if (result != MA_SUCCESS) {
        return 1;
    }

    result = ma_device_process_notifications__wasapi(&fixture.state, MA_WASAPI_NOTIFICATION_OP_RENDER);
    if (result != MA_SUCCESS || ma_device_get_state(&fixture.device) != ma_device_state_stopped) {
        printf("stopped reroute: result=%d state=%d\n", result, ma_device_get_state(&fixture.device));
        failed = 1;
    }
    failed += hardening_expect_trace(expected, ma_countof(expected), "stopped reroute");

    hardening_reroute_fixture_uninit(&fixture);
    return failed;
}

static int hardening_test_state_change_reattaches(void)
{
    hardening_reroute_fixture fixture;
    const hardening_trace_event expected[] = {
        hardening_trace_reinit_playback,
        hardening_trace_rerouted
    };
    ma_result result;
    int failed = 0;

    result = hardening_reroute_fixture_init(&fixture, ma_device_type_playback, ma_device_state_stopped, MA_FALSE);
    if (result != MA_SUCCESS) {
        return 1;
    }
    fixture.device.wasapi.isDetachedPlayback = MA_TRUE;

    result = ma_device_process_notifications__wasapi(&fixture.state, MA_WASAPI_NOTIFICATION_OP_STATE);
    if (result != MA_SUCCESS || fixture.device.wasapi.isDetachedPlayback) {
        printf("state-change reattach: result=%d detached=%d\n", result, fixture.device.wasapi.isDetachedPlayback);
        failed = 1;
    }
    failed += hardening_expect_trace(expected, ma_countof(expected), "state-change reattach");

    hardening_reroute_fixture_uninit(&fixture);
    return failed;
}

typedef struct
{
    size_t allocationCallCount;
    size_t failAllocationCall;
    size_t liveAllocationCount;
} hardening_reinit_allocator;

typedef struct
{
    ma_context context;
    ma_device device;
    ma_device_notification_state__wasapi state;
    hardening_reinit_allocator allocator;
} hardening_reinit_fixture;

static void* hardening_reinit_malloc(size_t size, void* pUserData)
{
    hardening_reinit_allocator* pAllocator = (hardening_reinit_allocator*)pUserData;
    void* pAllocation;

    pAllocator->allocationCallCount += 1;
    if (pAllocator->allocationCallCount == pAllocator->failAllocationCall) {
        return NULL;
    }
    pAllocation = malloc(size);
    if (pAllocation != NULL) {
        pAllocator->liveAllocationCount += 1;
    }
    return pAllocation;
}

static void* hardening_reinit_realloc(void* pMemory, size_t size, void* pUserData)
{
    hardening_reinit_allocator* pAllocator = (hardening_reinit_allocator*)pUserData;
    void* pAllocation;

    pAllocator->allocationCallCount += 1;
    if (pAllocator->allocationCallCount == pAllocator->failAllocationCall) {
        return NULL;
    }
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

static void hardening_reinit_free(void* pMemory, void* pUserData)
{
    hardening_reinit_allocator* pAllocator = (hardening_reinit_allocator*)pUserData;

    if (pMemory != NULL) {
        free(pMemory);
        pAllocator->liveAllocationCount -= 1;
    }
}

static void hardening_reinit_hooks_reset(void)
{
    MA_ZERO_OBJECT(&g_reinitHooks);
    g_reinitHooks.initResult = MA_SUCCESS;
    g_reinitHooks.setEventResult = (ma_int32)S_OK;
    g_reinitHooks.getBufferSizeResult = (ma_int32)S_OK;
    g_reinitHooks.oldReleaseResult = MA_SUCCESS;
    g_reinitHooks.candidateReleaseResult = MA_SUCCESS;
}

static ma_result hardening_reinit_fixture_init(hardening_reinit_fixture* pFixture)
{
    ma_resampler_config resamplerConfig;
    ma_result result;

    MA_ZERO_OBJECT(pFixture);
    hardening_reinit_hooks_reset();
    pFixture->context.backend = ma_backend_wasapi;
    pFixture->context.allocationCallbacks.pUserData = &pFixture->allocator;
    pFixture->context.allocationCallbacks.onMalloc = hardening_reinit_malloc;
    pFixture->context.allocationCallbacks.onRealloc = hardening_reinit_realloc;
    pFixture->context.allocationCallbacks.onFree = hardening_reinit_free;

    pFixture->device.pContext = &pFixture->context;
    pFixture->device.type = ma_device_type_duplex;
    pFixture->device.sampleRate = 48000;
    pFixture->device.playback.format = ma_format_f32;
    pFixture->device.playback.channels = 2;
    pFixture->device.playback.internalFormat = ma_format_f32;
    pFixture->device.playback.internalChannels = 2;
    pFixture->device.playback.internalSampleRate = 48000;
    pFixture->device.playback.internalPeriodSizeInFrames = 64;
    pFixture->device.playback.internalPeriods = 2;
    pFixture->device.playback.shareMode = ma_share_mode_shared;
    ma_channel_map_init_standard(ma_standard_channel_map_default, pFixture->device.playback.channelMap, MA_MAX_CHANNELS, 2);
    ma_channel_map_init_standard(ma_standard_channel_map_default, pFixture->device.playback.internalChannelMap, MA_MAX_CHANNELS, 2);
    resamplerConfig = ma_resampler_config_init(ma_format_f32, 2, 48000, 48000, ma_resample_algorithm_linear);
    pFixture->device.resampling.algorithm = resamplerConfig.algorithm;
    pFixture->device.resampling.linear.lpfOrder = resamplerConfig.linear.lpfOrder;

    pFixture->state.pDevice = &pFixture->device;
    pFixture->device.wasapi.pDeviceEnumerator = (ma_ptr)&pFixture->state;
    pFixture->device.wasapi.pAudioClientPlayback = &g_oldAudioClientMarker;
    pFixture->device.wasapi.pRenderClient = &g_oldRenderClientMarker;

    result = ma_device__post_init_setup(&pFixture->device, ma_device_type_playback);
    if (result == MA_SUCCESS) {
        ma_device__set_state(&pFixture->device, ma_device_state_stopped);
    }
    return result;
}

static int hardening_reinit_fixture_uninit(hardening_reinit_fixture* pFixture, const char* pCase)
{
    ma_data_converter_uninit(&pFixture->device.playback.converter, &pFixture->context.allocationCallbacks);
    MA_ZERO_OBJECT(&pFixture->device.playback.converter);
    ma_free(pFixture->device.playback.pInputCache, &pFixture->context.allocationCallbacks);
    pFixture->device.playback.pInputCache = NULL;
    if (pFixture->allocator.liveAllocationCount != 0) {
        printf("%s: leaked %lu generic allocations\n", pCase, (unsigned long)pFixture->allocator.liveAllocationCount);
        return 1;
    }
    return 0;
}

static int hardening_reinit_expect_candidate_failure(hardening_reinit_fixture* pFixture, void* pOldConverterHeap, void* pOldInputCache, size_t oldLiveAllocationCount, const char* pCase)
{
    int failed = 0;

    if (pFixture->device.wasapi.pAudioClientPlayback != &g_oldAudioClientMarker ||
        pFixture->device.wasapi.pRenderClient != &g_oldRenderClientMarker ||
        pFixture->device.playback.converter._pHeap != pOldConverterHeap ||
        pFixture->device.playback.pInputCache != pOldInputCache ||
        pFixture->allocator.liveAllocationCount != oldLiveAllocationCount ||
        g_reinitHooks.oldReleaseCount != 0 || g_reinitHooks.candidateReleaseCount != 1 ||
        g_reinitHooks.unexpectedReleaseCount != 0) {
        printf("%s: published state or release accounting changed on candidate failure\n", pCase);
        failed = 1;
    }
    return failed;
}

static int hardening_test_reinit_allocation_failure(size_t failureOffset, const char* pCase)
{
    hardening_reinit_fixture fixture;
    void* pOldConverterHeap;
    void* pOldInputCache;
    size_t oldLiveAllocationCount;
    ma_result result;
    int failed = 0;

    result = hardening_reinit_fixture_init(&fixture);
    if (result != MA_SUCCESS) {
        return 1;
    }
    pOldConverterHeap = fixture.device.playback.converter._pHeap;
    pOldInputCache = fixture.device.playback.pInputCache;
    oldLiveAllocationCount = fixture.allocator.liveAllocationCount;
    fixture.allocator.failAllocationCall = fixture.allocator.allocationCallCount + failureOffset;

    result = ma_device_reinit__wasapi(&fixture.device, ma_device_type_playback);
    if (result != MA_OUT_OF_MEMORY) {
        printf("%s: result=%d expected MA_OUT_OF_MEMORY\n", pCase, result);
        failed = 1;
    }
    failed += hardening_reinit_expect_candidate_failure(&fixture, pOldConverterHeap, pOldInputCache, oldLiveAllocationCount, pCase);
    failed += hardening_reinit_fixture_uninit(&fixture, pCase);
    return failed;
}

static int hardening_test_reinit_event_failure(ma_bool32 failBufferQuery, const char* pCase)
{
    hardening_reinit_fixture fixture;
    void* pOldConverterHeap;
    void* pOldInputCache;
    size_t oldLiveAllocationCount;
    ma_result result;
    int failed = 0;

    result = hardening_reinit_fixture_init(&fixture);
    if (result != MA_SUCCESS) {
        return 1;
    }
    pOldConverterHeap = fixture.device.playback.converter._pHeap;
    pOldInputCache = fixture.device.playback.pInputCache;
    oldLiveAllocationCount = fixture.allocator.liveAllocationCount;
    if (failBufferQuery) {
        g_reinitHooks.getBufferSizeResult = (ma_int32)E_FAIL;
    } else {
        g_reinitHooks.setEventResult = (ma_int32)E_FAIL;
    }

    result = ma_device_reinit__wasapi(&fixture.device, ma_device_type_playback);
    if (result == MA_SUCCESS || g_reinitHooks.setEventCount != 1 ||
        g_reinitHooks.getBufferSizeCount != (failBufferQuery ? 1U : 0U)) {
        printf("%s: result/counts=%d/%u/%u\n", pCase, result, g_reinitHooks.setEventCount, g_reinitHooks.getBufferSizeCount);
        failed = 1;
    }
    failed += hardening_reinit_expect_candidate_failure(&fixture, pOldConverterHeap, pOldInputCache, oldLiveAllocationCount, pCase);
    failed += hardening_reinit_fixture_uninit(&fixture, pCase);
    return failed;
}

static int hardening_test_reinit_success(void)
{
    hardening_reinit_fixture fixture;
    size_t oldLiveAllocationCount;
    ma_result result;
    int failed = 0;

    result = hardening_reinit_fixture_init(&fixture);
    if (result != MA_SUCCESS) {
        return 1;
    }
    oldLiveAllocationCount = fixture.allocator.liveAllocationCount;

    result = ma_device_reinit__wasapi(&fixture.device, ma_device_type_playback);
    if (result != MA_SUCCESS ||
        fixture.device.wasapi.pAudioClientPlayback != &g_candidateAudioClientMarker ||
        fixture.device.wasapi.pRenderClient != &g_candidateRenderClientMarker ||
        fixture.device.playback.internalSampleRate != 44100 ||
        fixture.device.playback.converter.resampler.pBackendUserData != &fixture.device.playback.converter.resampler ||
        fixture.device.playback.converter.resampler.pBackend != &fixture.device.playback.converter.resampler.state.linear ||
        fixture.allocator.liveAllocationCount != oldLiveAllocationCount ||
        g_reinitHooks.oldReleaseCount != 1 || g_reinitHooks.candidateReleaseCount != 0 ||
        g_reinitHooks.unexpectedReleaseCount != 0) {
        printf("successful reinit did not publish one complete candidate\n");
        failed = 1;
    }

    failed += hardening_reinit_fixture_uninit(&fixture, "successful reinit");
    return failed;
}

static int hardening_test_reinit_old_release_failure(void)
{
    hardening_reinit_fixture fixture;
    ma_result result;
    int failed = 0;

    result = hardening_reinit_fixture_init(&fixture);
    if (result != MA_SUCCESS) {
        return 1;
    }
    g_reinitHooks.oldReleaseResult = MA_ERROR;

    result = ma_device_reinit__wasapi(&fixture.device, ma_device_type_playback);
    if (result != MA_ERROR || fixture.device.wasapi.pAudioClientPlayback != NULL ||
        fixture.device.wasapi.pRenderClient != NULL || fixture.device.playback.converter._pHeap != NULL ||
        fixture.device.playback.pInputCache != NULL || fixture.allocator.liveAllocationCount != 0 ||
        g_reinitHooks.oldReleaseCount != 1 || g_reinitHooks.candidateReleaseCount != 1 ||
        g_reinitHooks.unexpectedReleaseCount != 0) {
        printf("failed old release did not leave an empty destroyable endpoint\n");
        failed = 1;
    }

    failed += hardening_reinit_fixture_uninit(&fixture, "failed old release");
    return failed;
}

typedef struct
{
    hardening_reroute_fixture* pFixture;
    ma_event attemptEvent;
    volatile ma_uint32 acquiredOperationLock;
    ma_result processResult;
} hardening_operation_barrier;

static ma_thread_result MA_THREADCALL hardening_process_thread(void* pUserData)
{
    hardening_operation_barrier* pBarrier = (hardening_operation_barrier*)pUserData;
    pBarrier->processResult = ma_device_process_notifications__wasapi(&pBarrier->pFixture->state, MA_WASAPI_NOTIFICATION_OP_RENDER);
    return (ma_thread_result)0;
}

static ma_thread_result MA_THREADCALL hardening_public_stop_thread(void* pUserData)
{
    hardening_operation_barrier* pBarrier = (hardening_operation_barrier*)pUserData;

    ma_event_signal(&pBarrier->attemptEvent);
    ma_mutex_lock(&pBarrier->pFixture->state.operationLock);
    ma_atomic_store_32(&pBarrier->acquiredOperationLock, MA_TRUE);
    ma_atomic_store_32(&pBarrier->pFixture->state.requestedStarted, MA_FALSE);
    ma_mutex_unlock(&pBarrier->pFixture->state.operationLock);
    return (ma_thread_result)0;
}

static int hardening_test_public_state_serialization(void)
{
    hardening_reroute_fixture fixture;
    hardening_operation_barrier barrier;
    ma_event reinitEnteredEvent;
    ma_event reinitContinueEvent;
    ma_thread processThread;
    ma_thread publicStopThread;
    const hardening_trace_event expected[] = {
        hardening_trace_stop,
        hardening_trace_reinit_playback,
        hardening_trace_start,
        hardening_trace_rerouted
    };
    ma_result result;
    int failed = 0;
    ma_bool32 attemptEventInitialized = MA_FALSE;
    ma_bool32 enteredEventInitialized = MA_FALSE;
    ma_bool32 continueEventInitialized = MA_FALSE;
    ma_bool32 processThreadCreated = MA_FALSE;
    ma_bool32 publicStopThreadCreated = MA_FALSE;

    result = hardening_reroute_fixture_init(&fixture, ma_device_type_playback, ma_device_state_started, MA_TRUE);
    if (result != MA_SUCCESS) {
        return 1;
    }
    MA_ZERO_OBJECT(&barrier);
    barrier.pFixture = &fixture;

    result = ma_event_init(&barrier.attemptEvent);
    if (result != MA_SUCCESS) {
        failed = 1;
        goto done;
    }
    attemptEventInitialized = MA_TRUE;
    result = ma_event_init(&reinitEnteredEvent);
    if (result != MA_SUCCESS) {
        failed = 1;
        goto done;
    }
    enteredEventInitialized = MA_TRUE;
    result = ma_event_init(&reinitContinueEvent);
    if (result != MA_SUCCESS) {
        failed = 1;
        goto done;
    }
    continueEventInitialized = MA_TRUE;

    g_hooks.blockReinit = MA_TRUE;
    g_hooks.pReinitEnteredEvent = &reinitEnteredEvent;
    g_hooks.pReinitContinueEvent = &reinitContinueEvent;

    result = ma_thread_create(&processThread, ma_thread_priority_normal, 0, hardening_process_thread, &barrier, NULL);
    if (result != MA_SUCCESS) {
        failed = 1;
        goto done;
    }
    processThreadCreated = MA_TRUE;
    ma_event_wait(&reinitEnteredEvent);

    result = ma_thread_create(&publicStopThread, ma_thread_priority_normal, 0, hardening_public_stop_thread, &barrier, NULL);
    if (result != MA_SUCCESS) {
        failed = 1;
        ma_event_signal(&reinitContinueEvent);
        goto done;
    }
    publicStopThreadCreated = MA_TRUE;
    ma_event_wait(&barrier.attemptEvent);

    if (ma_atomic_load_32(&barrier.acquiredOperationLock) != MA_FALSE) {
        printf("public state change entered while reroute owned the operation lock\n");
        failed = 1;
    }
    ma_event_signal(&reinitContinueEvent);
    ma_thread_wait(&processThread);
    processThreadCreated = MA_FALSE;
    ma_thread_wait(&publicStopThread);
    publicStopThreadCreated = MA_FALSE;

    if (barrier.processResult != MA_SUCCESS || ma_atomic_load_32(&barrier.acquiredOperationLock) != MA_TRUE ||
        ma_atomic_load_32(&fixture.state.requestedStarted) != MA_FALSE) {
        printf("serialized public state: result=%d acquired=%u requested=%u\n", barrier.processResult,
            ma_atomic_load_32(&barrier.acquiredOperationLock), ma_atomic_load_32(&fixture.state.requestedStarted));
        failed = 1;
    }
    failed += hardening_expect_trace(expected, ma_countof(expected), "serialized public state");

done:
    if (processThreadCreated) {
        ma_event_signal(&reinitContinueEvent);
        ma_thread_wait(&processThread);
    }
    if (publicStopThreadCreated) {
        ma_thread_wait(&publicStopThread);
    }
    if (continueEventInitialized) {
        ma_event_uninit(&reinitContinueEvent);
    }
    if (enteredEventInitialized) {
        ma_event_uninit(&reinitEnteredEvent);
    }
    if (attemptEventInitialized) {
        ma_event_uninit(&barrier.attemptEvent);
    }
    hardening_reroute_fixture_uninit(&fixture);
    return failed;
}

static int hardening_expect_pending(ma_device_notification_state__wasapi* pState, ma_uint32 expected, const char* pCase)
{
    ma_uint32 actual = ma_atomic_exchange_32(&pState->pendingOperations, 0);
    if (actual != expected) {
        printf("%s: pending=0x%08X expected=0x%08X\n", pCase, actual, expected);
        return 1;
    }

    return 0;
}

static int hardening_test_callback_publication(void)
{
    ma_device device;
    ma_device_notification_state__wasapi state;
    WCHAR borrowedID[8] = L"device";
    ma_uint32 i;
    int failed = 0;

    MA_ZERO_OBJECT(&device);
    MA_ZERO_OBJECT(&state);

    state.pDevice = &device;
    if (ma_event_init(&state.wakeEvent) != MA_SUCCESS) {
        return 1;
    }
    ma_atomic_store_32(&state.acceptingNotifications, MA_TRUE);
    device.wasapi.pDeviceEnumerator = (ma_ptr)&state;
    device.wasapi.notificationClient.pDevice = &device;
    device.type = ma_device_type_playback;
    device.playback.shareMode = ma_share_mode_shared;
    device.wasapi.allowPlaybackAutoStreamRouting = MA_TRUE;

    ma_IMMNotificationClient_OnDefaultDeviceChanged(&device.wasapi.notificationClient, ma_eRender, ma_eConsole, borrowedID);
    borrowedID[0] = L'X';
    failed += hardening_expect_pending(&state, MA_WASAPI_NOTIFICATION_OP_RENDER, "render route");

    ma_IMMNotificationClient_OnDefaultDeviceChanged(&device.wasapi.notificationClient, ma_eRender, ma_eCommunications, borrowedID);
    failed += hardening_expect_pending(&state, 0, "role filter");

    ma_IMMNotificationClient_OnDefaultDeviceChanged(&device.wasapi.notificationClient, ma_eCapture, ma_eConsole, borrowedID);
    failed += hardening_expect_pending(&state, 0, "flow filter");

    device.wasapi.allowPlaybackAutoStreamRouting = MA_FALSE;
    ma_IMMNotificationClient_OnDefaultDeviceChanged(&device.wasapi.notificationClient, ma_eRender, ma_eConsole, borrowedID);
    failed += hardening_expect_pending(&state, 0, "routing disabled");

    device.wasapi.allowPlaybackAutoStreamRouting = MA_TRUE;
    device.playback.shareMode = ma_share_mode_exclusive;
    ma_IMMNotificationClient_OnDefaultDeviceChanged(&device.wasapi.notificationClient, ma_eRender, ma_eConsole, borrowedID);
    failed += hardening_expect_pending(&state, 0, "exclusive filter");

    device.type = ma_device_type_loopback;
    device.capture.shareMode = ma_share_mode_shared;
    device.wasapi.allowCaptureAutoStreamRouting = MA_TRUE;
    ma_IMMNotificationClient_OnDefaultDeviceChanged(&device.wasapi.notificationClient, ma_eRender, ma_eConsole, borrowedID);
    failed += hardening_expect_pending(&state, MA_WASAPI_NOTIFICATION_OP_CAPTURE, "loopback flow");

    for (i = 0; i < 100; i += 1) {
        ma_IMMNotificationClient_OnDeviceStateChanged(&device.wasapi.notificationClient, borrowedID, MA_MM_DEVICE_STATE_ACTIVE);
        ma_IMMNotificationClient_OnDeviceAdded(&device.wasapi.notificationClient, borrowedID);
        ma_IMMNotificationClient_OnDeviceRemoved(&device.wasapi.notificationClient, borrowedID);
    }
    failed += hardening_expect_pending(&state, MA_WASAPI_NOTIFICATION_OP_STATE, "coalesced state changes");

    ma_atomic_store_32(&state.acceptingNotifications, MA_FALSE);
    ma_IMMNotificationClient_OnDefaultDeviceChanged(&device.wasapi.notificationClient, ma_eRender, ma_eConsole, borrowedID);
    ma_IMMNotificationClient_OnDeviceStateChanged(&device.wasapi.notificationClient, borrowedID, 0);
    failed += hardening_expect_pending(&state, 0, "teardown publication gate");

    ma_event_uninit(&state.wakeEvent);
    return failed;
}

static ma_result hardening_command_queue_init(ma_context* pContext)
{
    ma_result result;

    MA_ZERO_OBJECT(pContext);
    result = ma_mutex_init(&pContext->wasapi.commandLock);
    if (result != MA_SUCCESS) {
        return result;
    }
    result = ma_semaphore_init(0, &pContext->wasapi.commandSem);
    if (result != MA_SUCCESS) {
        ma_mutex_uninit(&pContext->wasapi.commandLock);
    }
    return result;
}

static void hardening_command_queue_uninit(ma_context* pContext)
{
    ma_semaphore_uninit(&pContext->wasapi.commandSem);
    ma_mutex_uninit(&pContext->wasapi.commandLock);
}

static int hardening_test_full_command_ring(void)
{
    ma_context context;
    ma_context_command__wasapi commands[4];
    ma_context_command__wasapi popped;
    ma_release_audio_client_data__wasapi payloads[4];
    ma_event events[4];
    ma_uint32 eventCount = 0;
    ma_uint32 i;
    ma_result result;
    int failed = 0;

    result = hardening_command_queue_init(&context);
    if (result != MA_SUCCESS) {
        return 1;
    }
    context.wasapi.commandIndex = 3;
    MA_ZERO_OBJECT(commands);
    MA_ZERO_OBJECT(payloads);

    for (i = 0; i < ma_countof(commands); i += 1) {
        result = ma_event_init(&events[i]);
        if (result != MA_SUCCESS) {
            failed = 1;
            goto done;
        }
        eventCount += 1;
        commands[i] = ma_context_init_command__wasapi(MA_CONTEXT_COMMAND_RELEASE_IAUDIOCLIENT__WASAPI);
        commands[i].pEvent = &events[i];
        commands[i].data.releaseAudioClient.pDevice = (ma_device*)&payloads[i];
        commands[i].data.releaseAudioClient.deviceType = (ma_device_type)i;
        result = ma_context_post_command__wasapi(&context, &commands[i]);
        if (result != MA_SUCCESS) {
            printf("full command ring: post %u failed: %d\n", i, result);
            failed = 1;
            goto done;
        }
    }

    if (context.wasapi.commandCount != ma_countof(commands)) {
        printf("full command ring: count=%u\n", context.wasapi.commandCount);
        failed = 1;
    }

    for (i = 0; i < ma_countof(commands); i += 1) {
        result = ma_context_next_command__wasapi(&context, &popped);
        if (result != MA_SUCCESS || popped.pEvent != &events[i] ||
            popped.data.releaseAudioClient.pDevice != (ma_device*)&payloads[i] ||
            popped.data.releaseAudioClient.deviceType != (ma_device_type)i) {
            printf("full command ring: pop %u failed or changed payload\n", i);
            failed = 1;
            goto done;
        }
    }

    if (context.wasapi.commandCount != 0 || context.wasapi.commandIndex != 3) {
        printf("full command ring: final count/index=%u/%u\n", context.wasapi.commandCount, context.wasapi.commandIndex);
        failed = 1;
    }

done:
    for (i = 0; i < eventCount; i += 1) {
        ma_event_uninit(&events[i]);
    }
    hardening_command_queue_uninit(&context);
    return failed;
}

typedef struct
{
    ma_IAudioClient iface;
    volatile ma_uint32 releaseCount;
} hardening_audio_client;

typedef struct
{
    ma_IAudioRenderClient iface;
    volatile ma_uint32 releaseCount;
} hardening_render_client;

typedef struct
{
    ma_IAudioCaptureClient iface;
    volatile ma_uint32 releaseCount;
} hardening_capture_client;

static ULONG STDMETHODCALLTYPE hardening_audio_client_release(ma_IAudioClient* pThis)
{
    hardening_audio_client* pClient = (hardening_audio_client*)pThis;
    ma_atomic_fetch_add_32(&pClient->releaseCount, 1);
    return 0;
}

static ULONG STDMETHODCALLTYPE hardening_render_client_release(ma_IAudioRenderClient* pThis)
{
    hardening_render_client* pClient = (hardening_render_client*)pThis;
    ma_atomic_fetch_add_32(&pClient->releaseCount, 1);
    return 0;
}

static ULONG STDMETHODCALLTYPE hardening_capture_client_release(ma_IAudioCaptureClient* pThis)
{
    hardening_capture_client* pClient = (hardening_capture_client*)pThis;
    ma_atomic_fetch_add_32(&pClient->releaseCount, 1);
    return 0;
}

static int hardening_test_command_worker_release(void)
{
    ma_context context;
    ma_IAudioClientVtbl audioVTable;
    ma_IAudioRenderClientVtbl renderVTable;
    ma_IAudioCaptureClientVtbl captureVTable;
    hardening_audio_client audioClient;
    hardening_render_client renderClient;
    hardening_capture_client captureClient;
    ma_context_command__wasapi quitCommand;
    ma_result result;
    int failed = 0;
    ma_bool32 threadCreated = MA_FALSE;

    result = hardening_command_queue_init(&context);
    if (result != MA_SUCCESS) {
        return 1;
    }
    MA_ZERO_OBJECT(&audioVTable);
    MA_ZERO_OBJECT(&renderVTable);
    MA_ZERO_OBJECT(&captureVTable);
    MA_ZERO_OBJECT(&audioClient);
    MA_ZERO_OBJECT(&renderClient);
    MA_ZERO_OBJECT(&captureClient);
    audioVTable.Release = hardening_audio_client_release;
    renderVTable.Release = hardening_render_client_release;
    captureVTable.Release = hardening_capture_client_release;
    audioClient.iface.lpVtbl = &audioVTable;
    renderClient.iface.lpVtbl = &renderVTable;
    captureClient.iface.lpVtbl = &captureVTable;

    result = ma_thread_create(&context.wasapi.commandThread, ma_thread_priority_normal, 0, ma_context_command_thread__wasapi, &context, NULL);
    if (result != MA_SUCCESS) {
        failed = 1;
        goto done;
    }
    threadCreated = MA_TRUE;

    result = ma_device_release_audio_client__wasapi(&context, &audioClient.iface, &renderClient.iface, &captureClient.iface, NULL);
    if (result != MA_SUCCESS || ma_atomic_load_32(&audioClient.releaseCount) != 1 ||
        ma_atomic_load_32(&renderClient.releaseCount) != 1 || ma_atomic_load_32(&captureClient.releaseCount) != 1) {
        printf("command worker release: result=%d counts=%u/%u/%u\n", result,
            ma_atomic_load_32(&audioClient.releaseCount), ma_atomic_load_32(&renderClient.releaseCount),
            ma_atomic_load_32(&captureClient.releaseCount));
        failed = 1;
    }

    quitCommand = ma_context_init_command__wasapi(MA_CONTEXT_COMMAND_QUIT__WASAPI);
    result = ma_context_post_command__wasapi(&context, &quitCommand);
    if (result != MA_SUCCESS) {
        failed = 1;
    }
    ma_thread_wait(&context.wasapi.commandThread);
    threadCreated = MA_FALSE;

done:
    if (threadCreated) {
        quitCommand = ma_context_init_command__wasapi(MA_CONTEXT_COMMAND_QUIT__WASAPI);
        ma_context_post_command__wasapi(&context, &quitCommand);
        ma_thread_wait(&context.wasapi.commandThread);
    }
    hardening_command_queue_uninit(&context);
    return failed;
}

typedef struct
{
    ma_IMMDeviceEnumerator iface;
    ma_IMMDeviceEnumeratorVtbl vtable;
    ma_event unregisterEnteredEvent;
    ma_event allowCallbackReleaseEvent;
    volatile ma_uint32 unregisterCount;
    volatile ma_uint32 releaseCount;
} hardening_device_enumerator;

typedef struct
{
    ma_device* pDevice;
    hardening_device_enumerator* pEnumerator;
    volatile ma_uint32 uninitFinished;
} hardening_uninit_barrier;

static ULONG STDMETHODCALLTYPE hardening_enumerator_release(ma_IMMDeviceEnumerator* pThis)
{
    hardening_device_enumerator* pEnumerator = (hardening_device_enumerator*)pThis;
    ma_atomic_fetch_add_32(&pEnumerator->releaseCount, 1);
    return 0;
}

static HRESULT STDMETHODCALLTYPE hardening_enumerator_unregister(ma_IMMDeviceEnumerator* pThis, ma_IMMNotificationClient* pClient)
{
    hardening_device_enumerator* pEnumerator = (hardening_device_enumerator*)pThis;
    (void)pClient;
    ma_atomic_fetch_add_32(&pEnumerator->unregisterCount, 1);
    ma_event_signal(&pEnumerator->unregisterEnteredEvent);
    return S_OK;
}

static ma_thread_result MA_THREADCALL hardening_delayed_callback_release_thread(void* pUserData)
{
    hardening_uninit_barrier* pBarrier = (hardening_uninit_barrier*)pUserData;

    ma_event_wait(&pBarrier->pEnumerator->allowCallbackReleaseEvent);
    ma_IMMNotificationClient_Release(&pBarrier->pDevice->wasapi.notificationClient);
    return (ma_thread_result)0;
}

static ma_thread_result MA_THREADCALL hardening_device_uninit_thread(void* pUserData)
{
    hardening_uninit_barrier* pBarrier = (hardening_uninit_barrier*)pUserData;

    ma_device_uninit__wasapi(pBarrier->pDevice);
    ma_atomic_store_32(&pBarrier->uninitFinished, MA_TRUE);
    return (ma_thread_result)0;
}

static int hardening_test_unregister_quiescence(void)
{
    ma_context context;
    ma_device device;
    ma_device_notification_state__wasapi* pState = NULL;
    hardening_device_enumerator enumerator;
    hardening_uninit_barrier barrier;
    ma_thread callbackReleaseThread;
    ma_thread uninitThread;
    ma_context_command__wasapi quitCommand;
    ma_result result;
    int failed = 0;
    ma_bool32 commandThreadCreated = MA_FALSE;
    ma_bool32 notificationThreadCreated = MA_FALSE;
    ma_bool32 callbackThreadCreated = MA_FALSE;
    ma_bool32 uninitThreadCreated = MA_FALSE;
    ma_bool32 rerouteLockInitialized = MA_FALSE;
    ma_bool32 initEventInitialized = MA_FALSE;
    ma_bool32 wakeEventInitialized = MA_FALSE;
    ma_bool32 commandEventInitialized = MA_FALSE;
    ma_bool32 quiesceEventInitialized = MA_FALSE;
    ma_bool32 operationLockInitialized = MA_FALSE;
    ma_bool32 unregisterEventInitialized = MA_FALSE;
    ma_bool32 releaseEventInitialized = MA_FALSE;

    result = hardening_command_queue_init(&context);
    if (result != MA_SUCCESS) {
        return 1;
    }
    context.backend = ma_backend_wasapi;
    /* The notification thread needs COM entry points even with a fake enumerator. */
    context.win32.CoInitializeEx = (ma_proc)CoInitializeEx;
    context.win32.CoUninitialize = (ma_proc)CoUninitialize;
    MA_ZERO_OBJECT(&device);
    MA_ZERO_OBJECT(&enumerator);
    MA_ZERO_OBJECT(&barrier);
    device.pContext = &context;

    result = ma_thread_create(&context.wasapi.commandThread, ma_thread_priority_normal, 0, ma_context_command_thread__wasapi, &context, NULL);
    if (result != MA_SUCCESS) {
        failed = 1;
        goto done;
    }
    commandThreadCreated = MA_TRUE;

    result = ma_mutex_init(&device.wasapi.rerouteLock);
    if (result != MA_SUCCESS) {
        failed = 1;
        goto done;
    }
    rerouteLockInitialized = MA_TRUE;

    pState = (ma_device_notification_state__wasapi*)ma_calloc(sizeof(*pState), NULL);
    if (pState == NULL) {
        failed = 1;
        goto done;
    }
    pState->pDevice = &device;
    pState->pDeviceEnumerator = &enumerator.iface;

    result = ma_event_init(&pState->initEvent);
    if (result != MA_SUCCESS) { failed = 1; goto done; }
    initEventInitialized = MA_TRUE;
    result = ma_event_init(&pState->wakeEvent);
    if (result != MA_SUCCESS) { failed = 1; goto done; }
    wakeEventInitialized = MA_TRUE;
    result = ma_event_init(&pState->commandEvent);
    if (result != MA_SUCCESS) { failed = 1; goto done; }
    commandEventInitialized = MA_TRUE;
    result = ma_event_init(&pState->quiesceEvent);
    if (result != MA_SUCCESS) { failed = 1; goto done; }
    quiesceEventInitialized = MA_TRUE;
    result = ma_mutex_init(&pState->operationLock);
    if (result != MA_SUCCESS) { failed = 1; goto done; }
    operationLockInitialized = MA_TRUE;
    result = ma_event_init(&enumerator.unregisterEnteredEvent);
    if (result != MA_SUCCESS) { failed = 1; goto done; }
    unregisterEventInitialized = MA_TRUE;
    result = ma_event_init(&enumerator.allowCallbackReleaseEvent);
    if (result != MA_SUCCESS) { failed = 1; goto done; }
    releaseEventInitialized = MA_TRUE;

    enumerator.vtable.Release = hardening_enumerator_release;
    enumerator.vtable.UnregisterEndpointNotificationCallback = hardening_enumerator_unregister;
    enumerator.iface.lpVtbl = &enumerator.vtable;
    device.wasapi.pDeviceEnumerator = (ma_ptr)pState;
    device.wasapi.notificationClient.lpVtbl = &g_maNotificationCientVtbl;
    device.wasapi.notificationClient.counter = 2;
    device.wasapi.notificationClient.pDevice = &device;
    ma_atomic_store_32(&pState->acceptingNotifications, MA_TRUE);

    result = ma_thread_create(&pState->thread, ma_thread_priority_normal, 0, ma_device_notification_thread__wasapi, pState, NULL);
    if (result != MA_SUCCESS) {
        failed = 1;
        goto done;
    }
    notificationThreadCreated = MA_TRUE;
    ma_event_wait(&pState->initEvent);
    if (pState->initResult != MA_SUCCESS) {
        printf("notification thread COM init failed: %d\n", pState->initResult);
        failed = 1;
        goto done;
    }

    barrier.pDevice = &device;
    barrier.pEnumerator = &enumerator;
    result = ma_thread_create(&callbackReleaseThread, ma_thread_priority_normal, 0, hardening_delayed_callback_release_thread, &barrier, NULL);
    if (result != MA_SUCCESS) {
        failed = 1;
        goto done;
    }
    callbackThreadCreated = MA_TRUE;
    result = ma_thread_create(&uninitThread, ma_thread_priority_normal, 0, hardening_device_uninit_thread, &barrier, NULL);
    if (result != MA_SUCCESS) {
        failed = 1;
        goto done;
    }
    uninitThreadCreated = MA_TRUE;

    ma_event_wait(&enumerator.unregisterEnteredEvent);
    ma_IMMNotificationClient_OnDeviceAdded(&device.wasapi.notificationClient, L"late-callback");
    if (ma_atomic_load_32(&pState->acceptingNotifications) != MA_FALSE ||
        ma_atomic_load_32(&pState->pendingOperations) != 0 ||
        ma_atomic_load_32(&barrier.uninitFinished) != MA_FALSE) {
        printf("unregister quiescence did not close publication and wait for callback release\n");
        failed = 1;
    }

    ma_event_signal(&enumerator.allowCallbackReleaseEvent);
    ma_thread_wait(&callbackReleaseThread);
    callbackThreadCreated = MA_FALSE;
    ma_thread_wait(&uninitThread);
    uninitThreadCreated = MA_FALSE;
    notificationThreadCreated = MA_FALSE;
    operationLockInitialized = MA_FALSE;
    quiesceEventInitialized = MA_FALSE;
    commandEventInitialized = MA_FALSE;
    wakeEventInitialized = MA_FALSE;
    initEventInitialized = MA_FALSE;
    rerouteLockInitialized = MA_FALSE;
    pState = NULL;

    if (ma_atomic_load_32(&barrier.uninitFinished) != MA_TRUE ||
        ma_atomic_load_32(&enumerator.unregisterCount) != 1 ||
        ma_atomic_load_32(&enumerator.releaseCount) != 1 ||
        device.wasapi.pDeviceEnumerator != NULL || device.wasapi.notificationClient.counter != 1) {
        printf("unregister quiescence final state: finished=%u unregister=%u release=%u sidecar=%p refs=%u\n",
            ma_atomic_load_32(&barrier.uninitFinished), ma_atomic_load_32(&enumerator.unregisterCount),
            ma_atomic_load_32(&enumerator.releaseCount), device.wasapi.pDeviceEnumerator,
            ma_atomic_load_32(&device.wasapi.notificationClient.counter));
        failed = 1;
    }

done:
    if (uninitThreadCreated) {
        ma_event_signal(&enumerator.allowCallbackReleaseEvent);
        if (callbackThreadCreated) {
            ma_thread_wait(&callbackReleaseThread);
            callbackThreadCreated = MA_FALSE;
        }
        ma_thread_wait(&uninitThread);
        notificationThreadCreated = MA_FALSE;
        pState = NULL;
        operationLockInitialized = MA_FALSE;
        quiesceEventInitialized = MA_FALSE;
        commandEventInitialized = MA_FALSE;
        wakeEventInitialized = MA_FALSE;
        initEventInitialized = MA_FALSE;
        rerouteLockInitialized = MA_FALSE;
    }
    if (callbackThreadCreated) {
        ma_event_signal(&enumerator.allowCallbackReleaseEvent);
        ma_thread_wait(&callbackReleaseThread);
    }
    if (notificationThreadCreated && pState != NULL) {
        ma_atomic_store_32(&pState->acceptingNotifications, MA_FALSE);
        ma_atomic_fetch_or_32(&pState->pendingOperations, MA_WASAPI_NOTIFICATION_OP_QUIT);
        ma_event_signal(&pState->wakeEvent);
        ma_thread_wait(&pState->thread);
    }
    if (operationLockInitialized) { ma_mutex_uninit(&pState->operationLock); }
    if (quiesceEventInitialized) { ma_event_uninit(&pState->quiesceEvent); }
    if (commandEventInitialized) { ma_event_uninit(&pState->commandEvent); }
    if (wakeEventInitialized) { ma_event_uninit(&pState->wakeEvent); }
    if (initEventInitialized) { ma_event_uninit(&pState->initEvent); }
    if (pState != NULL) { ma_free(pState, NULL); }
    if (rerouteLockInitialized) { ma_mutex_uninit(&device.wasapi.rerouteLock); }
    if (releaseEventInitialized) { ma_event_uninit(&enumerator.allowCallbackReleaseEvent); }
    if (unregisterEventInitialized) { ma_event_uninit(&enumerator.unregisterEnteredEvent); }
    if (commandThreadCreated) {
        quitCommand = ma_context_init_command__wasapi(MA_CONTEXT_COMMAND_QUIT__WASAPI);
        ma_context_post_command__wasapi(&context, &quitCommand);
        ma_thread_wait(&context.wasapi.commandThread);
    }
    hardening_command_queue_uninit(&context);
    return failed;
}

int main(void)
{
    int failed = 0;

    failed += hardening_test_callback_publication();
    failed += hardening_test_reroute_success();
    failed += hardening_test_reroute_failure();
    failed += hardening_test_restart_failure();
    failed += hardening_test_stopped_device_stays_stopped();
    failed += hardening_test_state_change_reattaches();
    failed += hardening_test_reinit_event_failure(MA_FALSE, "candidate event failure");
    failed += hardening_test_reinit_event_failure(MA_TRUE, "candidate buffer query failure");
    failed += hardening_test_reinit_allocation_failure(1, "candidate converter allocation failure");
    failed += hardening_test_reinit_allocation_failure(2, "candidate cache allocation failure");
    failed += hardening_test_reinit_success();
    failed += hardening_test_reinit_old_release_failure();
    failed += hardening_test_public_state_serialization();
    failed += hardening_test_full_command_ring();
    failed += hardening_test_command_worker_release();
    failed += hardening_test_unregister_quiescence();

    return failed != 0;
}
