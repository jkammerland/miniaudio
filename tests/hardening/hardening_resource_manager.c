#define MA_NO_DEVICE_IO
#include "../../miniaudio.c"

#include <stdio.h>

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
    failed += resource_manager_test_shared_synchronous_failure();

    return failed != 0;
}
