#include "../../miniaudio.c"

#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    size_t allocationCallCount;
    size_t failAllocationCall;
    size_t liveAllocationCount;
} post_init_allocator;

typedef struct
{
    ma_context context;
    ma_device device;
    post_init_allocator allocator;
} post_init_fixture;

static ma_uint32 g_backendInitCount;
static ma_uint32 g_backendUninitCount;

static ma_result post_init_backend_init_invalid_descriptor(ma_device* pDevice, const ma_device_config* pConfig, ma_device_descriptor* pPlayback, ma_device_descriptor* pCapture)
{
    (void)pDevice;
    (void)pConfig;
    (void)pPlayback;
    (void)pCapture;
    g_backendInitCount += 1;
    return MA_SUCCESS;
}

static ma_result post_init_backend_uninit(ma_device* pDevice)
{
    (void)pDevice;
    g_backendUninitCount += 1;
    return MA_SUCCESS;
}

static void* post_init_malloc(size_t size, void* pUserData)
{
    post_init_allocator* pAllocator = (post_init_allocator*)pUserData;
    void* pAllocation;

    pAllocator->allocationCallCount += 1;
    if (pAllocator->allocationCallCount == pAllocator->failAllocationCall) {
        return NULL;
    }

    pAllocation = malloc(size);
    if (pAllocation != NULL) {
        pAllocator->liveAllocationCount += 1;
    }
    return pAllocation;
}

static void* post_init_realloc(void* pMemory, size_t size, void* pUserData)
{
    post_init_allocator* pAllocator = (post_init_allocator*)pUserData;
    void* pAllocation;

    pAllocator->allocationCallCount += 1;
    if (pAllocator->allocationCallCount == pAllocator->failAllocationCall) {
        return NULL;
    }

    if (pMemory == NULL) {
        pAllocation = malloc(size);
        if (pAllocation != NULL) {
            pAllocator->liveAllocationCount += 1;
        }
        return pAllocation;
    }
    if (size == 0) {
        free(pMemory);
        pAllocator->liveAllocationCount -= 1;
        return NULL;
    }

    return realloc(pMemory, size);
}

static void post_init_free(void* pMemory, void* pUserData)
{
    post_init_allocator* pAllocator = (post_init_allocator*)pUserData;

    if (pMemory != NULL) {
        free(pMemory);
        pAllocator->liveAllocationCount -= 1;
    }
}

static ma_result post_init_fixture_init(post_init_fixture* pFixture)
{
    ma_resampler_config resamplerConfig;
    ma_result result;

    MA_ZERO_OBJECT(pFixture);
    pFixture->context.allocationCallbacks.pUserData = &pFixture->allocator;
    pFixture->context.allocationCallbacks.onMalloc  = post_init_malloc;
    pFixture->context.allocationCallbacks.onRealloc = post_init_realloc;
    pFixture->context.allocationCallbacks.onFree    = post_init_free;

    pFixture->device.pContext = &pFixture->context;
    pFixture->device.type = ma_device_type_duplex; /* Forces playback input-cache ownership. */
    pFixture->device.sampleRate = 48000;
    pFixture->device.playback.format = ma_format_f32;
    pFixture->device.playback.channels = 2;
    pFixture->device.playback.internalFormat = ma_format_f32;
    pFixture->device.playback.internalChannels = 2;
    pFixture->device.playback.internalSampleRate = 48000;
    pFixture->device.playback.internalPeriodSizeInFrames = 64;
    ma_channel_map_init_standard(ma_standard_channel_map_default, pFixture->device.playback.channelMap, MA_MAX_CHANNELS, 2);
    ma_channel_map_init_standard(ma_standard_channel_map_default, pFixture->device.playback.internalChannelMap, MA_MAX_CHANNELS, 2);

    resamplerConfig = ma_resampler_config_init(ma_format_f32, 2, 48000, 48000, ma_resample_algorithm_linear);
    pFixture->device.resampling.algorithm       = resamplerConfig.algorithm;
    pFixture->device.resampling.linear.lpfOrder = resamplerConfig.linear.lpfOrder;

    result = ma_device__post_init_setup(&pFixture->device, ma_device_type_playback);
    if (result == MA_SUCCESS) {
        ma_device__set_state(&pFixture->device, ma_device_state_stopped);
    }
    return result;
}

static int post_init_fixture_uninit(post_init_fixture* pFixture, const char* pCase)
{
    ma_data_converter_uninit(&pFixture->device.playback.converter, &pFixture->context.allocationCallbacks);
    ma_free(pFixture->device.playback.pInputCache, &pFixture->context.allocationCallbacks);
    if (pFixture->allocator.liveAllocationCount != 0) {
        printf("%s leaked %lu allocations\n", pCase, (unsigned long)pFixture->allocator.liveAllocationCount);
        return 1;
    }
    return 0;
}

static int post_init_converter_still_works(post_init_fixture* pFixture, const char* pCase)
{
    const float input[2] = {0.25f, -0.5f};
    float output[2] = {0, 0};
    ma_uint64 framesIn = 1;
    ma_uint64 framesOut = 1;
    ma_result result;

    result = ma_data_converter_process_pcm_frames(&pFixture->device.playback.converter, input, &framesIn, output, &framesOut);
    if (result != MA_SUCCESS || framesIn != 1 || framesOut != 1 || output[0] != input[0] || output[1] != input[1]) {
        printf("%s left the prior converter unusable: result=%d frames=%u:%08u/%u:%08u output=%f,%f\n",
            pCase, result,
            (ma_uint32)(framesIn >> 32), (ma_uint32)framesIn,
            (ma_uint32)(framesOut >> 32), (ma_uint32)framesOut,
            output[0], output[1]);
        return 1;
    }
    return 0;
}

static int post_init_test_failure(size_t failureOffset, const char* pCase)
{
    post_init_fixture fixture;
    void* pOldConverterHeap;
    void* pOldInputCache;
    ma_uint64 oldInputCacheCap;
    size_t oldLiveAllocationCount;
    ma_result result;
    int failed = 0;

    result = post_init_fixture_init(&fixture);
    if (result != MA_SUCCESS) {
        printf("%s fixture init failed: %d\n", pCase, result);
        return 1;
    }

    pOldConverterHeap = fixture.device.playback.converter._pHeap;
    pOldInputCache = fixture.device.playback.pInputCache;
    oldInputCacheCap = fixture.device.playback.inputCacheCap;
    oldLiveAllocationCount = fixture.allocator.liveAllocationCount;

    fixture.device.playback.internalSampleRate = 44100;
    fixture.allocator.failAllocationCall = fixture.allocator.allocationCallCount + failureOffset;
    result = ma_device__post_init_setup(&fixture.device, ma_device_type_playback);
    if (result != MA_OUT_OF_MEMORY) {
        printf("%s returned %d, expected MA_OUT_OF_MEMORY\n", pCase, result);
        failed = 1;
    }
    if (fixture.device.playback.converter._pHeap != pOldConverterHeap ||
        fixture.device.playback.pInputCache != pOldInputCache ||
        fixture.device.playback.inputCacheCap != oldInputCacheCap ||
        fixture.allocator.liveAllocationCount != oldLiveAllocationCount) {
        printf("%s changed published ownership on failure\n", pCase);
        return 1; /* The old implementation has dangling owners; do not double-free them in RED. */
    }

    failed += post_init_converter_still_works(&fixture, pCase);
    failed += post_init_fixture_uninit(&fixture, pCase);
    return failed;
}

static int post_init_test_success(void)
{
    post_init_fixture fixture;
    void* pOldConverterHeap;
    void* pOldInputCache;
    size_t oldLiveAllocationCount;
    ma_result result;
    int failed = 0;

    result = post_init_fixture_init(&fixture);
    if (result != MA_SUCCESS) {
        printf("post-init success fixture init failed: %d\n", result);
        return 1;
    }

    pOldConverterHeap = fixture.device.playback.converter._pHeap;
    pOldInputCache = fixture.device.playback.pInputCache;
    oldLiveAllocationCount = fixture.allocator.liveAllocationCount;

    fixture.device.playback.internalSampleRate = 44100;
    result = ma_device__post_init_setup(&fixture.device, ma_device_type_playback);
    if (result != MA_SUCCESS) {
        printf("post-init replacement failed: %d\n", result);
        return 1;
    }
    if (fixture.device.playback.converter._pHeap == pOldConverterHeap ||
        fixture.device.playback.pInputCache == pOldInputCache ||
        fixture.allocator.liveAllocationCount != oldLiveAllocationCount) {
        printf("post-init replacement did not atomically exchange ownership\n");
        failed = 1;
    }
    if (fixture.device.playback.converter.resampler.pBackendUserData != &fixture.device.playback.converter.resampler ||
        fixture.device.playback.converter.resampler.pBackend != &fixture.device.playback.converter.resampler.state.linear) {
        printf("post-init replacement retained pointers into temporary converter storage\n");
        failed = 1;
    }

    failed += post_init_fixture_uninit(&fixture, "post-init success");
    return failed;
}

static int post_init_test_backend_rollback_before_worker(void)
{
    ma_context context;
    ma_device device;
    ma_device_config config;
    ma_result result;

    MA_ZERO_OBJECT(&context);
    MA_ZERO_OBJECT(&device);
    g_backendInitCount = 0;
    g_backendUninitCount = 0;
    context.callbacks.onDeviceInit = post_init_backend_init_invalid_descriptor;
    context.callbacks.onDeviceUninit = post_init_backend_uninit;
    config = ma_device_config_init(ma_device_type_playback);

    result = ma_device_init(&context, &config, &device);
    if (result != MA_INVALID_ARGS || g_backendInitCount != 1 || g_backendUninitCount != 1 || device.pContext != NULL) {
        printf("pre-worker rollback mismatch: result=%d init=%u uninit=%u context=%p\n",
            result, g_backendInitCount, g_backendUninitCount, (void*)device.pContext);
        return 1;
    }

    return 0;
}

int main(void)
{
    int failed = 0;
    failed += post_init_test_failure(1, "converter allocation failure");
    failed += post_init_test_failure(2, "cache allocation failure");
    failed += post_init_test_success();
    failed += post_init_test_backend_rollback_before_worker();
    return failed != 0;
}
