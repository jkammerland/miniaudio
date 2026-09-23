#include "../../external/fs/fs.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#error This test is Windows-only.
#endif

#define HARDENING_PATH_CAPACITY 1024
#define HARDENING_SEGMENT_LENGTH 120

typedef struct
{
    size_t liveCount;
    size_t allocationCount;
    size_t freeCount;
} hardening_allocator_state;

static void* hardening_malloc(size_t sizeInBytes, void* pUserData)
{
    hardening_allocator_state* pState = (hardening_allocator_state*)pUserData;
    void* pMemory = malloc(sizeInBytes);

    if (pMemory != NULL) {
        pState->liveCount += 1;
        pState->allocationCount += 1;
    }

    return pMemory;
}

static void* hardening_realloc(void* pMemory, size_t sizeInBytes, void* pUserData)
{
    hardening_allocator_state* pState = (hardening_allocator_state*)pUserData;
    void* pNewMemory;

    if (pMemory == NULL) {
        return hardening_malloc(sizeInBytes, pUserData);
    }
    if (sizeInBytes == 0) {
        free(pMemory);
        pState->liveCount -= 1;
        pState->freeCount += 1;
        return NULL;
    }

    pNewMemory = realloc(pMemory, sizeInBytes);
    return pNewMemory;
}

static void hardening_free(void* pMemory, void* pUserData)
{
    hardening_allocator_state* pState = (hardening_allocator_state*)pUserData;

    if (pMemory == NULL) {
        return;
    }

    free(pMemory);
    pState->liveCount -= 1;
    pState->freeCount += 1;
}

static int hardening_path_append(char* pPath, size_t pathCapacity, const char* pSuffix)
{
    size_t pathLength = strlen(pPath);
    size_t suffixLength = strlen(pSuffix);

    if (pathLength + suffixLength + 1 > pathCapacity) {
        return 0;
    }

    memcpy(pPath + pathLength, pSuffix, suffixLength + 1);
    return 1;
}

int main(void)
{
    hardening_allocator_state allocatorState;
    fs_allocation_callbacks allocationCallbacks;
    fs_config config;
    fs* pFS;
    fs_result result;
    char temporaryDirectory[MAX_PATH];
    char temporaryFile[MAX_PATH];
    char rootPath[HARDENING_PATH_CAPACITY];
    char firstChildPath[HARDENING_PATH_CAPACITY];
    char secondChildPath[HARDENING_PATH_CAPACITY];
    char firstSegment[HARDENING_SEGMENT_LENGTH + 1];
    char secondSegment[HARDENING_SEGMENT_LENGTH + 1];
    DWORD temporaryDirectoryLength;
    size_t liveCountBeforeRemove;
    size_t i;
    int temporaryFileCreated;
    int rootCreated;
    int firstChildCreated;
    int secondChildCreated;
    int failed;

    memset(&allocatorState, 0, sizeof(allocatorState));
    allocationCallbacks.pUserData = &allocatorState;
    allocationCallbacks.onMalloc = hardening_malloc;
    allocationCallbacks.onRealloc = hardening_realloc;
    allocationCallbacks.onFree = hardening_free;

    config = fs_config_init_default();
    config.pAllocationCallbacks = &allocationCallbacks;
    pFS = NULL;
    result = fs_init(&config, &pFS);
    if (result != FS_SUCCESS) {
        printf("fs_init failed: %d\n", result);
        return 1;
    }

    temporaryFileCreated = 0;
    rootCreated = 0;
    firstChildCreated = 0;
    secondChildCreated = 0;
    failed = 0;

    temporaryDirectoryLength = GetTempPathA((DWORD)sizeof(temporaryDirectory), temporaryDirectory);
    if (temporaryDirectoryLength == 0 || temporaryDirectoryLength >= sizeof(temporaryDirectory)) {
        printf("GetTempPathA failed: %lu\n", (unsigned long)GetLastError());
        failed = 1;
        goto done;
    }
    if (GetTempFileNameA(temporaryDirectory, "mah", 0, temporaryFile) == 0) {
        printf("GetTempFileNameA failed: %lu\n", (unsigned long)GetLastError());
        failed = 1;
        goto done;
    }
    temporaryFileCreated = 1;
    if (DeleteFileA(temporaryFile) == 0) {
        printf("DeleteFileA failed: %lu\n", (unsigned long)GetLastError());
        failed = 1;
        goto done;
    }
    temporaryFileCreated = 0;

    rootPath[0] = '\0';
    if (!hardening_path_append(rootPath, sizeof(rootPath), "\\\\?\\") ||
        !hardening_path_append(rootPath, sizeof(rootPath), temporaryFile)) {
        failed = 1;
        goto done;
    }

    for (i = 0; i < HARDENING_SEGMENT_LENGTH; i += 1) {
        firstSegment[i] = (char)('a' + (i % 26));
        secondSegment[i] = (char)('z' - (i % 26));
    }
    firstSegment[HARDENING_SEGMENT_LENGTH] = '\0';
    secondSegment[HARDENING_SEGMENT_LENGTH] = '\0';

    memcpy(firstChildPath, rootPath, strlen(rootPath) + 1);
    if (!hardening_path_append(firstChildPath, sizeof(firstChildPath), "\\") ||
        !hardening_path_append(firstChildPath, sizeof(firstChildPath), firstSegment)) {
        failed = 1;
        goto done;
    }
    memcpy(secondChildPath, firstChildPath, strlen(firstChildPath) + 1);
    if (!hardening_path_append(secondChildPath, sizeof(secondChildPath), "\\") ||
        !hardening_path_append(secondChildPath, sizeof(secondChildPath), secondSegment) ||
        strlen(secondChildPath) < 256) {
        printf("long-path fixture was too short\n");
        failed = 1;
        goto done;
    }

    result = fs_mkdir(pFS, rootPath, FS_IGNORE_MOUNTS);
    if (result != FS_SUCCESS) {
        printf("root mkdir failed: %d\n", result);
        failed = 1;
        goto done;
    }
    rootCreated = 1;
    result = fs_mkdir(pFS, firstChildPath, FS_IGNORE_MOUNTS);
    if (result != FS_SUCCESS) {
        printf("first-child mkdir failed: %d\n", result);
        failed = 1;
        goto done;
    }
    firstChildCreated = 1;
    result = fs_mkdir(pFS, secondChildPath, FS_IGNORE_MOUNTS);
    if (result != FS_SUCCESS) {
        printf("second-child mkdir failed: %d\n", result);
        failed = 1;
        goto done;
    }
    secondChildCreated = 1;

    liveCountBeforeRemove = allocatorState.liveCount;
    result = fs_remove(pFS, secondChildPath, FS_IGNORE_MOUNTS);
    if (result != FS_SUCCESS) {
        printf("long-directory remove failed: %d\n", result);
        failed = 1;
    } else {
        secondChildCreated = 0;
    }
    if (allocatorState.liveCount != liveCountBeforeRemove) {
        printf("long-directory remove leaked path storage: before=%lu after=%lu\n",
            (unsigned long)liveCountBeforeRemove, (unsigned long)allocatorState.liveCount);
        failed = 1;
    }

done:
    if (secondChildCreated) {
        (void)fs_remove(pFS, secondChildPath, FS_IGNORE_MOUNTS);
    }
    if (firstChildCreated) {
        (void)fs_remove(pFS, firstChildPath, FS_IGNORE_MOUNTS);
    }
    if (rootCreated) {
        (void)fs_remove(pFS, rootPath, FS_IGNORE_MOUNTS);
    }
    if (temporaryFileCreated) {
        (void)DeleteFileA(temporaryFile);
    }

    fs_uninit(pFS);
    if (allocatorState.liveCount != 0 || allocatorState.allocationCount != allocatorState.freeCount) {
        printf("allocator imbalance: live=%lu allocations=%lu frees=%lu\n",
            (unsigned long)allocatorState.liveCount,
            (unsigned long)allocatorState.allocationCount,
            (unsigned long)allocatorState.freeCount);
        failed = 1;
    }

    return failed;
}
