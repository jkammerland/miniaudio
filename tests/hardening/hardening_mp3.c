#define MA_NO_DEVICE_IO
#include "../../miniaudio.c"

#include <stdio.h>
#include <stdlib.h>

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
} mp3_allocator_state;

static void* mp3_malloc(size_t size, void* pUserData)
{
    mp3_allocator_state* pState = (mp3_allocator_state*)pUserData;
    if (size > pState->largestRequest) {
        pState->largestRequest = size;
    }
    if (size > 1024 * 1024) {
        return NULL;
    }
    return malloc(size);
}

static void* mp3_realloc(void* pMemory, size_t size, void* pUserData)
{
    mp3_allocator_state* pState = (mp3_allocator_state*)pUserData;
    if (size > pState->largestRequest) {
        pState->largestRequest = size;
    }
    if (size > 1024 * 1024) {
        return NULL;
    }
    return realloc(pMemory, size);
}

static void mp3_free(void* pMemory, void* pUserData)
{
    (void)pUserData;
    free(pMemory);
}

static int mp3_test_truncated_metadata(void)
{
    ma_decoder decoder;
    ma_result result = ma_decoder_init_memory(g_mp3_1128, sizeof(g_mp3_1128), NULL, &decoder);
    if (result == MA_SUCCESS) {
        ma_decoder_uninit(&decoder);
    }
    return 0;
}

static int mp3_test_frame_count(void)
{
    const ma_uint64 expectedFrameCount = (ma_uint64)0x73000002 * 1152;
    ma_dr_mp3 mp3;

    if (!ma_dr_mp3_init_memory(&mp3, g_mp3_1117, sizeof(g_mp3_1117), NULL)) {
        printf("issue 1117 fixture failed to initialize\n");
        return 1;
    }
    if (mp3.totalPCMFrameCount != expectedFrameCount) {
        printf("MP3 frame count was %llu, expected %llu\n",
            (unsigned long long)mp3.totalPCMFrameCount,
            (unsigned long long)expectedFrameCount);
        ma_dr_mp3_uninit(&mp3);
        return 1;
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
        printf("issue 1117 fixture failed full decode: result=%d frames=%llu\n", result, (unsigned long long)frameCount);
        return 1;
    }
    if (state.largestRequest > 1024 * 1024) {
        printf("hostile MP3 length requested %zu bytes\n", state.largestRequest);
        return 1;
    }
    return 0;
}

int main(void)
{
    int failed = 0;
    failed += mp3_test_truncated_metadata();
    failed += mp3_test_frame_count();
    failed += mp3_test_full_decode_allocation();
    return failed != 0;
}
