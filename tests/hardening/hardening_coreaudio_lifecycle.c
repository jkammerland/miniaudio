#include "../../miniaudio.h"

static ma_result hardening_coreaudio_init_internal(ma_context* pContext, ma_device_type deviceType, const ma_device_id* pDeviceID, void* pData, void* pDevice);
static ma_result hardening_coreaudio_get_uid(ma_context* pContext, ma_uint32 objectID, size_t bufferSize, char* pBufferOut);
static void hardening_coreaudio_callback_hook(void);

#define MA_COREAUDIO_DEVICE_INIT_INTERNAL(pContext, deviceType, pDeviceID, pData, pDevice) hardening_coreaudio_init_internal((pContext), (deviceType), (pDeviceID), (void*)(pData), (pDevice))
#define MA_COREAUDIO_GET_AUDIO_OBJECT_UID(pContext, objectID, bufferSize, pBufferOut) hardening_coreaudio_get_uid((pContext), (objectID), (bufferSize), (pBufferOut))
#define MA_COREAUDIO_DEFAULT_DEVICE_CALLBACK_HOOK() hardening_coreaudio_callback_hook()
#define MINIAUDIO_IMPLEMENTATION
#include "../../miniaudio.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    size_t allocationCallCount;
    size_t failAllocationCall;
    volatile ma_uint32 liveAllocationCount;
    ma_uint32 oldBufferFreeCount;
    ma_uint32 candidateBufferFreeCount;
} hardening_allocator;

typedef struct
{
    ma_result captureResult;
    ma_result playbackResult;
    ma_uint32 initCount;
    ma_uint32 captureInitCount;
    ma_uint32 playbackInitCount;
    ma_uint32 uidCount;
    ma_uint32 oldCaptureStopCount;
    ma_uint32 oldPlaybackStopCount;
    ma_uint32 candidateCaptureStopCount;
    ma_uint32 candidatePlaybackStopCount;
    ma_uint32 oldCaptureDisposeCount;
    ma_uint32 oldPlaybackDisposeCount;
    ma_uint32 candidateCaptureDisposeCount;
    ma_uint32 candidatePlaybackDisposeCount;
    ma_uint32 unexpectedStopCount;
    ma_uint32 unexpectedDisposeCount;
    ma_uint32 publicStoppedCount;
    ma_uint32 publicReroutedCount;
    ma_uint32 addListenerCount;
    ma_uint32 removeListenerCount;
    ma_uint32 failAddListenerCall;
    ma_uint32 candidateSampleRate;
    ma_bool32 allocateCaptureBuffer;
    ma_bool32 invokeStopCallback;
} hardening_hooks;

typedef struct
{
    ma_context context;
    ma_device device;
    hardening_allocator allocator;
} hardening_fixture;

static hardening_hooks g_hooks;
static ma_uint8 g_oldCaptureUnit;
static ma_uint8 g_oldPlaybackUnit;
static ma_uint8 g_candidateCaptureUnit;
static ma_uint8 g_candidatePlaybackUnit;
static void* g_pOldBuffer;
static void* g_pCandidateBuffer;
static ma_device* g_pStopCallbackDevice;
static ma_event* g_pCallbackEnteredEvent;
static ma_event* g_pCallbackReleaseEvent;
static ma_event* g_pSecondRemoveEvent;
static ma_event* g_pRerouteCompletedEvent;
static volatile ma_uint32 g_callbackHookEnabled;

typedef struct
{
    ma_context* pContext;
    ma_result result;
    volatile ma_uint32 complete;
} hardening_uninit_thread_data;

static void hardening_coreaudio_callback_hook(void)
{
    if (ma_atomic_load_32(&g_callbackHookEnabled) != MA_FALSE) {
        ma_event_signal(g_pCallbackEnteredEvent);
        ma_event_wait(g_pCallbackReleaseEvent);
    }
}

static void* hardening_malloc(size_t size, void* pUserData)
{
    hardening_allocator* pAllocator = (hardening_allocator*)pUserData;
    void* pAllocation;

    pAllocator->allocationCallCount += 1;
    if (pAllocator->allocationCallCount == pAllocator->failAllocationCall) {
        return NULL;
    }

    pAllocation = malloc(size);
    if (pAllocation != NULL) {
        ma_atomic_fetch_add_32(&pAllocator->liveAllocationCount, 1);
    }
    return pAllocation;
}

static void* hardening_realloc(void* pMemory, size_t size, void* pUserData)
{
    hardening_allocator* pAllocator = (hardening_allocator*)pUserData;
    void* pAllocation;

    pAllocator->allocationCallCount += 1;
    if (pAllocator->allocationCallCount == pAllocator->failAllocationCall) {
        return NULL;
    }

    if (pMemory == NULL) {
        pAllocation = malloc(size);
        if (pAllocation != NULL) {
            ma_atomic_fetch_add_32(&pAllocator->liveAllocationCount, 1);
        }
        return pAllocation;
    }
    if (size == 0) {
        free(pMemory);
        ma_atomic_fetch_sub_32(&pAllocator->liveAllocationCount, 1);
        return NULL;
    }

    return realloc(pMemory, size);
}

static void hardening_free(void* pMemory, void* pUserData)
{
    hardening_allocator* pAllocator = (hardening_allocator*)pUserData;

    if (pMemory == NULL) {
        return;
    }

    if (pMemory == g_pOldBuffer) {
        pAllocator->oldBufferFreeCount += 1;
    }
    if (pMemory == g_pCandidateBuffer) {
        pAllocator->candidateBufferFreeCount += 1;
    }

    free(pMemory);
    ma_atomic_fetch_sub_32(&pAllocator->liveAllocationCount, 1);
}

static OSStatus hardening_stop(AudioUnit audioUnit)
{
    if (audioUnit == (AudioUnit)&g_oldCaptureUnit) {
        g_hooks.oldCaptureStopCount += 1;
    } else if (audioUnit == (AudioUnit)&g_oldPlaybackUnit) {
        g_hooks.oldPlaybackStopCount += 1;
    } else if (audioUnit == (AudioUnit)&g_candidateCaptureUnit) {
        g_hooks.candidateCaptureStopCount += 1;
    } else if (audioUnit == (AudioUnit)&g_candidatePlaybackUnit) {
        g_hooks.candidatePlaybackStopCount += 1;
    } else {
        g_hooks.unexpectedStopCount += 1;
    }

    if (g_hooks.invokeStopCallback && g_pStopCallbackDevice != NULL) {
        on_start_stop__coreaudio(g_pStopCallbackDevice, audioUnit, kAudioOutputUnitProperty_IsRunning, kAudioUnitScope_Global, 0);
    }

    return noErr;
}

static OSStatus hardening_get_property(AudioUnit audioUnit, AudioUnitPropertyID propertyID, AudioUnitScope scope, AudioUnitElement element, void* pData, UInt32* pDataSize)
{
    (void)audioUnit;
    (void)propertyID;
    (void)scope;
    (void)element;
    (void)pDataSize;
    *(UInt32*)pData = 0;
    return noErr;
}

static void hardening_on_notification(const ma_device_notification* pNotification)
{
    if (pNotification->type == ma_device_notification_type_stopped) {
        g_hooks.publicStoppedCount += 1;
    } else if (pNotification->type == ma_device_notification_type_rerouted) {
        g_hooks.publicReroutedCount += 1;
        if (g_pRerouteCompletedEvent != NULL) {
            ma_event_signal(g_pRerouteCompletedEvent);
        }
    }
}

static OSStatus hardening_add_listener(AudioObjectID objectID, const AudioObjectPropertyAddress* pAddress, AudioObjectPropertyListenerProc listener, void* pUserData)
{
    (void)objectID;
    (void)pAddress;
    (void)listener;
    (void)pUserData;
    g_hooks.addListenerCount += 1;
    if (g_hooks.addListenerCount == g_hooks.failAddListenerCall) {
        return -1;
    }
    return noErr;
}

static OSStatus hardening_remove_listener(AudioObjectID objectID, const AudioObjectPropertyAddress* pAddress, AudioObjectPropertyListenerProc listener, void* pUserData)
{
    (void)objectID;
    (void)pAddress;
    (void)listener;
    (void)pUserData;
    g_hooks.removeListenerCount += 1;
    if (g_hooks.removeListenerCount == 2 && g_pSecondRemoveEvent != NULL) {
        ma_event_signal(g_pSecondRemoveEvent);
    }
    return noErr;
}

static OSStatus hardening_dispose(AudioComponentInstance audioUnit)
{
    if (audioUnit == (AudioComponentInstance)&g_oldCaptureUnit) {
        g_hooks.oldCaptureDisposeCount += 1;
    } else if (audioUnit == (AudioComponentInstance)&g_oldPlaybackUnit) {
        g_hooks.oldPlaybackDisposeCount += 1;
    } else if (audioUnit == (AudioComponentInstance)&g_candidateCaptureUnit) {
        g_hooks.candidateCaptureDisposeCount += 1;
    } else if (audioUnit == (AudioComponentInstance)&g_candidatePlaybackUnit) {
        g_hooks.candidatePlaybackDisposeCount += 1;
    } else {
        g_hooks.unexpectedDisposeCount += 1;
    }

    return noErr;
}

static ma_result hardening_coreaudio_init_internal(ma_context* pContext, ma_device_type deviceType, const ma_device_id* pDeviceID, void* pData, void* pDevice)
{
    ma_device_init_internal_data__coreaudio* pInternalData = (ma_device_init_internal_data__coreaudio*)pData;
    ma_result result;

    (void)pDeviceID;
    (void)pDevice;

    g_hooks.initCount += 1;
    if (deviceType == ma_device_type_capture) {
        g_hooks.captureInitCount += 1;
        result = g_hooks.captureResult;
    } else {
        g_hooks.playbackInitCount += 1;
        result = g_hooks.playbackResult;
    }
    if (result != MA_SUCCESS) {
        return result;
    }

    if (deviceType == ma_device_type_capture && g_hooks.allocateCaptureBuffer) {
        pInternalData->pAudioBufferList = (AudioBufferList*)ma_malloc(sizeof(AudioBufferList), &pContext->allocationCallbacks);
        if (pInternalData->pAudioBufferList == NULL) {
            return MA_OUT_OF_MEMORY;
        }
        MA_ZERO_MEMORY(pInternalData->pAudioBufferList, sizeof(AudioBufferList));
        g_pCandidateBuffer = pInternalData->pAudioBufferList;
    }

    pInternalData->audioUnit = (deviceType == ma_device_type_capture) ? (AudioUnit)&g_candidateCaptureUnit : (AudioUnit)&g_candidatePlaybackUnit;
    pInternalData->formatOut = ma_format_f32;
    pInternalData->channelsOut = 2;
    pInternalData->sampleRateOut = g_hooks.candidateSampleRate;
    ma_channel_map_init_standard(ma_standard_channel_map_default, pInternalData->channelMapOut, MA_MAX_CHANNELS, 2);
    pInternalData->periodSizeInFramesOut = 64;
    pInternalData->periodsOut = 3;
    pInternalData->deviceObjectID = (deviceType == ma_device_type_capture) ? 101 : 202;
    ma_strcpy_s(pInternalData->deviceName, sizeof(pInternalData->deviceName), "hardening candidate");
    return MA_SUCCESS;
}

static ma_result hardening_coreaudio_get_uid(ma_context* pContext, ma_uint32 objectID, size_t bufferSize, char* pBufferOut)
{
    (void)pContext;
    (void)objectID;
    g_hooks.uidCount += 1;
    ma_strncpy_s(pBufferOut, bufferSize, "hardening-uid", (size_t)-1);
    return MA_SUCCESS;
}

static void hardening_hooks_reset(void)
{
    MA_ZERO_OBJECT(&g_hooks);
    g_hooks.captureResult = MA_SUCCESS;
    g_hooks.playbackResult = MA_SUCCESS;
    g_hooks.candidateSampleRate = 48000;
    g_pOldBuffer = NULL;
    g_pCandidateBuffer = NULL;
    g_pStopCallbackDevice = NULL;
    g_pCallbackEnteredEvent = NULL;
    g_pCallbackReleaseEvent = NULL;
    g_pSecondRemoveEvent = NULL;
    g_pRerouteCompletedEvent = NULL;
    ma_atomic_store_32(&g_callbackHookEnabled, MA_FALSE);
}

static void hardening_fixture_init(hardening_fixture* pFixture, ma_device_type deviceType)
{
    MA_ZERO_OBJECT(pFixture);
    hardening_hooks_reset();

    pFixture->context.backend = ma_backend_coreaudio;
    pFixture->context.allocationCallbacks.pUserData = &pFixture->allocator;
    pFixture->context.allocationCallbacks.onMalloc = hardening_malloc;
    pFixture->context.allocationCallbacks.onRealloc = hardening_realloc;
    pFixture->context.allocationCallbacks.onFree = hardening_free;
    pFixture->context.coreaudio.AudioOutputUnitStop = (ma_proc)hardening_stop;
    pFixture->context.coreaudio.AudioComponentInstanceDispose = (ma_proc)hardening_dispose;
    pFixture->context.coreaudio.AudioUnitGetProperty = (ma_proc)hardening_get_property;

    pFixture->device.pContext = &pFixture->context;
    pFixture->device.type = deviceType;
    pFixture->device.onNotification = hardening_on_notification;
    pFixture->device.sampleRate = 48000;
    pFixture->device.capture.format = ma_format_f32;
    pFixture->device.capture.channels = 2;
    pFixture->device.capture.shareMode = ma_share_mode_shared;
    pFixture->device.playback.format = ma_format_f32;
    pFixture->device.playback.channels = 2;
    pFixture->device.playback.shareMode = ma_share_mode_shared;
    ma_channel_map_init_standard(ma_standard_channel_map_default, pFixture->device.capture.channelMap, MA_MAX_CHANNELS, 2);
    ma_channel_map_init_standard(ma_standard_channel_map_default, pFixture->device.playback.channelMap, MA_MAX_CHANNELS, 2);
    pFixture->device.coreaudio.originalPeriodSizeInFrames = 64;
    pFixture->device.coreaudio.originalPeriods = 3;
    pFixture->device.coreaudio.originalPerformanceProfile = ma_performance_profile_low_latency;
}

static void hardening_descriptor_init(ma_device_descriptor* pDescriptor)
{
    MA_ZERO_OBJECT(pDescriptor);
    pDescriptor->shareMode = ma_share_mode_shared;
    pDescriptor->format = ma_format_f32;
    pDescriptor->channels = 2;
    pDescriptor->sampleRate = 48000;
    pDescriptor->periodSizeInFrames = 64;
    pDescriptor->periodCount = 3;
    ma_channel_map_init_standard(ma_standard_channel_map_default, pDescriptor->channelMap, MA_MAX_CHANNELS, 2);
}

static ma_result hardening_prepare_capture_converter(hardening_fixture* pFixture)
{
    pFixture->device.capture.internalFormat = ma_format_f32;
    pFixture->device.capture.internalChannels = 2;
    pFixture->device.capture.internalSampleRate = 48000;
    pFixture->device.capture.internalPeriodSizeInFrames = 64;
    pFixture->device.capture.internalPeriods = 3;
    ma_channel_map_init_standard(ma_standard_channel_map_default, pFixture->device.capture.internalChannelMap, MA_MAX_CHANNELS, 2);
    return ma_device__post_init_setup(&pFixture->device, ma_device_type_capture);
}

static int hardening_test_tracking_worker_lifecycle(void)
{
    ma_context context;
    AudioObjectPropertyAddress address;
    ma_result result;
    int failed = 0;

    MA_ZERO_OBJECT(&context);
    hardening_hooks_reset();
    context.allocationCallbacks = ma_allocation_callbacks_init_default();
    context.coreaudio.AudioObjectAddPropertyListener = (ma_proc)hardening_add_listener;
    context.coreaudio.AudioObjectRemovePropertyListener = (ma_proc)hardening_remove_listener;

    g_hooks.failAddListenerCall = 2;
    result = ma_context__init_device_tracking__coreaudio(&context);
    if (result == MA_SUCCESS || g_DeviceTrackingInitCounter_CoreAudio != 0 ||
        g_DefaultDeviceChangePipe_CoreAudio[0] != -1 || g_DefaultDeviceChangePipe_CoreAudio[1] != -1 ||
        g_hooks.addListenerCount != 2 || g_hooks.removeListenerCount != 1) {
        printf("tracking listener failure cleanup mismatch: result=%d counter=%u pipe=%d,%d add=%u remove=%u\n",
            result, g_DeviceTrackingInitCounter_CoreAudio, g_DefaultDeviceChangePipe_CoreAudio[0],
            g_DefaultDeviceChangePipe_CoreAudio[1], g_hooks.addListenerCount, g_hooks.removeListenerCount);
        failed = 1;
    }

    hardening_hooks_reset();
    result = ma_context__init_device_tracking__coreaudio(&context);
    if (result != MA_SUCCESS || g_DeviceTrackingInitCounter_CoreAudio != 1 ||
        ma_atomic_load_32(&g_AcceptingDefaultDeviceChanges_CoreAudio) == MA_FALSE || g_hooks.addListenerCount != 2) {
        printf("tracking worker init mismatch: result=%d counter=%u accepting=%u add=%u\n",
            result, g_DeviceTrackingInitCounter_CoreAudio,
            ma_atomic_load_32(&g_AcceptingDefaultDeviceChanges_CoreAudio), g_hooks.addListenerCount);
        return 1;
    }

    address.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    address.mScope = kAudioObjectPropertyScopeGlobal;
    address.mElement = AUDIO_OBJECT_PROPERTY_ELEMENT;
    ma_default_device_changed__coreaudio(kAudioObjectSystemObject, 1, &address, NULL);

    result = ma_context__uninit_device_tracking__coreaudio(&context);
    if (result != MA_SUCCESS || g_DeviceTrackingInitCounter_CoreAudio != 0 ||
        ma_atomic_load_32(&g_AcceptingDefaultDeviceChanges_CoreAudio) != MA_FALSE ||
        g_DefaultDeviceChangePipe_CoreAudio[0] != -1 || g_DefaultDeviceChangePipe_CoreAudio[1] != -1 ||
        g_hooks.removeListenerCount != 2) {
        printf("tracking worker teardown mismatch: result=%d counter=%u accepting=%u pipe=%d,%d remove=%u\n",
            result, g_DeviceTrackingInitCounter_CoreAudio,
            ma_atomic_load_32(&g_AcceptingDefaultDeviceChanges_CoreAudio),
            g_DefaultDeviceChangePipe_CoreAudio[0], g_DefaultDeviceChangePipe_CoreAudio[1],
            g_hooks.removeListenerCount);
        failed = 1;
    }

    return failed;
}

static ma_thread_result MA_THREADCALL hardening_delayed_callback_thread(void* pUserData)
{
    AudioObjectPropertyAddress address;

    (void)pUserData;
    address.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    address.mScope = kAudioObjectPropertyScopeGlobal;
    address.mElement = AUDIO_OBJECT_PROPERTY_ELEMENT;
    ma_default_device_changed__coreaudio(kAudioObjectSystemObject, 1, &address, NULL);
    return (ma_thread_result)0;
}

static ma_thread_result MA_THREADCALL hardening_tracking_uninit_thread(void* pUserData)
{
    hardening_uninit_thread_data* pThreadData = (hardening_uninit_thread_data*)pUserData;

    pThreadData->result = ma_context__uninit_device_tracking__coreaudio(pThreadData->pContext);
    ma_atomic_store_32(&pThreadData->complete, MA_TRUE);
    return (ma_thread_result)0;
}

static int hardening_test_callback_retirement_before_pipe_close(void)
{
    ma_context context;
    hardening_uninit_thread_data uninitData;
    ma_event callbackEnteredEvent;
    ma_event callbackReleaseEvent;
    ma_event secondRemoveEvent;
    ma_thread callbackThread;
    ma_thread uninitThread;
    ma_result result;
    ma_bool32 callbackThreadCreated = MA_FALSE;
    ma_bool32 uninitThreadCreated = MA_FALSE;
    int failed = 0;

    MA_ZERO_OBJECT(&context);
    MA_ZERO_OBJECT(&uninitData);
    hardening_hooks_reset();
    context.allocationCallbacks = ma_allocation_callbacks_init_default();
    context.coreaudio.AudioObjectAddPropertyListener = (ma_proc)hardening_add_listener;
    context.coreaudio.AudioObjectRemovePropertyListener = (ma_proc)hardening_remove_listener;

    result = ma_event_init(&callbackEnteredEvent);
    if (result != MA_SUCCESS) {
        return 1;
    }
    result = ma_event_init(&callbackReleaseEvent);
    if (result != MA_SUCCESS) {
        ma_event_uninit(&callbackEnteredEvent);
        return 1;
    }
    result = ma_event_init(&secondRemoveEvent);
    if (result != MA_SUCCESS) {
        ma_event_uninit(&callbackReleaseEvent);
        ma_event_uninit(&callbackEnteredEvent);
        return 1;
    }

    result = ma_context__init_device_tracking__coreaudio(&context);
    if (result != MA_SUCCESS) {
        ma_event_uninit(&secondRemoveEvent);
        ma_event_uninit(&callbackReleaseEvent);
        ma_event_uninit(&callbackEnteredEvent);
        return 1;
    }

    g_pCallbackEnteredEvent = &callbackEnteredEvent;
    g_pCallbackReleaseEvent = &callbackReleaseEvent;
    g_pSecondRemoveEvent = &secondRemoveEvent;
    ma_atomic_store_32(&g_callbackHookEnabled, MA_TRUE);

    result = ma_thread_create(&callbackThread, ma_thread_priority_normal, 0, hardening_delayed_callback_thread, NULL, &context.allocationCallbacks);
    if (result == MA_SUCCESS) {
        callbackThreadCreated = MA_TRUE;
        ma_event_wait(&callbackEnteredEvent);

        uninitData.pContext = &context;
        result = ma_thread_create(&uninitThread, ma_thread_priority_normal, 0, hardening_tracking_uninit_thread, &uninitData, &context.allocationCallbacks);
        if (result == MA_SUCCESS) {
            uninitThreadCreated = MA_TRUE;
            ma_event_wait(&secondRemoveEvent);

            if (ma_atomic_load_32(&uninitData.complete) != MA_FALSE ||
                ma_atomic_load_32(&g_DefaultDeviceChangeCallbackCount_CoreAudio) != 1) {
                printf("tracking teardown did not wait for the entered callback\n");
                failed = 1;
            }
        } else {
            failed = 1;
        }
    } else {
        failed = 1;
    }

    ma_atomic_store_32(&g_callbackHookEnabled, MA_FALSE);
    ma_event_signal(&callbackReleaseEvent);

    if (callbackThreadCreated) {
        ma_thread_wait(&callbackThread);
    }
    if (uninitThreadCreated) {
        ma_thread_wait(&uninitThread);
    } else {
        ma_context__uninit_device_tracking__coreaudio(&context);
    }

    if (uninitThreadCreated && (uninitData.result != MA_SUCCESS || ma_atomic_load_32(&uninitData.complete) == MA_FALSE)) {
        printf("tracking teardown thread failed: result=%d complete=%u\n", uninitData.result, ma_atomic_load_32(&uninitData.complete));
        failed = 1;
    }
    if (g_DefaultDeviceChangePipe_CoreAudio[0] != -1 || g_DefaultDeviceChangePipe_CoreAudio[1] != -1 ||
        ma_atomic_load_32(&g_DefaultDeviceChangeCallbackCount_CoreAudio) != 0 ||
        (ma_atomic_load_32(&g_PendingDefaultDeviceChanges_CoreAudio) &
            (MA_COREAUDIO_DEFAULT_DEVICE_CHANGE_CAPTURE | MA_COREAUDIO_DEFAULT_DEVICE_CHANGE_PLAYBACK)) != 0) {
        printf("callback retirement left published work or live descriptors\n");
        failed = 1;
    }

    g_pCallbackEnteredEvent = NULL;
    g_pCallbackReleaseEvent = NULL;
    g_pSecondRemoveEvent = NULL;
    ma_event_uninit(&secondRemoveEvent);
    ma_event_uninit(&callbackReleaseEvent);
    ma_event_uninit(&callbackEnteredEvent);
    return failed;
}

static int hardening_test_callback_publication_is_bounded_and_coalesced(void)
{
    AudioObjectPropertyAddress addresses[2];
    ma_uint8 wakeBuffer[8];
    ssize_t bytesRead;
    ma_uint32 pendingChanges;
    int failed = 0;

    hardening_hooks_reset();
    if (pipe(g_DefaultDeviceChangePipe_CoreAudio) != 0) {
        return 1;
    }
    ma_atomic_store_32(&g_PendingDefaultDeviceChanges_CoreAudio, 0);
    ma_atomic_store_32(&g_AcceptingDefaultDeviceChanges_CoreAudio, MA_TRUE);

    addresses[0].mScope = kAudioObjectPropertyScopeGlobal;
    addresses[0].mElement = AUDIO_OBJECT_PROPERTY_ELEMENT;
    addresses[0].mSelector = kAudioHardwarePropertyDevices;
    addresses[1].mScope = kAudioObjectPropertyScopeGlobal;
    addresses[1].mElement = AUDIO_OBJECT_PROPERTY_ELEMENT;
    addresses[1].mSelector = kAudioHardwarePropertyDefaultInputDevice;
    ma_default_device_changed__coreaudio(kAudioObjectSystemObject, 2, addresses, NULL);
    ma_default_device_changed__coreaudio(kAudioObjectSystemObject, 2, addresses, NULL);
    addresses[1].mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    ma_default_device_changed__coreaudio(kAudioObjectSystemObject, 2, addresses, NULL);

    bytesRead = read(g_DefaultDeviceChangePipe_CoreAudio[0], wakeBuffer, sizeof(wakeBuffer));
    pendingChanges = ma_atomic_exchange_32(&g_PendingDefaultDeviceChanges_CoreAudio, 0);
    if (bytesRead != 3 || pendingChanges != (MA_COREAUDIO_DEFAULT_DEVICE_CHANGE_CAPTURE | MA_COREAUDIO_DEFAULT_DEVICE_CHANGE_PLAYBACK) ||
        g_hooks.initCount != 0) {
        printf("callback publication mismatch: bytes=%ld pending=0x%08X init=%u\n",
            (long)bytesRead, pendingChanges, g_hooks.initCount);
        failed = 1;
    }

    ma_atomic_store_32(&g_AcceptingDefaultDeviceChanges_CoreAudio, MA_FALSE);
    close(g_DefaultDeviceChangePipe_CoreAudio[1]);
    close(g_DefaultDeviceChangePipe_CoreAudio[0]);
    g_DefaultDeviceChangePipe_CoreAudio[0] = -1;
    g_DefaultDeviceChangePipe_CoreAudio[1] = -1;
    return failed;
}

static int hardening_test_control_worker_dispatches_published_reroute(void)
{
    hardening_fixture fixture;
    AudioObjectPropertyAddress address;
    ma_event rerouteCompletedEvent;
    ma_result result;
    ma_bool32 trackingInitialized;
    ma_bool32 startStopLockInitialized;
    ma_bool32 rerouteEventInitialized;
    int failed;

    trackingInitialized = MA_FALSE;
    startStopLockInitialized = MA_FALSE;
    rerouteEventInitialized = MA_FALSE;
    failed = 0;
    hardening_fixture_init(&fixture, ma_device_type_playback);
    fixture.context.coreaudio.AudioObjectAddPropertyListener = (ma_proc)hardening_add_listener;
    fixture.context.coreaudio.AudioObjectRemovePropertyListener = (ma_proc)hardening_remove_listener;

    result = ma_event_init(&fixture.device.coreaudio.stopEvent);
    if (result != MA_SUCCESS) {
        return 1;
    }
    result = ma_mutex_init(&fixture.device.startStopLock);
    if (result != MA_SUCCESS) {
        failed = 1;
        goto done;
    }
    startStopLockInitialized = MA_TRUE;
    result = ma_event_init(&rerouteCompletedEvent);
    if (result != MA_SUCCESS) {
        failed = 1;
        goto done;
    }
    rerouteEventInitialized = MA_TRUE;

    ma_device__set_state(&fixture.device, ma_device_state_stopped);
    fixture.device.coreaudio.isDefaultPlaybackDevice = MA_TRUE;

    result = ma_context__init_device_tracking__coreaudio(&fixture.context);
    if (result != MA_SUCCESS) {
        printf("control worker dispatch tracking init failed: %d\n", result);
        failed = 1;
        goto done;
    }
    trackingInitialized = MA_TRUE;

    result = ma_device__track__coreaudio(&fixture.device);
    if (result != MA_SUCCESS) {
        printf("control worker dispatch device tracking failed: %d\n", result);
        failed = 1;
        goto done;
    }

    g_pRerouteCompletedEvent = &rerouteCompletedEvent;
    address.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    address.mScope = kAudioObjectPropertyScopeGlobal;
    address.mElement = AUDIO_OBJECT_PROPERTY_ELEMENT;
    ma_default_device_changed__coreaudio(kAudioObjectSystemObject, 1, &address, NULL);

    /* The notification is the acknowledgement that the real worker consumed and applied the publication. */
    result = ma_event_wait(&rerouteCompletedEvent);
    if (result != MA_SUCCESS) {
        printf("control worker dispatch wait failed: %d\n", result);
        failed = 1;
    }

    ma_mutex_lock(&g_DeviceTrackingMutex_CoreAudio);
    ma_mutex_unlock(&g_DeviceTrackingMutex_CoreAudio);

    g_pRerouteCompletedEvent = NULL;

    if (g_hooks.initCount != 1 || g_hooks.publicReroutedCount != 1 ||
        fixture.device.coreaudio.audioUnitPlayback != (ma_ptr)&g_candidatePlaybackUnit ||
        ma_atomic_load_32(&g_PendingDefaultDeviceChanges_CoreAudio) != 0) {
        printf("control worker dispatch mismatch: init=%u rerouted=%u unit=%p pending=0x%08X\n",
            g_hooks.initCount,
            g_hooks.publicReroutedCount,
            fixture.device.coreaudio.audioUnitPlayback,
            ma_atomic_load_32(&g_PendingDefaultDeviceChanges_CoreAudio));
        failed = 1;
    }

done:
    g_pRerouteCompletedEvent = NULL;
    if (trackingInitialized) {
        /* Device teardown acquires the worker's tracking mutex, so this also acknowledges dispatch exit. */
        ma_device__set_state(&fixture.device, ma_device_state_uninitialized);
        ma_device_uninit__coreaudio(&fixture.device);
        result = ma_context__uninit_device_tracking__coreaudio(&fixture.context);
        if (result != MA_SUCCESS) {
            printf("control worker dispatch tracking uninit failed: %d\n", result);
            failed = 1;
        }
    } else {
        ma_event_uninit(&fixture.device.coreaudio.stopEvent);
    }

    if (startStopLockInitialized) {
        ma_mutex_uninit(&fixture.device.startStopLock);
    }
    ma_data_converter_uninit(&fixture.device.playback.converter, &fixture.context.allocationCallbacks);
    if (rerouteEventInitialized) {
        ma_event_uninit(&rerouteCompletedEvent);
    }
    if (ma_atomic_load_32(&fixture.allocator.liveAllocationCount) != 0 || g_hooks.candidatePlaybackDisposeCount != (g_hooks.initCount != 0 ? 1U : 0U)) {
        printf("control worker dispatch teardown mismatch: live=%lu candidate-dispose=%u init=%u\n",
            (unsigned long)ma_atomic_load_32(&fixture.allocator.liveAllocationCount),
            g_hooks.candidatePlaybackDisposeCount,
            g_hooks.initCount);
        failed = 1;
    }
    return failed;
}

static int hardening_test_control_worker_filters_explicit_endpoint(void)
{
    hardening_fixture fixture;
    ma_device* trackedDevices[1];
    ma_result result;
    int failed = 0;

    hardening_fixture_init(&fixture, ma_device_type_playback);
    result = ma_event_init(&fixture.device.coreaudio.stopEvent);
    if (result != MA_SUCCESS) {
        return 1;
    }
    result = ma_mutex_init(&fixture.device.startStopLock);
    if (result != MA_SUCCESS) {
        ma_event_uninit(&fixture.device.coreaudio.stopEvent);
        return 1;
    }
    ma_device__set_state(&fixture.device, ma_device_state_stopped);

    trackedDevices[0] = &fixture.device;
    g_ppTrackedDevices_CoreAudio = trackedDevices;
    g_TrackedDeviceCap_CoreAudio = 1;
    g_TrackedDeviceCount_CoreAudio = 1;

    fixture.device.coreaudio.isDefaultPlaybackDevice = MA_FALSE;
    ma_process_default_device_changes__coreaudio(MA_COREAUDIO_DEFAULT_DEVICE_CHANGE_PLAYBACK);
    if (g_hooks.initCount != 0 || g_hooks.publicReroutedCount != 0) {
        printf("control worker rerouted an explicit endpoint\n");
        failed = 1;
    }

    fixture.device.coreaudio.isDefaultPlaybackDevice = MA_TRUE;
    ma_process_default_device_changes__coreaudio(MA_COREAUDIO_DEFAULT_DEVICE_CHANGE_PLAYBACK);
    if (g_hooks.initCount != 1 || g_hooks.publicReroutedCount != 1 ||
        fixture.device.coreaudio.audioUnitPlayback != (ma_ptr)&g_candidatePlaybackUnit) {
        printf("control worker default reroute mismatch: init=%u rerouted=%u unit=%p\n",
            g_hooks.initCount, g_hooks.publicReroutedCount, fixture.device.coreaudio.audioUnitPlayback);
        failed = 1;
    }

    g_ppTrackedDevices_CoreAudio = NULL;
    g_TrackedDeviceCap_CoreAudio = 0;
    g_TrackedDeviceCount_CoreAudio = 0;
    ma_device__set_state(&fixture.device, ma_device_state_uninitialized);
    ma_device_uninit__coreaudio(&fixture.device);
    ma_mutex_uninit(&fixture.device.startStopLock);
    ma_data_converter_uninit(&fixture.device.playback.converter, &fixture.context.allocationCallbacks);
    if (ma_atomic_load_32(&fixture.allocator.liveAllocationCount) != 0) {
        printf("control worker test leaked %lu allocations\n", (unsigned long)ma_atomic_load_32(&fixture.allocator.liveAllocationCount));
        failed = 1;
    }
    return failed;
}

static int hardening_test_duplex_playback_failure_is_not_tracked(void)
{
    hardening_fixture fixture;
    ma_device_config config;
    ma_device_descriptor playbackDescriptor;
    ma_device_descriptor captureDescriptor;
    AudioObjectPropertyAddress address;
    ma_result result;
    int failed = 0;

    hardening_fixture_init(&fixture, ma_device_type_duplex);
    config = ma_device_config_init(ma_device_type_duplex);
    hardening_descriptor_init(&playbackDescriptor);
    hardening_descriptor_init(&captureDescriptor);
    g_hooks.allocateCaptureBuffer = MA_TRUE;
    g_hooks.playbackResult = MA_ERROR;

    result = ma_device_init__coreaudio(&fixture.device, &config, &playbackDescriptor, &captureDescriptor);
    if (result != MA_ERROR) {
        printf("duplex failure returned %d, expected MA_ERROR\n", result);
        failed = 1;
    }
    if (g_TrackedDeviceCount_CoreAudio != 0 || g_ppTrackedDevices_CoreAudio != NULL) {
        printf("duplex failure published a global tracking pointer\n");
        failed = 1;
    }
    if (fixture.device.coreaudio.audioUnitCapture != NULL || fixture.device.coreaudio.pAudioBufferList != NULL ||
        g_hooks.candidateCaptureDisposeCount != 1 || fixture.allocator.candidateBufferFreeCount != 1) {
        printf("duplex failure cleanup mismatch: unit=%p buffer=%p dispose=%u free=%u\n",
            fixture.device.coreaudio.audioUnitCapture, fixture.device.coreaudio.pAudioBufferList,
            g_hooks.candidateCaptureDisposeCount, fixture.allocator.candidateBufferFreeCount);
        failed = 1;
    }

    address.mSelector = kAudioHardwarePropertyDefaultInputDevice;
    address.mScope = kAudioObjectPropertyScopeGlobal;
    address.mElement = AUDIO_OBJECT_PROPERTY_ELEMENT;
    ma_default_device_changed__coreaudio(kAudioObjectSystemObject, 1, &address, NULL);
    if (g_hooks.initCount != 2) {
        printf("notification reached failed duplex device: init count=%u\n", g_hooks.initCount);
        failed = 1;
    }
    if (ma_atomic_load_32(&fixture.allocator.liveAllocationCount) != 0) {
        printf("duplex failure leaked %lu allocations\n", (unsigned long)ma_atomic_load_32(&fixture.allocator.liveAllocationCount));
        failed = 1;
    }

    return failed;
}

static int hardening_test_candidate_failure_preserves_old_endpoint(void)
{
    hardening_fixture fixture;
    ma_result result;
    int failed = 0;

    hardening_fixture_init(&fixture, ma_device_type_playback);
    result = ma_event_init(&fixture.device.coreaudio.stopEvent);
    if (result != MA_SUCCESS) {
        return 1;
    }
    fixture.device.coreaudio.audioUnitPlayback = (ma_ptr)&g_oldPlaybackUnit;
    ma_device__set_state(&fixture.device, ma_device_state_started);
    g_hooks.playbackResult = MA_ERROR;

    result = ma_device_reinit_internal__coreaudio(&fixture.device, ma_device_type_playback);
    if (result != MA_ERROR || fixture.device.coreaudio.audioUnitPlayback != (ma_ptr)&g_oldPlaybackUnit ||
        g_hooks.oldPlaybackStopCount != 0 || g_hooks.oldPlaybackDisposeCount != 0 ||
        ma_device_get_state(&fixture.device) != ma_device_state_started) {
        printf("candidate failure disturbed old endpoint: result=%d unit=%p stop=%u dispose=%u state=%d\n",
            result, fixture.device.coreaudio.audioUnitPlayback, g_hooks.oldPlaybackStopCount,
            g_hooks.oldPlaybackDisposeCount, ma_device_get_state(&fixture.device));
        failed = 1;
    }

    ma_device__set_state(&fixture.device, ma_device_state_uninitialized);
    ma_device_uninit__coreaudio(&fixture.device);
    if (g_hooks.oldPlaybackDisposeCount != 1 || g_hooks.unexpectedDisposeCount != 0) {
        printf("candidate failure teardown mismatch: old=%u unexpected=%u\n",
            g_hooks.oldPlaybackDisposeCount, g_hooks.unexpectedDisposeCount);
        failed = 1;
    }
    return failed;
}

static int hardening_test_post_init_failure_is_empty_and_destroyable(void)
{
    hardening_fixture fixture;
    ma_result result;
    int failed = 0;

    hardening_fixture_init(&fixture, ma_device_type_capture);
    result = ma_event_init(&fixture.device.coreaudio.stopEvent);
    if (result != MA_SUCCESS) {
        return 1;
    }
    result = hardening_prepare_capture_converter(&fixture);
    if (result != MA_SUCCESS) {
        ma_event_uninit(&fixture.device.coreaudio.stopEvent);
        return 1;
    }

    fixture.device.coreaudio.audioUnitCapture = (ma_ptr)&g_oldCaptureUnit;
    g_pOldBuffer = ma_malloc(sizeof(AudioBufferList), &fixture.context.allocationCallbacks);
    fixture.device.coreaudio.pAudioBufferList = g_pOldBuffer;
    fixture.device.coreaudio.audioBufferCapInFrames = 64;
    g_hooks.allocateCaptureBuffer = MA_TRUE;
    g_hooks.candidateSampleRate = 44100;
    g_hooks.invokeStopCallback = MA_TRUE;
    g_pStopCallbackDevice = &fixture.device;
    fixture.device.coreaudio.isSwitchingCaptureDevice = MA_TRUE;
    fixture.allocator.failAllocationCall = fixture.allocator.allocationCallCount + 2;
    ma_device__set_state(&fixture.device, ma_device_state_started);

    result = ma_device_reinit_internal__coreaudio(&fixture.device, ma_device_type_capture);
    if (result != MA_OUT_OF_MEMORY || fixture.device.coreaudio.audioUnitCapture != NULL ||
        fixture.device.coreaudio.pAudioBufferList != NULL || fixture.device.coreaudio.audioBufferCapInFrames != 0 ||
        ma_device_get_state(&fixture.device) != ma_device_state_stopped) {
        printf("post-init failure did not leave an empty stopped endpoint: result=%d unit=%p buffer=%p cap=%u state=%d\n",
            result, fixture.device.coreaudio.audioUnitCapture, fixture.device.coreaudio.pAudioBufferList,
            fixture.device.coreaudio.audioBufferCapInFrames, ma_device_get_state(&fixture.device));
        failed = 1;
    }
    if (g_hooks.oldCaptureStopCount != 1 || g_hooks.oldCaptureDisposeCount != 1 ||
        g_hooks.candidateCaptureDisposeCount != 1 || fixture.allocator.oldBufferFreeCount != 1 ||
        fixture.allocator.candidateBufferFreeCount != 1) {
        printf("post-init failure cleanup mismatch: stop=%u old-dispose=%u candidate-dispose=%u old-free=%u candidate-free=%u\n",
            g_hooks.oldCaptureStopCount, g_hooks.oldCaptureDisposeCount, g_hooks.candidateCaptureDisposeCount,
            fixture.allocator.oldBufferFreeCount, fixture.allocator.candidateBufferFreeCount);
        failed = 1;
    }
    if (g_hooks.publicStoppedCount != 0) {
        printf("internal capture reroute emitted %u public stopped notifications\n", g_hooks.publicStoppedCount);
        failed = 1;
    }

    ma_device__set_state(&fixture.device, ma_device_state_uninitialized);
    ma_device_uninit__coreaudio(&fixture.device);
    if (g_hooks.oldCaptureDisposeCount != 1 || g_hooks.candidateCaptureDisposeCount != 1 ||
        fixture.allocator.oldBufferFreeCount != 1 || fixture.allocator.candidateBufferFreeCount != 1) {
        printf("ordinary teardown released a retired owner twice\n");
        failed = 1;
    }

    ma_data_converter_uninit(&fixture.device.capture.converter, &fixture.context.allocationCallbacks);
    if (ma_atomic_load_32(&fixture.allocator.liveAllocationCount) != 0) {
        printf("post-init failure leaked %lu allocations\n", (unsigned long)ma_atomic_load_32(&fixture.allocator.liveAllocationCount));
        failed = 1;
    }
    return failed;
}

static int hardening_test_success_publishes_complete_candidate(void)
{
    hardening_fixture fixture;
    ma_result result;
    int failed = 0;

    hardening_fixture_init(&fixture, ma_device_type_capture);
    result = ma_event_init(&fixture.device.coreaudio.stopEvent);
    if (result != MA_SUCCESS) {
        return 1;
    }
    result = hardening_prepare_capture_converter(&fixture);
    if (result != MA_SUCCESS) {
        ma_event_uninit(&fixture.device.coreaudio.stopEvent);
        return 1;
    }

    fixture.device.coreaudio.audioUnitCapture = (ma_ptr)&g_oldCaptureUnit;
    g_pOldBuffer = ma_malloc(sizeof(AudioBufferList), &fixture.context.allocationCallbacks);
    fixture.device.coreaudio.pAudioBufferList = g_pOldBuffer;
    fixture.device.coreaudio.audioBufferCapInFrames = 64;
    g_hooks.allocateCaptureBuffer = MA_TRUE;
    ma_device__set_state(&fixture.device, ma_device_state_started);

    result = ma_device_reinit_internal__coreaudio(&fixture.device, ma_device_type_capture);
    if (result != MA_SUCCESS || fixture.device.coreaudio.audioUnitCapture != (ma_ptr)&g_candidateCaptureUnit ||
        fixture.device.coreaudio.pAudioBufferList != g_pCandidateBuffer || fixture.device.coreaudio.audioBufferCapInFrames != 64 ||
        g_hooks.uidCount != 1) {
        printf("successful reroute publication mismatch: result=%d unit=%p buffer=%p cap=%u uid=%u\n",
            result, fixture.device.coreaudio.audioUnitCapture, fixture.device.coreaudio.pAudioBufferList,
            fixture.device.coreaudio.audioBufferCapInFrames, g_hooks.uidCount);
        failed = 1;
    }
    if (g_hooks.oldCaptureStopCount != 1 || g_hooks.oldCaptureDisposeCount != 1 ||
        fixture.allocator.oldBufferFreeCount != 1 || g_hooks.candidateCaptureDisposeCount != 0 ||
        fixture.allocator.candidateBufferFreeCount != 0) {
        printf("successful reroute retired or released the wrong owner\n");
        failed = 1;
    }

    ma_device__set_state(&fixture.device, ma_device_state_uninitialized);
    ma_device_uninit__coreaudio(&fixture.device);
    ma_data_converter_uninit(&fixture.device.capture.converter, &fixture.context.allocationCallbacks);
    if (g_hooks.candidateCaptureDisposeCount != 1 || fixture.allocator.candidateBufferFreeCount != 1 ||
        ma_atomic_load_32(&fixture.allocator.liveAllocationCount) != 0 || g_hooks.unexpectedDisposeCount != 0) {
        printf("successful reroute teardown mismatch: dispose=%u free=%u live=%lu unexpected=%u\n",
            g_hooks.candidateCaptureDisposeCount, fixture.allocator.candidateBufferFreeCount,
            (unsigned long)ma_atomic_load_32(&fixture.allocator.liveAllocationCount), g_hooks.unexpectedDisposeCount);
        failed = 1;
    }
    return failed;
}

int main(void)
{
    ma_result result;
    int failed = 0;

    failed += hardening_test_tracking_worker_lifecycle();
    failed += hardening_test_callback_retirement_before_pipe_close();
    failed += hardening_test_callback_publication_is_bounded_and_coalesced();
    failed += hardening_test_control_worker_dispatches_published_reroute();

    g_ppTrackedDevices_CoreAudio = NULL;
    g_TrackedDeviceCap_CoreAudio = 0;
    g_TrackedDeviceCount_CoreAudio = 0;
    result = ma_mutex_init(&g_DeviceTrackingMutex_CoreAudio);
    if (result != MA_SUCCESS) {
        return 1;
    }

    failed += hardening_test_control_worker_filters_explicit_endpoint();
    failed += hardening_test_duplex_playback_failure_is_not_tracked();
    failed += hardening_test_candidate_failure_preserves_old_endpoint();
    failed += hardening_test_post_init_failure_is_empty_and_destroyable();
    failed += hardening_test_success_publishes_complete_candidate();

    if (g_ppTrackedDevices_CoreAudio != NULL || g_TrackedDeviceCount_CoreAudio != 0) {
        printf("test suite left tracked Core Audio devices\n");
        failed = 1;
    }
    ma_mutex_uninit(&g_DeviceTrackingMutex_CoreAudio);
    return failed != 0;
}
