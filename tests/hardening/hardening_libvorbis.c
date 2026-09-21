#define MA_NO_DEVICE_IO
#define MINIAUDIO_IMPLEMENTATION
#include "../../miniaudio.h"
#include "../../extras/decoders/libvorbis/miniaudio_libvorbis.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HARDENING_INT64_MAX ((ma_int64)(MA_UINT64_MAX >> 1))
#define HARDENING_INT64_MIN (-HARDENING_INT64_MAX - 1)

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
} hardening_vorbis_stream;

static ma_result hardening_vorbis_read(void* pUserData, void* pBufferOut, size_t bytesToRead, size_t* pBytesRead)
{
    hardening_vorbis_stream* pStream;
    size_t bytesRemaining;

    pStream = (hardening_vorbis_stream*)pUserData;
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

static ma_result hardening_vorbis_seek(void* pUserData, ma_int64 offset, ma_seek_origin origin)
{
    hardening_vorbis_stream* pStream;
    ma_int64 base;
    ma_int64 target;

    pStream = (hardening_vorbis_stream*)pUserData;
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

static ma_result hardening_vorbis_tell(void* pUserData, ma_int64* pCursor)
{
    hardening_vorbis_stream* pStream;

    pStream = (hardening_vorbis_stream*)pUserData;
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

static void hardening_vorbis_stream_init(hardening_vorbis_stream* pStream, const unsigned char* pData, size_t dataSize)
{
    MA_ZERO_OBJECT(pStream);
    pStream->pData = pData;
    pStream->dataSize = dataSize;
    pStream->active = MA_TRUE;
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

static int hardening_test_direct(const unsigned char* pData, size_t dataSize)
{
    hardening_vorbis_stream stream;
    ma_decoding_backend_config config;
    ma_libvorbis vorbis;
    ma_result result;
    ma_format format;
    ma_uint32 channels;
    ma_uint32 sampleRate;
    ma_uint64 length;
    ma_uint64 framesRead;
    float frames[257 * 2];
    size_t i;
    double magnitude;
    ma_uint32 callbackCountBeforeRetirement;
    int failed;

    failed = 0;
    hardening_vorbis_stream_init(&stream, pData, dataSize);
    config = ma_decoding_backend_config_init(ma_format_f32, 0);
    result = ma_libvorbis_init(hardening_vorbis_read, hardening_vorbis_seek, hardening_vorbis_tell, &stream, &config, NULL, &vorbis);
    if (result != MA_SUCCESS) {
        printf("direct Vorbis initialization failed: %d\n", result);
        return 1;
    }
    result = ma_libvorbis_get_data_format(&vorbis, &format, &channels, &sampleRate, NULL, 0);
    if (result != MA_SUCCESS || format != ma_format_f32 || channels != 2 || sampleRate != 48000) {
        printf("direct Vorbis format was result=%d format=%d channels=%u rate=%u\n", result, format, channels, sampleRate);
        failed = 1;
    }
    result = ma_libvorbis_get_length_in_pcm_frames(&vorbis, &length);
    if (result != MA_SUCCESS || length < 1600000 || length > 1800000) {
        printf("direct Vorbis length was result=%d length=%lu\n", result, (unsigned long)length);
        failed = 1;
    }
    result = ma_libvorbis_seek_to_pcm_frame(&vorbis, 48000);
    framesRead = 0;
    if (result != MA_SUCCESS || ma_libvorbis_read_pcm_frames(&vorbis, frames, 257, &framesRead) != MA_SUCCESS || framesRead != 257) {
        printf("direct Vorbis seek/read failed with %lu frames\n", (unsigned long)framesRead);
        failed = 1;
    }
    magnitude = 0;
    for (i = 0; i < (size_t)framesRead * 2; ++i) {
        magnitude += frames[i] < 0 ? -frames[i] : frames[i];
    }
    if (magnitude <= 0.001) {
        printf("direct Vorbis read did not produce an independently observable signal\n");
        failed = 1;
    }
    if (length >= 100) {
        result = ma_libvorbis_seek_to_pcm_frame(&vorbis, length - 100);
        framesRead = 0;
        if (result != MA_SUCCESS) {
            failed = 1;
        } else {
            result = ma_libvorbis_read_pcm_frames(&vorbis, frames, 257, &framesRead);
            if (result != MA_AT_END || framesRead != 100) {
                printf("direct Vorbis tail returned %d with %lu frames\n", result, (unsigned long)framesRead);
                failed = 1;
            }
        }
    }

    callbackCountBeforeRetirement = stream.readCount + stream.seekCount + stream.tellCount;
    ma_libvorbis_uninit(&vorbis, NULL);
    stream.active = MA_FALSE;
    if (callbackCountBeforeRetirement != stream.readCount + stream.seekCount + stream.tellCount || stream.callbackAfterRetirementCount != 0) {
        printf("direct Vorbis callback userdata was used after owner retirement\n");
        failed = 1;
    }
    return failed;
}

static int hardening_test_optional_tell(const unsigned char* pData, size_t dataSize)
{
    hardening_vorbis_stream stream;
    ma_decoding_backend_config config;
    ma_libvorbis vorbis;
    ma_result result;
    ma_uint64 framesRead;
    float frames[128 * 2];
    int failed;

    failed = 0;
    hardening_vorbis_stream_init(&stream, pData, dataSize);
    config = ma_decoding_backend_config_init(ma_format_f32, 0);
    result = ma_libvorbis_init(hardening_vorbis_read, hardening_vorbis_seek, NULL, &stream, &config, NULL, &vorbis);
    if (result != MA_SUCCESS) {
        printf("Vorbis initialization with optional tell callback failed: %d\n", result);
        return 1;
    }
    framesRead = 0;
    result = ma_libvorbis_read_pcm_frames(&vorbis, frames, 128, &framesRead);
    if (result != MA_SUCCESS || framesRead != 128) {
        printf("unseekable Vorbis read returned %d with %lu frames\n", result, (unsigned long)framesRead);
        failed = 1;
    }
    ma_libvorbis_uninit(&vorbis, NULL);
    stream.active = MA_FALSE;
    if (stream.callbackAfterRetirementCount != 0) {
        printf("unseekable Vorbis used callback userdata after retirement\n");
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
    hardening_vorbis_stream stream;
    ma_decoding_backend_config config;
    ma_libvorbis vorbis;
    ma_result result;
    ma_uint64 framesRead;
    float frames[256 * 2];
    ma_uint32 iteration;
    int failed;

    failed = 0;
    config = ma_decoding_backend_config_init(ma_format_f32, 0);
    hardening_vorbis_stream_init(&stream, malformedData, sizeof(malformedData));
    result = ma_libvorbis_init(hardening_vorbis_read, hardening_vorbis_seek, hardening_vorbis_tell, &stream, &config, NULL, &vorbis);
    if (result != MA_INVALID_FILE) {
        printf("malformed Vorbis initialization returned %d\n", result);
        if (result == MA_SUCCESS) {
            ma_libvorbis_uninit(&vorbis, NULL);
        }
        failed = 1;
    }
    stream.active = MA_FALSE;

    hardening_vorbis_stream_init(&stream, pData, dataSize / 2);
    result = ma_libvorbis_init(hardening_vorbis_read, hardening_vorbis_seek, hardening_vorbis_tell, &stream, &config, NULL, &vorbis);
    if (result == MA_SUCCESS) {
        result = MA_SUCCESS;
        for (iteration = 0; iteration < 8192 && result == MA_SUCCESS; ++iteration) {
            framesRead = 0;
            result = ma_libvorbis_read_pcm_frames(&vorbis, frames, 256, &framesRead);
        }
        if (iteration == 8192 || (result != MA_AT_END && result != MA_ERROR)) {
            printf("truncated Vorbis did not terminate with a bounded decoder result: %d\n", result);
            failed = 1;
        }
        ma_libvorbis_uninit(&vorbis, NULL);
    } else if (result != MA_INVALID_FILE) {
        printf("truncated Vorbis initialization returned unexpected result %d\n", result);
        failed = 1;
    }
    stream.active = MA_FALSE;
    if (stream.callbackAfterRetirementCount != 0) {
        printf("malformed/truncated Vorbis used callback userdata after retirement\n");
        failed = 1;
    }
    return failed;
}

static int hardening_rewrite_ogg_serials(unsigned char* pData, size_t dataSize)
{
    size_t offset;

    offset = 0;
    while (offset < dataSize) {
        ogg_page page;
        size_t segmentCount;
        size_t headerSize;
        size_t bodySize;
        size_t i;
        ma_uint32 serial;

        if (dataSize - offset < 27 || memcmp(pData + offset, "OggS", 4) != 0) {
            return 0;
        }
        segmentCount = pData[offset + 26];
        headerSize = 27 + segmentCount;
        if (headerSize > dataSize - offset) {
            return 0;
        }
        bodySize = 0;
        for (i = 0; i < segmentCount; ++i) {
            bodySize += pData[offset + 27 + i];
        }
        if (bodySize > dataSize - offset - headerSize) {
            return 0;
        }
        serial = ((ma_uint32)pData[offset + 14]      ) |
                 ((ma_uint32)pData[offset + 15] <<  8) |
                 ((ma_uint32)pData[offset + 16] << 16) |
                 ((ma_uint32)pData[offset + 17] << 24);
        serial += 1;
        pData[offset + 14] = (unsigned char)(serial >> 0);
        pData[offset + 15] = (unsigned char)(serial >> 8);
        pData[offset + 16] = (unsigned char)(serial >> 16);
        pData[offset + 17] = (unsigned char)(serial >> 24);
        pData[offset + 22] = 0;
        pData[offset + 23] = 0;
        pData[offset + 24] = 0;
        pData[offset + 25] = 0;
        page.header = pData + offset;
        page.header_len = (long)headerSize;
        page.body = pData + offset + headerSize;
        page.body_len = (long)bodySize;
        ogg_page_checksum_set(&page);
        offset += headerSize + bodySize;
    }
    return 1;
}

static int hardening_test_chained(const unsigned char* pData, size_t dataSize)
{
    unsigned char* pChainedData;
    hardening_vorbis_stream stream;
    ma_decoding_backend_config config;
    ma_libvorbis vorbis;
    ma_result result;
    ma_uint64 framesRead;
    ma_uint64 totalFramesRead;
    float frames[1024 * 2];
    ma_uint32 iteration;
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
    if (!hardening_rewrite_ogg_serials(pChainedData + dataSize, dataSize)) {
        free(pChainedData);
        return 1;
    }

    failed = 0;
    hardening_vorbis_stream_init(&stream, pChainedData, dataSize * 2);
    config = ma_decoding_backend_config_init(ma_format_f32, 0);
    result = ma_libvorbis_init(hardening_vorbis_read, hardening_vorbis_seek, hardening_vorbis_tell, &stream, &config, NULL, &vorbis);
    if (result != MA_SUCCESS) {
        printf("chained Vorbis initialization failed: %d\n", result);
        failed = 1;
    } else {
        totalFramesRead = 0;
        result = MA_SUCCESS;
        for (iteration = 0; iteration < 4096 && result == MA_SUCCESS; ++iteration) {
            framesRead = 0;
            result = ma_libvorbis_read_pcm_frames(&vorbis, frames, 1024, &framesRead);
            totalFramesRead += framesRead;
        }
        if (result != MA_AT_END || totalFramesRead < 3200000 || totalFramesRead > 3600000) {
            printf("chained Vorbis returned %d with %lu frames\n", result, (unsigned long)totalFramesRead);
            failed = 1;
        }
        ma_libvorbis_uninit(&vorbis, NULL);
    }
    stream.active = MA_FALSE;
    if (stream.callbackAfterRetirementCount != 0) {
        printf("chained Vorbis used callback userdata after retirement\n");
        failed = 1;
    }
    free(pChainedData);
    return failed;
}

int main(int argc, char** argv)
{
    unsigned char* pData;
    size_t dataSize;
    int failed;

    if (argc != 2) {
        printf("usage: hardening_libvorbis <valid-vorbis-file>\n");
        return 1;
    }
    pData = hardening_read_file(argv[1], &dataSize);
    if (pData == NULL) {
        printf("failed to load Vorbis fixture: %s\n", argv[1]);
        return 1;
    }

    failed = 0;
    failed += hardening_test_direct(pData, dataSize);
    failed += hardening_test_optional_tell(pData, dataSize);
    failed += hardening_test_malformed_and_truncated(pData, dataSize);
    failed += hardening_test_chained(pData, dataSize);
    free(pData);
    return failed != 0;
}
