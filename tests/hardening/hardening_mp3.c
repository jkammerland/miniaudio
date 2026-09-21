#define MA_NO_DEVICE_IO
#include "../../miniaudio.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Original issue #1117 attachment, bad-length.mp3.
   SHA-256: def360067fce53bb73425193784e073216f358a158eaa7ecfe3d06241555e14a. */
#include "hardening_mp3_1117.inc"

static const ma_uint8 g_mp3_1128[] = {
    0x49,0xff,0xe3,0x00,0xff,0x00,0x00,0x00,0x6c,0xff,0x10,0xff,
    0x6c,0xff,0x10,0xff,0xe3,0x00,0x10,0x00,0x00,0x00,0xe3,0x00,
    0x10,0x00,0x00,0x00,0x41,0xff,0xe3,0x01,0x0d,0x0c,0xff
};

typedef struct
{
    size_t largestRequest;
    ma_uint32 allocationCount;
    ma_uint32 freeCount;
    ma_uint32 liveAllocationCount;
} mp3_allocator_state;

typedef struct
{
    ma_vfs_callbacks callbacks;
    const ma_uint8* pData;
    size_t dataSize;
    size_t cursor;
} mp3_memory_vfs;

static void* mp3_malloc(size_t size, void* pUserData)
{
    mp3_allocator_state* pState = (mp3_allocator_state*)pUserData;
    if (size > pState->largestRequest) {
        pState->largestRequest = size;
    }
    if (size > 1024 * 1024) {
        return NULL;
    }
    {
        void* pMemory = malloc(size);
        if (pMemory != NULL) {
            pState->allocationCount += 1;
            pState->liveAllocationCount += 1;
        }
        return pMemory;
    }
}

static void* mp3_realloc(void* pMemory, size_t size, void* pUserData)
{
    mp3_allocator_state* pState = (mp3_allocator_state*)pUserData;
    void* pNewMemory;
    ma_bool32 wasNull = pMemory == NULL;

    if (size > pState->largestRequest) {
        pState->largestRequest = size;
    }
    if (size > 1024 * 1024) {
        return NULL;
    }
    if (size == 0) {
        if (pMemory != NULL) {
            pState->freeCount += 1;
            pState->liveAllocationCount -= 1;
            free(pMemory);
        }
        return NULL;
    }

    pNewMemory = realloc(pMemory, size);
    if (pNewMemory != NULL && wasNull) {
        pState->allocationCount += 1;
        pState->liveAllocationCount += 1;
    }
    return pNewMemory;
}

static void mp3_free(void* pMemory, void* pUserData)
{
    mp3_allocator_state* pState = (mp3_allocator_state*)pUserData;

    if (pMemory != NULL) {
        pState->freeCount += 1;
        pState->liveAllocationCount -= 1;
    }
    free(pMemory);
}

static ma_result mp3_memory_vfs_on_open(ma_vfs* pVFS, const char* pFilePath, ma_uint32 openMode, ma_vfs_file* pFile)
{
    mp3_memory_vfs* pMemoryVFS = (mp3_memory_vfs*)pVFS;

    (void)pFilePath;
    if ((openMode & MA_OPEN_MODE_READ) == 0) {
        return MA_INVALID_OPERATION;
    }

    pMemoryVFS->cursor = 0;
    *pFile = (ma_vfs_file)pMemoryVFS;
    return MA_SUCCESS;
}

static ma_result mp3_memory_vfs_on_close(ma_vfs* pVFS, ma_vfs_file file)
{
    (void)pVFS;
    (void)file;
    return MA_SUCCESS;
}

static ma_result mp3_memory_vfs_on_read(ma_vfs* pVFS, ma_vfs_file file, void* pDst, size_t sizeInBytes, size_t* pBytesRead)
{
    mp3_memory_vfs* pMemoryVFS = (mp3_memory_vfs*)pVFS;
    size_t bytesRemaining;

    (void)file;
    bytesRemaining = pMemoryVFS->dataSize - pMemoryVFS->cursor;
    if (sizeInBytes > bytesRemaining) {
        sizeInBytes = bytesRemaining;
    }

    if (sizeInBytes > 0) {
        memcpy(pDst, pMemoryVFS->pData + pMemoryVFS->cursor, sizeInBytes);
        pMemoryVFS->cursor += sizeInBytes;
    }
    if (pBytesRead != NULL) {
        *pBytesRead = sizeInBytes;
    }

    return MA_SUCCESS;
}

static ma_result mp3_memory_vfs_on_seek(ma_vfs* pVFS, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin)
{
    mp3_memory_vfs* pMemoryVFS = (mp3_memory_vfs*)pVFS;
    ma_int64 base;

    (void)file;
    if (origin == ma_seek_origin_start) {
        base = 0;
    } else if (origin == ma_seek_origin_current) {
        base = (ma_int64)pMemoryVFS->cursor;
    } else if (origin == ma_seek_origin_end) {
        base = (ma_int64)pMemoryVFS->dataSize;
    } else {
        return MA_INVALID_ARGS;
    }

    if (offset < -base || offset > (ma_int64)pMemoryVFS->dataSize - base) {
        return MA_BAD_SEEK;
    }

    pMemoryVFS->cursor = (size_t)(base + offset);
    return MA_SUCCESS;
}

static ma_result mp3_memory_vfs_on_tell(ma_vfs* pVFS, ma_vfs_file file, ma_int64* pCursor)
{
    mp3_memory_vfs* pMemoryVFS = (mp3_memory_vfs*)pVFS;

    (void)file;
    *pCursor = (ma_int64)pMemoryVFS->cursor;
    return MA_SUCCESS;
}

static int mp3_test_truncated_metadata(void)
{
    mp3_allocator_state state;
    ma_decoder_config config;
    ma_decoder decoder;
    ma_format format;
    ma_uint32 channels;
    ma_uint32 sampleRate;
    ma_uint64 framesRead;
    float frames[64];
    ma_result result;
    int failed;

    failed = 0;
    MA_ZERO_OBJECT(&state);
    config = ma_decoder_config_init_default();
    config.encodingFormat = ma_encoding_format_mp3;
    config.allocationCallbacks.pUserData = &state;
    config.allocationCallbacks.onMalloc = mp3_malloc;
    config.allocationCallbacks.onRealloc = mp3_realloc;
    config.allocationCallbacks.onFree = mp3_free;

    result = ma_decoder_init_memory(g_mp3_1128, sizeof(g_mp3_1128), &config, &decoder);
    if (result != MA_SUCCESS) {
        printf("issue 1128 truncated metadata returned %d instead of ignoring the incomplete optional tag\n", result);
        if (state.liveAllocationCount != 0 || state.allocationCount != state.freeCount) {
            printf("issue 1128 failed initialization leaked allocations: allocated=%u freed=%u live=%u\n",
                state.allocationCount,
                state.freeCount,
                state.liveAllocationCount);
        }
        return 1;
    }

    result = ma_decoder_get_data_format(&decoder, &format, &channels, &sampleRate, NULL, 0);
    if (result != MA_SUCCESS || format != ma_format_f32 || channels != 1 || sampleRate != 11025) {
        printf("issue 1128 selected unexpected decoder format: result=%d format=%d channels=%u rate=%u\n",
            result,
            format,
            channels,
            sampleRate);
        failed = 1;
    }

    framesRead = 0;
    result = ma_decoder_read_pcm_frames(&decoder, frames, 64, &framesRead);
    if (result != MA_SUCCESS || framesRead != 64) {
        printf("issue 1128 bounded MP3 decode returned %d with %u:%08u frames\n",
            result,
            (ma_uint32)(framesRead >> 32),
            (ma_uint32)framesRead);
        failed = 1;
    }

    result = ma_decoder_uninit(&decoder);
    if (result != MA_SUCCESS) {
        printf("issue 1128 decoder uninit returned %d\n", result);
        failed = 1;
    }
    if (state.allocationCount == 0 || state.liveAllocationCount != 0 || state.allocationCount != state.freeCount) {
        printf("issue 1128 allocation lifetime was allocated=%u freed=%u live=%u\n",
            state.allocationCount,
            state.freeCount,
            state.liveAllocationCount);
        failed = 1;
    }
    if (state.largestRequest > 1024 * 1024) {
        printf("issue 1128 requested an oversized allocation of %lu bytes\n", (unsigned long)state.largestRequest);
        failed = 1;
    }
    return failed;
}

static int mp3_test_frame_count(void)
{
    const ma_uint64 declaredFrameCount = (ma_uint64)0x73000002 * 1152;
    const ma_uint64 expectedRawFrameCount = 2304;
    const ma_uint64 expectedAudibleFrameCount = 210;
    float frames[32 * 2];
    ma_uint64 actualFrameCount;
    ma_dr_mp3 mp3;

    if (!ma_dr_mp3_init_memory(&mp3, g_mp3_1117, sizeof(g_mp3_1117), NULL)) {
        printf("issue 1117 fixture failed to initialize\n");
        return 1;
    }
    if (mp3.totalPCMFrameCount != declaredFrameCount) {
        printf("MP3 declared frame count was %u:%08u, expected %u:%08u\n",
            (ma_uint32)(mp3.totalPCMFrameCount >> 32), (ma_uint32)mp3.totalPCMFrameCount,
            (ma_uint32)(declaredFrameCount >> 32), (ma_uint32)declaredFrameCount);
        ma_dr_mp3_uninit(&mp3);
        return 1;
    }

    actualFrameCount = ma_dr_mp3_get_pcm_frame_count(&mp3);
    if (actualFrameCount != expectedAudibleFrameCount || mp3.totalPCMFrameCount != expectedRawFrameCount) {
        printf("MP3 validated frame count was %u:%08u (raw %u:%08u), expected %u (raw %u)\n",
            (ma_uint32)(actualFrameCount >> 32), (ma_uint32)actualFrameCount,
            (ma_uint32)(mp3.totalPCMFrameCount >> 32), (ma_uint32)mp3.totalPCMFrameCount,
            (ma_uint32)expectedAudibleFrameCount, (ma_uint32)expectedRawFrameCount);
        ma_dr_mp3_uninit(&mp3);
        return 1;
    }

    if (ma_dr_mp3_read_pcm_frames_f32(&mp3, 32, frames) != 32) {
        printf("MP3 failed to advance before a repeated length query\n");
        ma_dr_mp3_uninit(&mp3);
        return 1;
    }
    {
        ma_uint64 cursorBeforeLengthQuery = mp3.currentPCMFrame;
        actualFrameCount = ma_dr_mp3_get_pcm_frame_count(&mp3);
        if (actualFrameCount != expectedAudibleFrameCount || mp3.currentPCMFrame != cursorBeforeLengthQuery) {
            printf("MP3 repeated length query changed cursor from %u:%08u to %u:%08u (length %u:%08u)\n",
                (ma_uint32)(cursorBeforeLengthQuery >> 32), (ma_uint32)cursorBeforeLengthQuery,
                (ma_uint32)(mp3.currentPCMFrame >> 32), (ma_uint32)mp3.currentPCMFrame,
                (ma_uint32)(actualFrameCount >> 32), (ma_uint32)actualFrameCount);
            ma_dr_mp3_uninit(&mp3);
            return 1;
        }
    }
    ma_dr_mp3_uninit(&mp3);
    return 0;
}

static int mp3_test_full_decode_allocation(void)
{
    mp3_allocator_state state;
    ma_decoder_config config;
    ma_uint64 frameCount = 0;
    void* pFrames = NULL;
    ma_result result;

    MA_ZERO_OBJECT(&state);
    config = ma_decoder_config_init_default();
    config.allocationCallbacks.pUserData = &state;
    config.allocationCallbacks.onMalloc = mp3_malloc;
    config.allocationCallbacks.onRealloc = mp3_realloc;
    config.allocationCallbacks.onFree = mp3_free;

    result = ma_decode_memory(g_mp3_1117, sizeof(g_mp3_1117), &config, &frameCount, &pFrames);
    if (pFrames != NULL) {
        ma_free(pFrames, &config.allocationCallbacks);
    }
    if (result != MA_SUCCESS || frameCount == 0) {
        printf("issue 1117 fixture failed full decode: result=%d frames=%u:%08u\n", result, (ma_uint32)(frameCount >> 32), (ma_uint32)frameCount);
        return 1;
    }
    if (state.largestRequest > 1024 * 1024) {
        printf("hostile MP3 length requested %lu bytes\n", (unsigned long)state.largestRequest);
        return 1;
    }
    if (state.liveAllocationCount != 0 || state.allocationCount != state.freeCount) {
        printf("hostile MP3 full decode leaked allocations: allocated=%u freed=%u live=%u\n",
            state.allocationCount,
            state.freeCount,
            state.liveAllocationCount);
        return 1;
    }
    return 0;
}

static int mp3_test_resource_manager_allocation(void)
{
    const ma_uint64 expectedAudibleFrameCount = 210;
    mp3_allocator_state state;
    mp3_memory_vfs vfs;
    ma_uint8 hostileData[sizeof(g_mp3_1117)];
    ma_resource_manager_config config;
    ma_resource_manager resourceManager;
    ma_resource_manager_data_buffer dataBuffer;
    ma_uint64 frameCount = 0;
    ma_bool32 resourceManagerInitialized = MA_FALSE;
    ma_bool32 dataBufferInitialized = MA_FALSE;
    ma_result result;
    int failed = 0;

    MA_ZERO_OBJECT(&state);
    MA_ZERO_OBJECT(&vfs);
    memcpy(hostileData, g_mp3_1117, sizeof(hostileData));
    /* Keep the forged allocation below the header's conservative 32-bit MA_SIZE_MAX fallback. */
    hostileData[44] = 0x00;
    hostileData[45] = 0x01;
    hostileData[46] = 0x00;
    hostileData[47] = 0x00;
    vfs.callbacks.onOpen  = mp3_memory_vfs_on_open;
    vfs.callbacks.onClose = mp3_memory_vfs_on_close;
    vfs.callbacks.onRead  = mp3_memory_vfs_on_read;
    vfs.callbacks.onSeek  = mp3_memory_vfs_on_seek;
    vfs.callbacks.onTell  = mp3_memory_vfs_on_tell;
    vfs.pData = hostileData;
    vfs.dataSize = sizeof(hostileData);

    config = ma_resource_manager_config_init();
    config.allocationCallbacks.pUserData = &state;
    config.allocationCallbacks.onMalloc  = mp3_malloc;
    config.allocationCallbacks.onRealloc = mp3_realloc;
    config.allocationCallbacks.onFree    = mp3_free;
    config.pVFS = (ma_vfs*)&vfs;
    config.jobThreadCount = 0;
    config.jobQueueCapacity = 8;
    config.flags = MA_RESOURCE_MANAGER_FLAG_NO_THREADING;

    result = ma_resource_manager_init(&config, &resourceManager);
    if (result != MA_SUCCESS) {
        printf("resource manager init failed: %d\n", result);
        return 1;
    }
    resourceManagerInitialized = MA_TRUE;

    result = ma_resource_manager_data_buffer_init(
        &resourceManager,
        "hostile-xing.mp3",
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE,
        NULL,
        &dataBuffer);
    if (result != MA_SUCCESS) {
        printf("resource manager hostile MP3 decode failed: %d (largest allocation %lu)\n", result, (unsigned long)state.largestRequest);
        failed = 1;
        goto done;
    }
    dataBufferInitialized = MA_TRUE;

    result = ma_resource_manager_data_buffer_get_length_in_pcm_frames(&dataBuffer, &frameCount);
    if (result != MA_SUCCESS || frameCount != expectedAudibleFrameCount) {
        printf("resource manager hostile MP3 length was %u:%08u, expected %u (result %d)\n",
            (ma_uint32)(frameCount >> 32), (ma_uint32)frameCount,
            (ma_uint32)expectedAudibleFrameCount, result);
        failed = 1;
    }
    if (state.largestRequest > 1024 * 1024) {
        printf("resource manager trusted hostile MP3 length for a %lu-byte allocation\n", (unsigned long)state.largestRequest);
        failed = 1;
    }

done:
    if (dataBufferInitialized) {
        ma_resource_manager_data_buffer_uninit(&dataBuffer);
    }
    if (resourceManagerInitialized) {
        ma_resource_manager_uninit(&resourceManager);
    }
    if (state.liveAllocationCount != 0 || state.allocationCount != state.freeCount) {
        printf("resource manager hostile MP3 leaked allocations: allocated=%u freed=%u live=%u\n",
            state.allocationCount,
            state.freeCount,
            state.liveAllocationCount);
        failed = 1;
    }

    return failed;
}

int main(void)
{
    int failed = 0;
    failed += mp3_test_truncated_metadata();
    failed += mp3_test_frame_count();
    failed += mp3_test_full_decode_allocation();
    failed += mp3_test_resource_manager_allocation();
    return failed != 0;
}
