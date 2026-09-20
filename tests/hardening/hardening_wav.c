#define MA_NO_DEVICE_IO
#include "../../miniaudio.c"

#include <stdio.h>
#include <string.h>

typedef struct
{
    ma_decoder memory;
    ma_uint32 readCount;
    ma_uint32 seekCount;
    ma_bool32 operationBudgetExhausted;
} hardening_wav_stream;

static const ma_uint8 g_wav_1151[] = {
    0x52,0x49,0x46,0x46,0x24,0x77,0x01,0x00,0x57,0x41,0x56,0x45,
    0x66,0x6d,0x74,0x20,0xff,0xff,0xff,0xff,0xff,0xff,0x08,0x00,
    0x10,0x00,0x00,0x00,0x01,0x00,0x47,0x00,0x00,0x00,0x00,0x00,
    0x27,0x00,0x00,0x02,0x00,0x01,0x00,0x10,0x20,0x64,0x61,0x74,
    0x61,0x00,0x56,0x56,0x56,0x56,0x2f,0xff,0xff,0xff,0x6f,0x2d,
    0x00,0x80,0x58,0xad,0xff,0xa6,0x60,0x04,0x00
};

static const ma_uint8 g_wav_1123[] = {
    0x52,0x49,0x46,0x46,0x98,0x00,0x00,0x00,0x57,0x41,0x56,0x45,
    0x66,0x6d,0x74,0x20,0x14,0x00,0x00,0x00,0x02,0x00,0x02,0x00,
    0x44,0xac,0x00,0x00,0x44,0xac,0x00,0x00,0x00,0x01,0x04,0x00,
    0x02,0x00,0x00,0x00,0x66,0x61,0x63,0x74,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x64,0x61,0x74,0x61,0x64,0x00,0x00,0x00
};

static ma_result hardening_wav_read(ma_decoder* pDecoder, void* pBufferOut, size_t bytesToRead, size_t* pBytesRead)
{
    hardening_wav_stream* pStream = (hardening_wav_stream*)pDecoder->pUserData;

    pStream->readCount += 1;
    if (pStream->readCount > 32) {
        pStream->operationBudgetExhausted = MA_TRUE;
        return MA_ERROR;
    }

    return ma_decoder__on_read_memory(&pStream->memory, pBufferOut, bytesToRead, pBytesRead);
}

static ma_result hardening_wav_seek(ma_decoder* pDecoder, ma_int64 byteOffset, ma_seek_origin origin)
{
    hardening_wav_stream* pStream = (hardening_wav_stream*)pDecoder->pUserData;

    pStream->seekCount += 1;
    if (pStream->seekCount > 16) {
        pStream->operationBudgetExhausted = MA_TRUE;
        return MA_BAD_SEEK;
    }

    return ma_decoder__on_seek_memory(&pStream->memory, byteOffset, origin);
}

static int hardening_wav_test_bounded_callbacks(const ma_uint8* pData, size_t dataSize, ma_bool32 expectSuccess, const char* pName)
{
    hardening_wav_stream stream;
    ma_decoder decoder;
    ma_result result;

    MA_ZERO_OBJECT(&stream);
    stream.memory.data.memory.pData = pData;
    stream.memory.data.memory.dataSize = dataSize;

    result = ma_decoder_init(hardening_wav_read, hardening_wav_seek, &stream, NULL, &decoder);
    if (result == MA_SUCCESS) {
        ma_decoder_uninit(&decoder);
    }

    if ((result == MA_SUCCESS) != expectSuccess) {
        printf("%s returned %d, expected %s\n", pName, result, expectSuccess ? "success" : "failure");
        return 1;
    }

    if (stream.operationBudgetExhausted) {
        printf("%s exhausted callback budget (reads=%u, seeks=%u)\n", pName, stream.readCount, stream.seekCount);
        return 1;
    }

    return 0;
}

static int hardening_wav_test_memory(const ma_uint8* pData, size_t dataSize, const char* pName)
{
    ma_decoder decoder;
    ma_uint64 frameCount = 0;
    void* pFrames = NULL;
    ma_result result;

    result = ma_decoder_init_memory(pData, dataSize, NULL, &decoder);

    if (result == MA_SUCCESS) {
        ma_decoder_uninit(&decoder);
        printf("%s unexpectedly initialized from memory\n", pName);
        return 1;
    }

    result = ma_decode_memory(pData, dataSize, NULL, &frameCount, &pFrames);
    if (pFrames != NULL) {
        ma_free(pFrames, NULL);
    }
    if (result == MA_SUCCESS) {
        printf("%s unexpectedly decoded from memory\n", pName);
        return 1;
    }

    return 0;
}

static void hardening_wav_write_u32le(ma_uint8* pData, ma_uint32 value)
{
    pData[0] = (ma_uint8)(value >> 0);
    pData[1] = (ma_uint8)(value >> 8);
    pData[2] = (ma_uint8)(value >> 16);
    pData[3] = (ma_uint8)(value >> 24);
}

static size_t hardening_wav_make_fmt(ma_uint8* pData, ma_uint32 fmtSize, ma_uint16 extendedSize)
{
    size_t dataOffset = 20 + fmtSize + (fmtSize & 1);

    MA_ZERO_MEMORY(pData, 128);
    MA_COPY_MEMORY(pData + 0, "RIFF", 4);
    hardening_wav_write_u32le(pData + 4, (ma_uint32)(dataOffset + 8 - 8));
    MA_COPY_MEMORY(pData + 8, "WAVEfmt ", 8);
    hardening_wav_write_u32le(pData + 16, fmtSize);
    if (fmtSize >= 16) {
        pData[20] = 1;
        pData[22] = 1;
        hardening_wav_write_u32le(pData + 24, 8000);
        hardening_wav_write_u32le(pData + 28, 16000);
        pData[32] = 2;
        pData[34] = 16;
    }
    if (fmtSize >= 18) {
        pData[36] = (ma_uint8)(extendedSize >> 0);
        pData[37] = (ma_uint8)(extendedSize >> 8);
    }
    MA_COPY_MEMORY(pData + dataOffset, "data", 4);
    hardening_wav_write_u32le(pData + dataOffset + 4, 0);
    return dataOffset + 8;
}

static int hardening_wav_test_fmt_boundaries(void)
{
    ma_uint8 data[128];
    ma_uint32 fmtSize;
    int failed = 0;

    for (fmtSize = 0; fmtSize <= 17; fmtSize += 1) {
        size_t dataSize = hardening_wav_make_fmt(data, fmtSize, 0);
        ma_bool32 expectSuccess = fmtSize == 16;
        failed += hardening_wav_test_bounded_callbacks(data, dataSize, expectSuccess, "fmt size boundary");
    }

    failed += hardening_wav_test_bounded_callbacks(data, hardening_wav_make_fmt(data, 18, 0), MA_TRUE, "empty fmt extension");
    failed += hardening_wav_test_bounded_callbacks(data, hardening_wav_make_fmt(data, 18, 1), MA_FALSE, "oversized fmt extension");
    failed += hardening_wav_test_bounded_callbacks(data, hardening_wav_make_fmt(data, 19, 1), MA_TRUE, "exact fmt extension");

    return failed;
}

static int hardening_wav_test_file(const ma_uint8* pData, size_t dataSize, const char* pName)
{
    const char* pPath = "miniaudio-hardening-malformed.wav";
    FILE* pFile;
    ma_decoder decoder;
    ma_result result;

    pFile = fopen(pPath, "wb");
    if (pFile == NULL) {
        printf("%s could not create temporary file\n", pName);
        return 1;
    }
    if (fwrite(pData, 1, dataSize, pFile) != dataSize || fclose(pFile) != 0) {
        remove(pPath);
        printf("%s could not write temporary file\n", pName);
        return 1;
    }

    result = ma_decoder_init_file(pPath, NULL, &decoder);
    remove(pPath);
    if (result == MA_SUCCESS) {
        ma_decoder_uninit(&decoder);
        printf("%s unexpectedly initialized from file\n", pName);
        return 1;
    }

    return 0;
}

int main(void)
{
    int failed = 0;

    failed += hardening_wav_test_bounded_callbacks(g_wav_1151, sizeof(g_wav_1151), MA_FALSE, "issue 1151");
    failed += hardening_wav_test_bounded_callbacks(g_wav_1123, sizeof(g_wav_1123), MA_FALSE, "issue 1123");
    failed += hardening_wav_test_fmt_boundaries();

    /* These calls are reached only when the callback-budget assertions prove
       the malformed parser paths make bounded progress. */
    if (failed == 0) {
        failed += hardening_wav_test_memory(g_wav_1151, sizeof(g_wav_1151), "issue 1151");
        failed += hardening_wav_test_memory(g_wav_1123, sizeof(g_wav_1123), "issue 1123");
        failed += hardening_wav_test_file(g_wav_1151, sizeof(g_wav_1151), "issue 1151");
        failed += hardening_wav_test_file(g_wav_1123, sizeof(g_wav_1123), "issue 1123");
    }

    return failed != 0;
}
