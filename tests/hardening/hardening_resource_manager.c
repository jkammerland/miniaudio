#define MA_NO_DEVICE_IO
#include "../../miniaudio.c"

#include <stdio.h>

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

int main(void)
{
    int failed = 0;

    failed += resource_manager_test_failed_acquire(MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE, "synchronous acquire");
    failed += resource_manager_test_failed_acquire(
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE |
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC |
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_WAIT_INIT,
        "wait-init acquire");

    return failed != 0;
}
