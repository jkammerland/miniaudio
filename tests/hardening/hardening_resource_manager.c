#define MA_NO_DEVICE_IO
#include "../../miniaudio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    ma_uint32 notificationInitCallCount;
    ma_uint32 notificationInitSuccessCount;
    ma_uint32 notificationWaitCallCount;
    ma_uint32 notificationUninitCallCount;
    ma_uint32 invalidNotificationSignalCount;
    ma_uint32 invalidNotificationWaitCount;
    ma_uint32 invalidNotificationUninitCount;
    ma_uint32 failNotificationInitCall;
    ma_result notificationInitFailureResult;
    ma_async_notification_event* pFailedNotificationEvent;
    ma_uint32 fenceAcquireCallCount;
    ma_uint32 fenceAcquireSuccessCount;
    ma_uint32 fenceReleaseCallCount;
    ma_uint32 failFenceAcquireCall;
    ma_result fenceAcquireFailureResult;
    ma_resource_manager_data_stream* pRetirementDataStream;
    void (* notificationEventOnSignal)(ma_async_notification* pNotification);
    ma_uint32 notificationBeforeRetirementCount;
} hardening_resource_manager_fault_state;

static hardening_resource_manager_fault_state g_hardeningResourceManagerFaultState;

static void hardening_invalid_notification_on_signal(ma_async_notification* pNotification)
{
    (void)pNotification;
    g_hardeningResourceManagerFaultState.invalidNotificationSignalCount += 1;
}

static void hardening_retirement_notification_on_signal(ma_async_notification* pNotification)
{
    ma_resource_manager_data_stream* pDataStream;

    pDataStream = g_hardeningResourceManagerFaultState.pRetirementDataStream;
    if (pDataStream != NULL && pDataStream->executionPointer != pDataStream->executionCounter) {
        g_hardeningResourceManagerFaultState.notificationBeforeRetirementCount += 1;
    }
    g_hardeningResourceManagerFaultState.notificationEventOnSignal(pNotification);
}

static ma_result hardening_inline_notification_event_init(ma_async_notification_event* pNotificationEvent)
{
    ma_result result;

    g_hardeningResourceManagerFaultState.notificationInitCallCount += 1;
    if (g_hardeningResourceManagerFaultState.notificationInitCallCount == g_hardeningResourceManagerFaultState.failNotificationInitCall) {
        pNotificationEvent->cb.onSignal = hardening_invalid_notification_on_signal;
        g_hardeningResourceManagerFaultState.pFailedNotificationEvent = pNotificationEvent;
        return g_hardeningResourceManagerFaultState.notificationInitFailureResult;
    }

    result = ma_async_notification_event_init(pNotificationEvent);
    if (result == MA_SUCCESS) {
        g_hardeningResourceManagerFaultState.notificationInitSuccessCount += 1;
        if (g_hardeningResourceManagerFaultState.pRetirementDataStream != NULL) {
            g_hardeningResourceManagerFaultState.notificationEventOnSignal = pNotificationEvent->cb.onSignal;
            pNotificationEvent->cb.onSignal = hardening_retirement_notification_on_signal;
        }
    }
    return result;
}

static ma_result hardening_inline_notification_event_uninit(ma_async_notification_event* pNotificationEvent)
{
    g_hardeningResourceManagerFaultState.notificationUninitCallCount += 1;
    if (pNotificationEvent == g_hardeningResourceManagerFaultState.pFailedNotificationEvent) {
        g_hardeningResourceManagerFaultState.invalidNotificationUninitCount += 1;
        g_hardeningResourceManagerFaultState.pFailedNotificationEvent = NULL;
        return MA_INVALID_OPERATION;
    }

    return ma_async_notification_event_uninit(pNotificationEvent);
}

static ma_result hardening_inline_notification_event_wait(ma_async_notification_event* pNotificationEvent)
{
    g_hardeningResourceManagerFaultState.notificationWaitCallCount += 1;
    if (pNotificationEvent == g_hardeningResourceManagerFaultState.pFailedNotificationEvent) {
        g_hardeningResourceManagerFaultState.invalidNotificationWaitCount += 1;
        return MA_INVALID_OPERATION;
    }

    return ma_async_notification_event_wait(pNotificationEvent);
}

static ma_result hardening_resource_manager_fence_acquire(ma_fence* pFence)
{
    ma_result result;

    g_hardeningResourceManagerFaultState.fenceAcquireCallCount += 1;
    if (g_hardeningResourceManagerFaultState.fenceAcquireCallCount == g_hardeningResourceManagerFaultState.failFenceAcquireCall) {
        return g_hardeningResourceManagerFaultState.fenceAcquireFailureResult;
    }

    result = ma_fence_acquire(pFence);
    if (result == MA_SUCCESS) {
        g_hardeningResourceManagerFaultState.fenceAcquireSuccessCount += 1;
    }
    return result;
}

static ma_result hardening_resource_manager_fence_release(ma_fence* pFence)
{
    g_hardeningResourceManagerFaultState.fenceReleaseCallCount += 1;
    return ma_fence_release(pFence);
}

static void hardening_resource_manager_fault_reset(void)
{
    memset(&g_hardeningResourceManagerFaultState, 0, sizeof(g_hardeningResourceManagerFaultState));
    g_hardeningResourceManagerFaultState.notificationInitFailureResult = MA_OUT_OF_MEMORY;
    g_hardeningResourceManagerFaultState.fenceAcquireFailureResult = MA_OUT_OF_RANGE;
}

#define MA_RESOURCE_MANAGER_INLINE_NOTIFICATION_EVENT_INIT(pNotificationEvent) hardening_inline_notification_event_init((pNotificationEvent))
#define MA_RESOURCE_MANAGER_INLINE_NOTIFICATION_EVENT_UNINIT(pNotificationEvent) hardening_inline_notification_event_uninit((pNotificationEvent))
#define MA_RESOURCE_MANAGER_INLINE_NOTIFICATION_EVENT_WAIT(pNotificationEvent) hardening_inline_notification_event_wait((pNotificationEvent))
#define MA_RESOURCE_MANAGER_FENCE_ACQUIRE(pFence) hardening_resource_manager_fence_acquire((pFence))
#define MA_RESOURCE_MANAGER_FENCE_RELEASE(pFence) hardening_resource_manager_fence_release((pFence))
#define MINIAUDIO_IMPLEMENTATION
#include "../../miniaudio.h"

typedef struct
{
    size_t liveAllocationCount;
    size_t rejectedSize;
    size_t rejectionCount;
    ma_bool32 rejectMatchingSize;
} hardening_allocation_tracker;

typedef struct
{
    ma_vfs_callbacks callbacks;
    ma_result openResult;
    ma_uint32 openCount;
    ma_uint32 closeCount;
} hardening_decoder_vfs;

typedef struct
{
    ma_result lengthResult;
    ma_uint64 lengthInFrames;
    ma_result readResult;
    ma_uint32 initCount;
    ma_uint32 uninitCount;
    ma_uint32 readCount;
} hardening_decoder_backend_state;

typedef struct
{
    ma_data_source_base ds;
    hardening_decoder_backend_state* pState;
    ma_uint64 cursor;
} hardening_decoder_backend;

typedef struct
{
    hardening_allocation_tracker allocations;
    hardening_decoder_vfs vfs;
    hardening_decoder_backend_state backendState;
    ma_decoding_backend_vtable* pBackendVTable;
    ma_resource_manager resourceManager;
    size_t baselineLiveAllocationCount;
} hardening_resource_manager_fixture;

typedef struct
{
    ma_async_notification_callbacks callbacks;
    ma_uint32 signalCount;
} hardening_counting_notification;

static void hardening_counting_notification_on_signal(ma_async_notification* pNotification)
{
    hardening_counting_notification* pCountingNotification = (hardening_counting_notification*)pNotification;
    pCountingNotification->signalCount += 1;
}

static void hardening_counting_notification_init(hardening_counting_notification* pNotification)
{
    MA_ZERO_OBJECT(pNotification);
    pNotification->callbacks.onSignal = hardening_counting_notification_on_signal;
}

static void* hardening_tracked_malloc(size_t size, void* pUserData)
{
    hardening_allocation_tracker* pTracker = (hardening_allocation_tracker*)pUserData;
    void* pAllocation;

    if (pTracker->rejectMatchingSize && size == pTracker->rejectedSize) {
        pTracker->rejectionCount += 1;
        return NULL;
    }

    pAllocation = malloc(size);
    if (pAllocation != NULL) {
        pTracker->liveAllocationCount += 1;
    }

    return pAllocation;
}

static void* hardening_tracked_realloc(void* p, size_t size, void* pUserData)
{
    hardening_allocation_tracker* pTracker = (hardening_allocation_tracker*)pUserData;
    void* pAllocation;

    if (p == NULL) {
        return hardening_tracked_malloc(size, pUserData);
    }
    if (pTracker->rejectMatchingSize && size == pTracker->rejectedSize) {
        pTracker->rejectionCount += 1;
        return NULL;
    }
    if (size == 0) {
        free(p);
        pTracker->liveAllocationCount -= 1;
        return NULL;
    }

    pAllocation = realloc(p, size);
    return pAllocation;
}

static void hardening_tracked_free(void* p, void* pUserData)
{
    hardening_allocation_tracker* pTracker = (hardening_allocation_tracker*)pUserData;

    if (p != NULL) {
        free(p);
        pTracker->liveAllocationCount -= 1;
    }
}

static ma_result hardening_decoder_vfs_on_open(ma_vfs* pVFS, const char* pFilePath, ma_uint32 openMode, ma_vfs_file* pFile)
{
    hardening_decoder_vfs* pDecoderVFS = (hardening_decoder_vfs*)pVFS;

    (void)pFilePath;
    (void)openMode;

    if (pDecoderVFS->openResult != MA_SUCCESS) {
        return pDecoderVFS->openResult;
    }

    pDecoderVFS->openCount += 1;
    *pFile = (ma_vfs_file)pDecoderVFS;
    return MA_SUCCESS;
}

static ma_result hardening_decoder_vfs_on_close(ma_vfs* pVFS, ma_vfs_file file)
{
    hardening_decoder_vfs* pDecoderVFS = (hardening_decoder_vfs*)pVFS;

    (void)file;
    pDecoderVFS->closeCount += 1;
    return MA_SUCCESS;
}

static ma_result hardening_decoder_vfs_on_read(ma_vfs* pVFS, ma_vfs_file file, void* pDst, size_t sizeInBytes, size_t* pBytesRead)
{
    (void)pVFS;
    (void)file;
    (void)pDst;
    (void)sizeInBytes;

    if (pBytesRead != NULL) {
        *pBytesRead = 0;
    }
    return MA_SUCCESS;
}

static ma_result hardening_decoder_vfs_on_seek(ma_vfs* pVFS, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin)
{
    (void)pVFS;
    (void)file;
    (void)offset;
    (void)origin;
    return MA_SUCCESS;
}

static ma_result hardening_decoder_vfs_on_tell(ma_vfs* pVFS, ma_vfs_file file, ma_int64* pCursor)
{
    (void)pVFS;
    (void)file;
    *pCursor = 0;
    return MA_SUCCESS;
}

static ma_result hardening_decoder_backend_on_read(ma_data_source* pDataSource, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead)
{
    hardening_decoder_backend* pBackend = (hardening_decoder_backend*)pDataSource;

    (void)pFramesOut;
    (void)frameCount;

    pBackend->pState->readCount += 1;
    if (pFramesRead != NULL) {
        *pFramesRead = 0;
    }
    return pBackend->pState->readResult;
}

static ma_result hardening_decoder_backend_on_seek(ma_data_source* pDataSource, ma_uint64 frameIndex)
{
    hardening_decoder_backend* pBackend = (hardening_decoder_backend*)pDataSource;

    pBackend->cursor = frameIndex;
    return MA_SUCCESS;
}

static ma_result hardening_decoder_backend_on_get_data_format(ma_data_source* pDataSource, ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap)
{
    (void)pDataSource;

    if (pFormat != NULL) {
        *pFormat = ma_format_f32;
    }
    if (pChannels != NULL) {
        *pChannels = 1;
    }
    if (pSampleRate != NULL) {
        *pSampleRate = 48000;
    }
    if (pChannelMap != NULL && channelMapCap > 0) {
        pChannelMap[0] = MA_CHANNEL_MONO;
    }
    return MA_SUCCESS;
}

static ma_result hardening_decoder_backend_on_get_cursor(ma_data_source* pDataSource, ma_uint64* pCursor)
{
    hardening_decoder_backend* pBackend = (hardening_decoder_backend*)pDataSource;

    *pCursor = pBackend->cursor;
    return MA_SUCCESS;
}

static ma_result hardening_decoder_backend_on_get_length(ma_data_source* pDataSource, ma_uint64* pLength)
{
    hardening_decoder_backend* pBackend = (hardening_decoder_backend*)pDataSource;

    if (pBackend->pState->lengthResult != MA_SUCCESS) {
        return pBackend->pState->lengthResult;
    }

    *pLength = pBackend->pState->lengthInFrames;
    return MA_SUCCESS;
}

static ma_data_source_vtable g_hardening_decoder_backend_data_source_vtable =
{
    hardening_decoder_backend_on_read,
    hardening_decoder_backend_on_seek,
    hardening_decoder_backend_on_get_data_format,
    hardening_decoder_backend_on_get_cursor,
    hardening_decoder_backend_on_get_length,
    NULL,
    0
};

static ma_result hardening_decoder_backend_on_init(void* pUserData, ma_read_proc onRead, ma_seek_proc onSeek, ma_tell_proc onTell, void* pReadSeekTellUserData, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_data_source** ppBackend)
{
    hardening_decoder_backend_state* pState = (hardening_decoder_backend_state*)pUserData;
    hardening_decoder_backend* pBackend;
    ma_data_source_config dataSourceConfig;
    ma_result result;

    (void)onRead;
    (void)onSeek;
    (void)onTell;
    (void)pReadSeekTellUserData;
    (void)pConfig;

    pBackend = (hardening_decoder_backend*)ma_malloc(sizeof(*pBackend), pAllocationCallbacks);
    if (pBackend == NULL) {
        return MA_OUT_OF_MEMORY;
    }

    MA_ZERO_OBJECT(pBackend);
    dataSourceConfig = ma_data_source_config_init();
    dataSourceConfig.vtable = &g_hardening_decoder_backend_data_source_vtable;
    result = ma_data_source_init(&dataSourceConfig, &pBackend->ds);
    if (result != MA_SUCCESS) {
        ma_free(pBackend, pAllocationCallbacks);
        return result;
    }

    pBackend->pState = pState;
    pState->initCount += 1;
    *ppBackend = (ma_data_source*)pBackend;
    return MA_SUCCESS;
}

static void hardening_decoder_backend_on_uninit(void* pUserData, ma_data_source* pBackend, const ma_allocation_callbacks* pAllocationCallbacks)
{
    hardening_decoder_backend_state* pState = (hardening_decoder_backend_state*)pUserData;

    ma_data_source_uninit(pBackend);
    pState->uninitCount += 1;
    ma_free(pBackend, pAllocationCallbacks);
}

static ma_decoding_backend_vtable g_hardening_decoder_backend_vtable =
{
    hardening_decoder_backend_on_init,
    NULL,
    NULL,
    NULL,
    hardening_decoder_backend_on_uninit
};

static ma_result hardening_resource_manager_fixture_init(hardening_resource_manager_fixture* pFixture, ma_result lengthResult, ma_uint64 lengthInFrames, ma_result readResult, ma_uint32 jobQueueCapacity)
{
    ma_resource_manager_config config;
    ma_result result;

    MA_ZERO_OBJECT(pFixture);
    pFixture->vfs.callbacks.onOpen  = hardening_decoder_vfs_on_open;
    pFixture->vfs.callbacks.onClose = hardening_decoder_vfs_on_close;
    pFixture->vfs.callbacks.onRead  = hardening_decoder_vfs_on_read;
    pFixture->vfs.callbacks.onSeek  = hardening_decoder_vfs_on_seek;
    pFixture->vfs.callbacks.onTell  = hardening_decoder_vfs_on_tell;
    pFixture->vfs.openResult = MA_SUCCESS;
    pFixture->backendState.lengthResult = lengthResult;
    pFixture->backendState.lengthInFrames = lengthInFrames;
    pFixture->backendState.readResult = readResult;
    pFixture->pBackendVTable = &g_hardening_decoder_backend_vtable;

    config = ma_resource_manager_config_init();
    config.allocationCallbacks.pUserData = &pFixture->allocations;
    config.allocationCallbacks.onMalloc  = hardening_tracked_malloc;
    config.allocationCallbacks.onRealloc = hardening_tracked_realloc;
    config.allocationCallbacks.onFree    = hardening_tracked_free;
    config.jobThreadCount = 0;
    config.jobQueueCapacity = jobQueueCapacity;
    config.flags = MA_RESOURCE_MANAGER_FLAG_NON_BLOCKING;
    config.pVFS = (ma_vfs*)&pFixture->vfs;
    config.ppCustomDecodingBackendVTables = &pFixture->pBackendVTable;
    config.customDecodingBackendCount = 1;
    config.pCustomDecodingBackendUserData = &pFixture->backendState;

    result = ma_resource_manager_init(&config, &pFixture->resourceManager);
    if (result == MA_SUCCESS) {
        pFixture->baselineLiveAllocationCount = pFixture->allocations.liveAllocationCount;
    }
    return result;
}

static int hardening_resource_manager_fixture_uninit(hardening_resource_manager_fixture* pFixture, const char* pName)
{
    ma_resource_manager_uninit(&pFixture->resourceManager);
    if (pFixture->allocations.liveAllocationCount != 0) {
        printf("%s leaked %lu tracked allocations after resource manager uninit\n", pName, (unsigned long)pFixture->allocations.liveAllocationCount);
        return 1;
    }

    return 0;
}

typedef struct
{
    ma_vfs_callbacks callbacks;
    ma_event openEnteredEvent;
    ma_event continueOpenEvent;
} hardening_blocking_vfs;

typedef struct
{
    ma_resource_manager* pResourceManager;
    ma_result result;
    ma_resource_manager_data_buffer_node* pNode;
} hardening_acquire_thread_data;

static ma_result hardening_blocking_vfs_on_open(ma_vfs* pVFS, const char* pFilePath, ma_uint32 openMode, ma_vfs_file* pFile)
{
    hardening_blocking_vfs* pBlockingVFS = (hardening_blocking_vfs*)pVFS;

    (void)pFilePath;
    (void)openMode;
    (void)pFile;

    if (ma_event_signal(&pBlockingVFS->openEnteredEvent) != MA_SUCCESS) {
        return MA_ERROR;
    }
    if (ma_event_wait(&pBlockingVFS->continueOpenEvent) != MA_SUCCESS) {
        return MA_ERROR;
    }

    return MA_DOES_NOT_EXIST;
}

static ma_thread_result MA_THREADCALL hardening_acquire_thread_entry(void* pUserData)
{
    hardening_acquire_thread_data* pThreadData = (hardening_acquire_thread_data*)pUserData;

    pThreadData->result = ma_resource_manager_data_buffer_node_acquire(
        pThreadData->pResourceManager,
        "shared-failing-load.wav",
        NULL,
        0,
        0,
        NULL,
        NULL,
        NULL,
        &pThreadData->pNode);

    return (ma_thread_result)0;
}

static int resource_manager_test_failed_acquire(ma_uint32 flags, const char* pName)
{
    ma_resource_manager_config config = ma_resource_manager_config_init();
    ma_resource_manager resourceManager;
    ma_resource_manager_data_buffer_node* pNode = (ma_resource_manager_data_buffer_node*)(ma_uintptr)1;
    ma_result result;

    result = ma_resource_manager_init(&config, &resourceManager);
    if (result != MA_SUCCESS) {
        printf("%s resource manager init failed: %d\n", pName, result);
        return 1;
    }

    result = ma_resource_manager_data_buffer_node_acquire(
        &resourceManager,
        "miniaudio-hardening-file-does-not-exist.wav",
        NULL,
        0,
        flags,
        NULL,
        NULL,
        NULL,
        &pNode);

    if (result == MA_SUCCESS) {
        printf("%s unexpectedly loaded missing data\n", pName);
        if (pNode != NULL) {
            ma_resource_manager_data_buffer_node_unacquire(&resourceManager, pNode, NULL, NULL);
        }
        ma_resource_manager_uninit(&resourceManager);
        return 1;
    }
    if (pNode != NULL) {
        printf("%s published a node after failed initialization\n", pName);
        ma_resource_manager_uninit(&resourceManager);
        return 1;
    }
    if (resourceManager.pRootDataBufferNode != NULL) {
        printf("%s left a failed node in the resource manager\n", pName);
        ma_resource_manager_uninit(&resourceManager);
        return 1;
    }

    ma_resource_manager_uninit(&resourceManager);
    return 0;
}

static int hardening_check_failed_decoded_acquire(hardening_resource_manager_fixture* pFixture, const char* pName, ma_result expectedResult, ma_uint32 flags)
{
    ma_resource_manager_data_buffer_node* pNode;
    ma_result result;
    int failed;

    failed = 0;
    pNode = (ma_resource_manager_data_buffer_node*)(ma_uintptr)1;
    result = ma_resource_manager_data_buffer_node_acquire(
        &pFixture->resourceManager,
        pName,
        NULL,
        0,
        flags,
        NULL,
        NULL,
        NULL,
        &pNode);

    if (result != expectedResult) {
        printf("%s returned %d, expected %d\n", pName, result, expectedResult);
        failed = 1;
    }
    if (pNode != NULL) {
        printf("%s published a node after failed initialization\n", pName);
        failed = 1;
        ma_resource_manager_data_buffer_node_unacquire(&pFixture->resourceManager, pNode, NULL, NULL);
    }
    if (pFixture->resourceManager.pRootDataBufferNode != NULL) {
        printf("%s left a failed node in the resource manager\n", pName);
        failed = 1;
    }
    if (pFixture->allocations.liveAllocationCount != pFixture->baselineLiveAllocationCount) {
        printf("%s left %lu extra tracked allocations\n", pName, (unsigned long)(pFixture->allocations.liveAllocationCount - pFixture->baselineLiveAllocationCount));
        failed = 1;
    }

    return failed;
}

static int hardening_check_no_invalid_notification_use(const char* pName)
{
    if (g_hardeningResourceManagerFaultState.invalidNotificationSignalCount != 0 ||
        g_hardeningResourceManagerFaultState.invalidNotificationWaitCount != 0 ||
        g_hardeningResourceManagerFaultState.invalidNotificationUninitCount != 0) {
        printf("%s used a failed notification: signal=%u wait=%u uninit=%u\n",
            pName,
            g_hardeningResourceManagerFaultState.invalidNotificationSignalCount,
            g_hardeningResourceManagerFaultState.invalidNotificationWaitCount,
            g_hardeningResourceManagerFaultState.invalidNotificationUninitCount);
        return 1;
    }

    return 0;
}

static int resource_manager_test_node_notification_init_failure(void)
{
    hardening_resource_manager_fixture fixture;
    ma_result result;
    int failed;

    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 8);
    if (result != MA_SUCCESS) {
        printf("node notification failure fixture init failed: %d\n", result);
        return 1;
    }

    hardening_resource_manager_fault_reset();
    g_hardeningResourceManagerFaultState.failNotificationInitCall = 1;
    failed = hardening_check_failed_decoded_acquire(
        &fixture,
        "node-notification-failure.custom",
        MA_OUT_OF_MEMORY,
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE |
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC |
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_WAIT_INIT);

    if (g_hardeningResourceManagerFaultState.notificationInitCallCount != 1 ||
        g_hardeningResourceManagerFaultState.notificationInitSuccessCount != 0 ||
        g_hardeningResourceManagerFaultState.notificationWaitCallCount != 0 ||
        g_hardeningResourceManagerFaultState.notificationUninitCallCount != 0) {
        printf("node notification failure had init=%u success=%u wait=%u uninit=%u\n",
            g_hardeningResourceManagerFaultState.notificationInitCallCount,
            g_hardeningResourceManagerFaultState.notificationInitSuccessCount,
            g_hardeningResourceManagerFaultState.notificationWaitCallCount,
            g_hardeningResourceManagerFaultState.notificationUninitCallCount);
        failed = 1;
    }
    if (fixture.backendState.initCount != 0 || fixture.vfs.openCount != 0) {
        printf("node notification failure started decoder work\n");
        failed = 1;
    }
    failed += hardening_check_no_invalid_notification_use("node notification failure");
    failed += hardening_resource_manager_fixture_uninit(&fixture, "node notification failure");
    return failed;
}

static int resource_manager_test_node_fence_acquire_failure(void)
{
    hardening_resource_manager_fixture fixture;
    ma_resource_manager_data_buffer_node* pNode;
    ma_fence initFence;
    ma_fence doneFence;
    ma_result result;
    int failed;

    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 8);
    if (result != MA_SUCCESS) {
        printf("node fence failure fixture init failed: %d\n", result);
        return 1;
    }
    if (ma_fence_init(&initFence) != MA_SUCCESS || ma_fence_init(&doneFence) != MA_SUCCESS) {
        printf("node fence failure fence init failed\n");
        hardening_resource_manager_fixture_uninit(&fixture, "node fence failure");
        return 1;
    }

    hardening_resource_manager_fault_reset();
    g_hardeningResourceManagerFaultState.failFenceAcquireCall = 2;
    pNode = (ma_resource_manager_data_buffer_node*)(ma_uintptr)1;
    result = ma_resource_manager_data_buffer_node_acquire(
        &fixture.resourceManager,
        "node-fence-failure.custom",
        NULL,
        0,
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE |
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC |
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_WAIT_INIT,
        NULL,
        &initFence,
        &doneFence,
        &pNode);

    failed = 0;
    if (result != MA_OUT_OF_RANGE || pNode != NULL || fixture.resourceManager.pRootDataBufferNode != NULL) {
        printf("node fence failure returned %d with node=%p root=%p\n", result, (void*)pNode, (void*)fixture.resourceManager.pRootDataBufferNode);
        failed = 1;
    }
    if (initFence.counter != 0 || doneFence.counter != 0 ||
        g_hardeningResourceManagerFaultState.fenceAcquireCallCount != 2 ||
        g_hardeningResourceManagerFaultState.fenceAcquireSuccessCount != 1 ||
        g_hardeningResourceManagerFaultState.fenceReleaseCallCount != 1) {
        printf("node fence failure was initFence=%u doneFence=%u acquire=%u success=%u release=%u\n",
            initFence.counter,
            doneFence.counter,
            g_hardeningResourceManagerFaultState.fenceAcquireCallCount,
            g_hardeningResourceManagerFaultState.fenceAcquireSuccessCount,
            g_hardeningResourceManagerFaultState.fenceReleaseCallCount);
        failed = 1;
    }
    if (g_hardeningResourceManagerFaultState.notificationInitCallCount != 1 ||
        g_hardeningResourceManagerFaultState.notificationInitSuccessCount != 1 ||
        g_hardeningResourceManagerFaultState.notificationWaitCallCount != 0 ||
        g_hardeningResourceManagerFaultState.notificationUninitCallCount != 1) {
        printf("node fence failure notification counts were init=%u success=%u wait=%u uninit=%u\n",
            g_hardeningResourceManagerFaultState.notificationInitCallCount,
            g_hardeningResourceManagerFaultState.notificationInitSuccessCount,
            g_hardeningResourceManagerFaultState.notificationWaitCallCount,
            g_hardeningResourceManagerFaultState.notificationUninitCallCount);
        failed = 1;
    }
    if (fixture.allocations.liveAllocationCount != fixture.baselineLiveAllocationCount) {
        printf("node fence failure leaked tracked allocations\n");
        failed = 1;
    }
    failed += hardening_check_no_invalid_notification_use("node fence failure");

    ma_fence_uninit(&doneFence);
    ma_fence_uninit(&initFence);
    failed += hardening_resource_manager_fixture_uninit(&fixture, "node fence failure");
    return failed;
}

static int resource_manager_test_pipeline_fence_acquire_failure(void)
{
    hardening_resource_manager_fixture fixture;
    hardening_counting_notification initNotification;
    hardening_counting_notification doneNotification;
    ma_resource_manager_pipeline_notifications notifications;
    ma_resource_manager_data_buffer dataBuffer;
    ma_fence initFence;
    ma_fence doneFence;
    ma_result result;
    int failed;

    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 8);
    if (result != MA_SUCCESS) {
        printf("pipeline fence failure fixture init failed: %d\n", result);
        return 1;
    }
    if (ma_fence_init(&initFence) != MA_SUCCESS || ma_fence_init(&doneFence) != MA_SUCCESS) {
        printf("pipeline fence failure fence init failed\n");
        hardening_resource_manager_fixture_uninit(&fixture, "pipeline fence failure");
        return 1;
    }

    hardening_counting_notification_init(&initNotification);
    hardening_counting_notification_init(&doneNotification);
    notifications = ma_resource_manager_pipeline_notifications_init();
    notifications.init.pNotification = (ma_async_notification*)&initNotification;
    notifications.init.pFence = &initFence;
    notifications.done.pNotification = (ma_async_notification*)&doneNotification;
    notifications.done.pFence = &doneFence;

    hardening_resource_manager_fault_reset();
    g_hardeningResourceManagerFaultState.failFenceAcquireCall = 2;
    result = ma_resource_manager_data_buffer_init(
        &fixture.resourceManager,
        "pipeline-fence-failure.custom",
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE,
        &notifications,
        &dataBuffer);

    failed = 0;
    if (result != MA_OUT_OF_RANGE || dataBuffer.pResourceManager != NULL || fixture.resourceManager.pRootDataBufferNode != NULL) {
        printf("pipeline fence failure returned %d with initialized state\n", result);
        failed = 1;
    }
    if (initNotification.signalCount != 1 || doneNotification.signalCount != 1) {
        printf("pipeline fence failure signalled init=%u done=%u\n", initNotification.signalCount, doneNotification.signalCount);
        failed = 1;
    }
    if (initFence.counter != 0 || doneFence.counter != 0 ||
        g_hardeningResourceManagerFaultState.fenceAcquireCallCount != 2 ||
        g_hardeningResourceManagerFaultState.fenceReleaseCallCount != 1) {
        printf("pipeline fence failure left unbalanced fences\n");
        failed = 1;
    }
    if (fixture.allocations.liveAllocationCount != fixture.baselineLiveAllocationCount) {
        printf("pipeline fence failure leaked tracked allocations\n");
        failed = 1;
    }

    ma_fence_uninit(&doneFence);
    ma_fence_uninit(&initFence);
    failed += hardening_resource_manager_fixture_uninit(&fixture, "pipeline fence failure");
    return failed;
}

static int resource_manager_test_data_buffer_notification_init_failure(void)
{
    hardening_resource_manager_fixture fixture;
    ma_resource_manager_data_buffer_node* pBusyNode;
    ma_resource_manager_data_source_config config;
    ma_resource_manager_data_buffer dataBuffer;
    ma_uint32 hashedName32;
    ma_result result;
    int failed;

    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 8);
    if (result != MA_SUCCESS) {
        printf("data buffer notification failure fixture init failed: %d\n", result);
        return 1;
    }

    hashedName32 = ma_hash_string_32("busy-buffer-node");
    pBusyNode = (ma_resource_manager_data_buffer_node*)ma_malloc(sizeof(*pBusyNode), &fixture.resourceManager.config.allocationCallbacks);
    if (pBusyNode == NULL) {
        printf("data buffer notification failure node allocation failed\n");
        hardening_resource_manager_fixture_uninit(&fixture, "data buffer notification failure");
        return 1;
    }
    MA_ZERO_OBJECT(pBusyNode);
    pBusyNode->hashedName32 = hashedName32;
    pBusyNode->refCount = 1;
    pBusyNode->result = MA_BUSY;
    pBusyNode->isDataOwnedByResourceManager = MA_TRUE;
    pBusyNode->data.type = ma_resource_manager_data_supply_type_unknown;
    fixture.resourceManager.pRootDataBufferNode = pBusyNode;

    config = ma_resource_manager_data_source_config_init();
    config.flags = MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC | MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_WAIT_INIT;
    hardening_resource_manager_fault_reset();
    g_hardeningResourceManagerFaultState.failNotificationInitCall = 1;
    result = ma_resource_manager_data_buffer_init_ex_internal(&fixture.resourceManager, &config, hashedName32, &dataBuffer);

    failed = 0;
    if (result != MA_OUT_OF_MEMORY || dataBuffer.pResourceManager != NULL || dataBuffer.pNode != NULL) {
        printf("data buffer notification failure returned %d with initialized state\n", result);
        failed = 1;
    }
    if (fixture.resourceManager.pRootDataBufferNode != pBusyNode || pBusyNode->refCount != 1) {
        printf("data buffer notification failure damaged the existing node\n");
        failed = 1;
    }
    if (g_hardeningResourceManagerFaultState.notificationInitCallCount != 1 ||
        g_hardeningResourceManagerFaultState.notificationWaitCallCount != 0 ||
        g_hardeningResourceManagerFaultState.notificationUninitCallCount != 0) {
        printf("data buffer notification failure had init=%u wait=%u uninit=%u\n",
            g_hardeningResourceManagerFaultState.notificationInitCallCount,
            g_hardeningResourceManagerFaultState.notificationWaitCallCount,
            g_hardeningResourceManagerFaultState.notificationUninitCallCount);
        failed = 1;
    }
    failed += hardening_check_no_invalid_notification_use("data buffer notification failure");

    ma_atomic_exchange_i32(&pBusyNode->result, MA_ERROR);
    ma_resource_manager_data_buffer_node_unacquire(&fixture.resourceManager, pBusyNode, NULL, NULL);
    if (fixture.allocations.liveAllocationCount != fixture.baselineLiveAllocationCount) {
        printf("data buffer notification failure leaked tracked allocations\n");
        failed = 1;
    }
    failed += hardening_resource_manager_fixture_uninit(&fixture, "data buffer notification failure");
    return failed;
}

static int resource_manager_test_data_stream_notification_init_failure(void)
{
    hardening_resource_manager_fixture fixture;
    hardening_counting_notification initNotification;
    hardening_counting_notification doneNotification;
    ma_resource_manager_pipeline_notifications notifications;
    ma_resource_manager_data_stream dataStream;
    ma_result result;
    int failed;

    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 8);
    if (result != MA_SUCCESS) {
        printf("data stream notification failure fixture init failed: %d\n", result);
        return 1;
    }

    hardening_counting_notification_init(&initNotification);
    hardening_counting_notification_init(&doneNotification);
    notifications = ma_resource_manager_pipeline_notifications_init();
    notifications.init.pNotification = (ma_async_notification*)&initNotification;
    notifications.done.pNotification = (ma_async_notification*)&doneNotification;

    hardening_resource_manager_fault_reset();
    g_hardeningResourceManagerFaultState.failNotificationInitCall = 1;
    result = ma_resource_manager_data_stream_init(&fixture.resourceManager, "stream-notification-failure.custom", 0, &notifications, &dataStream);

    failed = 0;
    if (result != MA_OUT_OF_MEMORY || dataStream.pResourceManager != NULL || dataStream.ds.vtable != NULL) {
        printf("data stream notification failure returned %d with initialized state\n", result);
        failed = 1;
    }
    if (initNotification.signalCount != 1 || doneNotification.signalCount != 1) {
        printf("data stream notification failure signalled init=%u done=%u\n", initNotification.signalCount, doneNotification.signalCount);
        failed = 1;
    }
    if (g_hardeningResourceManagerFaultState.notificationInitCallCount != 1 ||
        g_hardeningResourceManagerFaultState.notificationWaitCallCount != 0 ||
        g_hardeningResourceManagerFaultState.notificationUninitCallCount != 0) {
        printf("data stream notification failure had init=%u wait=%u uninit=%u\n",
            g_hardeningResourceManagerFaultState.notificationInitCallCount,
            g_hardeningResourceManagerFaultState.notificationWaitCallCount,
            g_hardeningResourceManagerFaultState.notificationUninitCallCount);
        failed = 1;
    }
    if (fixture.allocations.liveAllocationCount != fixture.baselineLiveAllocationCount) {
        printf("data stream notification failure leaked tracked allocations\n");
        failed = 1;
    }
    failed += hardening_check_no_invalid_notification_use("data stream notification failure");
    failed += hardening_resource_manager_fixture_uninit(&fixture, "data stream notification failure");
    return failed;
}

static int resource_manager_test_data_buffer_uninit_notification_failure(void)
{
    hardening_resource_manager_fixture fixture;
    ma_resource_manager_data_buffer dataBuffer;
    float externalFrames[2];
    ma_result result;
    int failed;

    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 8);
    if (result != MA_SUCCESS) {
        printf("data buffer uninit notification fixture init failed: %d\n", result);
        return 1;
    }
    externalFrames[0] = 0;
    externalFrames[1] = 0;
    result = ma_resource_manager_register_decoded_data(&fixture.resourceManager, "buffer-uninit-notification", externalFrames, 2, ma_format_f32, 1, 48000);
    if (result != MA_SUCCESS) {
        printf("data buffer uninit notification registration failed: %d\n", result);
        hardening_resource_manager_fixture_uninit(&fixture, "data buffer uninit notification");
        return 1;
    }
    result = ma_resource_manager_data_buffer_init(&fixture.resourceManager, "buffer-uninit-notification", 0, NULL, &dataBuffer);
    if (result != MA_SUCCESS) {
        printf("data buffer uninit notification buffer init failed: %d\n", result);
        ma_resource_manager_unregister_data(&fixture.resourceManager, "buffer-uninit-notification");
        hardening_resource_manager_fixture_uninit(&fixture, "data buffer uninit notification");
        return 1;
    }

    ma_atomic_exchange_i32(&dataBuffer.result, MA_BUSY);
    hardening_resource_manager_fault_reset();
    g_hardeningResourceManagerFaultState.failNotificationInitCall = 1;
    result = ma_resource_manager_data_buffer_uninit(&dataBuffer);

    failed = 0;
    if (result != MA_OUT_OF_MEMORY || ma_resource_manager_data_buffer_result(&dataBuffer) != MA_BUSY || dataBuffer.executionCounter != 0) {
        printf("data buffer uninit notification failure changed live state: result=%d state=%d order=%u\n", result, ma_resource_manager_data_buffer_result(&dataBuffer), dataBuffer.executionCounter);
        failed = 1;
    }
    if (g_hardeningResourceManagerFaultState.notificationInitCallCount != 1 ||
        g_hardeningResourceManagerFaultState.notificationWaitCallCount != 0 ||
        g_hardeningResourceManagerFaultState.notificationUninitCallCount != 0) {
        printf("data buffer uninit notification failure had init=%u wait=%u uninit=%u\n",
            g_hardeningResourceManagerFaultState.notificationInitCallCount,
            g_hardeningResourceManagerFaultState.notificationWaitCallCount,
            g_hardeningResourceManagerFaultState.notificationUninitCallCount);
        failed = 1;
    }
    failed += hardening_check_no_invalid_notification_use("data buffer uninit notification failure");

    ma_atomic_exchange_i32(&dataBuffer.result, MA_SUCCESS);
    if (ma_resource_manager_data_buffer_uninit(&dataBuffer) != MA_SUCCESS) {
        printf("data buffer uninit notification cleanup failed\n");
        failed = 1;
    }
    if (ma_resource_manager_unregister_data(&fixture.resourceManager, "buffer-uninit-notification") != MA_SUCCESS) {
        printf("data buffer uninit notification unregister failed\n");
        failed = 1;
    }
    if (fixture.allocations.liveAllocationCount != fixture.baselineLiveAllocationCount) {
        printf("data buffer uninit notification failure leaked tracked allocations\n");
        failed = 1;
    }
    failed += hardening_resource_manager_fixture_uninit(&fixture, "data buffer uninit notification");
    return failed;
}

static int resource_manager_test_data_stream_uninit_notification_failure(void)
{
    hardening_resource_manager_fixture fixture;
    ma_resource_manager_data_stream dataStream;
    ma_data_source_config dataSourceConfig;
    ma_result result;
    int failed;

    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 8);
    if (result != MA_SUCCESS) {
        printf("data stream uninit notification fixture init failed: %d\n", result);
        return 1;
    }

    MA_ZERO_OBJECT(&dataStream);
    dataSourceConfig = ma_data_source_config_init();
    dataSourceConfig.vtable = &g_ma_resource_manager_data_stream_vtable;
    result = ma_data_source_init(&dataSourceConfig, &dataStream.ds);
    if (result != MA_SUCCESS) {
        printf("data stream uninit notification data source init failed: %d\n", result);
        hardening_resource_manager_fixture_uninit(&fixture, "data stream uninit notification");
        return 1;
    }
    dataStream.pResourceManager = &fixture.resourceManager;
    dataStream.result = MA_SUCCESS;

    hardening_resource_manager_fault_reset();
    g_hardeningResourceManagerFaultState.failNotificationInitCall = 1;
    result = ma_resource_manager_data_stream_uninit(&dataStream);

    failed = 0;
    if (result != MA_OUT_OF_MEMORY || ma_resource_manager_data_stream_result(&dataStream) != MA_SUCCESS || dataStream.executionCounter != 0 || dataStream.ds.vtable == NULL) {
        printf("data stream uninit notification failure changed live state\n");
        failed = 1;
    }
    if (g_hardeningResourceManagerFaultState.notificationInitCallCount != 1 ||
        g_hardeningResourceManagerFaultState.notificationWaitCallCount != 0 ||
        g_hardeningResourceManagerFaultState.notificationUninitCallCount != 0) {
        printf("data stream uninit notification failure had init=%u wait=%u uninit=%u\n",
            g_hardeningResourceManagerFaultState.notificationInitCallCount,
            g_hardeningResourceManagerFaultState.notificationWaitCallCount,
            g_hardeningResourceManagerFaultState.notificationUninitCallCount);
        failed = 1;
    }
    failed += hardening_check_no_invalid_notification_use("data stream uninit notification failure");

    ma_data_source_uninit(&dataStream.ds);
    failed += hardening_resource_manager_fixture_uninit(&fixture, "data stream uninit notification");
    return failed;
}

static int resource_manager_test_data_stream_uninit_post_failure(void)
{
    hardening_resource_manager_fixture fixture;
    ma_resource_manager_data_stream dataStream;
    ma_data_source_config dataSourceConfig;
    ma_job fillerJob;
    ma_result result;
    int failed;

    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 2);
    if (result != MA_SUCCESS) {
        printf("data stream uninit post failure fixture init failed: %d\n", result);
        return 1;
    }
    fillerJob = ma_job_init(MA_JOB_TYPE_CUSTOM);
    result = ma_resource_manager_post_job(&fixture.resourceManager, &fillerJob);
    if (result != MA_SUCCESS) {
        printf("data stream uninit post failure could not fill queue: %d\n", result);
        hardening_resource_manager_fixture_uninit(&fixture, "data stream uninit post failure");
        return 1;
    }

    MA_ZERO_OBJECT(&dataStream);
    dataSourceConfig = ma_data_source_config_init();
    dataSourceConfig.vtable = &g_ma_resource_manager_data_stream_vtable;
    result = ma_data_source_init(&dataSourceConfig, &dataStream.ds);
    if (result != MA_SUCCESS) {
        printf("data stream uninit post failure data source init failed: %d\n", result);
        hardening_resource_manager_fixture_uninit(&fixture, "data stream uninit post failure");
        return 1;
    }
    dataStream.pResourceManager = &fixture.resourceManager;
    dataStream.result = MA_SUCCESS;

    hardening_resource_manager_fault_reset();
    result = ma_resource_manager_data_stream_uninit(&dataStream);

    failed = 0;
    if (result != MA_OUT_OF_MEMORY || ma_resource_manager_data_stream_result(&dataStream) != MA_SUCCESS || dataStream.executionCounter != 0 || dataStream.executionPointer != 0) {
        printf("data stream uninit post failure was result=%d state=%d counter=%u pointer=%u\n",
            result,
            ma_resource_manager_data_stream_result(&dataStream),
            dataStream.executionCounter,
            dataStream.executionPointer);
        failed = 1;
    }
    if (g_hardeningResourceManagerFaultState.notificationInitCallCount != 1 ||
        g_hardeningResourceManagerFaultState.notificationInitSuccessCount != 1 ||
        g_hardeningResourceManagerFaultState.notificationWaitCallCount != 0 ||
        g_hardeningResourceManagerFaultState.notificationUninitCallCount != 1) {
        printf("data stream uninit post failure had init=%u success=%u wait=%u uninit=%u\n",
            g_hardeningResourceManagerFaultState.notificationInitCallCount,
            g_hardeningResourceManagerFaultState.notificationInitSuccessCount,
            g_hardeningResourceManagerFaultState.notificationWaitCallCount,
            g_hardeningResourceManagerFaultState.notificationUninitCallCount);
        failed = 1;
    }
    failed += hardening_check_no_invalid_notification_use("data stream uninit post failure");

    ma_data_source_uninit(&dataStream.ds);
    failed += hardening_resource_manager_fixture_uninit(&fixture, "data stream uninit post failure");
    return failed;
}

static int resource_manager_test_data_stream_load_failure(ma_result lengthResult, ma_bool32 rejectPageAllocation, ma_result expectedResult, const char* pName)
{
    hardening_resource_manager_fixture fixture;
    ma_resource_manager_data_stream dataStream;
    ma_data_source_config dataSourceConfig;
    ma_job job;
    char* pFilePathCopy;
    ma_result result;
    int failed;

    result = hardening_resource_manager_fixture_init(&fixture, lengthResult, 13, MA_SUCCESS, 8);
    if (result != MA_SUCCESS) {
        printf("%s fixture init failed: %d\n", pName, result);
        return 1;
    }

    MA_ZERO_OBJECT(&dataStream);
    dataSourceConfig = ma_data_source_config_init();
    dataSourceConfig.vtable = &g_ma_resource_manager_data_stream_vtable;
    result = ma_data_source_init(&dataSourceConfig, &dataStream.ds);
    if (result != MA_SUCCESS) {
        printf("%s data source init failed: %d\n", pName, result);
        hardening_resource_manager_fixture_uninit(&fixture, pName);
        return 1;
    }
    dataStream.pResourceManager = &fixture.resourceManager;
    dataStream.result = MA_BUSY;

    pFilePathCopy = ma_copy_string("stream-load-failure.custom", &fixture.resourceManager.config.allocationCallbacks);
    if (pFilePathCopy == NULL) {
        printf("%s path allocation failed\n", pName);
        ma_data_source_uninit(&dataStream.ds);
        hardening_resource_manager_fixture_uninit(&fixture, pName);
        return 1;
    }

    if (rejectPageAllocation) {
        fixture.allocations.rejectedSize = 48000 * 2 * sizeof(float);
        fixture.allocations.rejectMatchingSize = MA_TRUE;
    }

    job = ma_job_init(MA_JOB_TYPE_RESOURCE_MANAGER_LOAD_DATA_STREAM);
    job.order = 0;
    job.data.resourceManager.loadDataStream.pDataStream = &dataStream;
    job.data.resourceManager.loadDataStream.pFilePath = pFilePathCopy;
    job.data.resourceManager.loadDataStream.pFilePathW = NULL;
    job.data.resourceManager.loadDataStream.initialSeekPoint = 0;
    job.data.resourceManager.loadDataStream.pInitNotification = NULL;
    job.data.resourceManager.loadDataStream.pInitFence = NULL;
    result = ma_job_process(&job);
    fixture.allocations.rejectMatchingSize = MA_FALSE;

    failed = 0;
    if (result != expectedResult || ma_resource_manager_data_stream_result(&dataStream) != expectedResult) {
        printf("%s returned result=%d state=%d, expected %d\n", pName, result, ma_resource_manager_data_stream_result(&dataStream), expectedResult);
        failed = 1;
    }
    if (dataStream.isDecoderInitialized || dataStream.pPageData != NULL) {
        printf("%s retained decoder or page ownership\n", pName);
        failed = 1;
    }
    if (fixture.backendState.initCount != 1 || fixture.backendState.uninitCount != 1 ||
        fixture.vfs.openCount != 1 || fixture.vfs.closeCount != 1) {
        printf("%s lifetime was decoder=%u/%u VFS=%u/%u\n",
            pName,
            fixture.backendState.initCount,
            fixture.backendState.uninitCount,
            fixture.vfs.openCount,
            fixture.vfs.closeCount);
        failed = 1;
    }
    if (rejectPageAllocation && fixture.allocations.rejectionCount != 1) {
        printf("%s rejected %lu allocations, expected 1\n", pName, (unsigned long)fixture.allocations.rejectionCount);
        failed = 1;
    }
    if (fixture.allocations.liveAllocationCount != fixture.baselineLiveAllocationCount) {
        printf("%s leaked tracked allocations\n", pName);
        failed = 1;
    }

    ma_data_source_uninit(&dataStream.ds);
    failed += hardening_resource_manager_fixture_uninit(&fixture, pName);
    return failed;
}

static int resource_manager_test_data_stream_notification_after_retirement(void)
{
    hardening_resource_manager_fixture fixture;
    ma_resource_manager_config config;
    ma_resource_manager_data_stream dataStream;
    ma_result result;
    int failed;

    MA_ZERO_OBJECT(&fixture);
    fixture.vfs.callbacks.onOpen  = hardening_decoder_vfs_on_open;
    fixture.vfs.callbacks.onClose = hardening_decoder_vfs_on_close;
    fixture.vfs.callbacks.onRead  = hardening_decoder_vfs_on_read;
    fixture.vfs.callbacks.onSeek  = hardening_decoder_vfs_on_seek;
    fixture.vfs.callbacks.onTell  = hardening_decoder_vfs_on_tell;
    fixture.vfs.openResult = MA_SUCCESS;
    fixture.backendState.lengthResult = MA_NOT_IMPLEMENTED;
    fixture.backendState.lengthInFrames = 13;
    fixture.backendState.readResult = MA_SUCCESS;
    fixture.pBackendVTable = &g_hardening_decoder_backend_vtable;

    config = ma_resource_manager_config_init();
    config.pVFS = (ma_vfs*)&fixture.vfs;
    config.ppCustomDecodingBackendVTables = &fixture.pBackendVTable;
    config.customDecodingBackendCount = 1;
    config.pCustomDecodingBackendUserData = &fixture.backendState;
    result = ma_resource_manager_init(&config, &fixture.resourceManager);
    if (result != MA_SUCCESS) {
        printf("data stream retirement fixture init failed: %d\n", result);
        return 1;
    }
    hardening_resource_manager_fault_reset();
    g_hardeningResourceManagerFaultState.pRetirementDataStream = &dataStream;
    result = ma_resource_manager_data_stream_init(&fixture.resourceManager, "stream-retirement.custom", 0, NULL, &dataStream);
    g_hardeningResourceManagerFaultState.pRetirementDataStream = NULL;

    failed = 0;
    if (result != MA_NOT_IMPLEMENTED || g_hardeningResourceManagerFaultState.notificationBeforeRetirementCount != 0) {
        printf("data stream retirement was result=%d early-notifications=%u\n",
            result,
            g_hardeningResourceManagerFaultState.notificationBeforeRetirementCount);
        failed = 1;
    }
    if (fixture.backendState.initCount != 1 || fixture.backendState.uninitCount != 1 || fixture.vfs.openCount != 1 || fixture.vfs.closeCount != 1) {
        printf("data stream retirement lifetime was decoder=%u/%u VFS=%u/%u\n",
            fixture.backendState.initCount,
            fixture.backendState.uninitCount,
            fixture.vfs.openCount,
            fixture.vfs.closeCount);
        failed = 1;
    }
    failed += hardening_resource_manager_fixture_uninit(&fixture, "data stream retirement");
    return failed;
}

static int resource_manager_test_decoder_length_failure_cleanup(void)
{
    hardening_resource_manager_fixture fixture;
    ma_result result;
    int failed;

    result = hardening_resource_manager_fixture_init(&fixture, MA_NOT_IMPLEMENTED, 0, MA_SUCCESS, 8);
    if (result != MA_SUCCESS) {
        printf("length failure fixture init failed: %d\n", result);
        return 1;
    }

    failed = hardening_check_failed_decoded_acquire(&fixture, "length-failure.custom", MA_NOT_IMPLEMENTED, MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE);
    if (fixture.backendState.initCount != 1 || fixture.backendState.uninitCount != 1) {
        printf("length failure decoder lifetime was init=%u uninit=%u\n", fixture.backendState.initCount, fixture.backendState.uninitCount);
        failed = 1;
    }
    if (fixture.vfs.openCount != 1 || fixture.vfs.closeCount != 1) {
        printf("length failure VFS lifetime was open=%u close=%u\n", fixture.vfs.openCount, fixture.vfs.closeCount);
        failed = 1;
    }

    failed += hardening_resource_manager_fixture_uninit(&fixture, "length failure");
    return failed;
}

static int resource_manager_test_mid_decode_failure_cleanup(ma_bool32 unknownLength, const char* pName)
{
    hardening_resource_manager_fixture fixture;
    ma_uint32 flags;
    ma_result result;
    int failed;

    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_IO_ERROR, 8);
    if (result != MA_SUCCESS) {
        printf("%s fixture init failed: %d\n", pName, result);
        return 1;
    }

    flags = MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE;
    if (unknownLength) {
        flags |= MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_UNKNOWN_LENGTH;
    }
    failed = hardening_check_failed_decoded_acquire(&fixture, pName, MA_IO_ERROR, flags);
    if (fixture.backendState.initCount != 1 || fixture.backendState.uninitCount != 1 || fixture.backendState.readCount != 1) {
        printf("%s decoder counts were init=%u uninit=%u read=%u\n", pName, fixture.backendState.initCount, fixture.backendState.uninitCount, fixture.backendState.readCount);
        failed = 1;
    }
    if (fixture.vfs.openCount != 1 || fixture.vfs.closeCount != 1) {
        printf("%s VFS lifetime was open=%u close=%u\n", pName, fixture.vfs.openCount, fixture.vfs.closeCount);
        failed = 1;
    }

    failed += hardening_resource_manager_fixture_uninit(&fixture, pName);
    return failed;
}

static int resource_manager_test_decoded_allocation_failure_cleanup(void)
{
    hardening_resource_manager_fixture fixture;
    ma_result result;
    int failed;

    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 8);
    if (result != MA_SUCCESS) {
        printf("decoded allocation failure fixture init failed: %d\n", result);
        return 1;
    }

    fixture.allocations.rejectedSize = 13 * sizeof(float);
    fixture.allocations.rejectMatchingSize = MA_TRUE;
    failed = hardening_check_failed_decoded_acquire(&fixture, "decoded-allocation-failure.custom", MA_OUT_OF_MEMORY, MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE);
    fixture.allocations.rejectMatchingSize = MA_FALSE;

    if (fixture.allocations.rejectionCount != 1) {
        printf("decoded allocation failure rejected %lu allocations, expected 1\n", (unsigned long)fixture.allocations.rejectionCount);
        failed = 1;
    }
    if (fixture.backendState.initCount != 1 || fixture.backendState.uninitCount != 1) {
        printf("decoded allocation failure decoder lifetime was init=%u uninit=%u\n", fixture.backendState.initCount, fixture.backendState.uninitCount);
        failed = 1;
    }

    failed += hardening_resource_manager_fixture_uninit(&fixture, "decoded allocation failure");
    return failed;
}

static int resource_manager_test_page_post_failure_cleanup(void)
{
    hardening_resource_manager_fixture fixture;
    ma_job fillerJob;
    ma_result result;
    int failed;

    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 2);
    if (result != MA_SUCCESS) {
        printf("page-post failure fixture init failed: %d\n", result);
        return 1;
    }

    fillerJob = ma_job_init(MA_JOB_TYPE_CUSTOM);
    result = ma_resource_manager_post_job(&fixture.resourceManager, &fillerJob);
    if (result != MA_SUCCESS) {
        printf("page-post failure could not fill the job queue: %d\n", result);
        hardening_resource_manager_fixture_uninit(&fixture, "page-post failure");
        return 1;
    }

    failed = hardening_check_failed_decoded_acquire(
        &fixture,
        "page-post-failure.custom",
        MA_OUT_OF_MEMORY,
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE |
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC |
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_WAIT_INIT);
    if (fixture.backendState.initCount != 1 || fixture.backendState.uninitCount != 1) {
        printf("page-post failure decoder lifetime was init=%u uninit=%u\n", fixture.backendState.initCount, fixture.backendState.uninitCount);
        failed = 1;
    }
    if (fixture.vfs.openCount != 1 || fixture.vfs.closeCount != 1) {
        printf("page-post failure VFS lifetime was open=%u close=%u\n", fixture.vfs.openCount, fixture.vfs.closeCount);
        failed = 1;
    }

    failed += hardening_resource_manager_fixture_uninit(&fixture, "page-post failure");
    return failed;
}

static int resource_manager_test_custom_vfs_failure_cleanup(void)
{
    hardening_resource_manager_fixture fixture;
    ma_result result;
    int failed;

    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 8);
    if (result != MA_SUCCESS) {
        printf("custom VFS failure fixture init failed: %d\n", result);
        return 1;
    }

    fixture.vfs.openResult = MA_ACCESS_DENIED;
    failed = hardening_check_failed_decoded_acquire(&fixture, "custom-vfs-failure.custom", MA_ACCESS_DENIED, MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE);
    if (fixture.backendState.initCount != 0 || fixture.backendState.uninitCount != 0) {
        printf("custom VFS failure unexpectedly initialized the decoder backend\n");
        failed = 1;
    }
    if (fixture.vfs.openCount != 0 || fixture.vfs.closeCount != 0) {
        printf("custom VFS failure lifetime was open=%u close=%u\n", fixture.vfs.openCount, fixture.vfs.closeCount);
        failed = 1;
    }

    failed += hardening_resource_manager_fixture_uninit(&fixture, "custom VFS failure");
    return failed;
}

static int resource_manager_test_external_decoded_storage_ownership(void)
{
    hardening_resource_manager_fixture fixture;
    float externalFrames[2];
    ma_result result;
    int failed;

    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 8);
    if (result != MA_SUCCESS) {
        printf("external decoded storage fixture init failed: %d\n", result);
        return 1;
    }

    failed = 0;
    externalFrames[0] = 0;
    externalFrames[1] = 0;
    result = ma_resource_manager_register_decoded_data(&fixture.resourceManager, "external-decoded", externalFrames, 2, ma_format_f32, 1, 48000);
    if (result != MA_SUCCESS) {
        printf("external decoded storage registration failed: %d\n", result);
        failed = 1;
    } else {
        result = ma_resource_manager_unregister_data(&fixture.resourceManager, "external-decoded");
        if (result != MA_SUCCESS) {
            printf("external decoded storage unregistration failed: %d\n", result);
            failed = 1;
        }
    }
    if (fixture.allocations.liveAllocationCount != fixture.baselineLiveAllocationCount) {
        printf("external decoded storage changed tracked ownership\n");
        failed = 1;
    }

    failed += hardening_resource_manager_fixture_uninit(&fixture, "external decoded storage");
    return failed;
}

static int resource_manager_test_shared_synchronous_failure(void)
{
    hardening_blocking_vfs blockingVFS;
    hardening_acquire_thread_data threadData;
    ma_resource_manager_config config;
    ma_resource_manager resourceManager;
    ma_resource_manager_data_buffer_node* pSharedNode;
    ma_thread acquireThread;
    ma_result result;
    int failed;

    MA_ZERO_OBJECT(&blockingVFS);
    MA_ZERO_OBJECT(&threadData);
    failed = 0;

    blockingVFS.callbacks.onOpen = hardening_blocking_vfs_on_open;
    result = ma_event_init(&blockingVFS.openEnteredEvent);
    if (result != MA_SUCCESS) {
        printf("shared failure open-entered event init failed: %d\n", result);
        return 1;
    }
    result = ma_event_init(&blockingVFS.continueOpenEvent);
    if (result != MA_SUCCESS) {
        printf("shared failure continue-open event init failed: %d\n", result);
        ma_event_uninit(&blockingVFS.openEnteredEvent);
        return 1;
    }

    config = ma_resource_manager_config_init();
    config.pVFS = (ma_vfs*)&blockingVFS;
    result = ma_resource_manager_init(&config, &resourceManager);
    if (result != MA_SUCCESS) {
        printf("shared failure resource manager init failed: %d\n", result);
        ma_event_uninit(&blockingVFS.continueOpenEvent);
        ma_event_uninit(&blockingVFS.openEnteredEvent);
        return 1;
    }

    threadData.pResourceManager = &resourceManager;
    threadData.result = MA_ERROR;
    threadData.pNode = (ma_resource_manager_data_buffer_node*)(ma_uintptr)1;
    result = ma_thread_create(&acquireThread, ma_thread_priority_normal, 0, hardening_acquire_thread_entry, &threadData, NULL);
    if (result != MA_SUCCESS) {
        printf("shared failure acquire thread creation failed: %d\n", result);
        ma_resource_manager_uninit(&resourceManager);
        ma_event_uninit(&blockingVFS.continueOpenEvent);
        ma_event_uninit(&blockingVFS.openEnteredEvent);
        return 1;
    }

    result = ma_event_wait(&blockingVFS.openEnteredEvent);
    if (result != MA_SUCCESS) {
        printf("shared failure open-entered wait failed: %d\n", result);
        failed = 1;
    }

    pSharedNode = NULL;
    if (failed == 0) {
        result = ma_resource_manager_data_buffer_node_acquire(
            &resourceManager,
            "shared-failing-load.wav",
            NULL,
            0,
            0,
            NULL,
            NULL,
            NULL,
            &pSharedNode);
        if (result != MA_SUCCESS || pSharedNode == NULL) {
            printf("shared failure second acquire failed: %d\n", result);
            failed = 1;
        } else if (ma_resource_manager_data_buffer_node_result(pSharedNode) != MA_BUSY) {
            printf("shared failure second acquire did not observe a busy node\n");
            failed = 1;
        }
    }

    result = ma_event_signal(&blockingVFS.continueOpenEvent);
    if (result != MA_SUCCESS) {
        printf("shared failure continue-open signal failed: %d\n", result);
        failed = 1;
    }
    ma_thread_wait(&acquireThread);

    if (threadData.result != MA_DOES_NOT_EXIST) {
        printf("shared failure creator returned unexpected result: %d\n", threadData.result);
        failed = 1;
    }
    if (threadData.pNode != NULL) {
        printf("shared failure creator published a node after failure\n");
        failed = 1;
    }

    if (pSharedNode != NULL) {
        if (resourceManager.pRootDataBufferNode != pSharedNode) {
            printf("shared failure removed a node still owned by another caller\n");
            failed = 1;
        } else {
            if (ma_resource_manager_data_buffer_node_result(pSharedNode) != MA_DOES_NOT_EXIST) {
                printf("shared failure did not publish the load error\n");
                failed = 1;
            }
            if (pSharedNode->refCount != 1) {
                printf("shared failure left an unexpected reference count: %u\n", pSharedNode->refCount);
                failed = 1;
            }
        }

        result = ma_resource_manager_data_buffer_node_unacquire(&resourceManager, pSharedNode, NULL, NULL);
        if (result != MA_SUCCESS) {
            printf("shared failure final unacquire failed: %d\n", result);
            failed = 1;
        } else if (resourceManager.pRootDataBufferNode != NULL) {
            printf("shared failure final unacquire left a stale node\n");
            failed = 1;
        }
    }

    ma_resource_manager_uninit(&resourceManager);
    ma_event_uninit(&blockingVFS.continueOpenEvent);
    ma_event_uninit(&blockingVFS.openEnteredEvent);

    return failed;
}

int main(void)
{
    int failed = 0;

    failed += resource_manager_test_failed_acquire(MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE, "synchronous acquire");
    failed += resource_manager_test_failed_acquire(
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE |
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC |
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_WAIT_INIT,
        "wait-init acquire");
    failed += resource_manager_test_node_notification_init_failure();
    failed += resource_manager_test_node_fence_acquire_failure();
    failed += resource_manager_test_pipeline_fence_acquire_failure();
    failed += resource_manager_test_data_buffer_notification_init_failure();
    failed += resource_manager_test_data_stream_notification_init_failure();
    failed += resource_manager_test_data_buffer_uninit_notification_failure();
    failed += resource_manager_test_data_stream_uninit_notification_failure();
    failed += resource_manager_test_data_stream_uninit_post_failure();
    failed += resource_manager_test_data_stream_load_failure(MA_NOT_IMPLEMENTED, MA_FALSE, MA_NOT_IMPLEMENTED, "data stream length failure");
    failed += resource_manager_test_data_stream_load_failure(MA_SUCCESS, MA_TRUE, MA_OUT_OF_MEMORY, "data stream page allocation failure");
    failed += resource_manager_test_data_stream_notification_after_retirement();
    failed += resource_manager_test_decoder_length_failure_cleanup();
    failed += resource_manager_test_mid_decode_failure_cleanup(MA_FALSE, "flat-mid-decode-failure.custom");
    failed += resource_manager_test_mid_decode_failure_cleanup(MA_TRUE, "paged-mid-decode-failure.custom");
    failed += resource_manager_test_decoded_allocation_failure_cleanup();
    failed += resource_manager_test_page_post_failure_cleanup();
    failed += resource_manager_test_custom_vfs_failure_cleanup();
    failed += resource_manager_test_external_decoded_storage_ownership();
    failed += resource_manager_test_shared_synchronous_failure();

    return failed != 0;
}
