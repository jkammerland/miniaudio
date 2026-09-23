#define MA_NO_DEVICE_IO
#include "../../miniaudio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void hardening_data_buffer_result_published_hook(void* pDataBuffer);
static void hardening_data_buffer_node_result_published_hook(void* pDataBufferNode);
static void hardening_data_buffer_async_uninit_hook(void* pDataBuffer);
static void hardening_data_buffer_node_async_free_hook(void* pDataBufferNode);
static void hardening_data_buffer_async_uninit_posted_hook(void* pDataBuffer);
static void hardening_data_buffer_node_async_free_posted_hook(void* pDataBufferNode);
static void hardening_data_stream_async_uninit_posted_hook(void* pDataStream);
static void hardening_resource_manager_defer_current_job_hook(ma_job* pJob);

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
    ma_uint32 expectedDataStreamExecutionPointer;
    ma_resource_manager_data_buffer* pRetirementDataBuffer;
    ma_uint32 expectedDataBufferExecutionPointer;
    ma_resource_manager_data_buffer_node* pRetirementDataBufferNode;
    ma_uint32 expectedDataBufferNodeExecutionPointer;
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
    if (pDataStream != NULL && pDataStream->executionPointer != g_hardeningResourceManagerFaultState.expectedDataStreamExecutionPointer) {
        g_hardeningResourceManagerFaultState.notificationBeforeRetirementCount += 1;
    }
    if (g_hardeningResourceManagerFaultState.pRetirementDataBuffer != NULL &&
        g_hardeningResourceManagerFaultState.pRetirementDataBuffer->executionPointer != g_hardeningResourceManagerFaultState.expectedDataBufferExecutionPointer) {
        g_hardeningResourceManagerFaultState.notificationBeforeRetirementCount += 1;
    }
    if (g_hardeningResourceManagerFaultState.pRetirementDataBufferNode != NULL &&
        g_hardeningResourceManagerFaultState.pRetirementDataBufferNode->executionPointer != g_hardeningResourceManagerFaultState.expectedDataBufferNodeExecutionPointer) {
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
        if (g_hardeningResourceManagerFaultState.pRetirementDataStream != NULL ||
            g_hardeningResourceManagerFaultState.pRetirementDataBuffer != NULL ||
            g_hardeningResourceManagerFaultState.pRetirementDataBufferNode != NULL) {
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
#define MA_RESOURCE_MANAGER_DATA_BUFFER_RESULT_PUBLISHED_HOOK(pDataBuffer) hardening_data_buffer_result_published_hook((pDataBuffer))
#define MA_RESOURCE_MANAGER_DATA_BUFFER_NODE_RESULT_PUBLISHED_HOOK(pDataBufferNode) hardening_data_buffer_node_result_published_hook((pDataBufferNode))
#define MA_RESOURCE_MANAGER_DATA_BUFFER_ASYNC_UNINIT_HOOK(pDataBuffer) hardening_data_buffer_async_uninit_hook((pDataBuffer))
#define MA_RESOURCE_MANAGER_DATA_BUFFER_NODE_ASYNC_FREE_HOOK(pDataBufferNode) hardening_data_buffer_node_async_free_hook((pDataBufferNode))
#define MA_RESOURCE_MANAGER_DATA_BUFFER_ASYNC_UNINIT_POSTED_HOOK(pDataBuffer) hardening_data_buffer_async_uninit_posted_hook((pDataBuffer))
#define MA_RESOURCE_MANAGER_DATA_BUFFER_NODE_ASYNC_FREE_POSTED_HOOK(pDataBufferNode) hardening_data_buffer_node_async_free_posted_hook((pDataBufferNode))
#define MA_RESOURCE_MANAGER_DATA_STREAM_ASYNC_UNINIT_POSTED_HOOK(pDataStream) hardening_data_stream_async_uninit_posted_hook((pDataStream))
#define MA_RESOURCE_MANAGER_DEFER_CURRENT_JOB_HOOK(pJob) hardening_resource_manager_defer_current_job_hook((pJob))
#define MINIAUDIO_IMPLEMENTATION
#include "../../miniaudio.h"

typedef struct
{
    void* pDataBuffer;
    void* pDataBufferNode;
    void* pDataStream;
    ma_event* pResultPublishedEvent;
    ma_event* pContinueEvent;
    ma_event* pAsyncRetirementEvent;
    ma_event* pAsyncRetirementContinueEvent;
    ma_event* pAsyncRetirementPostedEvent;
    void* pDeferredDataBuffer;
    ma_event* pDeferralEnteredEvent;
    ma_event* pDeferralContinueEvent;
    volatile ma_uint32 enabled;
    volatile ma_uint32 asyncBufferRetirementCount;
    volatile ma_uint32 asyncNodeRetirementCount;
} hardening_retirement_barrier;

static hardening_retirement_barrier g_hardeningRetirementBarrier;

static void hardening_data_buffer_result_published_hook(void* pDataBuffer)
{
    if (ma_atomic_load_32(&g_hardeningRetirementBarrier.enabled) != MA_FALSE &&
        pDataBuffer == g_hardeningRetirementBarrier.pDataBuffer) {
        if (g_hardeningRetirementBarrier.pResultPublishedEvent != NULL) {
            ma_event_signal(g_hardeningRetirementBarrier.pResultPublishedEvent);
        }
        if (g_hardeningRetirementBarrier.pContinueEvent != NULL) {
            ma_event_wait(g_hardeningRetirementBarrier.pContinueEvent);
        }
    }
}

static void hardening_data_buffer_node_result_published_hook(void* pDataBufferNode)
{
    if (ma_atomic_load_32(&g_hardeningRetirementBarrier.enabled) != MA_FALSE &&
        pDataBufferNode == g_hardeningRetirementBarrier.pDataBufferNode) {
        if (g_hardeningRetirementBarrier.pResultPublishedEvent != NULL) {
            ma_event_signal(g_hardeningRetirementBarrier.pResultPublishedEvent);
        }
        if (g_hardeningRetirementBarrier.pContinueEvent != NULL) {
            ma_event_wait(g_hardeningRetirementBarrier.pContinueEvent);
        }
    }
}

static void hardening_data_buffer_async_uninit_hook(void* pDataBuffer)
{
    if (ma_atomic_load_32(&g_hardeningRetirementBarrier.enabled) != MA_FALSE &&
        pDataBuffer == g_hardeningRetirementBarrier.pDataBuffer) {
        ma_atomic_fetch_add_32(&g_hardeningRetirementBarrier.asyncBufferRetirementCount, 1);
        if (g_hardeningRetirementBarrier.pAsyncRetirementEvent != NULL) {
            ma_event_signal(g_hardeningRetirementBarrier.pAsyncRetirementEvent);
        }
        if (g_hardeningRetirementBarrier.pAsyncRetirementContinueEvent != NULL) {
            ma_event_wait(g_hardeningRetirementBarrier.pAsyncRetirementContinueEvent);
        }
    }
}

static void hardening_data_buffer_node_async_free_hook(void* pDataBufferNode)
{
    if (ma_atomic_load_32(&g_hardeningRetirementBarrier.enabled) != MA_FALSE &&
        pDataBufferNode == g_hardeningRetirementBarrier.pDataBufferNode) {
        ma_atomic_fetch_add_32(&g_hardeningRetirementBarrier.asyncNodeRetirementCount, 1);
        if (g_hardeningRetirementBarrier.pAsyncRetirementEvent != NULL) {
            ma_event_signal(g_hardeningRetirementBarrier.pAsyncRetirementEvent);
        }
    }
}

static void hardening_data_buffer_async_uninit_posted_hook(void* pDataBuffer)
{
    if (ma_atomic_load_32(&g_hardeningRetirementBarrier.enabled) != MA_FALSE &&
        pDataBuffer == g_hardeningRetirementBarrier.pDataBuffer &&
        g_hardeningRetirementBarrier.pAsyncRetirementPostedEvent != NULL) {
        ma_event_signal(g_hardeningRetirementBarrier.pAsyncRetirementPostedEvent);
    }
}

static void hardening_data_buffer_node_async_free_posted_hook(void* pDataBufferNode)
{
    if (ma_atomic_load_32(&g_hardeningRetirementBarrier.enabled) != MA_FALSE &&
        pDataBufferNode == g_hardeningRetirementBarrier.pDataBufferNode &&
        g_hardeningRetirementBarrier.pAsyncRetirementPostedEvent != NULL) {
        ma_event_signal(g_hardeningRetirementBarrier.pAsyncRetirementPostedEvent);
    }
}

static void hardening_data_stream_async_uninit_posted_hook(void* pDataStream)
{
    if (ma_atomic_load_32(&g_hardeningRetirementBarrier.enabled) != MA_FALSE &&
        pDataStream == g_hardeningRetirementBarrier.pDataStream &&
        g_hardeningRetirementBarrier.pAsyncRetirementPostedEvent != NULL) {
        ma_event_signal(g_hardeningRetirementBarrier.pAsyncRetirementPostedEvent);
    }
}

static void hardening_resource_manager_defer_current_job_hook(ma_job* pJob)
{
    if (ma_atomic_load_32(&g_hardeningRetirementBarrier.enabled) != MA_FALSE &&
        pJob->toc.breakup.code == MA_JOB_TYPE_RESOURCE_MANAGER_LOAD_DATA_BUFFER &&
        pJob->data.resourceManager.loadDataBuffer.pDataBuffer == g_hardeningRetirementBarrier.pDeferredDataBuffer) {
        if (g_hardeningRetirementBarrier.pDeferralEnteredEvent != NULL) {
            ma_event_signal(g_hardeningRetirementBarrier.pDeferralEnteredEvent);
        }
        if (g_hardeningRetirementBarrier.pDeferralContinueEvent != NULL) {
            ma_event_wait(g_hardeningRetirementBarrier.pDeferralContinueEvent);
        }
    }
}

typedef struct
{
    ma_spinlock lock;
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
    ma_uint64 framesAvailable;
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

typedef struct
{
    ma_job job;
    ma_result result;
} hardening_job_thread_data;

typedef struct
{
    ma_resource_manager_data_buffer* pDataBuffer;
    ma_result result;
    volatile ma_uint32 complete;
} hardening_buffer_uninit_thread_data;

typedef struct
{
    ma_resource_manager_data_stream* pDataStream;
    ma_result result;
} hardening_stream_uninit_thread_data;

typedef struct
{
    ma_event enteredEvents[2];
    ma_event continueEvents[2];
    volatile ma_uint32 enteredCount;
    volatile ma_uint32 completedCount;
} hardening_managed_worker_barrier;

static ma_thread_result MA_THREADCALL hardening_job_thread_entry(void* pUserData)
{
    hardening_job_thread_data* pThreadData = (hardening_job_thread_data*)pUserData;
    pThreadData->result = ma_job_process(&pThreadData->job);
    return (ma_thread_result)0;
}

static ma_thread_result MA_THREADCALL hardening_process_next_job_thread_entry(void* pUserData)
{
    hardening_job_thread_data* pThreadData = (hardening_job_thread_data*)pUserData;
    ma_resource_manager* pResourceManager = (ma_resource_manager*)pThreadData->job.data.custom.data0;
    pThreadData->result = ma_resource_manager_process_next_job(pResourceManager);
    return (ma_thread_result)0;
}

static ma_thread_result MA_THREADCALL hardening_buffer_uninit_thread_entry(void* pUserData)
{
    hardening_buffer_uninit_thread_data* pThreadData = (hardening_buffer_uninit_thread_data*)pUserData;
    pThreadData->result = ma_resource_manager_data_buffer_uninit(pThreadData->pDataBuffer);
    ma_atomic_store_32(&pThreadData->complete, MA_TRUE);
    return (ma_thread_result)0;
}

static ma_thread_result MA_THREADCALL hardening_stream_uninit_thread_entry(void* pUserData)
{
    hardening_stream_uninit_thread_data* pThreadData = (hardening_stream_uninit_thread_data*)pUserData;
    pThreadData->result = ma_resource_manager_data_stream_uninit(pThreadData->pDataStream);
    return (ma_thread_result)0;
}

static ma_result hardening_managed_worker_barrier_job(ma_job* pJob)
{
    hardening_managed_worker_barrier* pBarrier;
    ma_uint32 workerIndex;

    pBarrier = (hardening_managed_worker_barrier*)pJob->data.custom.data0;
    workerIndex = (ma_uint32)pJob->data.custom.data1;
    if (pBarrier == NULL || workerIndex >= 2) {
        return MA_INVALID_ARGS;
    }

    ma_atomic_fetch_add_32(&pBarrier->enteredCount, 1);
    ma_event_signal(&pBarrier->enteredEvents[workerIndex]);
    ma_event_wait(&pBarrier->continueEvents[workerIndex]);
    ma_atomic_fetch_add_32(&pBarrier->completedCount, 1);
    return MA_SUCCESS;
}

static ma_result hardening_managed_worker_barrier_init(hardening_managed_worker_barrier* pBarrier)
{
    ma_uint32 iEvent;
    ma_uint32 enteredInitialized;
    ma_uint32 continueInitialized;
    ma_result result;

    MA_ZERO_OBJECT(pBarrier);
    enteredInitialized = 0;
    continueInitialized = 0;

    for (iEvent = 0; iEvent < 2; iEvent += 1) {
        result = ma_event_init(&pBarrier->enteredEvents[iEvent]);
        if (result != MA_SUCCESS) {
            goto error;
        }
        enteredInitialized += 1;
    }
    for (iEvent = 0; iEvent < 2; iEvent += 1) {
        result = ma_event_init(&pBarrier->continueEvents[iEvent]);
        if (result != MA_SUCCESS) {
            goto error;
        }
        continueInitialized += 1;
    }

    return MA_SUCCESS;

error:
    while (continueInitialized > 0) {
        continueInitialized -= 1;
        ma_event_uninit(&pBarrier->continueEvents[continueInitialized]);
    }
    while (enteredInitialized > 0) {
        enteredInitialized -= 1;
        ma_event_uninit(&pBarrier->enteredEvents[enteredInitialized]);
    }
    return result;
}

static void hardening_managed_worker_barrier_uninit(hardening_managed_worker_barrier* pBarrier)
{
    ma_uint32 iEvent;

    for (iEvent = 0; iEvent < 2; iEvent += 1) {
        ma_event_uninit(&pBarrier->continueEvents[iEvent]);
        ma_event_uninit(&pBarrier->enteredEvents[iEvent]);
    }
}

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

    ma_spinlock_lock(&pTracker->lock);
    if (pTracker->rejectMatchingSize && size == pTracker->rejectedSize) {
        pTracker->rejectionCount += 1;
        ma_spinlock_unlock(&pTracker->lock);
        return NULL;
    }
    ma_spinlock_unlock(&pTracker->lock);

    pAllocation = malloc(size);
    if (pAllocation != NULL) {
        ma_spinlock_lock(&pTracker->lock);
        pTracker->liveAllocationCount += 1;
        ma_spinlock_unlock(&pTracker->lock);
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
    ma_spinlock_lock(&pTracker->lock);
    if (pTracker->rejectMatchingSize && size == pTracker->rejectedSize) {
        pTracker->rejectionCount += 1;
        ma_spinlock_unlock(&pTracker->lock);
        return NULL;
    }
    if (size == 0) {
        pTracker->liveAllocationCount -= 1;
        ma_spinlock_unlock(&pTracker->lock);
        free(p);
        return NULL;
    }
    ma_spinlock_unlock(&pTracker->lock);

    pAllocation = realloc(p, size);
    return pAllocation;
}

static void hardening_tracked_free(void* p, void* pUserData)
{
    hardening_allocation_tracker* pTracker = (hardening_allocation_tracker*)pUserData;

    if (p != NULL) {
        ma_spinlock_lock(&pTracker->lock);
        pTracker->liveAllocationCount -= 1;
        ma_spinlock_unlock(&pTracker->lock);
        free(p);
    }
}

static size_t hardening_tracked_live_allocation_count(hardening_allocation_tracker* pTracker)
{
    size_t liveAllocationCount;

    ma_spinlock_lock(&pTracker->lock);
    liveAllocationCount = pTracker->liveAllocationCount;
    ma_spinlock_unlock(&pTracker->lock);
    return liveAllocationCount;
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
    ma_uint64 framesRead;

    pBackend->pState->readCount += 1;
    framesRead = frameCount;
    if (framesRead > pBackend->pState->framesAvailable) {
        framesRead = pBackend->pState->framesAvailable;
    }
    if (pBackend->pState->readResult == MA_SUCCESS && framesRead > 0) {
        if (pFramesOut != NULL) {
            MA_ZERO_MEMORY(pFramesOut, (size_t)framesRead * sizeof(float));
        }
        pBackend->pState->framesAvailable -= framesRead;
        pBackend->cursor += framesRead;
    } else {
        framesRead = 0;
    }
    if (pFramesRead != NULL) {
        *pFramesRead = framesRead;
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

static ma_result hardening_resource_manager_fixture_init_ex(hardening_resource_manager_fixture* pFixture, ma_result lengthResult, ma_uint64 lengthInFrames, ma_result readResult, ma_uint32 jobQueueCapacity, ma_uint32 jobThreadCount, ma_uint32 flags)
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
    config.jobThreadCount = jobThreadCount;
    config.jobQueueCapacity = jobQueueCapacity;
    config.flags = flags;
    config.pVFS = (ma_vfs*)&pFixture->vfs;
    config.ppCustomDecodingBackendVTables = &pFixture->pBackendVTable;
    config.customDecodingBackendCount = 1;
    config.pCustomDecodingBackendUserData = &pFixture->backendState;

    result = ma_resource_manager_init(&config, &pFixture->resourceManager);
    if (result == MA_SUCCESS && jobThreadCount == 0) {
        pFixture->baselineLiveAllocationCount = hardening_tracked_live_allocation_count(&pFixture->allocations);
    }
    return result;
}

static ma_result hardening_resource_manager_fixture_init(hardening_resource_manager_fixture* pFixture, ma_result lengthResult, ma_uint64 lengthInFrames, ma_result readResult, ma_uint32 jobQueueCapacity)
{
    return hardening_resource_manager_fixture_init_ex(
        pFixture,
        lengthResult,
        lengthInFrames,
        readResult,
        jobQueueCapacity,
        0,
        MA_RESOURCE_MANAGER_FLAG_NON_BLOCKING);
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

static int resource_manager_test_saturated_data_stream_uninit_retirement(void)
{
    hardening_resource_manager_fixture fixture;
    hardening_stream_uninit_thread_data uninitThreadData;
    ma_resource_manager_data_stream dataStream;
    ma_data_source_config dataSourceConfig;
    ma_event postedEvent;
    ma_thread uninitThread;
    ma_job fillerJob;
    ma_result result;
    int failed;

    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 2);
    if (result != MA_SUCCESS) {
        printf("saturated data stream fixture init failed: %d\n", result);
        return 1;
    }
    fillerJob = ma_job_init(MA_JOB_TYPE_CUSTOM);
    result = ma_resource_manager_post_job(&fixture.resourceManager, &fillerJob);
    if (result != MA_SUCCESS) {
        printf("saturated data stream could not fill queue: %d\n", result);
        hardening_resource_manager_fixture_uninit(&fixture, "saturated data stream");
        return 1;
    }

    MA_ZERO_OBJECT(&dataStream);
    dataSourceConfig = ma_data_source_config_init();
    dataSourceConfig.vtable = &g_ma_resource_manager_data_stream_vtable;
    result = ma_data_source_init(&dataSourceConfig, &dataStream.ds);
    if (result != MA_SUCCESS) {
        printf("saturated data stream data source init failed: %d\n", result);
        hardening_resource_manager_fixture_uninit(&fixture, "saturated data stream");
        return 1;
    }
    dataStream.pResourceManager = &fixture.resourceManager;
    dataStream.result = MA_SUCCESS;

    hardening_resource_manager_fault_reset();
    if (ma_event_init(&postedEvent) != MA_SUCCESS) {
        printf("saturated data stream posted event init failed\n");
        ma_data_source_uninit(&dataStream.ds);
        hardening_resource_manager_fixture_uninit(&fixture, "saturated data stream");
        return 1;
    }

    MA_ZERO_OBJECT(&uninitThreadData);
    MA_ZERO_OBJECT(&g_hardeningRetirementBarrier);
    g_hardeningRetirementBarrier.pDataStream = &dataStream;
    g_hardeningRetirementBarrier.pAsyncRetirementPostedEvent = &postedEvent;
    ma_atomic_store_32(&g_hardeningRetirementBarrier.enabled, MA_TRUE);
    uninitThreadData.pDataStream = &dataStream;
    result = ma_thread_create(&uninitThread, ma_thread_priority_normal, 0, hardening_stream_uninit_thread_entry, &uninitThreadData, NULL);
    if (result != MA_SUCCESS) {
        printf("saturated data stream uninit thread failed: %d\n", result);
        ma_atomic_store_32(&g_hardeningRetirementBarrier.enabled, MA_FALSE);
        MA_ZERO_OBJECT(&g_hardeningRetirementBarrier);
        ma_event_uninit(&postedEvent);
        ma_data_source_uninit(&dataStream.ds);
        hardening_resource_manager_fixture_uninit(&fixture, "saturated data stream");
        return 1;
    }

    ma_event_wait(&postedEvent);
    result = ma_resource_manager_process_next_job(&fixture.resourceManager);
    if (result == MA_SUCCESS) {
        result = ma_resource_manager_process_next_job(&fixture.resourceManager);
    }
    ma_thread_wait(&uninitThread);

    failed = 0;
    if (result != MA_SUCCESS || uninitThreadData.result != MA_SUCCESS || dataStream.executionCounter != 1 || dataStream.executionPointer != 1) {
        printf("saturated data stream retirement was process=%d uninit=%d counter=%u pointer=%u\n",
            result,
            uninitThreadData.result,
            dataStream.executionCounter,
            dataStream.executionPointer);
        failed = 1;
    }
    if (g_hardeningResourceManagerFaultState.notificationInitCallCount != 1 ||
        g_hardeningResourceManagerFaultState.notificationInitSuccessCount != 1 ||
        g_hardeningResourceManagerFaultState.notificationWaitCallCount != 1 ||
        g_hardeningResourceManagerFaultState.notificationUninitCallCount != 1) {
        printf("saturated data stream notification lifetime was init=%u success=%u wait=%u uninit=%u\n",
            g_hardeningResourceManagerFaultState.notificationInitCallCount,
            g_hardeningResourceManagerFaultState.notificationInitSuccessCount,
            g_hardeningResourceManagerFaultState.notificationWaitCallCount,
            g_hardeningResourceManagerFaultState.notificationUninitCallCount);
        failed = 1;
    }
    failed += hardening_check_no_invalid_notification_use("saturated data stream retirement");

    ma_atomic_store_32(&g_hardeningRetirementBarrier.enabled, MA_FALSE);
    MA_ZERO_OBJECT(&g_hardeningRetirementBarrier);
    ma_event_uninit(&postedEvent);
    failed += hardening_resource_manager_fixture_uninit(&fixture, "saturated data stream");
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
    g_hardeningResourceManagerFaultState.expectedDataStreamExecutionPointer = 1;
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

static int resource_manager_test_data_buffer_notifications_after_retirement(void)
{
    hardening_resource_manager_fixture fixture;
    ma_resource_manager_data_buffer dataBuffer;
    ma_async_notification_event notification;
    ma_job job;
    float externalFrames[2];
    ma_result result;
    int failed = 0;

    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 8);
    if (result != MA_SUCCESS) {
        printf("data buffer retirement fixture init failed: %d\n", result);
        return 1;
    }

    externalFrames[0] = 0;
    externalFrames[1] = 0;
    result = ma_resource_manager_register_decoded_data(&fixture.resourceManager, "buffer-retirement", externalFrames, 2, ma_format_f32, 1, 48000);
    if (result == MA_SUCCESS) {
        result = ma_resource_manager_data_buffer_init(&fixture.resourceManager, "buffer-retirement", 0, NULL, &dataBuffer);
    }
    if (result != MA_SUCCESS) {
        printf("data buffer retirement setup failed: %d\n", result);
        ma_resource_manager_unregister_data(&fixture.resourceManager, "buffer-retirement");
        hardening_resource_manager_fixture_uninit(&fixture, "data buffer retirement");
        return 1;
    }

    dataBuffer.result = MA_BUSY;
    dataBuffer.executionPointer = 0;
    dataBuffer.executionCounter = 1;
    hardening_resource_manager_fault_reset();
    g_hardeningResourceManagerFaultState.pRetirementDataBuffer = &dataBuffer;
    g_hardeningResourceManagerFaultState.expectedDataBufferExecutionPointer = 1;
    result = hardening_inline_notification_event_init(&notification);
    if (result == MA_SUCCESS) {
        job = ma_job_init(MA_JOB_TYPE_RESOURCE_MANAGER_LOAD_DATA_BUFFER);
        job.order = 0;
        job.data.resourceManager.loadDataBuffer.pDataBuffer = &dataBuffer;
        job.data.resourceManager.loadDataBuffer.pDoneNotification = (ma_async_notification*)&notification;
        result = ma_job_process(&job);
    }
    g_hardeningResourceManagerFaultState.pRetirementDataBuffer = NULL;
    if (result == MA_SUCCESS) {
        hardening_inline_notification_event_uninit(&notification);
    }
    if (result != MA_SUCCESS || dataBuffer.executionPointer != 1 ||
        g_hardeningResourceManagerFaultState.notificationBeforeRetirementCount != 0) {
        printf("data buffer load retirement was result=%d pointer=%u early-notifications=%u\n",
            result,
            dataBuffer.executionPointer,
            g_hardeningResourceManagerFaultState.notificationBeforeRetirementCount);
        failed = 1;
    }

    dataBuffer.result = MA_UNAVAILABLE;
    dataBuffer.executionPointer = 0;
    dataBuffer.executionCounter = 1;
    hardening_resource_manager_fault_reset();
    g_hardeningResourceManagerFaultState.pRetirementDataBuffer = &dataBuffer;
    g_hardeningResourceManagerFaultState.expectedDataBufferExecutionPointer = 1;
    result = hardening_inline_notification_event_init(&notification);
    if (result == MA_SUCCESS) {
        job = ma_job_init(MA_JOB_TYPE_RESOURCE_MANAGER_FREE_DATA_BUFFER);
        job.order = 0;
        job.data.resourceManager.freeDataBuffer.pDataBuffer = &dataBuffer;
        job.data.resourceManager.freeDataBuffer.pDoneNotification = (ma_async_notification*)&notification;
        result = ma_job_process(&job);
    }
    g_hardeningResourceManagerFaultState.pRetirementDataBuffer = NULL;
    if (result == MA_SUCCESS) {
        hardening_inline_notification_event_uninit(&notification);
    }
    if (result != MA_SUCCESS || dataBuffer.executionPointer != 1 ||
        g_hardeningResourceManagerFaultState.notificationBeforeRetirementCount != 0) {
        printf("data buffer free retirement was result=%d pointer=%u early-notifications=%u\n",
            result,
            dataBuffer.executionPointer,
            g_hardeningResourceManagerFaultState.notificationBeforeRetirementCount);
        failed = 1;
    }

    if (ma_resource_manager_unregister_data(&fixture.resourceManager, "buffer-retirement") != MA_SUCCESS) {
        printf("data buffer retirement unregister failed\n");
        failed = 1;
    }
    if (fixture.allocations.liveAllocationCount != fixture.baselineLiveAllocationCount) {
        printf("data buffer retirement leaked tracked allocations\n");
        failed = 1;
    }
    failed += hardening_resource_manager_fixture_uninit(&fixture, "data buffer retirement");
    return failed;
}

static int resource_manager_test_data_buffer_node_notifications_after_retirement(void)
{
    hardening_resource_manager_fixture fixture;
    ma_resource_manager_data_buffer_node* pNode;
    ma_async_notification_event notification;
    ma_job job;
    char* pFilePathCopy;
    ma_result result;
    int failed = 0;

    result = hardening_resource_manager_fixture_init(&fixture, MA_NOT_IMPLEMENTED, 0, MA_AT_END, 8);
    if (result != MA_SUCCESS) {
        printf("data buffer node retirement fixture init failed: %d\n", result);
        return 1;
    }

    pNode = (ma_resource_manager_data_buffer_node*)ma_malloc(sizeof(*pNode), &fixture.resourceManager.config.allocationCallbacks);
    pFilePathCopy = ma_copy_string("node-retirement.custom", &fixture.resourceManager.config.allocationCallbacks);
    if (pNode == NULL || pFilePathCopy == NULL) {
        printf("data buffer node retirement setup allocation failed\n");
        ma_free(pFilePathCopy, &fixture.resourceManager.config.allocationCallbacks);
        ma_free(pNode, &fixture.resourceManager.config.allocationCallbacks);
        hardening_resource_manager_fixture_uninit(&fixture, "data buffer node retirement");
        return 1;
    }

    MA_ZERO_OBJECT(pNode);
    pNode->refCount = 1;
    pNode->result = MA_BUSY;
    pNode->isDataOwnedByResourceManager = MA_TRUE;
    pNode->data.type = ma_resource_manager_data_supply_type_unknown;
    pNode->executionCounter = 1;

    hardening_resource_manager_fault_reset();
    g_hardeningResourceManagerFaultState.pRetirementDataBufferNode = pNode;
    g_hardeningResourceManagerFaultState.expectedDataBufferNodeExecutionPointer = 1;
    result = hardening_inline_notification_event_init(&notification);
    if (result == MA_SUCCESS) {
        job = ma_job_init(MA_JOB_TYPE_RESOURCE_MANAGER_LOAD_DATA_BUFFER_NODE);
        job.order = 0;
        job.data.resourceManager.loadDataBufferNode.pResourceManager = &fixture.resourceManager;
        job.data.resourceManager.loadDataBufferNode.pDataBufferNode = pNode;
        job.data.resourceManager.loadDataBufferNode.pFilePath = pFilePathCopy;
        job.data.resourceManager.loadDataBufferNode.flags = MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE | MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_UNKNOWN_LENGTH;
        job.data.resourceManager.loadDataBufferNode.pInitNotification = (ma_async_notification*)&notification;
        job.data.resourceManager.loadDataBufferNode.pDoneNotification = (ma_async_notification*)&notification;
        result = ma_job_process(&job);
    }
    if (result != MA_SUCCESS || pNode->executionPointer != 1 || pNode->executionCounter != 2 ||
        g_hardeningResourceManagerFaultState.notificationBeforeRetirementCount != 0) {
        printf("data buffer node load retirement was result=%d pointer=%u counter=%u early-notifications=%u\n",
            result,
            pNode->executionPointer,
            pNode->executionCounter,
            g_hardeningResourceManagerFaultState.notificationBeforeRetirementCount);
        failed = 1;
    }

    g_hardeningResourceManagerFaultState.expectedDataBufferNodeExecutionPointer = 2;
    result = ma_resource_manager_process_next_job(&fixture.resourceManager);
    g_hardeningResourceManagerFaultState.pRetirementDataBufferNode = NULL;
    hardening_inline_notification_event_uninit(&notification);
    if (result != MA_SUCCESS || pNode->executionPointer != 2 || ma_resource_manager_data_buffer_node_result(pNode) != MA_SUCCESS ||
        g_hardeningResourceManagerFaultState.notificationBeforeRetirementCount != 0) {
        printf("data buffer node page retirement was result=%d state=%d pointer=%u early-notifications=%u\n",
            result,
            ma_resource_manager_data_buffer_node_result(pNode),
            pNode->executionPointer,
            g_hardeningResourceManagerFaultState.notificationBeforeRetirementCount);
        failed = 1;
    }

    ma_resource_manager_data_buffer_node_free(&fixture.resourceManager, pNode);
    if (fixture.backendState.initCount != 1 || fixture.backendState.uninitCount != 1 ||
        fixture.vfs.openCount != 1 || fixture.vfs.closeCount != 1) {
        printf("data buffer node retirement lifetime was decoder=%u/%u VFS=%u/%u\n",
            fixture.backendState.initCount,
            fixture.backendState.uninitCount,
            fixture.vfs.openCount,
            fixture.vfs.closeCount);
        failed = 1;
    }
    if (fixture.allocations.liveAllocationCount != fixture.baselineLiveAllocationCount) {
        printf("data buffer node retirement leaked tracked allocations\n");
        failed = 1;
    }
    failed += hardening_resource_manager_fixture_uninit(&fixture, "data buffer node retirement");
    return failed;
}

static int resource_manager_test_data_buffer_result_polling_defers_uninit(void)
{
    hardening_resource_manager_fixture fixture;
    hardening_job_thread_data jobThreadData;
    hardening_buffer_uninit_thread_data uninitThreadData;
    ma_resource_manager_data_buffer dataBuffer;
    ma_thread jobThread;
    ma_thread uninitThread;
    ma_event resultPublishedEvent;
    ma_event continueEvent;
    ma_event asyncRetirementEvent;
    ma_event asyncRetirementContinueEvent;
    ma_event asyncRetirementPostedEvent;
    float externalFrames[2];
    ma_result result;
    int failed;

    failed = 0;
    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 8);
    if (result != MA_SUCCESS) {
        printf("buffer result-poll fixture init failed: %d\n", result);
        return 1;
    }

    externalFrames[0] = 0;
    externalFrames[1] = 0;
    result = ma_resource_manager_register_decoded_data(&fixture.resourceManager, "buffer-result-poll", externalFrames, 2, ma_format_f32, 1, 48000);
    if (result == MA_SUCCESS) {
        result = ma_resource_manager_data_buffer_init(&fixture.resourceManager, "buffer-result-poll", 0, NULL, &dataBuffer);
    }
    if (result != MA_SUCCESS) {
        printf("buffer result-poll setup failed: %d\n", result);
        hardening_resource_manager_fixture_uninit(&fixture, "buffer result-poll");
        return 1;
    }

    if (ma_event_init(&resultPublishedEvent) != MA_SUCCESS ||
        ma_event_init(&continueEvent) != MA_SUCCESS ||
        ma_event_init(&asyncRetirementEvent) != MA_SUCCESS ||
        ma_event_init(&asyncRetirementContinueEvent) != MA_SUCCESS ||
        ma_event_init(&asyncRetirementPostedEvent) != MA_SUCCESS) {
        printf("buffer result-poll barrier init failed\n");
        ma_resource_manager_data_buffer_uninit(&dataBuffer);
        ma_resource_manager_unregister_data(&fixture.resourceManager, "buffer-result-poll");
        hardening_resource_manager_fixture_uninit(&fixture, "buffer result-poll");
        return 1;
    }

    MA_ZERO_OBJECT(&jobThreadData);
    MA_ZERO_OBJECT(&uninitThreadData);
    MA_ZERO_OBJECT(&g_hardeningRetirementBarrier);
    g_hardeningRetirementBarrier.pDataBuffer = &dataBuffer;
    g_hardeningRetirementBarrier.pResultPublishedEvent = &resultPublishedEvent;
    g_hardeningRetirementBarrier.pContinueEvent = &continueEvent;
    g_hardeningRetirementBarrier.pAsyncRetirementEvent = &asyncRetirementEvent;
    g_hardeningRetirementBarrier.pAsyncRetirementContinueEvent = &asyncRetirementContinueEvent;
    g_hardeningRetirementBarrier.pAsyncRetirementPostedEvent = &asyncRetirementPostedEvent;
    ma_atomic_store_32(&g_hardeningRetirementBarrier.enabled, MA_TRUE);

    dataBuffer.result = MA_BUSY;
    dataBuffer.executionPointer = 0;
    dataBuffer.executionCounter = 1;
    jobThreadData.job = ma_job_init(MA_JOB_TYPE_RESOURCE_MANAGER_LOAD_DATA_BUFFER);
    jobThreadData.job.order = 0;
    jobThreadData.job.data.resourceManager.loadDataBuffer.pDataBuffer = &dataBuffer;

    result = ma_thread_create(&jobThread, ma_thread_priority_normal, 0, hardening_job_thread_entry, &jobThreadData, NULL);
    if (result != MA_SUCCESS) {
        printf("buffer result-poll worker creation failed: %d\n", result);
        failed = 1;
        goto done;
    }
    ma_event_wait(&resultPublishedEvent);

    if (ma_resource_manager_data_buffer_result(&dataBuffer) != MA_SUCCESS || dataBuffer.executionPointer != 0) {
        printf("buffer result-poll barrier missed publication window\n");
        failed = 1;
    }

    uninitThreadData.pDataBuffer = &dataBuffer;
    result = ma_thread_create(&uninitThread, ma_thread_priority_normal, 0, hardening_buffer_uninit_thread_entry, &uninitThreadData, NULL);
    if (result != MA_SUCCESS) {
        printf("buffer result-poll uninit thread creation failed: %d\n", result);
        ma_event_signal(&continueEvent);
        ma_thread_wait(&jobThread);
        failed = 1;
        goto done;
    }
    ma_event_wait(&asyncRetirementEvent);

    if (ma_atomic_load_32(&g_hardeningRetirementBarrier.asyncBufferRetirementCount) != 1 ||
        ma_atomic_load_32(&uninitThreadData.complete) != MA_FALSE) {
        printf("terminal buffer result did not retain the outstanding job\n");
        failed = 1;
    }

    ma_event_signal(&continueEvent);
    ma_thread_wait(&jobThread);
    ma_event_signal(&asyncRetirementContinueEvent);
    ma_event_wait(&asyncRetirementPostedEvent);
    result = ma_resource_manager_process_next_job(&fixture.resourceManager);
    ma_thread_wait(&uninitThread);
    if (jobThreadData.result != MA_SUCCESS || result != MA_SUCCESS ||
        uninitThreadData.result != MA_SUCCESS || ma_atomic_load_32(&uninitThreadData.complete) != MA_TRUE) {
        printf("buffer result-poll retirement failed: load=%d free=%d uninit=%d complete=%u\n",
            jobThreadData.result, result, uninitThreadData.result, ma_atomic_load_32(&uninitThreadData.complete));
        failed = 1;
    }

done:
    ma_atomic_store_32(&g_hardeningRetirementBarrier.enabled, MA_FALSE);
    MA_ZERO_OBJECT(&g_hardeningRetirementBarrier);
    ma_event_uninit(&asyncRetirementPostedEvent);
    ma_event_uninit(&asyncRetirementContinueEvent);
    ma_event_uninit(&asyncRetirementEvent);
    ma_event_uninit(&continueEvent);
    ma_event_uninit(&resultPublishedEvent);
    if (ma_resource_manager_unregister_data(&fixture.resourceManager, "buffer-result-poll") != MA_SUCCESS) {
        printf("buffer result-poll unregister failed\n");
        failed = 1;
    }
    if (fixture.allocations.liveAllocationCount != fixture.baselineLiveAllocationCount) {
        printf("buffer result-poll leaked tracked allocations\n");
        failed = 1;
    }
    failed += hardening_resource_manager_fixture_uninit(&fixture, "buffer result-poll");
    return failed;
}

static int resource_manager_test_node_result_polling_defers_last_release(void)
{
    hardening_resource_manager_fixture fixture;
    hardening_job_thread_data jobThreadData;
    ma_resource_manager_data_buffer_node* pNode;
    ma_thread jobThread;
    ma_event resultPublishedEvent;
    ma_event continueEvent;
    ma_event asyncRetirementEvent;
    ma_uint32 flags;
    ma_result result;
    int failed;

    failed = 0;
    result = hardening_resource_manager_fixture_init(&fixture, MA_NOT_IMPLEMENTED, 0, MA_AT_END, 8);
    if (result != MA_SUCCESS) {
        printf("node result-poll fixture init failed: %d\n", result);
        return 1;
    }
    if (ma_event_init(&resultPublishedEvent) != MA_SUCCESS ||
        ma_event_init(&continueEvent) != MA_SUCCESS ||
        ma_event_init(&asyncRetirementEvent) != MA_SUCCESS) {
        printf("node result-poll barrier init failed\n");
        hardening_resource_manager_fixture_uninit(&fixture, "node result-poll");
        return 1;
    }

    flags = MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC |
            MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE |
            MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_UNKNOWN_LENGTH;
    result = ma_resource_manager_register_file(&fixture.resourceManager, "node-result-poll.custom", flags);
    if (result != MA_SUCCESS) {
        printf("node result-poll registration failed: %d\n", result);
        failed = 1;
        goto done;
    }

    result = ma_resource_manager_process_next_job(&fixture.resourceManager);
    pNode = fixture.resourceManager.pRootDataBufferNode;
    if (result != MA_SUCCESS || pNode == NULL || pNode->executionPointer != 1 || pNode->executionCounter != 2) {
        printf("node result-poll load setup failed: result=%d node=%p\n", result, (void*)pNode);
        failed = 1;
        goto done;
    }

    MA_ZERO_OBJECT(&jobThreadData);
    MA_ZERO_OBJECT(&g_hardeningRetirementBarrier);
    g_hardeningRetirementBarrier.pDataBufferNode = pNode;
    g_hardeningRetirementBarrier.pResultPublishedEvent = &resultPublishedEvent;
    g_hardeningRetirementBarrier.pContinueEvent = &continueEvent;
    g_hardeningRetirementBarrier.pAsyncRetirementEvent = &asyncRetirementEvent;
    ma_atomic_store_32(&g_hardeningRetirementBarrier.enabled, MA_TRUE);
    jobThreadData.job.data.custom.data0 = (ma_uintptr)&fixture.resourceManager;

    result = ma_thread_create(&jobThread, ma_thread_priority_normal, 0, hardening_process_next_job_thread_entry, &jobThreadData, NULL);
    if (result != MA_SUCCESS) {
        printf("node result-poll page worker creation failed: %d\n", result);
        failed = 1;
        goto done;
    }
    ma_event_wait(&resultPublishedEvent);

    if (ma_resource_manager_data_buffer_node_result(pNode) != MA_SUCCESS || pNode->executionPointer != 1) {
        printf("node result-poll barrier missed publication window\n");
        failed = 1;
    }

    result = ma_resource_manager_unregister_file(&fixture.resourceManager, "node-result-poll.custom");
    if (result != MA_SUCCESS || fixture.resourceManager.pRootDataBufferNode != NULL ||
        ma_atomic_load_32(&g_hardeningRetirementBarrier.asyncNodeRetirementCount) != 1) {
        printf("terminal node result did not defer last release: result=%d root=%p deferred=%u\n",
            result, (void*)fixture.resourceManager.pRootDataBufferNode,
            ma_atomic_load_32(&g_hardeningRetirementBarrier.asyncNodeRetirementCount));
        failed = 1;
    }

    ma_event_signal(&continueEvent);
    ma_thread_wait(&jobThread);
    result = ma_resource_manager_process_next_job(&fixture.resourceManager);
    if (jobThreadData.result != MA_SUCCESS || result != MA_SUCCESS) {
        printf("node result-poll retirement failed: page=%d free=%d\n", jobThreadData.result, result);
        failed = 1;
    }

done:
    ma_atomic_store_32(&g_hardeningRetirementBarrier.enabled, MA_FALSE);
    MA_ZERO_OBJECT(&g_hardeningRetirementBarrier);
    ma_event_uninit(&asyncRetirementEvent);
    ma_event_uninit(&continueEvent);
    ma_event_uninit(&resultPublishedEvent);
    if (fixture.allocations.liveAllocationCount != fixture.baselineLiveAllocationCount) {
        printf("node result-poll leaked tracked allocations\n");
        failed = 1;
    }
    failed += hardening_resource_manager_fixture_uninit(&fixture, "node result-poll");
    return failed;
}

static int resource_manager_test_wait_init_requeue_failure_releases_fences(void)
{
    hardening_resource_manager_fixture fixture;
    hardening_counting_notification initNotification;
    hardening_counting_notification doneNotification;
    ma_resource_manager_pipeline_notifications notifications;
    ma_resource_manager_data_buffer dataBuffer;
    ma_fence initFence;
    ma_fence doneFence;
    ma_uint32 flags;
    ma_result result;
    ma_bool32 sentinelAcquired;
    int failed;

    failed = 0;
    sentinelAcquired = MA_FALSE;
    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 2);
    if (result != MA_SUCCESS) {
        printf("wait-init requeue fixture init failed: %d\n", result);
        return 1;
    }
    if (ma_fence_init(&initFence) != MA_SUCCESS || ma_fence_init(&doneFence) != MA_SUCCESS) {
        printf("wait-init requeue fence init failed\n");
        hardening_resource_manager_fixture_uninit(&fixture, "wait-init requeue");
        return 1;
    }

    result = ma_resource_manager_register_file(
        &fixture.resourceManager,
        "wait-init-requeue.custom",
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC |
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE);
    if (result != MA_SUCCESS) {
        printf("wait-init requeue could not post node load: %d\n", result);
        failed = 1;
        goto done;
    }
    result = ma_resource_manager_process_next_job(&fixture.resourceManager);
    if (result != MA_SUCCESS || fixture.resourceManager.pRootDataBufferNode == NULL ||
        ma_resource_manager_data_buffer_node_get_data_supply_type(fixture.resourceManager.pRootDataBufferNode) == ma_resource_manager_data_supply_type_unknown ||
        ma_resource_manager_data_buffer_node_result(fixture.resourceManager.pRootDataBufferNode) != MA_BUSY) {
        printf("wait-init requeue did not publish a busy decoded supply: %d\n", result);
        failed = 1;
        goto done;
    }

    hardening_counting_notification_init(&initNotification);
    hardening_counting_notification_init(&doneNotification);
    notifications = ma_resource_manager_pipeline_notifications_init();
    notifications.init.pNotification = (ma_async_notification*)&initNotification;
    notifications.init.pFence = &initFence;
    notifications.done.pNotification = (ma_async_notification*)&doneNotification;
    notifications.done.pFence = &doneFence;
    flags = MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC | MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_WAIT_INIT;
    hardening_resource_manager_fault_reset();
    if (ma_fence_acquire(&initFence) != MA_SUCCESS) {
        printf("wait-init requeue sentinel acquire failed\n");
        failed = 1;
        goto done;
    }
    sentinelAcquired = MA_TRUE;
    result = ma_resource_manager_data_buffer_init(
        &fixture.resourceManager,
        "wait-init-requeue.custom",
        flags,
        &notifications,
        &dataBuffer);

    if (result != MA_OUT_OF_MEMORY || dataBuffer.pResourceManager != NULL || dataBuffer.pNode != NULL) {
        printf("wait-init requeue returned %d with initialized state\n", result);
        failed = 1;
    }
    if (initFence.counter != 1 || doneFence.counter != 0 ||
        initNotification.signalCount != 1 || doneNotification.signalCount != 1) {
        printf("wait-init requeue changed sentinel/completion ownership: fences=%u/%u notifications=%u/%u\n",
            initFence.counter, doneFence.counter, initNotification.signalCount, doneNotification.signalCount);
        failed = 1;
    }
    if (g_hardeningResourceManagerFaultState.fenceAcquireCallCount != 4 ||
        g_hardeningResourceManagerFaultState.fenceReleaseCallCount != 3) {
        printf("wait-init requeue fence accounting was acquire=%u release=%u\n",
            g_hardeningResourceManagerFaultState.fenceAcquireCallCount,
            g_hardeningResourceManagerFaultState.fenceReleaseCallCount);
        failed = 1;
    }
    if (ma_fence_release(&initFence) != MA_SUCCESS || initFence.counter != 0) {
        printf("wait-init requeue sentinel did not remain independently owned\n");
        failed = 1;
    }
    sentinelAcquired = MA_FALSE;

    result = ma_resource_manager_process_next_job(&fixture.resourceManager);
    if (result != MA_SUCCESS) {
        printf("wait-init requeue retained-node page returned %d\n", result);
        failed = 1;
    }
    result = ma_resource_manager_unregister_file(&fixture.resourceManager, "wait-init-requeue.custom");
    if (result != MA_SUCCESS) {
        printf("wait-init requeue retained-node cleanup failed: %d\n", result);
        failed = 1;
    }

done:
    if (sentinelAcquired) {
        ma_fence_release(&initFence);
    }
    ma_fence_uninit(&doneFence);
    ma_fence_uninit(&initFence);
    if (fixture.allocations.liveAllocationCount != fixture.baselineLiveAllocationCount) {
        printf("wait-init requeue leaked tracked allocations\n");
        failed = 1;
    }
    failed += hardening_resource_manager_fixture_uninit(&fixture, "wait-init requeue");
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

static int resource_manager_test_saturated_initial_page_continuation(void)
{
    hardening_resource_manager_fixture fixture;
    ma_job loadJob;
    ma_job fillerJob;
    ma_resource_manager_data_buffer_node* pNode;
    ma_result result;
    int failed;

    failed = 0;
    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_AT_END, 3);
    if (result != MA_SUCCESS) {
        return 1;
    }

    result = ma_resource_manager_register_file(&fixture.resourceManager, "saturated-initial-page.custom",
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC | MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE);
    if (result != MA_SUCCESS || ma_resource_manager_next_job(&fixture.resourceManager, &loadJob) != MA_SUCCESS) {
        printf("saturated initial-page setup failed: %d\n", result);
        failed = 1;
        goto done;
    }

    fillerJob = ma_job_init(MA_JOB_TYPE_CUSTOM);
    if (ma_resource_manager_post_job(&fixture.resourceManager, &fillerJob) != MA_SUCCESS) {
        printf("saturated initial-page could not occupy the remaining admission record\n");
        (void)ma_job_process(&loadJob);
        failed = 1;
        goto done;
    }

    result = ma_job_process(&loadJob);
    pNode = fixture.resourceManager.pRootDataBufferNode;
    if (result != MA_SUCCESS || pNode == NULL ||
        pNode->executionPointer != 1 || pNode->executionCounter != 2 ||
        ma_resource_manager_data_buffer_node_result(pNode) != MA_BUSY) {
        printf("saturated initial-page continuation was result=%d pointer/counter=%u/%u\n", result,
            pNode != NULL ? pNode->executionPointer : 0, pNode != NULL ? pNode->executionCounter : 0);
        failed = 1;
    }

    if (ma_resource_manager_process_next_job(&fixture.resourceManager) != MA_SUCCESS || /* filler */
        ma_resource_manager_process_next_job(&fixture.resourceManager) != MA_SUCCESS || /* terminal page */
        pNode == NULL || pNode->executionPointer != 2 || pNode->executionCounter != 2 ||
        ma_resource_manager_data_buffer_node_result(pNode) != MA_SUCCESS ||
        ma_resource_manager_unregister_file(&fixture.resourceManager, "saturated-initial-page.custom") != MA_SUCCESS ||
        fixture.resourceManager.pRootDataBufferNode != NULL) {
        printf("saturated initial-page node did not complete and retire\n");
        failed = 1;
    }

done:
    if (fixture.resourceManager.pRootDataBufferNode != NULL) {
        (void)ma_resource_manager_unregister_file(&fixture.resourceManager, "saturated-initial-page.custom");
    }
    if (fixture.backendState.initCount != fixture.backendState.uninitCount ||
        fixture.vfs.openCount != fixture.vfs.closeCount ||
        fixture.allocations.liveAllocationCount != fixture.baselineLiveAllocationCount) {
        printf("saturated initial-page cleanup was unbalanced\n");
        failed = 1;
    }
    failed += hardening_resource_manager_fixture_uninit(&fixture, "saturated initial-page");
    return failed;
}

static int resource_manager_test_saturated_page_continuation(void)
{
    hardening_resource_manager_fixture fixture;
    ma_job pageJob;
    ma_job fillerJob;
    ma_resource_manager_data_buffer_node* pNode;
    ma_result result;
    int failed;

    failed = 0;
    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 3);
    if (result != MA_SUCCESS) {
        return 1;
    }
    fixture.backendState.framesAvailable = 13;

    result = ma_resource_manager_register_file(&fixture.resourceManager, "saturated-page-continuation.custom",
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC | MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE);
    if (result != MA_SUCCESS || ma_resource_manager_process_next_job(&fixture.resourceManager) != MA_SUCCESS ||
        ma_resource_manager_next_job(&fixture.resourceManager, &pageJob) != MA_SUCCESS) {
        printf("saturated page continuation setup failed: %d\n", result);
        failed = 1;
        goto done;
    }

    fillerJob = ma_job_init(MA_JOB_TYPE_CUSTOM);
    if (ma_resource_manager_post_job(&fixture.resourceManager, &fillerJob) != MA_SUCCESS) {
        printf("saturated page continuation could not occupy the remaining admission record\n");
        (void)ma_job_process(&pageJob);
        failed = 1;
        goto done;
    }

    result = ma_job_process(&pageJob);
    pNode = fixture.resourceManager.pRootDataBufferNode;
    if (result != MA_BUSY || pNode == NULL ||
        pNode->executionPointer != 2 || pNode->executionCounter != 3 ||
        ma_resource_manager_data_buffer_node_result(pNode) != MA_BUSY) {
        printf("saturated page continuation was result=%d pointer/counter=%u/%u\n", result,
            pNode != NULL ? pNode->executionPointer : 0, pNode != NULL ? pNode->executionCounter : 0);
        failed = 1;
    }

    if (ma_resource_manager_process_next_job(&fixture.resourceManager) != MA_SUCCESS || /* filler */
        ma_resource_manager_process_next_job(&fixture.resourceManager) != MA_SUCCESS || /* terminal page */
        pNode == NULL || pNode->executionPointer != 3 || pNode->executionCounter != 3 ||
        ma_resource_manager_data_buffer_node_result(pNode) != MA_SUCCESS ||
        ma_resource_manager_unregister_file(&fixture.resourceManager, "saturated-page-continuation.custom") != MA_SUCCESS ||
        fixture.resourceManager.pRootDataBufferNode != NULL) {
        printf("saturated page continuation node did not complete and retire\n");
        failed = 1;
    }

done:
    if (fixture.resourceManager.pRootDataBufferNode != NULL) {
        (void)ma_resource_manager_unregister_file(&fixture.resourceManager, "saturated-page-continuation.custom");
    }
    if (fixture.backendState.initCount != fixture.backendState.uninitCount ||
        fixture.vfs.openCount != fixture.vfs.closeCount ||
        fixture.allocations.liveAllocationCount != fixture.baselineLiveAllocationCount) {
        printf("saturated page continuation cleanup was unbalanced\n");
        failed = 1;
    }
    failed += hardening_resource_manager_fixture_uninit(&fixture, "saturated page continuation");
    return failed;
}

static int hardening_check_saturated_deferral(hardening_resource_manager_fixture* pFixture, ma_job* pPostedJob, const char* pName)
{
    ma_job currentJob;
    ma_job deferredJob;
    ma_job fillerJob;
    ma_job_queue_record* pAdmissionRecord;
    ma_result processResult;
    ma_result result;
    int failed;

    failed = 0;
    result = ma_resource_manager_post_job(&pFixture->resourceManager, pPostedJob);
    if (result != MA_SUCCESS) {
        printf("%s initial post failed: %d\n", pName, result);
        return 1;
    }

    result = ma_resource_manager_next_job(&pFixture->resourceManager, &currentJob);
    if (result != MA_SUCCESS) {
        printf("%s initial dequeue failed: %d\n", pName, result);
        return 1;
    }
    pAdmissionRecord = ma_job_queue_record_from_job(&currentJob);
    if (pAdmissionRecord == NULL) {
        printf("%s was not associated with a retained admission record\n", pName);
        return 1;
    }

    fillerJob = ma_job_init(MA_JOB_TYPE_CUSTOM);
    result = ma_resource_manager_post_job(&pFixture->resourceManager, &fillerJob);
    if (result != MA_OUT_OF_MEMORY) {
        printf("%s did not retain the only ordinary queue credit: %d\n", pName, result);
        failed = 1;
    }

    processResult = ma_job_process(&currentJob);
    if (processResult != MA_SUCCESS || currentJob.next != 0) {
        printf("%s deferral failed: result=%d transport-cleared=%u\n", pName, processResult, currentJob.next == 0);
        return 1;
    }

    result = ma_resource_manager_next_job(&pFixture->resourceManager, &deferredJob);
    if (result != MA_SUCCESS || deferredJob.toc.breakup.code != pPostedJob->toc.breakup.code ||
        deferredJob.order != pPostedJob->order || ma_job_queue_record_from_job(&deferredJob) != pAdmissionRecord) {
        printf("%s did not preserve its code/order/admission record after deferral\n", pName);
        return 1;
    }

    if (ma_job_queue_complete_current_job(&deferredJob) == MA_FALSE) {
        printf("%s retained record could not be completed\n", pName);
        return 1;
    }
    if (ma_resource_manager_next_job(&pFixture->resourceManager, &currentJob) != MA_NO_DATA_AVAILABLE) {
        printf("%s left an unexpected queued job after completion\n", pName);
        failed = 1;
    }

    return failed;
}

static int resource_manager_test_all_saturated_deferral_sites(void)
{
    hardening_resource_manager_fixture fixture;
    ma_resource_manager_data_buffer_node node;
    ma_resource_manager_data_buffer dataBuffer;
    ma_resource_manager_data_stream dataStream;
    ma_job job;
    ma_result result;
    int failed;

    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 2);
    if (result != MA_SUCCESS) {
        printf("saturated deferral fixture init failed: %d\n", result);
        return 1;
    }

    MA_ZERO_OBJECT(&node);
    MA_ZERO_OBJECT(&dataBuffer);
    MA_ZERO_OBJECT(&dataStream);
    node.isDataOwnedByResourceManager = MA_TRUE;
    node.result = MA_BUSY;
    dataBuffer.pResourceManager = &fixture.resourceManager;
    dataBuffer.pNode = &node;
    dataBuffer.result = MA_BUSY;
    dataStream.pResourceManager = &fixture.resourceManager;
    dataStream.result = MA_SUCCESS;
    failed = 0;

    node.executionPointer = 0;
    job = ma_job_init(MA_JOB_TYPE_RESOURCE_MANAGER_LOAD_DATA_BUFFER_NODE);
    job.order = 1;
    job.data.resourceManager.loadDataBufferNode.pResourceManager = &fixture.resourceManager;
    job.data.resourceManager.loadDataBufferNode.pDataBufferNode = &node;
    failed += hardening_check_saturated_deferral(&fixture, &job, "load data-buffer node order deferral");

    node.executionPointer = 0;
    job = ma_job_init(MA_JOB_TYPE_RESOURCE_MANAGER_FREE_DATA_BUFFER_NODE);
    job.order = 1;
    job.data.resourceManager.freeDataBufferNode.pResourceManager = &fixture.resourceManager;
    job.data.resourceManager.freeDataBufferNode.pDataBufferNode = &node;
    failed += hardening_check_saturated_deferral(&fixture, &job, "free data-buffer node order deferral");

    node.executionPointer = 0;
    job = ma_job_init(MA_JOB_TYPE_RESOURCE_MANAGER_PAGE_DATA_BUFFER_NODE);
    job.order = 1;
    job.data.resourceManager.pageDataBufferNode.pResourceManager = &fixture.resourceManager;
    job.data.resourceManager.pageDataBufferNode.pDataBufferNode = &node;
    failed += hardening_check_saturated_deferral(&fixture, &job, "page data-buffer node order deferral");

    dataBuffer.executionPointer = 0;
    job = ma_job_init(MA_JOB_TYPE_RESOURCE_MANAGER_LOAD_DATA_BUFFER);
    job.order = 1;
    job.data.resourceManager.loadDataBuffer.pDataBuffer = &dataBuffer;
    failed += hardening_check_saturated_deferral(&fixture, &job, "load data-buffer order deferral");

    dataBuffer.executionPointer = 0;
    node.result = MA_BUSY;
    node.data.type = ma_resource_manager_data_supply_type_unknown;
    job = ma_job_init(MA_JOB_TYPE_RESOURCE_MANAGER_LOAD_DATA_BUFFER);
    job.order = 0;
    job.data.resourceManager.loadDataBuffer.pDataBuffer = &dataBuffer;
    failed += hardening_check_saturated_deferral(&fixture, &job, "load data-buffer dependency deferral");

    dataBuffer.executionPointer = 0;
    job = ma_job_init(MA_JOB_TYPE_RESOURCE_MANAGER_FREE_DATA_BUFFER);
    job.order = 1;
    job.data.resourceManager.freeDataBuffer.pDataBuffer = &dataBuffer;
    failed += hardening_check_saturated_deferral(&fixture, &job, "free data-buffer order deferral");

    dataStream.executionPointer = 0;
    job = ma_job_init(MA_JOB_TYPE_RESOURCE_MANAGER_LOAD_DATA_STREAM);
    job.order = 1;
    job.data.resourceManager.loadDataStream.pDataStream = &dataStream;
    failed += hardening_check_saturated_deferral(&fixture, &job, "load data-stream order deferral");

    dataStream.executionPointer = 0;
    job = ma_job_init(MA_JOB_TYPE_RESOURCE_MANAGER_FREE_DATA_STREAM);
    job.order = 1;
    job.data.resourceManager.freeDataStream.pDataStream = &dataStream;
    failed += hardening_check_saturated_deferral(&fixture, &job, "free data-stream order deferral");

    dataStream.executionPointer = 0;
    job = ma_job_init(MA_JOB_TYPE_RESOURCE_MANAGER_PAGE_DATA_STREAM);
    job.order = 1;
    job.data.resourceManager.pageDataStream.pDataStream = &dataStream;
    failed += hardening_check_saturated_deferral(&fixture, &job, "page data-stream order deferral");

    dataStream.executionPointer = 0;
    job = ma_job_init(MA_JOB_TYPE_RESOURCE_MANAGER_SEEK_DATA_STREAM);
    job.order = 1;
    job.data.resourceManager.seekDataStream.pDataStream = &dataStream;
    failed += hardening_check_saturated_deferral(&fixture, &job, "seek data-stream order deferral");

    failed += hardening_resource_manager_fixture_uninit(&fixture, "saturated deferrals");
    return failed;
}

static int resource_manager_test_single_worker_shutdown_drains_deferral(void)
{
    hardening_resource_manager_fixture fixture;
    ma_resource_manager_data_buffer gateBuffer;
    ma_event enteredEvent;
    ma_event continueEvent;
    ma_job gateJob;
    ma_job refusedJob;
    ma_result quitResult;
    ma_result refusedResult;
    ma_result result;
    int failed;

    if (ma_event_init(&enteredEvent) != MA_SUCCESS) {
        return 1;
    }
    if (ma_event_init(&continueEvent) != MA_SUCCESS) {
        ma_event_uninit(&enteredEvent);
        return 1;
    }

    result = hardening_resource_manager_fixture_init_ex(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 2, 1, 0);
    if (result != MA_SUCCESS) {
        ma_event_uninit(&continueEvent);
        ma_event_uninit(&enteredEvent);
        return 1;
    }

    MA_ZERO_OBJECT(&gateBuffer);
    gateBuffer.pResourceManager = &fixture.resourceManager;
    gateBuffer.result = MA_UNAVAILABLE;
    gateJob = ma_job_init(MA_JOB_TYPE_RESOURCE_MANAGER_LOAD_DATA_BUFFER);
    gateJob.order = 1;
    gateJob.data.resourceManager.loadDataBuffer.pDataBuffer = &gateBuffer;

    MA_ZERO_OBJECT(&g_hardeningRetirementBarrier);
    g_hardeningRetirementBarrier.pDeferredDataBuffer = &gateBuffer;
    g_hardeningRetirementBarrier.pDeferralEnteredEvent = &enteredEvent;
    g_hardeningRetirementBarrier.pDeferralContinueEvent = &continueEvent;
    ma_atomic_store_32(&g_hardeningRetirementBarrier.enabled, MA_TRUE);

    result = ma_resource_manager_post_job(&fixture.resourceManager, &gateJob);
    if (result != MA_SUCCESS) {
        ma_atomic_store_32(&g_hardeningRetirementBarrier.enabled, MA_FALSE);
        MA_ZERO_OBJECT(&g_hardeningRetirementBarrier);
        ma_resource_manager_uninit(&fixture.resourceManager);
        ma_event_uninit(&continueEvent);
        ma_event_uninit(&enteredEvent);
        return 1;
    }
    ma_event_wait(&enteredEvent);
    quitResult = ma_resource_manager_post_job_quit(&fixture.resourceManager);
    refusedJob = ma_job_init(MA_JOB_TYPE_CUSTOM);
    refusedResult = ma_resource_manager_post_job(&fixture.resourceManager, &refusedJob);
    gateBuffer.executionPointer = 1;
    ma_event_signal(&continueEvent);

    ma_resource_manager_uninit(&fixture.resourceManager);
    ma_atomic_store_32(&g_hardeningRetirementBarrier.enabled, MA_FALSE);
    MA_ZERO_OBJECT(&g_hardeningRetirementBarrier);

    failed = 0;
    if (result != MA_SUCCESS || quitResult != MA_SUCCESS || refusedResult != MA_CANCELLED ||
        gateBuffer.executionPointer != 2 || fixture.allocations.liveAllocationCount != 0) {
        printf("single-worker shutdown was post=%d quit=%d refused=%d pointer=%u live=%lu\n",
            result, quitResult, refusedResult, gateBuffer.executionPointer,
            (unsigned long)fixture.allocations.liveAllocationCount);
        failed = 1;
    }

    ma_event_uninit(&continueEvent);
    ma_event_uninit(&enteredEvent);
    return failed;
}

static int resource_manager_test_two_worker_saturated_shutdown(void)
{
    hardening_resource_manager_fixture fixture;
    hardening_managed_worker_barrier barrier;
    ma_job firstJob;
    ma_job secondJob;
    ma_job refusedJob;
    ma_result firstPostResult;
    ma_result secondPostResult;
    ma_result quitResult;
    ma_result refusedResult;
    ma_result result;
    int failed;

    result = hardening_managed_worker_barrier_init(&barrier);
    if (result != MA_SUCCESS) {
        return 1;
    }
    result = hardening_resource_manager_fixture_init_ex(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 3, 2, 0);
    if (result != MA_SUCCESS) {
        hardening_managed_worker_barrier_uninit(&barrier);
        return 1;
    }

    firstJob = ma_job_init(MA_JOB_TYPE_CUSTOM);
    firstJob.data.custom.proc = hardening_managed_worker_barrier_job;
    firstJob.data.custom.data0 = (ma_uintptr)&barrier;
    firstJob.data.custom.data1 = 0;
    secondJob = firstJob;
    secondJob.data.custom.data1 = 1;

    firstPostResult = ma_resource_manager_post_job(&fixture.resourceManager, &firstJob);
    secondPostResult = ma_resource_manager_post_job(&fixture.resourceManager, &secondJob);
    if (firstPostResult != MA_SUCCESS || secondPostResult != MA_SUCCESS) {
        (void)ma_resource_manager_post_job_quit(&fixture.resourceManager);
        ma_event_signal(&barrier.continueEvents[0]);
        ma_event_signal(&barrier.continueEvents[1]);
        ma_resource_manager_uninit(&fixture.resourceManager);
        hardening_managed_worker_barrier_uninit(&barrier);
        return 1;
    }
    ma_event_wait(&barrier.enteredEvents[0]);
    ma_event_wait(&barrier.enteredEvents[1]);

    quitResult = ma_resource_manager_post_job_quit(&fixture.resourceManager);
    refusedJob = ma_job_init(MA_JOB_TYPE_CUSTOM);
    refusedResult = ma_resource_manager_post_job(&fixture.resourceManager, &refusedJob);
    ma_event_signal(&barrier.continueEvents[0]);
    ma_event_signal(&barrier.continueEvents[1]);
    ma_resource_manager_uninit(&fixture.resourceManager);

    failed = 0;
    if (firstPostResult != MA_SUCCESS || secondPostResult != MA_SUCCESS ||
        ma_atomic_load_32(&barrier.enteredCount) != 2 || ma_atomic_load_32(&barrier.completedCount) != 2 ||
        quitResult != MA_SUCCESS || refusedResult != MA_CANCELLED || fixture.allocations.liveAllocationCount != 0) {
        printf("two-worker shutdown was posts=%d/%d entered/completed=%u/%u quit=%d refused=%d live=%lu\n",
            firstPostResult, secondPostResult,
            ma_atomic_load_32(&barrier.enteredCount), ma_atomic_load_32(&barrier.completedCount),
            quitResult, refusedResult, (unsigned long)fixture.allocations.liveAllocationCount);
        failed = 1;
    }

    hardening_managed_worker_barrier_uninit(&barrier);
    return failed;
}

static int resource_manager_test_single_worker_nested_retirement(void)
{
    hardening_resource_manager_fixture fixture;
    hardening_buffer_uninit_thread_data uninitThreadData;
    ma_resource_manager_data_buffer gateBuffer;
    ma_resource_manager_data_buffer dataBuffer;
    ma_resource_manager_data_buffer_node* pNode;
    ma_event events[4];
    ma_thread uninitThread;
    ma_job gateJob;
    ma_job fillerJob;
    float externalFrames[2];
    ma_result fillerResult;
    ma_result overflowResult;
    ma_result result;
    ma_uint32 initializedEventCount;
    ma_uint32 iEvent;
    int failed;

    initializedEventCount = 0;
    for (iEvent = 0; iEvent < 4; iEvent += 1) {
        result = ma_event_init(&events[iEvent]);
        if (result != MA_SUCCESS) {
            while (initializedEventCount > 0) {
                initializedEventCount -= 1;
                ma_event_uninit(&events[initializedEventCount]);
            }
            return 1;
        }
        initializedEventCount += 1;
    }

    result = hardening_resource_manager_fixture_init_ex(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 3, 1, 0);
    if (result != MA_SUCCESS) {
        for (iEvent = 0; iEvent < 4; iEvent += 1) {
            ma_event_uninit(&events[iEvent]);
        }
        return 1;
    }

    externalFrames[0] = 0;
    externalFrames[1] = 0;
    result = ma_resource_manager_register_decoded_data(&fixture.resourceManager, "single-worker-nested", externalFrames, 2, ma_format_f32, 1, 48000);
    if (result == MA_SUCCESS) {
        result = ma_resource_manager_data_buffer_init(&fixture.resourceManager, "single-worker-nested", 0, NULL, &dataBuffer);
    }
    if (result == MA_SUCCESS) {
        result = ma_resource_manager_unregister_data(&fixture.resourceManager, "single-worker-nested");
    }
    if (result != MA_SUCCESS) {
        printf("single-worker nested-retirement setup failed: %d\n", result);
        ma_resource_manager_uninit(&fixture.resourceManager);
        for (iEvent = 0; iEvent < 4; iEvent += 1) {
            ma_event_uninit(&events[iEvent]);
        }
        return 1;
    }

    pNode = dataBuffer.pNode;
    dataBuffer.result = MA_BUSY;
    dataBuffer.executionPointer = 0;
    dataBuffer.executionCounter = 0;
    pNode->result = MA_BUSY;
    pNode->executionPointer = 0;
    pNode->executionCounter = 0;

    MA_ZERO_OBJECT(&gateBuffer);
    gateBuffer.pResourceManager = &fixture.resourceManager;
    gateBuffer.result = MA_UNAVAILABLE;
    gateJob = ma_job_init(MA_JOB_TYPE_RESOURCE_MANAGER_LOAD_DATA_BUFFER);
    gateJob.order = 1;
    gateJob.data.resourceManager.loadDataBuffer.pDataBuffer = &gateBuffer;

    MA_ZERO_OBJECT(&g_hardeningRetirementBarrier);
    g_hardeningRetirementBarrier.pDataBuffer = &dataBuffer;
    g_hardeningRetirementBarrier.pDataBufferNode = pNode;
    g_hardeningRetirementBarrier.pAsyncRetirementEvent = &events[0];
    g_hardeningRetirementBarrier.pAsyncRetirementPostedEvent = &events[1];
    g_hardeningRetirementBarrier.pDeferredDataBuffer = &gateBuffer;
    g_hardeningRetirementBarrier.pDeferralEnteredEvent = &events[2];
    g_hardeningRetirementBarrier.pDeferralContinueEvent = &events[3];
    ma_atomic_store_32(&g_hardeningRetirementBarrier.enabled, MA_TRUE);

    result = ma_resource_manager_post_job(&fixture.resourceManager, &gateJob);
    if (result != MA_SUCCESS) {
        ma_atomic_store_32(&g_hardeningRetirementBarrier.enabled, MA_FALSE);
        MA_ZERO_OBJECT(&g_hardeningRetirementBarrier);
        dataBuffer.result = MA_SUCCESS;
        pNode->result = MA_SUCCESS;
        (void)ma_resource_manager_data_buffer_uninit(&dataBuffer);
        ma_resource_manager_uninit(&fixture.resourceManager);
        for (iEvent = 0; iEvent < 4; iEvent += 1) {
            ma_event_uninit(&events[iEvent]);
        }
        return 1;
    }
    ma_event_wait(&events[2]);

    MA_ZERO_OBJECT(&uninitThreadData);
    uninitThreadData.pDataBuffer = &dataBuffer;
    result = ma_thread_create(&uninitThread, ma_thread_priority_normal, 0, hardening_buffer_uninit_thread_entry, &uninitThreadData, NULL);
    if (result != MA_SUCCESS) {
        gateBuffer.executionPointer = 1;
        ma_event_signal(&events[3]);
        ma_atomic_store_32(&g_hardeningRetirementBarrier.enabled, MA_FALSE);
        (void)ma_resource_manager_data_buffer_uninit(&dataBuffer);
        ma_resource_manager_uninit(&fixture.resourceManager);
        MA_ZERO_OBJECT(&g_hardeningRetirementBarrier);
        for (iEvent = 0; iEvent < 4; iEvent += 1) {
            ma_event_uninit(&events[iEvent]);
        }
        return 1;
    }

    ma_event_wait(&events[1]);
    fillerJob = ma_job_init(MA_JOB_TYPE_CUSTOM);
    fillerResult = ma_resource_manager_post_job(&fixture.resourceManager, &fillerJob);
    overflowResult = ma_resource_manager_post_job(&fixture.resourceManager, &fillerJob);
    gateBuffer.executionPointer = 1;
    ma_event_signal(&events[3]);
    ma_thread_wait(&uninitThread);

    failed = 0;
    if (fillerResult != MA_SUCCESS || overflowResult != MA_OUT_OF_MEMORY ||
        uninitThreadData.result != MA_SUCCESS || ma_atomic_load_32(&uninitThreadData.complete) != MA_TRUE ||
        ma_atomic_load_32(&g_hardeningRetirementBarrier.asyncBufferRetirementCount) != 1 ||
        ma_atomic_load_32(&g_hardeningRetirementBarrier.asyncNodeRetirementCount) != 1 ||
        dataBuffer.executionPointer != 1 || dataBuffer.executionCounter != 1 ||
        fixture.resourceManager.pRootDataBufferNode != NULL) {
        printf("single-worker nested retirement was filler=%d overflow=%d uninit=%d complete=%u hooks=%u/%u buffer=%u/%u root=%p\n",
            fillerResult, overflowResult, uninitThreadData.result, ma_atomic_load_32(&uninitThreadData.complete),
            ma_atomic_load_32(&g_hardeningRetirementBarrier.asyncBufferRetirementCount),
            ma_atomic_load_32(&g_hardeningRetirementBarrier.asyncNodeRetirementCount),
            dataBuffer.executionPointer, dataBuffer.executionCounter,
            (void*)fixture.resourceManager.pRootDataBufferNode);
        failed = 1;
    }

    ma_resource_manager_uninit(&fixture.resourceManager);
    ma_atomic_store_32(&g_hardeningRetirementBarrier.enabled, MA_FALSE);
    MA_ZERO_OBJECT(&g_hardeningRetirementBarrier);
    if (fixture.allocations.liveAllocationCount != 0) {
        printf("single-worker nested retirement leaked %lu tracked allocations\n", (unsigned long)fixture.allocations.liveAllocationCount);
        failed = 1;
    }

    for (iEvent = 0; iEvent < 4; iEvent += 1) {
        ma_event_uninit(&events[iEvent]);
    }
    return failed;
}

static int resource_manager_test_execution_order_wrap(void)
{
    hardening_resource_manager_fixture fixture;
    ma_job job;
    ma_job first;
    ma_job second;
    volatile ma_uint32 executionCounter;
    ma_result result;
    int failed;

    failed = 0;
    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 4);
    if (result != MA_SUCCESS) {
        return 1;
    }

    executionCounter = 0xFFFFFFFF;
    job = ma_job_init(MA_JOB_TYPE_CUSTOM);
    MA_ZERO_OBJECT(&first);
    MA_ZERO_OBJECT(&second);
    if (ma_resource_manager_post_job_with_execution_order(&fixture.resourceManager, &job, &executionCounter) != MA_SUCCESS ||
        ma_resource_manager_post_job_with_execution_order(&fixture.resourceManager, &job, &executionCounter) != MA_SUCCESS ||
        ma_resource_manager_next_job(&fixture.resourceManager, &first) != MA_SUCCESS ||
        ma_resource_manager_next_job(&fixture.resourceManager, &second) != MA_SUCCESS ||
        first.order != 0xFFFFFFFF || second.order != 0 || executionCounter != 1) {
        printf("execution-order wrap was first=%u second=%u counter=%u\n", first.order, second.order, executionCounter);
        failed = 1;
    }

    if (first.toc.breakup.code == MA_JOB_TYPE_CUSTOM) {
        (void)ma_job_process(&first);
    }
    if (second.toc.breakup.code == MA_JOB_TYPE_CUSTOM) {
        (void)ma_job_process(&second);
    }

    failed += hardening_resource_manager_fixture_uninit(&fixture, "execution-order wrap");
    return failed;
}

static int resource_manager_test_saturated_last_reference_retirement(void)
{
    hardening_resource_manager_fixture fixture;
    ma_result freeResult;
    ma_result loadResult;
    ma_result result;
    int failed;

    failed = 0;
    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 2);
    if (result != MA_SUCCESS) {
        return 1;
    }

    result = ma_resource_manager_register_file(&fixture.resourceManager, "saturated-last-reference.custom",
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC | MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE);
    if (result != MA_SUCCESS || ma_resource_manager_unregister_file(&fixture.resourceManager, "saturated-last-reference.custom") != MA_SUCCESS ||
        fixture.resourceManager.pRootDataBufferNode != NULL) {
        printf("saturated last-reference unregister failed: %d\n", result);
        failed = 1;
    } else {
        loadResult = ma_resource_manager_process_next_job(&fixture.resourceManager);
        freeResult = ma_resource_manager_process_next_job(&fixture.resourceManager);
        if (loadResult != MA_UNAVAILABLE || freeResult != MA_SUCCESS) {
            printf("saturated last-reference jobs were load=%d free=%d\n", loadResult, freeResult);
            failed = 1;
        }
    }
    if (fixture.backendState.initCount != 0 || fixture.backendState.uninitCount != 0 ||
        fixture.vfs.openCount != 0 || fixture.vfs.closeCount != 0 ||
        fixture.allocations.liveAllocationCount != fixture.baselineLiveAllocationCount) {
        printf("saturated last-reference ownership was unbalanced\n");
        failed = 1;
    }

    failed += hardening_resource_manager_fixture_uninit(&fixture, "saturated last-reference");
    return failed;
}

static int resource_manager_test_saturated_buffer_uninit_retirement(void)
{
    hardening_resource_manager_fixture fixture;
    hardening_buffer_uninit_thread_data uninitThreadData;
    ma_resource_manager_data_buffer dataBuffer;
    ma_thread uninitThread;
    ma_event retirementEvent;
    ma_event postedEvent;
    ma_job loadJob;
    ma_result freeResult;
    ma_result loadResult;
    float externalFrames[2];
    ma_result result;
    int failed;

    failed = 0;
    result = hardening_resource_manager_fixture_init(&fixture, MA_SUCCESS, 13, MA_SUCCESS, 2);
    if (result != MA_SUCCESS) {
        return 1;
    }
    externalFrames[0] = 0;
    externalFrames[1] = 0;
    result = ma_resource_manager_register_decoded_data(&fixture.resourceManager, "saturated-buffer", externalFrames, 2, ma_format_f32, 1, 48000);
    if (result == MA_SUCCESS) {
        result = ma_resource_manager_data_buffer_init(&fixture.resourceManager, "saturated-buffer", 0, NULL, &dataBuffer);
    }
    if (result != MA_SUCCESS || ma_event_init(&retirementEvent) != MA_SUCCESS || ma_event_init(&postedEvent) != MA_SUCCESS) {
        printf("saturated buffer setup failed: %d\n", result);
        hardening_resource_manager_fixture_uninit(&fixture, "saturated buffer");
        return 1;
    }

    dataBuffer.result = MA_BUSY;
    dataBuffer.executionPointer = 0;
    dataBuffer.executionCounter = 1;
    loadJob = ma_job_init(MA_JOB_TYPE_RESOURCE_MANAGER_LOAD_DATA_BUFFER);
    loadJob.order = 0;
    loadJob.data.resourceManager.loadDataBuffer.pDataBuffer = &dataBuffer;
    if (ma_resource_manager_post_job(&fixture.resourceManager, &loadJob) != MA_SUCCESS) {
        printf("saturated buffer could not fill queue\n");
        failed = 1;
        goto done;
    }

    MA_ZERO_OBJECT(&uninitThreadData);
    MA_ZERO_OBJECT(&g_hardeningRetirementBarrier);
    g_hardeningRetirementBarrier.pDataBuffer = &dataBuffer;
    g_hardeningRetirementBarrier.pAsyncRetirementEvent = &retirementEvent;
    g_hardeningRetirementBarrier.pAsyncRetirementPostedEvent = &postedEvent;
    ma_atomic_store_32(&g_hardeningRetirementBarrier.enabled, MA_TRUE);
    uninitThreadData.pDataBuffer = &dataBuffer;
    result = ma_thread_create(&uninitThread, ma_thread_priority_normal, 0, hardening_buffer_uninit_thread_entry, &uninitThreadData, NULL);
    if (result != MA_SUCCESS) {
        printf("saturated buffer uninit thread failed: %d\n", result);
        failed = 1;
        goto done;
    }
    ma_event_wait(&retirementEvent);
    ma_event_wait(&postedEvent);
    loadResult = ma_resource_manager_process_next_job(&fixture.resourceManager);
    freeResult = ma_resource_manager_process_next_job(&fixture.resourceManager);
    ma_thread_wait(&uninitThread);
    if (loadResult != MA_UNAVAILABLE || freeResult != MA_SUCCESS || uninitThreadData.result != MA_SUCCESS ||
        ma_atomic_load_32(&uninitThreadData.complete) != MA_TRUE ||
        dataBuffer.executionPointer != 2 || dataBuffer.executionCounter != 2) {
        printf("saturated buffer did not retire: load=%d free=%d uninit=%d pointer/counter=%u/%u\n",
            loadResult, freeResult, uninitThreadData.result, dataBuffer.executionPointer, dataBuffer.executionCounter);
        failed = 1;
    }

done:
    ma_atomic_store_32(&g_hardeningRetirementBarrier.enabled, MA_FALSE);
    MA_ZERO_OBJECT(&g_hardeningRetirementBarrier);
    ma_event_uninit(&postedEvent);
    ma_event_uninit(&retirementEvent);
    if (ma_resource_manager_unregister_data(&fixture.resourceManager, "saturated-buffer") != MA_SUCCESS ||
        fixture.allocations.liveAllocationCount != fixture.baselineLiveAllocationCount) {
        printf("saturated buffer final ownership was unbalanced\n");
        failed = 1;
    }
    failed += hardening_resource_manager_fixture_uninit(&fixture, "saturated buffer");
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

static int resource_manager_test_job_queue_rejects_null_heap(void)
{
    ma_job_queue_config config;
    ma_job_queue queue;
    ma_result result;

    MA_ZERO_OBJECT(&queue);
    config = ma_job_queue_config_init(MA_JOB_QUEUE_FLAG_NON_BLOCKING, 4);
    result = ma_job_queue_init_preallocated(&config, NULL, &queue);
    if (result != MA_INVALID_ARGS || queue._pHeap != NULL || queue.pJobs != NULL) {
        printf("preallocated job queue accepted null heap: result=%d heap=%p jobs=%p\n",
            result, queue._pHeap, (void*)queue.pJobs);
        return 1;
    }

    return 0;
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
    failed += resource_manager_test_saturated_data_stream_uninit_retirement();
    failed += resource_manager_test_data_stream_load_failure(MA_NOT_IMPLEMENTED, MA_FALSE, MA_NOT_IMPLEMENTED, "data stream length failure");
    failed += resource_manager_test_data_stream_load_failure(MA_SUCCESS, MA_TRUE, MA_OUT_OF_MEMORY, "data stream page allocation failure");
    failed += resource_manager_test_data_stream_notification_after_retirement();
    failed += resource_manager_test_data_buffer_notifications_after_retirement();
    failed += resource_manager_test_data_buffer_node_notifications_after_retirement();
    failed += resource_manager_test_data_buffer_result_polling_defers_uninit();
    failed += resource_manager_test_node_result_polling_defers_last_release();
    failed += resource_manager_test_wait_init_requeue_failure_releases_fences();
    failed += resource_manager_test_decoder_length_failure_cleanup();
    failed += resource_manager_test_mid_decode_failure_cleanup(MA_FALSE, "flat-mid-decode-failure.custom");
    failed += resource_manager_test_mid_decode_failure_cleanup(MA_TRUE, "paged-mid-decode-failure.custom");
    failed += resource_manager_test_decoded_allocation_failure_cleanup();
    failed += resource_manager_test_page_post_failure_cleanup();
    failed += resource_manager_test_saturated_initial_page_continuation();
    failed += resource_manager_test_saturated_page_continuation();
    failed += resource_manager_test_all_saturated_deferral_sites();
    failed += resource_manager_test_execution_order_wrap();
    failed += resource_manager_test_saturated_last_reference_retirement();
    failed += resource_manager_test_saturated_buffer_uninit_retirement();
    failed += resource_manager_test_single_worker_shutdown_drains_deferral();
    failed += resource_manager_test_two_worker_saturated_shutdown();
    failed += resource_manager_test_single_worker_nested_retirement();
    failed += resource_manager_test_custom_vfs_failure_cleanup();
    failed += resource_manager_test_external_decoded_storage_ownership();
    failed += resource_manager_test_shared_synchronous_failure();
    failed += resource_manager_test_job_queue_rejects_null_heap();

    return failed != 0;
}
