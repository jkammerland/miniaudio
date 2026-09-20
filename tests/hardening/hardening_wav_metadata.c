#define MA_NO_DEVICE_IO
#include "../../miniaudio.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    size_t largestRequest;
    ma_uint32 allocationCount;
    ma_uint32 liveAllocationCount;
} metadata_allocator_state;

typedef struct
{
    const ma_uint8* pData;
    size_t dataSize;
    size_t cursor;
    size_t shortReadOffset;
    ma_bool32 isMaterializationPass;
    ma_bool32 didShortRead;
} metadata_stream;

static void metadata_write_u32le(ma_uint8* pData, ma_uint32 value)
{
    pData[0] = (ma_uint8)(value >> 0);
    pData[1] = (ma_uint8)(value >> 8);
    pData[2] = (ma_uint8)(value >> 16);
    pData[3] = (ma_uint8)(value >> 24);
}

static void metadata_write_u64le(ma_uint8* pData, ma_uint64 value)
{
    metadata_write_u32le(pData + 0, (ma_uint32)(value >> 0));
    metadata_write_u32le(pData + 4, (ma_uint32)(value >> 32));
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

static size_t metadata_append_wav_chunk(ma_uint8* pData, size_t dataSize, const char chunkID[4], const ma_uint8* pChunkData, ma_uint32 chunkSize)
{
    size_t totalSize = dataSize + 8 + chunkSize + (chunkSize & 1);

    MA_COPY_MEMORY(pData + dataSize, chunkID, 4);
    metadata_write_u32le(pData + dataSize + 4, chunkSize);
    if (chunkSize > 0) {
        MA_COPY_MEMORY(pData + dataSize + 8, pChunkData, chunkSize);
    }
    if ((chunkSize & 1) != 0) {
        pData[totalSize - 1] = 0;
    }
    metadata_write_u32le(pData + 4, (ma_uint32)(totalSize - 8));

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
        pState->liveAllocationCount += 1;
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
    metadata_allocator_state* pState = (metadata_allocator_state*)pUserData;
    if (pMemory != NULL) {
        pState->liveAllocationCount -= 1;
    }
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

static size_t metadata_stream_read(void* pUserData, void* pBufferOut, size_t bytesToRead)
{
    metadata_stream* pStream = (metadata_stream*)pUserData;
    size_t bytesAvailable = pStream->dataSize - pStream->cursor;

    if (bytesToRead > bytesAvailable) {
        bytesToRead = bytesAvailable;
    }
    if (pStream->isMaterializationPass && !pStream->didShortRead && pStream->cursor == pStream->shortReadOffset && bytesToRead > 8) {
        bytesToRead = 8;
        pStream->didShortRead = MA_TRUE;
    }
    if (bytesToRead > 0) {
        MA_COPY_MEMORY(pBufferOut, pStream->pData + pStream->cursor, bytesToRead);
        pStream->cursor += bytesToRead;
    }

    return bytesToRead;
}

static ma_bool32 metadata_stream_seek(void* pUserData, int offset, ma_dr_wav_seek_origin origin)
{
    metadata_stream* pStream = (metadata_stream*)pUserData;
    ma_int64 target;

    if (origin == MA_DR_WAV_SEEK_SET) {
        target = offset;
    } else if (origin == MA_DR_WAV_SEEK_CUR) {
        target = (ma_int64)pStream->cursor + offset;
    } else {
        target = (ma_int64)pStream->dataSize + offset;
    }
    if (target < 0 || (ma_uint64)target > pStream->dataSize) {
        return MA_FALSE;
    }

    pStream->cursor = (size_t)target;
    if (origin == MA_DR_WAV_SEEK_SET && offset == 12) {
        pStream->isMaterializationPass = MA_TRUE;
    }
    return MA_TRUE;
}

static ma_bool32 metadata_stream_tell(void* pUserData, ma_int64* pCursor)
{
    metadata_stream* pStream = (metadata_stream*)pUserData;
    *pCursor = (ma_int64)pStream->cursor;
    return MA_TRUE;
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

static int metadata_test_invalid_smpl_before_valid_metadata(void)
{
    ma_uint8 invalidSmpl[MA_DR_WAV_SMPL_BYTES];
    ma_uint8 inst[MA_DR_WAV_INST_BYTES];
    ma_uint8 wavData[1024];
    metadata_allocator_state state;
    ma_allocation_callbacks callbacks;
    ma_dr_wav wav;
    size_t wavSize;

    MA_ZERO_MEMORY(invalidSmpl, sizeof(invalidSmpl));
    metadata_write_u32le(invalidSmpl + 32, 1); /* No byte remains for the declared sampler data. */
    wavSize = metadata_make_wav(wavData, "smpl", invalidSmpl, (ma_uint32)sizeof(invalidSmpl));

    inst[0] = 60;
    inst[1] = 1;
    inst[2] = 2;
    inst[3] = 3;
    inst[4] = 100;
    inst[5] = 4;
    inst[6] = 101;
    wavSize = metadata_append_wav_chunk(wavData, wavSize, "inst", inst, (ma_uint32)sizeof(inst));

    MA_ZERO_OBJECT(&state);
    callbacks = metadata_callbacks(&state);
    if (!ma_dr_wav_init_memory_with_metadata(&wav, wavData, wavSize, 0, &callbacks)) {
        printf("mixed invalid SMPL file failed to initialize\n");
        return 1;
    }
    if (wav.metadataCount != 1 || wav.pMetadata[0].type != ma_dr_wav_metadata_type_inst ||
        wav.pMetadata[0].data.inst.midiUnityNote != 60 || wav.pMetadata[0].data.inst.highVelocity != 101) {
        printf("invalid SMPL consumed the valid metadata record\n");
        ma_dr_wav_uninit(&wav);
        return 1;
    }

    ma_dr_wav_uninit(&wav);
    return 0;
}

static int metadata_test_smpl_short_read(void)
{
    ma_uint8 smpl[MA_DR_WAV_SMPL_BYTES + MA_DR_WAV_SMPL_LOOP_BYTES];
    ma_uint8 inst[MA_DR_WAV_INST_BYTES];
    ma_uint8 wavData[1024];
    metadata_allocator_state state;
    ma_allocation_callbacks callbacks;
    metadata_stream stream;
    ma_dr_wav wav;
    size_t wavSize;
    size_t smplPayloadOffset;

    MA_ZERO_MEMORY(smpl, sizeof(smpl));
    metadata_write_u32le(smpl + 28, 1);
    metadata_write_u32le(smpl + MA_DR_WAV_SMPL_BYTES, 23);
    wavSize = metadata_make_wav(wavData, "smpl", smpl, (ma_uint32)sizeof(smpl));
    smplPayloadOffset = 44 + 8;

    MA_ZERO_MEMORY(inst, sizeof(inst));
    inst[0] = 64;
    inst[6] = 127;
    wavSize = metadata_append_wav_chunk(wavData, wavSize, "inst", inst, (ma_uint32)sizeof(inst));

    MA_ZERO_OBJECT(&state);
    callbacks = metadata_callbacks(&state);
    MA_ZERO_OBJECT(&stream);
    stream.pData = wavData;
    stream.dataSize = wavSize;
    stream.shortReadOffset = smplPayloadOffset + MA_DR_WAV_SMPL_BYTES;

    if (!ma_dr_wav_init_with_metadata(&wav, metadata_stream_read, metadata_stream_seek, metadata_stream_tell, &stream, 0, &callbacks)) {
        printf("SMPL short-read file failed to initialize safely\n");
        return 1;
    }
    if (!stream.didShortRead || wav.metadataCount != 1 || wav.pMetadata[0].type != ma_dr_wav_metadata_type_inst ||
        wav.pMetadata[0].data.inst.midiUnityNote != 64 || wav.pMetadata[0].data.inst.highVelocity != 127) {
        printf("SMPL short read published a partial record or lost following metadata\n");
        ma_dr_wav_uninit(&wav);
        return 1;
    }

    ma_dr_wav_uninit(&wav);
    if (state.liveAllocationCount != 0) {
        printf("SMPL short read leaked metadata allocation\n");
        return 1;
    }
    return 0;
}

static int metadata_test_rf64_roundtrip(void)
{
    ma_dr_wav wav;
    ma_dr_wav_data_format format;
    void* pWriterData = NULL;
    ma_uint8* pData;
    size_t dataSize = 0;
    size_t extendedDataSize;
    ma_int16 sampleIn = 123;
    ma_int16 sampleOut = 0;
    ma_uint8 inst[MA_DR_WAV_INST_BYTES];

    format.container = ma_dr_wav_container_rf64;
    format.format = MA_DR_WAVE_FORMAT_PCM;
    format.channels = 1;
    format.sampleRate = 48000;
    format.bitsPerSample = 16;

    if (!ma_dr_wav_init_memory_write(&wav, &pWriterData, &dataSize, &format, NULL)) {
        printf("RF64 writer failed to initialize\n");
        return 1;
    }
    if (ma_dr_wav_write_pcm_frames(&wav, 1, &sampleIn) != 1 || ma_dr_wav_uninit(&wav) != MA_SUCCESS) {
        printf("RF64 writer failed to finalize\n");
        ma_dr_wav_free(pWriterData, NULL);
        return 1;
    }

    extendedDataSize = dataSize + 8 + sizeof(inst) + (sizeof(inst) & 1);
    pData = (ma_uint8*)malloc(extendedDataSize);
    if (pData == NULL) {
        ma_dr_wav_free(pWriterData, NULL);
        return 1;
    }
    MA_COPY_MEMORY(pData, pWriterData, dataSize);
    ma_dr_wav_free(pWriterData, NULL);

    MA_ZERO_MEMORY(inst, sizeof(inst));
    inst[0] = 67;
    MA_COPY_MEMORY(pData + dataSize, "inst", 4);
    metadata_write_u32le(pData + dataSize + 4, (ma_uint32)sizeof(inst));
    MA_COPY_MEMORY(pData + dataSize + 8, inst, sizeof(inst));
    pData[extendedDataSize - 1] = 0;
    dataSize = extendedDataSize;
    metadata_write_u64le(pData + 20, (ma_uint64)dataSize - 8); /* ds64 RIFF size. */

    if (!ma_dr_wav_init_memory_with_metadata(&wav, pData, dataSize, 0, NULL)) {
        printf("valid RF64 failed metadata-enabled initialization\n");
        free(pData);
        return 1;
    }
    if (wav.metadataCount != 1 || wav.pMetadata[0].type != ma_dr_wav_metadata_type_inst ||
        wav.pMetadata[0].data.inst.midiUnityNote != 67 ||
        ma_dr_wav_read_pcm_frames_s16(&wav, 1, &sampleOut) != 1 || sampleOut != sampleIn) {
        printf("valid RF64 sample did not round trip\n");
        ma_dr_wav_uninit(&wav);
        free(pData);
        return 1;
    }

    ma_dr_wav_uninit(&wav);
    free(pData);
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
    failed += metadata_test_invalid_smpl_before_valid_metadata();
    failed += metadata_test_smpl_short_read();
    failed += metadata_test_rf64_roundtrip();

    return failed != 0;
}
