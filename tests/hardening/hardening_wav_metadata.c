#define MA_NO_DEVICE_IO
#include "../../miniaudio.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    size_t largestRequest;
    ma_uint32 allocationCount;
} metadata_allocator_state;

static void metadata_write_u32le(ma_uint8* pData, ma_uint32 value)
{
    pData[0] = (ma_uint8)(value >> 0);
    pData[1] = (ma_uint8)(value >> 8);
    pData[2] = (ma_uint8)(value >> 16);
    pData[3] = (ma_uint8)(value >> 24);
}

static size_t metadata_make_wav(ma_uint8* pData, const char chunkID[4], const ma_uint8* pChunkData, ma_uint32 chunkSize)
{
    size_t chunkOffset = 44;
    size_t totalSize = chunkOffset + 8 + chunkSize + (chunkSize & 1);

    MA_ZERO_MEMORY(pData, 1024);
    MA_COPY_MEMORY(pData + 0, "RIFF", 4);
    metadata_write_u32le(pData + 4, (ma_uint32)(totalSize - 8));
    MA_COPY_MEMORY(pData + 8, "WAVEfmt ", 8);
    metadata_write_u32le(pData + 16, 16);
    pData[20] = 1;
    pData[22] = 1;
    metadata_write_u32le(pData + 24, 8000);
    metadata_write_u32le(pData + 28, 16000);
    pData[32] = 2;
    pData[34] = 16;
    MA_COPY_MEMORY(pData + 36, "data", 4);
    metadata_write_u32le(pData + 40, 0);
    MA_COPY_MEMORY(pData + chunkOffset, chunkID, 4);
    metadata_write_u32le(pData + chunkOffset + 4, chunkSize);
    if (chunkSize > 0) {
        MA_COPY_MEMORY(pData + chunkOffset + 8, pChunkData, chunkSize);
    }

    return totalSize;
}

static void* metadata_malloc(size_t size, void* pUserData)
{
    metadata_allocator_state* pState = (metadata_allocator_state*)pUserData;
    void* pMemory;

    pState->allocationCount += 1;
    if (size > pState->largestRequest) {
        pState->largestRequest = size;
    }
    if (size > 1024 * 1024) {
        return NULL;
    }
    pMemory = malloc(size);
    if (pMemory != NULL) {
        memset(pMemory, 0xA5, size);
    }
    return pMemory;
}

static void* metadata_realloc(void* pMemory, size_t size, void* pUserData)
{
    metadata_allocator_state* pState = (metadata_allocator_state*)pUserData;
    pState->allocationCount += 1;
    if (size > pState->largestRequest) {
        pState->largestRequest = size;
    }
    if (size > 1024 * 1024) {
        return NULL;
    }
    return realloc(pMemory, size);
}

static void metadata_free(void* pMemory, void* pUserData)
{
    (void)pUserData;
    free(pMemory);
}

static ma_allocation_callbacks metadata_callbacks(metadata_allocator_state* pState)
{
    ma_allocation_callbacks callbacks;
    callbacks.pUserData = pState;
    callbacks.onMalloc = metadata_malloc;
    callbacks.onRealloc = metadata_realloc;
    callbacks.onFree = metadata_free;
    return callbacks;
}

static int metadata_test_bext_history(const ma_uint8* pHistory, ma_uint32 historySize, ma_uint32 expectedStringSize, const char* pName)
{
    ma_uint8 chunk[MA_DR_WAV_BEXT_BYTES + 16];
    ma_uint8 wavData[1024];
    metadata_allocator_state state;
    ma_allocation_callbacks callbacks;
    ma_dr_wav wav;
    size_t wavSize;

    MA_ZERO_OBJECT(&state);
    MA_ZERO_MEMORY(chunk, sizeof(chunk));
    if (historySize > 0) {
        MA_COPY_MEMORY(chunk + MA_DR_WAV_BEXT_BYTES, pHistory, historySize);
    }
    wavSize = metadata_make_wav(wavData, "bext", chunk, MA_DR_WAV_BEXT_BYTES + historySize);
    callbacks = metadata_callbacks(&state);

    if (!ma_dr_wav_init_memory_with_metadata(&wav, wavData, wavSize, 0, &callbacks)) {
        printf("%s failed to initialize\n", pName);
        return 1;
    }
    if (wav.metadataCount != 1 || wav.pMetadata[0].type != ma_dr_wav_metadata_type_bext) {
        printf("%s did not produce one BEXT record\n", pName);
        ma_dr_wav_uninit(&wav);
        return 1;
    }
    if (wav.pMetadata[0].data.bext.codingHistorySize != expectedStringSize) {
        printf("%s coding history size was %u, expected %u\n", pName, wav.pMetadata[0].data.bext.codingHistorySize, expectedStringSize);
        ma_dr_wav_uninit(&wav);
        return 1;
    }
    if (historySize > 0 && wav.pMetadata[0].data.bext.pCodingHistory[historySize] != '\0') {
        printf("%s coding history was not terminated at the bytes read\n", pName);
        ma_dr_wav_uninit(&wav);
        return 1;
    }

    ma_dr_wav_uninit(&wav);
    return 0;
}

static int metadata_test_smpl(void)
{
    ma_uint8 chunk[MA_DR_WAV_SMPL_BYTES + MA_DR_WAV_SMPL_LOOP_BYTES + 3];
    ma_uint8 wavData[1024];
    metadata_allocator_state state;
    ma_allocation_callbacks callbacks;
    ma_dr_wav wav;
    size_t wavSize;

    MA_ZERO_MEMORY(chunk, sizeof(chunk));
    metadata_write_u32le(chunk + 28, 1);
    metadata_write_u32le(chunk + 32, 3);
    metadata_write_u32le(chunk + MA_DR_WAV_SMPL_BYTES + 0, 17);
    chunk[sizeof(chunk) - 3] = 0x11;
    chunk[sizeof(chunk) - 2] = 0x22;
    chunk[sizeof(chunk) - 1] = 0x33;
    wavSize = metadata_make_wav(wavData, "smpl", chunk, (ma_uint32)sizeof(chunk));
    MA_ZERO_OBJECT(&state);
    callbacks = metadata_callbacks(&state);

    if (!ma_dr_wav_init_memory_with_metadata(&wav, wavData, wavSize, 0, &callbacks)) {
        printf("valid SMPL with sampler data failed to initialize\n");
        return 1;
    }
    if (wav.metadataCount != 1 || wav.pMetadata[0].type != ma_dr_wav_metadata_type_smpl ||
        wav.pMetadata[0].data.smpl.sampleLoopCount != 1 ||
        wav.pMetadata[0].data.smpl.pLoops[0].cuePointId != 17 ||
        wav.pMetadata[0].data.smpl.samplerSpecificDataSizeInBytes != 3 ||
        memcmp(wav.pMetadata[0].data.smpl.pSamplerSpecificData, "\x11\x22\x33", 3) != 0) {
        printf("valid SMPL metadata did not round trip\n");
        ma_dr_wav_uninit(&wav);
        return 1;
    }
    ma_dr_wav_uninit(&wav);

    MA_ZERO_MEMORY(chunk, sizeof(chunk));
    metadata_write_u32le(chunk + 32, 0x7FFFFFFF);
    wavSize = metadata_make_wav(wavData, "smpl", chunk, MA_DR_WAV_SMPL_BYTES);
    MA_ZERO_OBJECT(&state);
    callbacks = metadata_callbacks(&state);
    if (ma_dr_wav_init_memory_with_metadata(&wav, wavData, wavSize, 0, &callbacks)) {
        ma_dr_wav_uninit(&wav);
    }
    if (state.largestRequest > 1024 * 1024) {
        printf("hostile SMPL requested %lu bytes\n", (unsigned long)state.largestRequest);
        return 1;
    }

    return 0;
}

int main(void)
{
    static const ma_uint8 noNul[] = {'A', 'B', 'C', 'D'};
    static const ma_uint8 embeddedNul[] = {'A', 'B', '\0', 'C'};
    static const ma_uint8 finalNul[] = {'A', 'B', 'C', '\0'};
    int failed = 0;

    failed += metadata_test_bext_history(NULL, 0, 0, "empty BEXT history");
    failed += metadata_test_bext_history(noNul, sizeof(noNul), 4, "unterminated BEXT history");
    failed += metadata_test_bext_history(embeddedNul, sizeof(embeddedNul), 2, "embedded-NUL BEXT history");
    failed += metadata_test_bext_history(finalNul, sizeof(finalNul), 3, "final-NUL BEXT history");
    failed += metadata_test_smpl();

    return failed != 0;
}
