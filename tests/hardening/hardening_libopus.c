#define MA_NO_DEVICE_IO
#include "../../miniaudio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HARDENING_INT64_MAX ((ma_int64)(MA_UINT64_MAX >> 1))
#define HARDENING_INT64_MIN (-HARDENING_INT64_MAX - 1)

static ma_event* g_hardeningWaitEnteredEvent;
static ma_result hardening_inline_notification_event_wait(ma_async_notification_event* pNotificationEvent);

#define MA_RESOURCE_MANAGER_INLINE_NOTIFICATION_EVENT_WAIT(pNotificationEvent) hardening_inline_notification_event_wait((pNotificationEvent))
#define MINIAUDIO_IMPLEMENTATION
#include "../../miniaudio.h"
#include "../../extras/decoders/libopus/miniaudio_libopus.c"

typedef struct
{
    const unsigned char* pData;
    size_t dataSize;
    size_t cursor;
    ma_bool32 active;
    ma_uint32 readCount;
    ma_uint32 seekCount;
    ma_uint32 tellCount;
    ma_uint32 callbackAfterRetirementCount;
} hardening_memory_stream;

typedef struct
{
    ma_vfs_callbacks callbacks;
    hardening_memory_stream stream;
    ma_event readEnteredEvent;
    ma_event continueReadEvent;
    ma_bool32 blockNextRead;
    ma_result openResult;
    ma_uint32 openCount;
    ma_uint32 closeCount;
} hardening_blocking_vfs;

typedef struct
{
    ma_bool32 failAllocation;
    ma_uint32 allocationCount;
    ma_uint32 freeCount;
    ma_uint32 liveCount;
} hardening_allocator;

typedef struct
{
    ma_decoding_backend_vtable vtable;
    ma_decoding_backend_vtable* pDelegate;
    ma_uint32 initCount;
    ma_uint32 initFileCount;
    ma_uint32 uninitCount;
} hardening_backend_proxy;

typedef struct
{
    ma_resource_manager* pResourceManager;
    ma_result result;
} hardening_process_job_thread_data;

typedef struct
{
    ma_resource_manager_data_stream* pDataStream;
    ma_result result;
} hardening_uninit_thread_data;

static hardening_backend_proxy* g_hardeningBackendProxy;

static ma_result hardening_inline_notification_event_wait(ma_async_notification_event* pNotificationEvent)
{
    if (g_hardeningWaitEnteredEvent != NULL) {
        ma_event_signal(g_hardeningWaitEnteredEvent);
    }

    return ma_async_notification_event_wait(pNotificationEvent);
}

static ma_result hardening_memory_read(void* pUserData, void* pBufferOut, size_t bytesToRead, size_t* pBytesRead)
{
    hardening_memory_stream* pStream;
    size_t bytesRemaining;

    pStream = (hardening_memory_stream*)pUserData;
    if (!pStream->active) {
        pStream->callbackAfterRetirementCount += 1;
        if (pBytesRead != NULL) {
            *pBytesRead = 0;
        }
        return MA_INVALID_OPERATION;
    }

    pStream->readCount += 1;
    bytesRemaining = pStream->dataSize - pStream->cursor;
    if (bytesToRead > bytesRemaining) {
        bytesToRead = bytesRemaining;
    }
    if (bytesToRead > 0) {
        memcpy(pBufferOut, pStream->pData + pStream->cursor, bytesToRead);
        pStream->cursor += bytesToRead;
    }
    if (pBytesRead != NULL) {
        *pBytesRead = bytesToRead;
    }

    return MA_SUCCESS;
}

static ma_result hardening_memory_seek(void* pUserData, ma_int64 offset, ma_seek_origin origin)
{
    hardening_memory_stream* pStream;
    ma_int64 base;
    ma_int64 target;

    pStream = (hardening_memory_stream*)pUserData;
    if (!pStream->active) {
        pStream->callbackAfterRetirementCount += 1;
        return MA_INVALID_OPERATION;
    }

    if (origin == ma_seek_origin_start) {
        base = 0;
    } else if (origin == ma_seek_origin_end) {
        if (pStream->dataSize > (size_t)HARDENING_INT64_MAX) {
            return MA_OUT_OF_RANGE;
        }
        base = (ma_int64)pStream->dataSize;
    } else {
        if (pStream->cursor > (size_t)HARDENING_INT64_MAX) {
            return MA_OUT_OF_RANGE;
        }
        base = (ma_int64)pStream->cursor;
    }

    if ((offset > 0 && base > HARDENING_INT64_MAX - offset) || (offset < 0 && base < HARDENING_INT64_MIN - offset)) {
        return MA_OUT_OF_RANGE;
    }
    target = base + offset;
    if (target < 0 || (ma_uint64)target > pStream->dataSize) {
        return MA_BAD_SEEK;
    }

    pStream->cursor = (size_t)target;
    pStream->seekCount += 1;
    return MA_SUCCESS;
}

static ma_result hardening_memory_tell(void* pUserData, ma_int64* pCursor)
{
    hardening_memory_stream* pStream;

    pStream = (hardening_memory_stream*)pUserData;
    if (!pStream->active) {
        pStream->callbackAfterRetirementCount += 1;
        return MA_INVALID_OPERATION;
    }
    if (pStream->cursor > (size_t)HARDENING_INT64_MAX) {
        return MA_OUT_OF_RANGE;
    }

    *pCursor = (ma_int64)pStream->cursor;
    pStream->tellCount += 1;
    return MA_SUCCESS;
}

static void hardening_memory_stream_init(hardening_memory_stream* pStream, const unsigned char* pData, size_t dataSize)
{
    MA_ZERO_OBJECT(pStream);
    pStream->pData = pData;
    pStream->dataSize = dataSize;
    pStream->active = MA_TRUE;
}

static ma_result hardening_failing_read(void* pUserData, void* pBufferOut, size_t bytesToRead, size_t* pBytesRead)
{
    (void)pUserData;
    (void)pBufferOut;
    (void)bytesToRead;
    if (pBytesRead != NULL) {
        *pBytesRead = 0;
    }
    return MA_ERROR;
}

static void* hardening_malloc(size_t size, void* pUserData)
{
    hardening_allocator* pAllocator;
    void* pMemory;

    pAllocator = (hardening_allocator*)pUserData;
    pAllocator->allocationCount += 1;
    if (pAllocator->failAllocation) {
        return NULL;
    }
    pMemory = malloc(size);
    if (pMemory != NULL) {
        pAllocator->liveCount += 1;
    }
    return pMemory;
}

static void* hardening_realloc(void* pMemory, size_t size, void* pUserData)
{
    hardening_allocator* pAllocator;
    void* pNewMemory;
    ma_bool32 wasNull;

    pAllocator = (hardening_allocator*)pUserData;
    pAllocator->allocationCount += 1;
    if (pAllocator->failAllocation) {
        return NULL;
    }
    wasNull = pMemory == NULL;
    pNewMemory = realloc(pMemory, size);
    if (pNewMemory != NULL && wasNull) {
        pAllocator->liveCount += 1;
    }
    return pNewMemory;
}

static void hardening_free(void* pMemory, void* pUserData)
{
    hardening_allocator* pAllocator;

    pAllocator = (hardening_allocator*)pUserData;
    if (pMemory != NULL) {
        pAllocator->freeCount += 1;
        pAllocator->liveCount -= 1;
    }
    free(pMemory);
}

static unsigned char* hardening_read_file(const char* pFilePath, size_t* pDataSize)
{
    FILE* pFile;
    long fileSize;
    unsigned char* pData;

    *pDataSize = 0;
    pFile = fopen(pFilePath, "rb");
    if (pFile == NULL) {
        return NULL;
    }
    if (fseek(pFile, 0, SEEK_END) != 0) {
        fclose(pFile);
        return NULL;
    }
    fileSize = ftell(pFile);
    if (fileSize <= 0 || fseek(pFile, 0, SEEK_SET) != 0) {
        fclose(pFile);
        return NULL;
    }

    pData = (unsigned char*)malloc((size_t)fileSize);
    if (pData == NULL) {
        fclose(pFile);
        return NULL;
    }
    if (fread(pData, 1, (size_t)fileSize, pFile) != (size_t)fileSize) {
        free(pData);
        fclose(pFile);
        return NULL;
    }

    fclose(pFile);
    *pDataSize = (size_t)fileSize;
    return pData;
}

static int hardening_test_direct_lifetime(const unsigned char* pData, size_t dataSize)
{
    hardening_memory_stream stream;
    ma_decoding_backend_config config;
    ma_libopus opus;
    ma_result result;
    ma_format format;
    ma_uint32 channels;
    ma_uint32 sampleRate;
    ma_uint64 length;
    ma_uint64 framesRead;
    float frames[257 * 2];
    ma_uint32 callbackCountBeforeRetirement;
    size_t i;
    double magnitude;
    int failed;

    failed = 0;
    hardening_memory_stream_init(&stream, pData, dataSize);
    config = ma_decoding_backend_config_init(ma_format_f32, 0);
    result = ma_libopus_init(hardening_memory_read, hardening_memory_seek, hardening_memory_tell, &stream, &config, NULL, &opus);
    if (result != MA_SUCCESS) {
        printf("direct Opus initialization failed: %d\n", result);
        return 1;
    }

    result = ma_libopus_get_data_format(&opus, &format, &channels, &sampleRate, NULL, 0);
    if (result != MA_SUCCESS || format != ma_format_f32 || channels != 2 || sampleRate != 48000) {
        printf("direct Opus format was result=%d format=%d channels=%u rate=%u\n", result, format, channels, sampleRate);
        failed = 1;
    }
    result = ma_libopus_get_length_in_pcm_frames(&opus, &length);
    if (result != MA_SUCCESS || length < 1600000 || length > 1800000) {
        printf("direct Opus length was result=%d length=%lu\n", result, (unsigned long)length);
        failed = 1;
    }

    result = ma_libopus_seek_to_pcm_frame(&opus, 48000);
    if (result != MA_SUCCESS) {
        printf("direct Opus seek failed: %d\n", result);
        failed = 1;
    }
    framesRead = 0;
    result = ma_libopus_read_pcm_frames(&opus, frames, 257, &framesRead);
    if (result != MA_SUCCESS || framesRead != 257) {
        printf("direct Opus read returned %d with %lu frames\n", result, (unsigned long)framesRead);
        failed = 1;
    }
    magnitude = 0;
    for (i = 0; i < (size_t)framesRead * 2; ++i) {
        magnitude += frames[i] < 0 ? -frames[i] : frames[i];
    }
    if (magnitude <= 0.001) {
        printf("direct Opus read did not produce an independently observable signal\n");
        failed = 1;
    }

    if (length >= 100) {
        result = ma_libopus_seek_to_pcm_frame(&opus, length - 100);
        framesRead = 0;
        if (result != MA_SUCCESS) {
            printf("direct Opus tail seek failed: %d\n", result);
            failed = 1;
        } else {
            result = ma_libopus_read_pcm_frames(&opus, frames, 257, &framesRead);
            if (result != MA_AT_END || framesRead != 100) {
                printf("direct Opus tail returned %d with %lu frames\n", result, (unsigned long)framesRead);
                failed = 1;
            }
        }
    }

    callbackCountBeforeRetirement = stream.readCount + stream.seekCount + stream.tellCount;
    ma_libopus_uninit(&opus, NULL);
    stream.active = MA_FALSE;
    if (stream.readCount + stream.seekCount + stream.tellCount != callbackCountBeforeRetirement || stream.callbackAfterRetirementCount != 0) {
        printf("direct Opus callback userdata was used after owner retirement\n");
        failed = 1;
    }

    return failed;
}

static int hardening_test_optional_tell(const unsigned char* pData, size_t dataSize)
{
    hardening_memory_stream stream;
    ma_decoding_backend_config config;
    ma_libopus opus;
    ma_result result;
    ma_uint64 framesRead;
    float frames[128 * 2];
    int failed;

    failed = 0;
    hardening_memory_stream_init(&stream, pData, dataSize);
    config = ma_decoding_backend_config_init(ma_format_f32, 0);
    result = ma_libopus_init(hardening_memory_read, hardening_memory_seek, NULL, &stream, &config, NULL, &opus);
    if (result != MA_SUCCESS) {
        printf("Opus initialization with optional tell callback failed: %d\n", result);
        return 1;
    }
    framesRead = 0;
    result = ma_libopus_read_pcm_frames(&opus, frames, 128, &framesRead);
    if (result != MA_SUCCESS || framesRead != 128) {
        printf("Opus read without tell callback returned %d with %lu frames\n", result, (unsigned long)framesRead);
        failed = 1;
    }
    ma_libopus_uninit(&opus, NULL);
    stream.active = MA_FALSE;
    if (stream.callbackAfterRetirementCount != 0) {
        printf("Opus without tell callback used callback userdata after retirement\n");
        failed = 1;
    }
    return failed;
}

static int hardening_test_backend_failures(const unsigned char* pData, size_t dataSize)
{
    hardening_allocator allocator;
    hardening_memory_stream stream;
    ma_allocation_callbacks allocationCallbacks;
    ma_decoding_backend_config config;
    ma_data_source* pBackend;
    ma_result result;
    int failed;

    failed = 0;
    MA_ZERO_OBJECT(&allocator);
    allocator.failAllocation = MA_TRUE;
    allocationCallbacks.pUserData = &allocator;
    allocationCallbacks.onMalloc = hardening_malloc;
    allocationCallbacks.onRealloc = hardening_realloc;
    allocationCallbacks.onFree = hardening_free;
    config = ma_decoding_backend_config_init(ma_format_f32, 0);
    hardening_memory_stream_init(&stream, pData, dataSize);
    pBackend = NULL;
    result = ma_decoding_backend_libopus->onInit(NULL, hardening_memory_read, hardening_memory_seek, hardening_memory_tell, &stream, &config, &allocationCallbacks, &pBackend);
    if (result != MA_OUT_OF_MEMORY || pBackend != NULL || allocator.allocationCount != 1 || allocator.freeCount != 0 || allocator.liveCount != 0) {
        printf("Opus allocation failure was result=%d backend=%p allocations=%u frees=%u live=%u\n",
            result, (void*)pBackend, allocator.allocationCount, allocator.freeCount, allocator.liveCount);
        failed = 1;
    }

    MA_ZERO_OBJECT(&allocator);
    hardening_memory_stream_init(&stream, pData, dataSize);
    pBackend = NULL;
    result = ma_decoding_backend_libopus->onInit(NULL, hardening_failing_read, hardening_memory_seek, hardening_memory_tell, &stream, &config, &allocationCallbacks, &pBackend);
    if (result != MA_INVALID_FILE || pBackend != NULL || allocator.allocationCount != 1 || allocator.freeCount != 1 || allocator.liveCount != 0) {
        printf("Opus callback failure was result=%d backend=%p allocations=%u frees=%u live=%u\n",
            result, (void*)pBackend, allocator.allocationCount, allocator.freeCount, allocator.liveCount);
        failed = 1;
    }

    MA_ZERO_OBJECT(&allocator);
    pBackend = NULL;
    result = ma_decoding_backend_libopus->onInitFile(NULL, "", &config, &allocationCallbacks, &pBackend);
    if (result != MA_INVALID_FILE || pBackend != NULL || allocator.allocationCount != 1 || allocator.freeCount != 1 || allocator.liveCount != 0) {
        printf("Opus file-open failure was result=%d backend=%p allocations=%u frees=%u live=%u\n",
            result, (void*)pBackend, allocator.allocationCount, allocator.freeCount, allocator.liveCount);
        failed = 1;
    }

    return failed;
}

static int hardening_test_malformed_and_truncated(const unsigned char* pData, size_t dataSize)
{
    static const unsigned char malformedData[32] = {
        0x4F, 0x67, 0x67, 0x53, 0x00, 0xFF, 0x7F, 0x11,
        0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
        0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x00
    };
    hardening_memory_stream stream;
    ma_decoding_backend_config config;
    ma_libopus opus;
    ma_result result;
    ma_uint64 framesRead;
    float frames[256 * 2];
    ma_uint32 iteration;
    int failed;

    failed = 0;
    config = ma_decoding_backend_config_init(ma_format_f32, 0);

    hardening_memory_stream_init(&stream, malformedData, sizeof(malformedData));
    result = ma_libopus_init(hardening_memory_read, hardening_memory_seek, hardening_memory_tell, &stream, &config, NULL, &opus);
    if (result != MA_INVALID_FILE) {
        printf("malformed Opus initialization returned %d\n", result);
        if (result == MA_SUCCESS) {
            ma_libopus_uninit(&opus, NULL);
        }
        failed = 1;
    }
    stream.active = MA_FALSE;
    if (stream.callbackAfterRetirementCount != 0) {
        printf("malformed Opus used callback userdata after retirement\n");
        failed = 1;
    }

    hardening_memory_stream_init(&stream, pData, dataSize / 2);
    result = ma_libopus_init(hardening_memory_read, hardening_memory_seek, hardening_memory_tell, &stream, &config, NULL, &opus);
    if (result == MA_SUCCESS) {
        result = MA_SUCCESS;
        framesRead = 0;
        for (iteration = 0; iteration < 8192 && result == MA_SUCCESS; ++iteration) {
            result = ma_libopus_read_pcm_frames(&opus, frames, 256, &framesRead);
        }
        if (iteration == 8192 || (result != MA_AT_END && result != MA_ERROR)) {
            printf("truncated Opus did not terminate with a bounded decoder result: %d\n", result);
            failed = 1;
        }
        ma_libopus_uninit(&opus, NULL);
    } else if (result != MA_INVALID_FILE) {
        printf("truncated Opus initialization returned unexpected result %d\n", result);
        failed = 1;
    }
    stream.active = MA_FALSE;
    if (stream.callbackAfterRetirementCount != 0) {
        printf("truncated Opus used callback userdata after retirement\n");
        failed = 1;
    }

    return failed;
}

static int hardening_test_chained(const unsigned char* pData, size_t dataSize)
{
    unsigned char* pChainedData;
    hardening_memory_stream stream;
    ma_decoding_backend_config config;
    ma_libopus opus;
    ma_result result;
    ma_uint64 length;
    size_t offset;
    int failed;

    if (dataSize > ((size_t)-1) / 2) {
        return 1;
    }
    pChainedData = (unsigned char*)malloc(dataSize * 2);
    if (pChainedData == NULL) {
        return 1;
    }
    memcpy(pChainedData, pData, dataSize);
    memcpy(pChainedData + dataSize, pData, dataSize);

    offset = dataSize;
    while (offset < dataSize * 2) {
        ogg_page page;
        size_t segmentCount;
        size_t headerSize;
        size_t bodySize;
        size_t i;
        ma_uint32 serial;

        if (dataSize * 2 - offset < 27 || memcmp(pChainedData + offset, "OggS", 4) != 0) {
            printf("chained Opus fixture contains an invalid Ogg page at %lu\n", (unsigned long)(offset - dataSize));
            free(pChainedData);
            return 1;
        }
        segmentCount = pChainedData[offset + 26];
        headerSize = 27 + segmentCount;
        if (headerSize > dataSize * 2 - offset) {
            free(pChainedData);
            return 1;
        }
        bodySize = 0;
        for (i = 0; i < segmentCount; ++i) {
            bodySize += pChainedData[offset + 27 + i];
        }
        if (bodySize > dataSize * 2 - offset - headerSize) {
            free(pChainedData);
            return 1;
        }

        serial = ((ma_uint32)pChainedData[offset + 14]      ) |
                 ((ma_uint32)pChainedData[offset + 15] <<  8) |
                 ((ma_uint32)pChainedData[offset + 16] << 16) |
                 ((ma_uint32)pChainedData[offset + 17] << 24);
        serial += 1;
        pChainedData[offset + 14] = (unsigned char)(serial >>  0);
        pChainedData[offset + 15] = (unsigned char)(serial >>  8);
        pChainedData[offset + 16] = (unsigned char)(serial >> 16);
        pChainedData[offset + 17] = (unsigned char)(serial >> 24);
        pChainedData[offset + 22] = 0;
        pChainedData[offset + 23] = 0;
        pChainedData[offset + 24] = 0;
        pChainedData[offset + 25] = 0;

        page.header = pChainedData + offset;
        page.header_len = (long)headerSize;
        page.body = pChainedData + offset + headerSize;
        page.body_len = (long)bodySize;
        ogg_page_checksum_set(&page);
        offset += headerSize + bodySize;
    }

    failed = 0;
    hardening_memory_stream_init(&stream, pChainedData, dataSize * 2);
    config = ma_decoding_backend_config_init(ma_format_f32, 0);
    result = ma_libopus_init(hardening_memory_read, hardening_memory_seek, hardening_memory_tell, &stream, &config, NULL, &opus);
    if (result != MA_SUCCESS) {
        printf("chained Opus initialization failed: %d\n", result);
        failed = 1;
    } else {
        result = ma_libopus_get_length_in_pcm_frames(&opus, &length);
        if (result != MA_SUCCESS || length < 3200000 || length > 3600000) {
            printf("chained Opus length was result=%d length=%lu\n", result, (unsigned long)length);
            failed = 1;
        }
        ma_libopus_uninit(&opus, NULL);
    }
    stream.active = MA_FALSE;
    if (stream.callbackAfterRetirementCount != 0) {
        printf("chained Opus used callback userdata after retirement\n");
        failed = 1;
    }
    free(pChainedData);
    return failed;
}

static ma_result hardening_blocking_vfs_open(ma_vfs* pVFS, const char* pFilePath, ma_uint32 openMode, ma_vfs_file* pFile)
{
    hardening_blocking_vfs* pBlockingVFS;

    (void)pFilePath;
    (void)openMode;
    pBlockingVFS = (hardening_blocking_vfs*)pVFS;
    pBlockingVFS->openCount += 1;
    if (pBlockingVFS->openResult != MA_SUCCESS) {
        return pBlockingVFS->openResult;
    }
    pBlockingVFS->stream.cursor = 0;
    pBlockingVFS->stream.active = MA_TRUE;
    *pFile = (ma_vfs_file)pBlockingVFS;
    return MA_SUCCESS;
}

static ma_result hardening_blocking_vfs_close(ma_vfs* pVFS, ma_vfs_file file)
{
    hardening_blocking_vfs* pBlockingVFS;

    (void)file;
    pBlockingVFS = (hardening_blocking_vfs*)pVFS;
    pBlockingVFS->closeCount += 1;
    return MA_SUCCESS;
}

static ma_result hardening_blocking_vfs_read(ma_vfs* pVFS, ma_vfs_file file, void* pDst, size_t sizeInBytes, size_t* pBytesRead)
{
    hardening_blocking_vfs* pBlockingVFS;

    (void)file;
    pBlockingVFS = (hardening_blocking_vfs*)pVFS;
    if (pBlockingVFS->blockNextRead) {
        pBlockingVFS->blockNextRead = MA_FALSE;
        ma_event_signal(&pBlockingVFS->readEnteredEvent);
        if (ma_event_wait(&pBlockingVFS->continueReadEvent) != MA_SUCCESS) {
            return MA_ERROR;
        }
    }

    return hardening_memory_read(&pBlockingVFS->stream, pDst, sizeInBytes, pBytesRead);
}

static ma_result hardening_blocking_vfs_seek(ma_vfs* pVFS, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin)
{
    hardening_blocking_vfs* pBlockingVFS;

    (void)file;
    pBlockingVFS = (hardening_blocking_vfs*)pVFS;
    return hardening_memory_seek(&pBlockingVFS->stream, offset, origin);
}

static ma_result hardening_blocking_vfs_tell(ma_vfs* pVFS, ma_vfs_file file, ma_int64* pCursor)
{
    hardening_blocking_vfs* pBlockingVFS;

    (void)file;
    pBlockingVFS = (hardening_blocking_vfs*)pVFS;
    return hardening_memory_tell(&pBlockingVFS->stream, pCursor);
}

static ma_result hardening_proxy_init(void* pUserData, ma_read_proc onRead, ma_seek_proc onSeek, ma_tell_proc onTell, void* pReadSeekTellUserData, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_data_source** ppBackend)
{
    hardening_backend_proxy* pProxy;

    (void)pUserData;
    pProxy = g_hardeningBackendProxy;
    pProxy->initCount += 1;
    return pProxy->pDelegate->onInit(NULL, onRead, onSeek, onTell, pReadSeekTellUserData, pConfig, pAllocationCallbacks, ppBackend);
}

static ma_result hardening_proxy_init_file(void* pUserData, const char* pFilePath, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_data_source** ppBackend)
{
    hardening_backend_proxy* pProxy;

    (void)pUserData;
    pProxy = g_hardeningBackendProxy;
    pProxy->initFileCount += 1;
    return pProxy->pDelegate->onInitFile(NULL, pFilePath, pConfig, pAllocationCallbacks, ppBackend);
}

static void hardening_proxy_uninit(void* pUserData, ma_data_source* pBackend, const ma_allocation_callbacks* pAllocationCallbacks)
{
    hardening_backend_proxy* pProxy;

    (void)pUserData;
    pProxy = g_hardeningBackendProxy;
    pProxy->uninitCount += 1;
    pProxy->pDelegate->onUninit(NULL, pBackend, pAllocationCallbacks);
}

static void hardening_backend_proxy_init(hardening_backend_proxy* pProxy)
{
    MA_ZERO_OBJECT(pProxy);
    pProxy->pDelegate = ma_decoding_backend_libopus;
    pProxy->vtable.onInit = hardening_proxy_init;
    pProxy->vtable.onInitFile = hardening_proxy_init_file;
    pProxy->vtable.onUninit = hardening_proxy_uninit;
    g_hardeningBackendProxy = pProxy;
}

static ma_thread_result MA_THREADCALL hardening_process_job_thread(void* pUserData)
{
    hardening_process_job_thread_data* pThreadData;

    pThreadData = (hardening_process_job_thread_data*)pUserData;
    pThreadData->result = ma_resource_manager_process_next_job(pThreadData->pResourceManager);
    return (ma_thread_result)0;
}

static ma_thread_result MA_THREADCALL hardening_uninit_thread(void* pUserData)
{
    hardening_uninit_thread_data* pThreadData;

    pThreadData = (hardening_uninit_thread_data*)pUserData;
    pThreadData->result = ma_resource_manager_data_stream_uninit(pThreadData->pDataStream);
    return (ma_thread_result)0;
}

static int hardening_test_resource_manager_cancel_before_start(const unsigned char* pData, size_t dataSize)
{
    hardening_blocking_vfs vfs;
    hardening_backend_proxy proxy;
    hardening_uninit_thread_data uninitThreadData;
    ma_decoding_backend_vtable* pVTable;
    ma_resource_manager_config config;
    ma_resource_manager resourceManager;
    ma_resource_manager_data_stream dataStream;
    ma_event waitEnteredEvent;
    ma_thread uninitThread;
    ma_result result;
    int failed;

    failed = 0;
    MA_ZERO_OBJECT(&vfs);
    hardening_memory_stream_init(&vfs.stream, pData, dataSize);
    vfs.callbacks.onOpen = hardening_blocking_vfs_open;
    vfs.callbacks.onClose = hardening_blocking_vfs_close;
    vfs.callbacks.onRead = hardening_blocking_vfs_read;
    vfs.callbacks.onSeek = hardening_blocking_vfs_seek;
    vfs.callbacks.onTell = hardening_blocking_vfs_tell;
    hardening_backend_proxy_init(&proxy);
    pVTable = &proxy.vtable;

    config = ma_resource_manager_config_init();
    config.jobThreadCount = 0;
    config.flags = MA_RESOURCE_MANAGER_FLAG_NON_BLOCKING;
    config.pVFS = (ma_vfs*)&vfs;
    config.ppCustomDecodingBackendVTables = &pVTable;
    config.customDecodingBackendCount = 1;
    result = ma_resource_manager_init(&config, &resourceManager);
    if (result != MA_SUCCESS) {
        printf("cancel-before-start resource manager initialization failed: %d\n", result);
        return 1;
    }

    result = ma_resource_manager_data_stream_init(&resourceManager, "memory.opus", MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC, NULL, &dataStream);
    if (result != MA_SUCCESS) {
        printf("cancel-before-start stream initialization failed: %d\n", result);
        ma_resource_manager_uninit(&resourceManager);
        return 1;
    }
    if (ma_event_init(&waitEnteredEvent) != MA_SUCCESS) {
        printf("cancel-before-start event initialization failed\n");
        ma_resource_manager_uninit(&resourceManager);
        return 1;
    }

    g_hardeningWaitEnteredEvent = &waitEnteredEvent;
    uninitThreadData.pDataStream = &dataStream;
    uninitThreadData.result = MA_ERROR;
    result = ma_thread_create(&uninitThread, ma_thread_priority_normal, 0, hardening_uninit_thread, &uninitThreadData, NULL);
    if (result != MA_SUCCESS) {
        printf("cancel-before-start thread creation failed: %d\n", result);
        g_hardeningWaitEnteredEvent = NULL;
        ma_event_uninit(&waitEnteredEvent);
        ma_resource_manager_uninit(&resourceManager);
        return 1;
    }
    ma_event_wait(&waitEnteredEvent);

    result = ma_resource_manager_process_next_job(&resourceManager);
    if (result != MA_INVALID_OPERATION) {
        printf("cancel-before-start load job returned %d\n", result);
        failed = 1;
    }
    result = ma_resource_manager_process_next_job(&resourceManager);
    if (result != MA_SUCCESS) {
        printf("cancel-before-start free job returned %d\n", result);
        failed = 1;
    }
    ma_thread_wait(&uninitThread);
    g_hardeningWaitEnteredEvent = NULL;
    ma_event_uninit(&waitEnteredEvent);

    if (uninitThreadData.result != MA_SUCCESS || proxy.initCount != 0 || proxy.uninitCount != 0 || vfs.openCount != 0 || vfs.closeCount != 0) {
        printf("cancel-before-start lifetime was result=%d init=%u uninit=%u open=%u close=%u\n",
            uninitThreadData.result, proxy.initCount, proxy.uninitCount, vfs.openCount, vfs.closeCount);
        failed = 1;
    }
    ma_resource_manager_uninit(&resourceManager);
    return failed;
}

static int hardening_test_resource_manager_open_failure(const unsigned char* pData, size_t dataSize)
{
    hardening_blocking_vfs vfs;
    hardening_backend_proxy proxy;
    ma_decoding_backend_vtable* pVTable;
    ma_resource_manager_config config;
    ma_resource_manager resourceManager;
    ma_resource_manager_data_stream dataStream;
    ma_result result;
    int failed;

    failed = 0;
    MA_ZERO_OBJECT(&vfs);
    hardening_memory_stream_init(&vfs.stream, pData, dataSize);
    vfs.callbacks.onOpen = hardening_blocking_vfs_open;
    vfs.callbacks.onClose = hardening_blocking_vfs_close;
    vfs.callbacks.onRead = hardening_blocking_vfs_read;
    vfs.callbacks.onSeek = hardening_blocking_vfs_seek;
    vfs.callbacks.onTell = hardening_blocking_vfs_tell;
    vfs.openResult = MA_DOES_NOT_EXIST;
    hardening_backend_proxy_init(&proxy);
    pVTable = &proxy.vtable;

    config = ma_resource_manager_config_init();
    config.pVFS = (ma_vfs*)&vfs;
    config.ppCustomDecodingBackendVTables = &pVTable;
    config.customDecodingBackendCount = 1;
    result = ma_resource_manager_init(&config, &resourceManager);
    if (result != MA_SUCCESS) {
        printf("open-failure resource manager initialization failed: %d\n", result);
        return 1;
    }

    result = ma_resource_manager_data_stream_init(&resourceManager, "missing.opus", 0, NULL, &dataStream);
    if (result != MA_DOES_NOT_EXIST || vfs.openCount != 1 || vfs.closeCount != 0 || proxy.initCount != 0 || proxy.uninitCount != 0) {
        printf("resource-manager open failure was result=%d open=%u close=%u init=%u uninit=%u\n",
            result, vfs.openCount, vfs.closeCount, proxy.initCount, proxy.uninitCount);
        failed = 1;
    }
    ma_resource_manager_uninit(&resourceManager);
    return failed;
}

static int hardening_test_resource_manager_cancel_during_read(const unsigned char* pData, size_t dataSize)
{
    hardening_blocking_vfs vfs;
    hardening_backend_proxy proxy;
    hardening_process_job_thread_data processThreadData;
    hardening_uninit_thread_data uninitThreadData;
    ma_decoding_backend_vtable* pVTable;
    ma_resource_manager_config config;
    ma_resource_manager resourceManager;
    ma_resource_manager_data_stream dataStream;
    ma_event waitEnteredEvent;
    ma_thread processThread;
    ma_thread uninitThread;
    ma_result result;
    int failed;

    failed = 0;
    MA_ZERO_OBJECT(&vfs);
    hardening_memory_stream_init(&vfs.stream, pData, dataSize);
    vfs.callbacks.onOpen = hardening_blocking_vfs_open;
    vfs.callbacks.onClose = hardening_blocking_vfs_close;
    vfs.callbacks.onRead = hardening_blocking_vfs_read;
    vfs.callbacks.onSeek = hardening_blocking_vfs_seek;
    vfs.callbacks.onTell = hardening_blocking_vfs_tell;
    if (ma_event_init(&vfs.readEnteredEvent) != MA_SUCCESS) {
        printf("cancel-during-read event initialization failed\n");
        return 1;
    }
    if (ma_event_init(&vfs.continueReadEvent) != MA_SUCCESS) {
        printf("cancel-during-read event initialization failed\n");
        ma_event_uninit(&vfs.readEnteredEvent);
        return 1;
    }
    if (ma_event_init(&waitEnteredEvent) != MA_SUCCESS) {
        printf("cancel-during-read event initialization failed\n");
        ma_event_uninit(&vfs.continueReadEvent);
        ma_event_uninit(&vfs.readEnteredEvent);
        return 1;
    }
    vfs.blockNextRead = MA_TRUE;
    hardening_backend_proxy_init(&proxy);
    pVTable = &proxy.vtable;

    config = ma_resource_manager_config_init();
    config.jobThreadCount = 0;
    config.flags = MA_RESOURCE_MANAGER_FLAG_NON_BLOCKING;
    config.pVFS = (ma_vfs*)&vfs;
    config.ppCustomDecodingBackendVTables = &pVTable;
    config.customDecodingBackendCount = 1;
    result = ma_resource_manager_init(&config, &resourceManager);
    if (result != MA_SUCCESS) {
        printf("cancel-during-read resource manager initialization failed: %d\n", result);
        ma_event_uninit(&waitEnteredEvent);
        ma_event_uninit(&vfs.continueReadEvent);
        ma_event_uninit(&vfs.readEnteredEvent);
        return 1;
    }
    result = ma_resource_manager_data_stream_init(&resourceManager, "memory.opus", MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC, NULL, &dataStream);
    if (result != MA_SUCCESS) {
        printf("cancel-during-read stream initialization failed: %d\n", result);
        ma_resource_manager_uninit(&resourceManager);
        ma_event_uninit(&waitEnteredEvent);
        ma_event_uninit(&vfs.continueReadEvent);
        ma_event_uninit(&vfs.readEnteredEvent);
        return 1;
    }

    processThreadData.pResourceManager = &resourceManager;
    processThreadData.result = MA_ERROR;
    result = ma_thread_create(&processThread, ma_thread_priority_normal, 0, hardening_process_job_thread, &processThreadData, NULL);
    if (result != MA_SUCCESS) {
        printf("cancel-during-read process thread creation failed: %d\n", result);
        ma_resource_manager_uninit(&resourceManager);
        ma_event_uninit(&waitEnteredEvent);
        ma_event_uninit(&vfs.continueReadEvent);
        ma_event_uninit(&vfs.readEnteredEvent);
        return 1;
    }
    ma_event_wait(&vfs.readEnteredEvent);

    g_hardeningWaitEnteredEvent = &waitEnteredEvent;
    uninitThreadData.pDataStream = &dataStream;
    uninitThreadData.result = MA_ERROR;
    result = ma_thread_create(&uninitThread, ma_thread_priority_normal, 0, hardening_uninit_thread, &uninitThreadData, NULL);
    if (result != MA_SUCCESS) {
        printf("cancel-during-read uninit thread creation failed: %d\n", result);
        g_hardeningWaitEnteredEvent = NULL;
        ma_event_signal(&vfs.continueReadEvent);
        ma_thread_wait(&processThread);
        ma_resource_manager_uninit(&resourceManager);
        ma_event_uninit(&waitEnteredEvent);
        ma_event_uninit(&vfs.continueReadEvent);
        ma_event_uninit(&vfs.readEnteredEvent);
        return 1;
    }
    ma_event_wait(&waitEnteredEvent);
    ma_event_signal(&vfs.continueReadEvent);
    ma_thread_wait(&processThread);

    result = ma_resource_manager_process_next_job(&resourceManager);
    if (result != MA_SUCCESS) {
        printf("cancel-during-read free job returned %d\n", result);
        failed = 1;
    }
    ma_thread_wait(&uninitThread);
    g_hardeningWaitEnteredEvent = NULL;

    if (processThreadData.result != MA_SUCCESS || uninitThreadData.result != MA_SUCCESS || proxy.initCount != 1 || proxy.uninitCount != 1 || vfs.openCount != 1 || vfs.closeCount != 1) {
        printf("cancel-during-read lifetime was process=%d uninit-result=%d init=%u uninit=%u open=%u close=%u\n",
            processThreadData.result, uninitThreadData.result, proxy.initCount, proxy.uninitCount, vfs.openCount, vfs.closeCount);
        failed = 1;
    }
    vfs.stream.active = MA_FALSE;
    if (vfs.stream.callbackAfterRetirementCount != 0) {
        printf("cancel-during-read used callback userdata after owner retirement\n");
        failed = 1;
    }

    ma_resource_manager_uninit(&resourceManager);
    ma_event_uninit(&waitEnteredEvent);
    ma_event_uninit(&vfs.continueReadEvent);
    ma_event_uninit(&vfs.readEnteredEvent);
    return failed;
}

int main(int argc, char** argv)
{
    unsigned char* pData;
    size_t dataSize;
    int failed;

    if (argc != 2) {
        printf("usage: hardening_libopus <valid-opus-file>\n");
        return 1;
    }
    pData = hardening_read_file(argv[1], &dataSize);
    if (pData == NULL) {
        printf("failed to load Opus fixture: %s\n", argv[1]);
        return 1;
    }

    failed = 0;
    failed += hardening_test_direct_lifetime(pData, dataSize);
    failed += hardening_test_optional_tell(pData, dataSize);
    failed += hardening_test_backend_failures(pData, dataSize);
    failed += hardening_test_malformed_and_truncated(pData, dataSize);
    failed += hardening_test_chained(pData, dataSize);
    failed += hardening_test_resource_manager_open_failure(pData, dataSize);
    failed += hardening_test_resource_manager_cancel_before_start(pData, dataSize);
    failed += hardening_test_resource_manager_cancel_during_read(pData, dataSize);

    free(pData);
    return failed != 0;
}
