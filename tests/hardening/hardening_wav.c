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

    if ((result == MA_SUCCESS) != (expectSuccess != MA_FALSE)) {
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

static void hardening_wav_write_u64le(ma_uint8* pData, ma_uint64 value)
{
    hardening_wav_write_u32le(pData + 0, (ma_uint32)(value >> 0));
    hardening_wav_write_u32le(pData + 4, (ma_uint32)(value >> 32));
}

static void hardening_wav_write_u16le(ma_uint8* pData, ma_uint16 value)
{
    pData[0] = (ma_uint8)(value >> 0);
    pData[1] = (ma_uint8)(value >> 8);
}

static void hardening_wav_write_u16be(ma_uint8* pData, ma_uint16 value)
{
    pData[0] = (ma_uint8)(value >> 8);
    pData[1] = (ma_uint8)(value >> 0);
}

static void hardening_wav_write_u32be(ma_uint8* pData, ma_uint32 value)
{
    pData[0] = (ma_uint8)(value >> 24);
    pData[1] = (ma_uint8)(value >> 16);
    pData[2] = (ma_uint8)(value >> 8);
    pData[3] = (ma_uint8)(value >> 0);
}

static size_t hardening_wav_make_rifx_fact(ma_uint8* pData, ma_uint32 factSize)
{
    size_t factOffset;
    size_t dataOffset;
    size_t totalSize;

    factOffset = 36;
    dataOffset = factOffset + 8 + factSize + (factSize & 1);
    totalSize = dataOffset + 10;
    MA_ZERO_MEMORY(pData, 160);
    MA_COPY_MEMORY(pData + 0, "RIFX", 4);
    hardening_wav_write_u32be(pData + 4, (ma_uint32)(totalSize - 8));
    MA_COPY_MEMORY(pData + 8, "WAVEfmt ", 8);
    hardening_wav_write_u32be(pData + 16, 16);
    hardening_wav_write_u16be(pData + 20, MA_DR_WAVE_FORMAT_PCM);
    hardening_wav_write_u16be(pData + 22, 1);
    hardening_wav_write_u32be(pData + 24, 8000);
    hardening_wav_write_u32be(pData + 28, 16000);
    hardening_wav_write_u16be(pData + 32, 2);
    hardening_wav_write_u16be(pData + 34, 16);
    MA_COPY_MEMORY(pData + factOffset, "fact", 4);
    hardening_wav_write_u32be(pData + factOffset + 4, factSize);
    if (factSize >= 4) {
        hardening_wav_write_u32be(pData + factOffset + 8, 1);
    }
    MA_COPY_MEMORY(pData + dataOffset, "data", 4);
    hardening_wav_write_u32be(pData + dataOffset + 4, 2);
    return totalSize;
}

static size_t hardening_wav_make_w64_fact(ma_uint8* pData, ma_uint32 factSize)
{
    size_t factOffset;
    size_t factPadding;
    size_t dataOffset;
    size_t totalSize;

    factOffset = 80;
    factPadding = (8 - (factSize & 7)) & 7;
    dataOffset = factOffset + 24 + factSize + factPadding;
    totalSize = dataOffset + 32;
    MA_ZERO_MEMORY(pData, 256);
    MA_COPY_MEMORY(pData + 0, ma_dr_wavGUID_W64_RIFF, 16);
    hardening_wav_write_u64le(pData + 16, totalSize);
    MA_COPY_MEMORY(pData + 24, ma_dr_wavGUID_W64_WAVE, 16);
    MA_COPY_MEMORY(pData + 40, ma_dr_wavGUID_W64_FMT, 16);
    hardening_wav_write_u64le(pData + 56, 40);
    hardening_wav_write_u16le(pData + 64, MA_DR_WAVE_FORMAT_PCM);
    hardening_wav_write_u16le(pData + 66, 1);
    hardening_wav_write_u32le(pData + 68, 8000);
    hardening_wav_write_u32le(pData + 72, 16000);
    hardening_wav_write_u16le(pData + 76, 2);
    hardening_wav_write_u16le(pData + 78, 16);
    MA_COPY_MEMORY(pData + factOffset, ma_dr_wavGUID_W64_FACT, 16);
    hardening_wav_write_u64le(pData + factOffset + 16, 24 + factSize);
    if (factSize >= 8) {
        hardening_wav_write_u64le(pData + factOffset + 24, 1);
    }
    MA_COPY_MEMORY(pData + dataOffset, ma_dr_wavGUID_W64_DATA, 16);
    hardening_wav_write_u64le(pData + dataOffset + 16, 26);
    return totalSize;
}

static size_t hardening_wav_make_short_w64_chunk(ma_uint8* pData, ma_uint64 totalChunkSize)
{
    MA_ZERO_MEMORY(pData, 64);
    MA_COPY_MEMORY(pData + 0, ma_dr_wavGUID_W64_RIFF, 16);
    hardening_wav_write_u64le(pData + 16, 64);
    MA_COPY_MEMORY(pData + 24, ma_dr_wavGUID_W64_WAVE, 16);
    MA_COPY_MEMORY(pData + 40, ma_dr_wavGUID_W64_FMT, 16);
    hardening_wav_write_u64le(pData + 56, totalChunkSize);
    return 64;
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

    if (ma_fopen(&pFile, pPath, "wb") != MA_SUCCESS) {
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

static int hardening_wav_validate_pcm(
    const void* pData,
    size_t dataSize,
    ma_dr_wav_container expectedContainer,
    const ma_int16* pExpectedFrames,
    ma_uint64 expectedFrameCount,
    const char* pName)
{
    ma_dr_wav wav;
    ma_int16 frames[4];
    ma_uint64 length = 0;
    ma_uint64 framesRead;

    MA_ZERO_MEMORY(frames, sizeof(frames));
    if (!ma_dr_wav_init_memory(&wav, pData, dataSize, NULL)) {
        printf("%s failed to initialize\n", pName);
        return 1;
    }
    if (wav.container != expectedContainer || wav.translatedFormatTag != MA_DR_WAVE_FORMAT_PCM ||
        wav.channels != 1 || wav.sampleRate != 8000 || wav.bitsPerSample != 16 ||
        ma_dr_wav_get_length_in_pcm_frames(&wav, &length) != MA_SUCCESS || length != expectedFrameCount) {
        printf("%s format mismatch: container=%d tag=%u channels=%u rate=%u bits=%u length=%u:%08u\n",
            pName, wav.container, wav.translatedFormatTag, wav.channels, wav.sampleRate, wav.bitsPerSample,
            (ma_uint32)(length >> 32), (ma_uint32)length);
        ma_dr_wav_uninit(&wav);
        return 1;
    }
    framesRead = ma_dr_wav_read_pcm_frames_s16(&wav, expectedFrameCount, frames);
    if (framesRead != expectedFrameCount || memcmp(frames, pExpectedFrames, (size_t)expectedFrameCount * sizeof(frames[0])) != 0 ||
        ma_dr_wav_read_pcm_frames_s16(&wav, 1, frames) != 0) {
        printf("%s PCM round trip mismatch: frames=%u:%08u first=%d second=%d\n",
            pName, (ma_uint32)(framesRead >> 32), (ma_uint32)framesRead, frames[0], frames[1]);
        ma_dr_wav_uninit(&wav);
        return 1;
    }

    ma_dr_wav_uninit(&wav);
    return 0;
}

static int hardening_wav_test_writer_container(ma_dr_wav_container container, const char* pName)
{
    const ma_int16 expectedFrames[2] = {0x1234, -0x2345};
    ma_dr_wav_data_format format;
    ma_dr_wav wav;
    void* pData = NULL;
    size_t dataSize = 0;
    int failed;

    format.container = container;
    format.format = MA_DR_WAVE_FORMAT_PCM;
    format.channels = 1;
    format.sampleRate = 8000;
    format.bitsPerSample = 16;
    if (!ma_dr_wav_init_memory_write(&wav, &pData, &dataSize, &format, NULL)) {
        printf("%s writer failed to initialize\n", pName);
        return 1;
    }
    if (ma_dr_wav_write_pcm_frames(&wav, 2, expectedFrames) != 2 || ma_dr_wav_uninit(&wav) != MA_SUCCESS) {
        printf("%s writer failed to finalize\n", pName);
        ma_dr_wav_free(pData, NULL);
        return 1;
    }

    failed = hardening_wav_validate_pcm(pData, dataSize, container, expectedFrames, 2, pName);
    ma_dr_wav_free(pData, NULL);
    return failed;
}

static size_t hardening_wav_make_rifx_pcm(ma_uint8* pData)
{
    MA_ZERO_MEMORY(pData, 48);
    MA_COPY_MEMORY(pData + 0, "RIFX", 4);
    hardening_wav_write_u32be(pData + 4, 40);
    MA_COPY_MEMORY(pData + 8, "WAVEfmt ", 8);
    hardening_wav_write_u32be(pData + 16, 16);
    hardening_wav_write_u16be(pData + 20, MA_DR_WAVE_FORMAT_PCM);
    hardening_wav_write_u16be(pData + 22, 1);
    hardening_wav_write_u32be(pData + 24, 8000);
    hardening_wav_write_u32be(pData + 28, 16000);
    hardening_wav_write_u16be(pData + 32, 2);
    hardening_wav_write_u16be(pData + 34, 16);
    MA_COPY_MEMORY(pData + 36, "data", 4);
    hardening_wav_write_u32be(pData + 40, 4);
    hardening_wav_write_u16be(pData + 44, 0x1234);
    hardening_wav_write_u16be(pData + 46, (ma_uint16)-0x2345);
    return 48;
}

static size_t hardening_wav_make_aiff_pcm(ma_uint8* pData)
{
    static const ma_uint8 sampleRate8000[10] = {0x40, 0x0B, 0xFA, 0, 0, 0, 0, 0, 0, 0};

    MA_ZERO_MEMORY(pData, 58);
    MA_COPY_MEMORY(pData + 0, "FORM", 4);
    hardening_wav_write_u32be(pData + 4, 50);
    MA_COPY_MEMORY(pData + 8, "AIFFCOMM", 8);
    hardening_wav_write_u32be(pData + 16, 18);
    hardening_wav_write_u16be(pData + 20, 1);
    hardening_wav_write_u32be(pData + 22, 2);
    hardening_wav_write_u16be(pData + 26, 16);
    MA_COPY_MEMORY(pData + 28, sampleRate8000, sizeof(sampleRate8000));
    MA_COPY_MEMORY(pData + 38, "SSND", 4);
    hardening_wav_write_u32be(pData + 42, 12);
    hardening_wav_write_u32be(pData + 46, 0);
    hardening_wav_write_u32be(pData + 50, 0);
    hardening_wav_write_u16be(pData + 54, 0x1234);
    hardening_wav_write_u16be(pData + 56, (ma_uint16)-0x2345);
    return 58;
}

static size_t hardening_wav_make_ima_adpcm(ma_uint8* pData, ma_uint32 factSize)
{
    size_t factOffset = 40;
    size_t dataOffset = factOffset + 8 + factSize + (factSize & 1);
    size_t totalSize = dataOffset + 16;

    MA_ZERO_MEMORY(pData, 128);
    MA_COPY_MEMORY(pData + 0, "RIFF", 4);
    hardening_wav_write_u32le(pData + 4, (ma_uint32)(totalSize - 8));
    MA_COPY_MEMORY(pData + 8, "WAVEfmt ", 8);
    hardening_wav_write_u32le(pData + 16, 20);
    hardening_wav_write_u16le(pData + 20, MA_DR_WAVE_FORMAT_DVI_ADPCM);
    hardening_wav_write_u16le(pData + 22, 1);
    hardening_wav_write_u32le(pData + 24, 8000);
    hardening_wav_write_u32le(pData + 28, 7111);
    hardening_wav_write_u16le(pData + 32, 8);
    hardening_wav_write_u16le(pData + 34, 4);
    hardening_wav_write_u16le(pData + 36, 2);
    hardening_wav_write_u16le(pData + 38, 9);
    MA_COPY_MEMORY(pData + factOffset, "fact", 4);
    hardening_wav_write_u32le(pData + factOffset + 4, factSize);
    if (factSize >= 4) {
        hardening_wav_write_u32le(pData + factOffset + 8, 9);
    }
    MA_COPY_MEMORY(pData + dataOffset, "data", 4);
    hardening_wav_write_u32le(pData + dataOffset + 4, 8);
    /* Predictor, step index, reserved byte and four zero nibbles decode to nine zero frames. */
    return totalSize;
}

static size_t hardening_wav_make_ms_adpcm(ma_uint8* pData)
{
    static const ma_int16 coefficients[14] = {
        256, 0, 512, -256, 0, 0, 192, 64, 240, 0, 460, -208, 392, -232
    };
    size_t i;
    size_t factOffset = 70;
    size_t dataOffset = 82;
    size_t totalSize = 98;

    MA_ZERO_MEMORY(pData, 128);
    MA_COPY_MEMORY(pData + 0, "RIFF", 4);
    hardening_wav_write_u32le(pData + 4, (ma_uint32)(totalSize - 8));
    MA_COPY_MEMORY(pData + 8, "WAVEfmt ", 8);
    hardening_wav_write_u32le(pData + 16, 50);
    hardening_wav_write_u16le(pData + 20, MA_DR_WAVE_FORMAT_ADPCM);
    hardening_wav_write_u16le(pData + 22, 1);
    hardening_wav_write_u32le(pData + 24, 8000);
    hardening_wav_write_u32le(pData + 28, 16000);
    hardening_wav_write_u16le(pData + 32, 8);
    hardening_wav_write_u16le(pData + 34, 4);
    hardening_wav_write_u16le(pData + 36, 32);
    hardening_wav_write_u16le(pData + 38, 4);
    hardening_wav_write_u16le(pData + 40, 7);
    for (i = 0; i < ma_countof(coefficients); i += 1) {
        hardening_wav_write_u16le(pData + 42 + i * 2, (ma_uint16)coefficients[i]);
    }
    MA_COPY_MEMORY(pData + factOffset, "fact", 4);
    hardening_wav_write_u32le(pData + factOffset + 4, 4);
    hardening_wav_write_u32le(pData + factOffset + 8, 4);
    MA_COPY_MEMORY(pData + dataOffset, "data", 4);
    hardening_wav_write_u32le(pData + dataOffset + 4, 8);
    pData[dataOffset + 8] = 0; /* Predictor index. */
    hardening_wav_write_u16le(pData + dataOffset + 9, 16);
    /* Two initial samples and the final two encoded nibbles remain zero. */
    return totalSize;
}

static int hardening_wav_validate_adpcm(const ma_uint8* pData, size_t dataSize, ma_uint16 expectedTag, ma_uint64 expectedFrameCount, const char* pName)
{
    ma_dr_wav wav;
    ma_int16 frames[16];
    ma_uint64 framesRead;
    ma_uint64 i;

    MA_ZERO_MEMORY(frames, sizeof(frames));
    if (!ma_dr_wav_init_memory(&wav, pData, dataSize, NULL)) {
        printf("%s failed to initialize\n", pName);
        return 1;
    }
    framesRead = ma_dr_wav_read_pcm_frames_s16(&wav, expectedFrameCount, frames);
    if (wav.translatedFormatTag != expectedTag || wav.totalPCMFrameCount != expectedFrameCount || framesRead != expectedFrameCount) {
        printf("%s format/count mismatch: tag=%u length=%u:%08u read=%u:%08u\n",
            pName, wav.translatedFormatTag, (ma_uint32)(wav.totalPCMFrameCount >> 32),
            (ma_uint32)wav.totalPCMFrameCount, (ma_uint32)(framesRead >> 32), (ma_uint32)framesRead);
        ma_dr_wav_uninit(&wav);
        return 1;
    }
    for (i = 0; i < framesRead; i += 1) {
        if (frames[i] != 0) {
            printf("%s decoded nonzero frame %u:%08u: %d\n", pName, (ma_uint32)(i >> 32), (ma_uint32)i, frames[i]);
            ma_dr_wav_uninit(&wav);
            return 1;
        }
    }
    ma_dr_wav_uninit(&wav);
    return 0;
}

typedef struct
{
    const ma_uint8* pData;
    size_t dataSize;
    size_t cursor;
    ma_uint32 readCount;
    ma_uint32 seekCount;
    ma_uint32 failSeekCall;
} hardening_wav_raw_stream;

static size_t hardening_wav_raw_read(void* pUserData, void* pBufferOut, size_t bytesToRead)
{
    hardening_wav_raw_stream* pStream = (hardening_wav_raw_stream*)pUserData;
    size_t bytesRemaining = pStream->dataSize - pStream->cursor;

    pStream->readCount += 1;
    if (bytesToRead > bytesRemaining) {
        bytesToRead = bytesRemaining;
    }
    if (bytesToRead > 0) {
        MA_COPY_MEMORY(pBufferOut, pStream->pData + pStream->cursor, bytesToRead);
        pStream->cursor += bytesToRead;
    }
    return bytesToRead;
}

static ma_bool32 hardening_wav_raw_seek(void* pUserData, int offset, ma_dr_wav_seek_origin origin)
{
    hardening_wav_raw_stream* pStream = (hardening_wav_raw_stream*)pUserData;
    ma_int64 base;
    ma_int64 target;

    pStream->seekCount += 1;
    if (pStream->seekCount == pStream->failSeekCall) {
        return MA_FALSE;
    }
    base = origin == MA_DR_WAV_SEEK_SET ? 0 : (origin == MA_DR_WAV_SEEK_CUR ? (ma_int64)pStream->cursor : (ma_int64)pStream->dataSize);
    target = base + offset;
    if (target < 0 || (ma_uint64)target > pStream->dataSize) {
        return MA_FALSE;
    }
    pStream->cursor = (size_t)target;
    return MA_TRUE;
}

static ma_bool32 hardening_wav_raw_tell(void* pUserData, ma_int64* pCursor)
{
    hardening_wav_raw_stream* pStream = (hardening_wav_raw_stream*)pUserData;
    *pCursor = (ma_int64)pStream->cursor;
    return MA_TRUE;
}

typedef struct
{
    ma_uint64 offset;
    const ma_uint8* pData;
    size_t size;
} hardening_wav_sparse_span;

typedef struct
{
    hardening_wav_sparse_span spans[4];
    ma_uint32 spanCount;
    ma_uint64 virtualSize;
    ma_uint64 cursor;
    ma_uint32 readCount;
    ma_uint32 seekCount;
    ma_uint32 intMaxForwardSeekCount;
    ma_uint8 top[40];
    ma_uint8 fmt[42];
    ma_uint8 unknown[24];
    ma_uint8 data[24];
} hardening_wav_sparse_stream;

static void hardening_wav_sparse_add_span(hardening_wav_sparse_stream* pStream, ma_uint64 offset, const ma_uint8* pData, size_t size)
{
    hardening_wav_sparse_span* pSpan = &pStream->spans[pStream->spanCount];
    pSpan->offset = offset;
    pSpan->pData = pData;
    pSpan->size = size;
    pStream->spanCount += 1;
}

static size_t hardening_wav_sparse_read(void* pUserData, void* pBufferOut, size_t bytesToRead)
{
    hardening_wav_sparse_stream* pStream = (hardening_wav_sparse_stream*)pUserData;
    ma_uint64 readEnd;
    ma_uint32 iSpan;

    pStream->readCount += 1;
    if (pStream->cursor >= pStream->virtualSize) {
        return 0;
    }
    if ((ma_uint64)bytesToRead > pStream->virtualSize - pStream->cursor) {
        bytesToRead = (size_t)(pStream->virtualSize - pStream->cursor);
    }
    MA_ZERO_MEMORY(pBufferOut, bytesToRead);
    readEnd = pStream->cursor + bytesToRead;
    for (iSpan = 0; iSpan < pStream->spanCount; iSpan += 1) {
        const hardening_wav_sparse_span* pSpan = &pStream->spans[iSpan];
        ma_uint64 spanEnd = pSpan->offset + pSpan->size;
        ma_uint64 copyBeg = pStream->cursor > pSpan->offset ? pStream->cursor : pSpan->offset;
        ma_uint64 copyEnd = readEnd < spanEnd ? readEnd : spanEnd;
        if (copyBeg < copyEnd) {
            MA_COPY_MEMORY((ma_uint8*)pBufferOut + (size_t)(copyBeg - pStream->cursor),
                pSpan->pData + (size_t)(copyBeg - pSpan->offset), (size_t)(copyEnd - copyBeg));
        }
    }
    pStream->cursor = readEnd;
    return bytesToRead;
}

static ma_bool32 hardening_wav_sparse_seek(void* pUserData, int offset, ma_dr_wav_seek_origin origin)
{
    hardening_wav_sparse_stream* pStream = (hardening_wav_sparse_stream*)pUserData;
    ma_int64 base;
    ma_int64 target;

    pStream->seekCount += 1;
    if (origin == MA_DR_WAV_SEEK_CUR && offset == 0x7FFFFFFF) {
        pStream->intMaxForwardSeekCount += 1;
    }
    base = origin == MA_DR_WAV_SEEK_SET ? 0 : (origin == MA_DR_WAV_SEEK_CUR ? (ma_int64)pStream->cursor : (ma_int64)pStream->virtualSize);
    target = base + offset;
    if (target < 0 || (ma_uint64)target > pStream->virtualSize) {
        return MA_FALSE;
    }
    pStream->cursor = (ma_uint64)target;
    return MA_TRUE;
}

static ma_bool32 hardening_wav_sparse_tell(void* pUserData, ma_int64* pCursor)
{
    hardening_wav_sparse_stream* pStream = (hardening_wav_sparse_stream*)pUserData;
    *pCursor = (ma_int64)pStream->cursor;
    return MA_TRUE;
}

static void hardening_wav_sparse_init_top(hardening_wav_sparse_stream* pStream, ma_uint64 virtualSize)
{
    MA_ZERO_OBJECT(pStream);
    pStream->virtualSize = virtualSize;
    MA_COPY_MEMORY(pStream->top + 0, ma_dr_wavGUID_W64_RIFF, 16);
    hardening_wav_write_u64le(pStream->top + 16, virtualSize);
    MA_COPY_MEMORY(pStream->top + 24, ma_dr_wavGUID_W64_WAVE, 16);
    hardening_wav_sparse_add_span(pStream, 0, pStream->top, sizeof(pStream->top));
}

static int hardening_wav_test_sparse_fmt_remainder(ma_uint64 remainder, ma_bool32 expectSuccess, const char* pName)
{
    hardening_wav_sparse_stream stream;
    ma_dr_wav wav;
    ma_uint64 fmtDataSize;
    ma_uint64 fmtPadding;
    ma_uint64 dataOffset;
    ma_bool32 initialized;

    fmtDataSize = 18 + remainder;
    fmtPadding = (8 - (fmtDataSize & 7)) & 7;
    dataOffset = 40 + 24 + fmtDataSize + fmtPadding;
    hardening_wav_sparse_init_top(&stream, dataOffset + 24);
    MA_COPY_MEMORY(stream.fmt + 0, ma_dr_wavGUID_W64_FMT, 16);
    hardening_wav_write_u64le(stream.fmt + 16, 24 + fmtDataSize);
    hardening_wav_write_u16le(stream.fmt + 24, MA_DR_WAVE_FORMAT_PCM);
    hardening_wav_write_u16le(stream.fmt + 26, 1);
    hardening_wav_write_u32le(stream.fmt + 28, 8000);
    hardening_wav_write_u32le(stream.fmt + 32, 16000);
    hardening_wav_write_u16le(stream.fmt + 36, 2);
    hardening_wav_write_u16le(stream.fmt + 38, 16);
    hardening_wav_write_u16le(stream.fmt + 40, 0);
    hardening_wav_sparse_add_span(&stream, 40, stream.fmt, sizeof(stream.fmt));
    MA_COPY_MEMORY(stream.data + 0, ma_dr_wavGUID_W64_DATA, 16);
    hardening_wav_write_u64le(stream.data + 16, 24);
    hardening_wav_sparse_add_span(&stream, dataOffset, stream.data, sizeof(stream.data));

    initialized = ma_dr_wav_init(&wav, hardening_wav_sparse_read, hardening_wav_sparse_seek, hardening_wav_sparse_tell, &stream, NULL);
    if (initialized) {
        ma_dr_wav_uninit(&wav);
    }
    if (initialized != expectSuccess || stream.readCount > 16 || stream.seekCount > 8 ||
        (expectSuccess && stream.intMaxForwardSeekCount != 1) ||
        (!expectSuccess && stream.intMaxForwardSeekCount != 0)) {
        printf("%s: initialized=%u reads=%u seeks=%u int-max-seeks=%u\n", pName,
            initialized, stream.readCount, stream.seekCount, stream.intMaxForwardSeekCount);
        return 1;
    }
    return 0;
}

static int hardening_wav_test_sparse_large_unknown(void)
{
    hardening_wav_sparse_stream stream;
    ma_dr_wav wav;
    ma_uint64 unknownSize;
    ma_uint64 unknownPadding;
    ma_uint64 dataOffset;

    unknownSize = (ma_uint64)0x7FFFFFFF + 1;
    unknownPadding = (8 - (unknownSize & 7)) & 7;
    dataOffset = 80 + 24 + unknownSize + unknownPadding;
    hardening_wav_sparse_init_top(&stream, dataOffset + 24);
    MA_COPY_MEMORY(stream.fmt + 0, ma_dr_wavGUID_W64_FMT, 16);
    hardening_wav_write_u64le(stream.fmt + 16, 40);
    hardening_wav_write_u16le(stream.fmt + 24, MA_DR_WAVE_FORMAT_PCM);
    hardening_wav_write_u16le(stream.fmt + 26, 1);
    hardening_wav_write_u32le(stream.fmt + 28, 8000);
    hardening_wav_write_u32le(stream.fmt + 32, 16000);
    hardening_wav_write_u16le(stream.fmt + 36, 2);
    hardening_wav_write_u16le(stream.fmt + 38, 16);
    hardening_wav_sparse_add_span(&stream, 40, stream.fmt, 40);
    MA_COPY_MEMORY(stream.unknown, ma_dr_wavGUID_W64_FACT, 16);
    MA_COPY_MEMORY(stream.unknown, "JUNK", 4);
    hardening_wav_write_u64le(stream.unknown + 16, 24 + unknownSize);
    hardening_wav_sparse_add_span(&stream, 80, stream.unknown, sizeof(stream.unknown));
    MA_COPY_MEMORY(stream.data, ma_dr_wavGUID_W64_DATA, 16);
    hardening_wav_write_u64le(stream.data + 16, 24);
    hardening_wav_sparse_add_span(&stream, dataOffset, stream.data, sizeof(stream.data));

    if (!ma_dr_wav_init(&wav, hardening_wav_sparse_read, hardening_wav_sparse_seek, hardening_wav_sparse_tell, &stream, NULL)) {
        printf("valid sparse large unknown chunk failed to initialize\n");
        return 1;
    }
    ma_dr_wav_uninit(&wav);
    if (stream.intMaxForwardSeekCount != 1 || stream.readCount > 16 || stream.seekCount > 8) {
        printf("valid sparse large unknown traversal was reads=%u seeks=%u int-max-seeks=%u\n",
            stream.readCount, stream.seekCount, stream.intMaxForwardSeekCount);
        return 1;
    }
    return 0;
}

static int hardening_wav_test_compatibility_matrix(void)
{
    const ma_int16 expectedFrames[2] = {0x1234, -0x2345};
    ma_uint8 data[256];
    ma_uint32 factSize;
    ma_uint64 w64ChunkSize;
    int failed = 0;

    failed += hardening_wav_test_writer_container(ma_dr_wav_container_riff, "RIFF PCM");
    failed += hardening_wav_test_writer_container(ma_dr_wav_container_w64, "W64 PCM");
    failed += hardening_wav_test_writer_container(ma_dr_wav_container_rf64, "RF64 PCM");
    failed += hardening_wav_validate_pcm(data, hardening_wav_make_rifx_pcm(data), ma_dr_wav_container_rifx, expectedFrames, 2, "RIFX PCM");
    failed += hardening_wav_validate_pcm(data, hardening_wav_make_aiff_pcm(data), ma_dr_wav_container_aiff, expectedFrames, 2, "AIFF PCM");

    failed += hardening_wav_validate_adpcm(data, hardening_wav_make_ima_adpcm(data, 4), MA_DR_WAVE_FORMAT_DVI_ADPCM, 9, "IMA ADPCM");
    failed += hardening_wav_validate_adpcm(data, hardening_wav_make_ima_adpcm(data, 8), MA_DR_WAVE_FORMAT_DVI_ADPCM, 9, "IMA ADPCM extended fact");
    failed += hardening_wav_validate_adpcm(data, hardening_wav_make_ms_adpcm(data), MA_DR_WAVE_FORMAT_ADPCM, 4, "MS ADPCM");
    for (factSize = 0; factSize < 4; factSize += 1) {
        ma_dr_wav wav;
        size_t dataSize = hardening_wav_make_ima_adpcm(data, factSize);
        if (ma_dr_wav_init_memory(&wav, data, dataSize, NULL)) {
            printf("IMA ADPCM fact size %u unexpectedly initialized\n", factSize);
            ma_dr_wav_uninit(&wav);
            failed = 1;
        }
    }
    for (factSize = 0; factSize <= 4; factSize += 1) {
        failed += hardening_wav_test_bounded_callbacks(data, hardening_wav_make_rifx_fact(data, factSize), factSize == 4, "RIFX fact boundary");
    }
    failed += hardening_wav_test_bounded_callbacks(data, hardening_wav_make_rifx_fact(data, 8), MA_TRUE, "RIFX extended fact");
    for (factSize = 0; factSize <= 8; factSize += 1) {
        failed += hardening_wav_test_bounded_callbacks(data, hardening_wav_make_w64_fact(data, factSize), factSize == 8, "W64 fact boundary");
    }
    failed += hardening_wav_test_bounded_callbacks(data, hardening_wav_make_w64_fact(data, 9), MA_TRUE, "W64 padded fact extension");
    failed += hardening_wav_test_bounded_callbacks(data, hardening_wav_make_w64_fact(data, 16), MA_TRUE, "W64 extended fact");
    for (w64ChunkSize = 0; w64ChunkSize < 24; w64ChunkSize += 1) {
        failed += hardening_wav_test_bounded_callbacks(data, hardening_wav_make_short_w64_chunk(data, w64ChunkSize), MA_FALSE, "W64 short total chunk header");
    }

    return failed;
}

static int hardening_wav_test_aiff_short_headers(void)
{
    ma_uint8 data[128];
    ma_uint32 chunkSize;
    int failed;

    failed = 0;
    for (chunkSize = 0; chunkSize < 18; chunkSize += 1) {
        size_t dataSize = hardening_wav_make_aiff_pcm(data);
        hardening_wav_write_u32be(data + 16, chunkSize);
        failed += hardening_wav_test_bounded_callbacks(data, dataSize, MA_FALSE, "AIFF short COMM header");
    }
    for (chunkSize = 0; chunkSize < 8; chunkSize += 1) {
        size_t dataSize = hardening_wav_make_aiff_pcm(data);
        hardening_wav_write_u32be(data + 42, chunkSize);
        failed += hardening_wav_test_bounded_callbacks(data, dataSize, MA_FALSE, "AIFF short SSND header");
    }

    return failed;
}

static int hardening_wav_test_failed_seek(void)
{
    ma_uint8 data[48];
    hardening_wav_raw_stream stream;
    ma_dr_wav wav;

    hardening_wav_make_rifx_pcm(data);
    MA_ZERO_OBJECT(&stream);
    stream.pData = data;
    stream.dataSize = sizeof(data);
    stream.failSeekCall = 1;
    if (ma_dr_wav_init(&wav, hardening_wav_raw_read, hardening_wav_raw_seek, hardening_wav_raw_tell, &stream, NULL)) {
        printf("valid RIFX unexpectedly initialized after injected seek failure\n");
        ma_dr_wav_uninit(&wav);
        return 1;
    }
    if (stream.seekCount != 1 || stream.readCount == 0 || stream.readCount > 8) {
        printf("RIFX failed-seek path was not bounded: reads=%u seeks=%u\n", stream.readCount, stream.seekCount);
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
    failed += hardening_wav_test_compatibility_matrix();
    failed += hardening_wav_test_aiff_short_headers();
    failed += hardening_wav_test_sparse_fmt_remainder((ma_uint64)0x7FFFFFFF, MA_TRUE, "fmt remainder INT_MAX");
    failed += hardening_wav_test_sparse_fmt_remainder((ma_uint64)0x7FFFFFFF + 1, MA_FALSE, "fmt remainder INT_MAX plus one");
    failed += hardening_wav_test_sparse_fmt_remainder((ma_uint64)0xFFFFFFFF, MA_FALSE, "fmt remainder UINT32_MAX");
    failed += hardening_wav_test_sparse_large_unknown();
    failed += hardening_wav_test_failed_seek();

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
