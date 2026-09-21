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

typedef struct
{
    const ma_uint8* pData;
    size_t dataSize;
    size_t cursor;
} mp3_callback_stream;

typedef struct
{
    ma_uint32 readCount;
    ma_uint32 nullDestinationCount;
} mp3_invalid_callback_state;

static size_t mp3_invalid_buffer_read(void* pUserData, void* pBufferOut, size_t bytesToRead)
{
    mp3_invalid_callback_state* pState = (mp3_invalid_callback_state*)pUserData;

    (void)bytesToRead;
    pState->readCount += 1;
    if (pBufferOut == NULL) {
        pState->nullDestinationCount += 1;
    }
    return 0;
}

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

static ma_result mp3_callback_read(void* pUserData, void* pBufferOut, size_t bytesToRead, size_t* pBytesRead)
{
    mp3_callback_stream* pStream = (mp3_callback_stream*)pUserData;
    size_t bytesRemaining = pStream->dataSize - pStream->cursor;

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

static ma_result mp3_callback_seek(void* pUserData, ma_int64 offset, ma_seek_origin origin)
{
    mp3_callback_stream* pStream = (mp3_callback_stream*)pUserData;
    ma_int64 base;

    if (origin == ma_seek_origin_start) {
        base = 0;
    } else if (origin == ma_seek_origin_current) {
        base = (ma_int64)pStream->cursor;
    } else if (origin == ma_seek_origin_end) {
        base = (ma_int64)pStream->dataSize;
    } else {
        return MA_INVALID_ARGS;
    }

    if (offset < -base || offset > (ma_int64)pStream->dataSize - base) {
        return MA_BAD_SEEK;
    }

    pStream->cursor = (size_t)(base + offset);
    return MA_SUCCESS;
}

static ma_result mp3_decoder_read(ma_decoder* pDecoder, void* pBufferOut, size_t bytesToRead, size_t* pBytesRead)
{
    return mp3_callback_read(pDecoder->pUserData, pBufferOut, bytesToRead, pBytesRead);
}

static ma_result mp3_decoder_seek(ma_decoder* pDecoder, ma_int64 offset, ma_seek_origin origin)
{
    return mp3_callback_seek(pDecoder->pUserData, offset, origin);
}

static int mp3_test_optional_tell(void)
{
    mp3_callback_stream stream;
    ma_decoding_backend_config config;
    ma_mp3 mp3;
    ma_result result;
    ma_uint64 framesRead;
    float frames[32 * 2];
    int failed;

    MA_ZERO_OBJECT(&stream);
    stream.pData = g_mp3_1117;
    stream.dataSize = sizeof(g_mp3_1117);
    config = ma_decoding_backend_config_init(ma_format_f32, 0);

    result = ma_mp3_init(mp3_callback_read, mp3_callback_seek, NULL, &stream, &config, NULL, &mp3);
    if (result != MA_SUCCESS) {
        printf("MP3 callback initialization without tell failed: %d\n", result);
        return 1;
    }

    failed = 0;
    framesRead = 0;
    result = ma_mp3_read_pcm_frames(&mp3, frames, 32, &framesRead);
    if (result != MA_SUCCESS || framesRead != 32) {
        printf("MP3 callback read without tell returned %d with %u:%08u frames\n",
            result,
            (ma_uint32)(framesRead >> 32),
            (ma_uint32)framesRead);
        failed = 1;
    }

    ma_mp3_uninit(&mp3, NULL);
    return failed;
}

static ma_decoder_config mp3_decoder_config_init(mp3_allocator_state* pState)
{
    ma_decoder_config config;

    config = ma_decoder_config_init_default();
    config.encodingFormat = ma_encoding_format_mp3;
    config.allocationCallbacks.pUserData = pState;
    config.allocationCallbacks.onMalloc = mp3_malloc;
    config.allocationCallbacks.onRealloc = mp3_realloc;
    config.allocationCallbacks.onFree = mp3_free;
    return config;
}

static int mp3_validate_truncated_decoder(ma_decoder* pDecoder, const char* pPathName)
{
    ma_format format;
    ma_uint32 channels;
    ma_uint32 sampleRate;
    ma_uint64 framesRead;
    float frames[64];
    ma_result result;
    int failed;

    failed = 0;
    result = ma_decoder_get_data_format(pDecoder, &format, &channels, &sampleRate, NULL, 0);
    if (result != MA_SUCCESS || format != ma_format_f32 || channels != 1 || sampleRate != 11025) {
        printf("issue 1128 %s selected unexpected decoder format: result=%d format=%d channels=%u rate=%u\n",
            pPathName,
            result,
            format,
            channels,
            sampleRate);
        failed = 1;
    }

    framesRead = 0;
    result = ma_decoder_read_pcm_frames(pDecoder, frames, 64, &framesRead);
    if (result != MA_SUCCESS || framesRead != 64) {
        printf("issue 1128 %s bounded MP3 decode returned %d with %u:%08u frames\n",
            pPathName,
            result,
            (ma_uint32)(framesRead >> 32),
            (ma_uint32)framesRead);
        failed = 1;
    }

    return failed;
}

static int mp3_validate_allocator_cleanup(const mp3_allocator_state* pState, const char* pPathName)
{
    if (pState->allocationCount == 0 || pState->liveAllocationCount != 0 || pState->allocationCount != pState->freeCount) {
        printf("issue 1128 %s allocation lifetime was allocated=%u freed=%u live=%u\n",
            pPathName,
            pState->allocationCount,
            pState->freeCount,
            pState->liveAllocationCount);
        return 1;
    }
    if (pState->largestRequest > 1024 * 1024) {
        printf("issue 1128 %s requested an oversized allocation of %lu bytes\n", pPathName, (unsigned long)pState->largestRequest);
        return 1;
    }
    return 0;
}

static int mp3_test_truncated_metadata(void)
{
    const char* pFilePath = "miniaudio-hardening-1128.mp3";
    mp3_allocator_state state;
    mp3_callback_stream stream;
    ma_decoder_config config;
    ma_decoder decoder;
    ma_result result;
    FILE* pFile;
    int failed;

    failed = 0;
    MA_ZERO_OBJECT(&state);
    config = mp3_decoder_config_init(&state);
    result = ma_decoder_init_memory(g_mp3_1128, sizeof(g_mp3_1128), &config, &decoder);
    if (result != MA_SUCCESS) {
        printf("issue 1128 memory initialization returned %d instead of ignoring the incomplete optional tag\n", result);
        failed = 1;
    } else {
        failed += mp3_validate_truncated_decoder(&decoder, "memory");
        result = ma_decoder_uninit(&decoder);
        if (result != MA_SUCCESS) {
            printf("issue 1128 memory decoder uninit returned %d\n", result);
            failed = 1;
        }
    }
    failed += mp3_validate_allocator_cleanup(&state, "memory");

    MA_ZERO_OBJECT(&state);
    MA_ZERO_OBJECT(&stream);
    stream.pData = g_mp3_1128;
    stream.dataSize = sizeof(g_mp3_1128);
    config = mp3_decoder_config_init(&state);
    result = ma_decoder_init(mp3_decoder_read, mp3_decoder_seek, &stream, &config, &decoder);
    if (result != MA_SUCCESS) {
        printf("issue 1128 callback initialization returned %d instead of ignoring the incomplete optional tag\n", result);
        failed = 1;
    } else {
        failed += mp3_validate_truncated_decoder(&decoder, "callback");
        result = ma_decoder_uninit(&decoder);
        if (result != MA_SUCCESS) {
            printf("issue 1128 callback decoder uninit returned %d\n", result);
            failed = 1;
        }
    }
    failed += mp3_validate_allocator_cleanup(&state, "callback");

    remove(pFilePath);
    if (ma_fopen(&pFile, pFilePath, "wb") != MA_SUCCESS) {
        printf("issue 1128 could not create the file-backed fixture\n");
        return 1;
    }
    if (fwrite(g_mp3_1128, 1, sizeof(g_mp3_1128), pFile) != sizeof(g_mp3_1128)) {
        fclose(pFile);
        remove(pFilePath);
        printf("issue 1128 could not write the file-backed fixture\n");
        return 1;
    }
    if (fclose(pFile) != 0) {
        remove(pFilePath);
        printf("issue 1128 could not close the file-backed fixture\n");
        return 1;
    }

    MA_ZERO_OBJECT(&state);
    config = mp3_decoder_config_init(&state);
    result = ma_decoder_init_file(pFilePath, &config, &decoder);
    if (result != MA_SUCCESS) {
        printf("issue 1128 file initialization returned %d instead of ignoring the incomplete optional tag\n", result);
        failed = 1;
    } else {
        failed += mp3_validate_truncated_decoder(&decoder, "file");
        result = ma_decoder_uninit(&decoder);
        if (result != MA_SUCCESS) {
            printf("issue 1128 file decoder uninit returned %d\n", result);
            failed = 1;
        }
    }
    remove(pFilePath);
    failed += mp3_validate_allocator_cleanup(&state, "file");

    return failed;
}

static void mp3_synthetic_write(ma_uint8* pData, size_t frameSize, size_t offset, const void* pSource, size_t bytesToWrite)
{
    if (offset >= frameSize) {
        return;
    }
    if (bytesToWrite > frameSize - offset) {
        bytesToWrite = frameSize - offset;
    }
    memcpy(pData + offset, pSource, bytesToWrite);
}

static void mp3_synthetic_write_u32(ma_uint8* pData, size_t frameSize, size_t offset, ma_uint32 value)
{
    ma_uint8 bytes[4];

    bytes[0] = (ma_uint8)(value >> 24);
    bytes[1] = (ma_uint8)(value >> 16);
    bytes[2] = (ma_uint8)(value >> 8);
    bytes[3] = (ma_uint8)value;
    mp3_synthetic_write(pData, frameSize, offset, bytes, sizeof(bytes));
}

static size_t mp3_make_synthetic_xing(
    ma_uint8* pData,
    size_t dataCapacity,
    ma_uint32 bitrateIndex,
    ma_uint32 sampleRateIndex,
    ma_bool32 padding,
    ma_bool32 mono,
    const char* pTagName,
    ma_uint8 flags,
    ma_bool32 includeLame)
{
    static const ma_uint32 bitrates[14] = {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
    static const ma_uint32 sampleRates[3] = {22050, 24000, 16000};
    ma_uint8 toc[100];
    ma_uint8 quality[4] = {0x01, 0x23, 0x45, 0x67};
    ma_uint8 lame[36];
    size_t frameSize;
    size_t dataSize;
    size_t tagOffset;
    size_t cursor;
    ma_uint32 i;

    if (bitrateIndex == 0 || bitrateIndex > 14 || sampleRateIndex > 2) {
        return 0;
    }
    frameSize = (72 * bitrates[bitrateIndex - 1] * 1000) / sampleRates[sampleRateIndex];
    if (padding) {
        frameSize += 1;
    }
    dataSize = frameSize * 3;
    if (dataSize > dataCapacity) {
        return 0;
    }

    memset(pData, 0, dataSize);
    for (i = 0; i < 3; i += 1) {
        size_t frameOffset = i * frameSize;
        pData[frameOffset + 0] = 0xFF;
        pData[frameOffset + 1] = 0xF3;  /* MPEG-2, Layer III, no CRC. */
        pData[frameOffset + 2] = (ma_uint8)((bitrateIndex << 4) | (sampleRateIndex << 2) | (padding ? 2 : 0));
        pData[frameOffset + 3] = mono ? 0xC4 : 0x04;
    }

    tagOffset = mono ? 13 : 21;  /* Header plus the MPEG-2 Layer III side information. */
    mp3_synthetic_write(pData, frameSize, tagOffset, pTagName, 4);
    if (tagOffset + 7 < frameSize) {
        pData[tagOffset + 7] = flags;
    }
    cursor = tagOffset + 8;
    if ((flags & 0x01) != 0) {
        mp3_synthetic_write_u32(pData, frameSize, cursor, 3);
        cursor += 4;
    }
    if ((flags & 0x02) != 0) {
        mp3_synthetic_write_u32(pData, frameSize, cursor, (ma_uint32)dataSize);
        cursor += 4;
    }
    if ((flags & 0x04) != 0) {
        for (i = 0; i < sizeof(toc); i += 1) {
            toc[i] = (ma_uint8)i;
        }
        mp3_synthetic_write(pData, frameSize, cursor, toc, sizeof(toc));
        cursor += sizeof(toc);
    }
    if ((flags & 0x08) != 0) {
        mp3_synthetic_write(pData, frameSize, cursor, quality, sizeof(quality));
        cursor += sizeof(quality);
    }
    if (includeLame) {
        memset(lame, 0, sizeof(lame));
        memcpy(lame, "LAME3.100", 9);
        lame[21] = 0x07;  /* Encoder delay 123. */
        lame[22] = 0xB2;  /* Delay low nibble plus padding high nibble. */
        lame[23] = 0x58;  /* Encoder padding 600. */
        mp3_synthetic_write(pData, frameSize, cursor, lame, sizeof(lame));
    }

    return dataSize;
}

static int mp3_test_xing_flag_matrix(void)
{
    ma_uint8 data[2048];
    ma_uint32 flags;
    int failed;

    failed = 0;
    for (flags = 0; flags <= 0x0F; flags += 1) {
        ma_dr_mp3 mp3;
        ma_uint64 expectedFrameCount;
        size_t dataSize;

        dataSize = mp3_make_synthetic_xing(data, sizeof(data), 6, 2, MA_FALSE, MA_TRUE, "Xing", (ma_uint8)flags, MA_FALSE);
        if (dataSize != 648 || !ma_dr_mp3_init_memory(&mp3, data, dataSize, NULL)) {
            printf("Xing flag combination 0x%02x failed to initialize\n", flags);
            failed = 1;
            continue;
        }

        expectedFrameCount = (flags & 0x01) != 0 ? 3 * 576 : MA_UINT64_MAX;
        if (!mp3.isVBR || mp3.isCBR || mp3.streamStartOffset != 216 || mp3.totalPCMFrameCount != expectedFrameCount ||
            mp3.delayInPCMFrames != 0 || mp3.paddingInPCMFrames != 0) {
            printf("Xing flag combination 0x%02x produced vbr=%u cbr=%u start=%lu count=%u:%08u delay=%u padding=%u\n",
                flags,
                mp3.isVBR,
                mp3.isCBR,
                (unsigned long)mp3.streamStartOffset,
                (ma_uint32)(mp3.totalPCMFrameCount >> 32),
                (ma_uint32)mp3.totalPCMFrameCount,
                mp3.delayInPCMFrames,
                mp3.paddingInPCMFrames);
            failed = 1;
        }
        ma_dr_mp3_uninit(&mp3);
    }
    return failed;
}

static int mp3_test_xing_boundaries(void)
{
    typedef struct
    {
        ma_uint32 bitrateIndex;
        ma_uint32 sampleRateIndex;
        ma_bool32 padding;
        ma_bool32 mono;
        ma_uint8 flags;
        ma_bool32 includeLame;
        ma_bool32 expectComplete;
        size_t expectedFrameSize;
        const char* pName;
    } mp3_boundary_case;
    static const mp3_boundary_case cases[] = {
        {1, 1, MA_FALSE, MA_FALSE, 0x00, MA_FALSE, MA_FALSE,  24, "signature truncation"},
        {1, 0, MA_FALSE, MA_FALSE, 0x00, MA_FALSE, MA_FALSE,  26, "flags truncation"},
        {1, 1, MA_FALSE, MA_TRUE,  0x08, MA_FALSE, MA_FALSE,  24, "four-byte field before boundary"},
        {1, 1, MA_TRUE,  MA_TRUE,  0x08, MA_FALSE, MA_TRUE,   25, "four-byte field exact boundary"},
        {1, 0, MA_FALSE, MA_TRUE,  0x08, MA_FALSE, MA_TRUE,   26, "four-byte field after boundary"},
        {5, 1, MA_FALSE, MA_TRUE,  0x04, MA_FALSE, MA_FALSE, 120, "TOC before boundary"},
        {5, 1, MA_TRUE,  MA_TRUE,  0x04, MA_FALSE, MA_TRUE,  121, "TOC exact boundary"},
        {5, 0, MA_FALSE, MA_TRUE,  0x04, MA_FALSE, MA_TRUE,  130, "TOC after boundary"},
        {7, 1, MA_FALSE, MA_TRUE,  0x0F, MA_TRUE,  MA_FALSE, 168, "LAME before boundary"},
        {7, 1, MA_TRUE,  MA_TRUE,  0x0F, MA_TRUE,  MA_TRUE,  169, "LAME exact boundary"},
        {5, 2, MA_FALSE, MA_TRUE,  0x0F, MA_TRUE,  MA_TRUE,  180, "LAME after boundary"}
    };
    ma_uint8 data[2048];
    size_t i;
    int failed;

    failed = 0;
    for (i = 0; i < ma_countof(cases); i += 1) {
        ma_dr_mp3 mp3;
        float frames[32 * 2];
        size_t dataSize;
        ma_bool32 initialized;

        dataSize = mp3_make_synthetic_xing(
            data,
            sizeof(data),
            cases[i].bitrateIndex,
            cases[i].sampleRateIndex,
            cases[i].padding,
            cases[i].mono,
            "Xing",
            cases[i].flags,
            cases[i].includeLame);
        if (dataSize != cases[i].expectedFrameSize * 3) {
            printf("%s built %lu bytes instead of %lu\n", cases[i].pName, (unsigned long)dataSize, (unsigned long)(cases[i].expectedFrameSize * 3));
            failed = 1;
            continue;
        }

        initialized = ma_dr_mp3_init_memory(&mp3, data, dataSize, NULL);
        if (!initialized) {
            printf("%s failed to initialize valid audio\n", cases[i].pName);
            failed = 1;
            continue;
        }
        if ((mp3.isVBR != MA_FALSE) != (cases[i].expectComplete != MA_FALSE) || mp3.isCBR ||
            mp3.streamStartOffset != (cases[i].expectComplete ? cases[i].expectedFrameSize : 0)) {
            printf("%s produced vbr=%u cbr=%u start=%lu\n", cases[i].pName, mp3.isVBR, mp3.isCBR, (unsigned long)mp3.streamStartOffset);
            failed = 1;
        }
        if (!cases[i].expectComplete &&
            (mp3.totalPCMFrameCount != MA_UINT64_MAX || mp3.delayInPCMFrames != 0 || mp3.paddingInPCMFrames != 0)) {
            printf("%s partially committed count=%u:%08u delay=%u padding=%u\n",
                cases[i].pName,
                (ma_uint32)(mp3.totalPCMFrameCount >> 32),
                (ma_uint32)mp3.totalPCMFrameCount,
                mp3.delayInPCMFrames,
                mp3.paddingInPCMFrames);
            failed = 1;
        }
        if (cases[i].includeLame && cases[i].expectComplete &&
            (mp3.delayInPCMFrames != 652 || mp3.paddingInPCMFrames != 71)) {
            printf("%s produced delay=%u padding=%u\n", cases[i].pName, mp3.delayInPCMFrames, mp3.paddingInPCMFrames);
            failed = 1;
        }
        if (ma_dr_mp3_read_pcm_frames_f32(&mp3, 32, frames) != 32) {
            printf("%s did not preserve decodable audio\n", cases[i].pName);
            failed = 1;
        }
        ma_dr_mp3_uninit(&mp3);
    }
    return failed;
}

static int mp3_test_valid_metadata_variants(void)
{
    ma_uint8 data[2048];
    ma_dr_mp3 mp3;
    size_t dataSize;
    int failed;

    failed = 0;
    dataSize = mp3_make_synthetic_xing(data, sizeof(data), 6, 2, MA_FALSE, MA_TRUE, "Info", 0x01, MA_FALSE);
    if (!ma_dr_mp3_init_memory(&mp3, data, dataSize, NULL)) {
        printf("valid Info metadata failed to initialize\n");
        failed = 1;
    } else {
        if (!mp3.isCBR || mp3.isVBR || mp3.streamStartOffset != 216 || mp3.totalPCMFrameCount != 3 * 576) {
            printf("valid Info metadata produced cbr=%u vbr=%u start=%lu count=%u:%08u\n",
                mp3.isCBR,
                mp3.isVBR,
                (unsigned long)mp3.streamStartOffset,
                (ma_uint32)(mp3.totalPCMFrameCount >> 32),
                (ma_uint32)mp3.totalPCMFrameCount);
            failed = 1;
        }
        ma_dr_mp3_uninit(&mp3);
    }

    dataSize = mp3_make_synthetic_xing(data, sizeof(data), 6, 2, MA_FALSE, MA_TRUE, "Junk", 0x00, MA_FALSE);
    if (!ma_dr_mp3_init_memory(&mp3, data, dataSize, NULL)) {
        printf("tag-free metadata control failed to initialize\n");
        failed = 1;
    } else {
        if (mp3.isCBR || mp3.isVBR || mp3.streamStartOffset != 0 || mp3.totalPCMFrameCount != MA_UINT64_MAX) {
            printf("tag-free metadata control produced cbr=%u vbr=%u start=%lu count=%u:%08u\n",
                mp3.isCBR,
                mp3.isVBR,
                (unsigned long)mp3.streamStartOffset,
                (ma_uint32)(mp3.totalPCMFrameCount >> 32),
                (ma_uint32)mp3.totalPCMFrameCount);
            failed = 1;
        }
        ma_dr_mp3_uninit(&mp3);
    }

    dataSize = mp3_make_synthetic_xing(data, sizeof(data), 6, 2, MA_FALSE, MA_TRUE, "Xing", 0x0F, MA_TRUE);
    if (!ma_dr_mp3_init_memory(&mp3, data, dataSize, NULL)) {
        printf("valid LAME metadata failed to initialize\n");
        failed = 1;
    } else {
        if (!mp3.isVBR || mp3.delayInPCMFrames != 652 || mp3.paddingInPCMFrames != 71) {
            printf("valid LAME metadata produced vbr=%u delay=%u padding=%u\n", mp3.isVBR, mp3.delayInPCMFrames, mp3.paddingInPCMFrames);
            failed = 1;
        }
        ma_dr_mp3_uninit(&mp3);
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

static int mp3_test_callback_buffer_invariant(void)
{
    ma_dr_mp3 mp3;
    mp3_invalid_callback_state state;
    ma_uint32 framesRead;

    MA_ZERO_OBJECT(&mp3);
    MA_ZERO_OBJECT(&state);
    mp3.onRead = mp3_invalid_buffer_read;
    mp3.pUserData = &state;
    mp3.dataCapacity = MA_DR_MP3_DATA_CHUNK_SIZE;

    framesRead = ma_dr_mp3_decode_next_frame_ex__callbacks(&mp3, NULL, NULL, NULL);
    if (framesRead != 0 || state.readCount != 0 || state.nullDestinationCount != 0) {
        printf("MP3 callback buffer invariant mismatch: frames=%u reads=%u null-destinations=%u\n",
            framesRead, state.readCount, state.nullDestinationCount);
        return 1;
    }

    return 0;
}

int main(void)
{
    int failed = 0;
    failed += mp3_test_optional_tell();
    failed += mp3_test_truncated_metadata();
    failed += mp3_test_xing_flag_matrix();
    failed += mp3_test_xing_boundaries();
    failed += mp3_test_valid_metadata_variants();
    failed += mp3_test_frame_count();
    failed += mp3_test_full_decode_allocation();
    failed += mp3_test_resource_manager_allocation();
    failed += mp3_test_callback_buffer_invariant();
    return failed != 0;
}
