#include "../../miniaudio.h"

static ma_result hardening_activation_wait(void* pHandler, DWORD timeoutInMilliseconds);
static ma_bool32 hardening_process_loopback_supported(ma_context* pContext);

#define MA_WASAPI_COMPLETION_HANDLER_WAIT(pHandler, timeoutInMilliseconds) hardening_activation_wait((void*)(pHandler), (timeoutInMilliseconds))
#define MA_WASAPI_PROCESS_LOOPBACK_SUPPORTED(pContext) hardening_process_loopback_supported((pContext))
#define MINIAUDIO_IMPLEMENTATION
#include "../../miniaudio.h"

#include <stdio.h>
#include <stdlib.h>

typedef enum
{
    hardening_async_immediate_success,
    hardening_async_call_failure,
    hardening_async_result_failure,
    hardening_async_query_failure,
    hardening_async_timeout
} hardening_async_mode;

typedef struct
{
    ma_uint32 allocations;
    ma_uint32 frees;
} hardening_allocator_state;

typedef struct
{
    ma_IAudioClientVtbl* lpVtbl;
    ULONG refs;
    ma_uint32 releases;
} hardening_audio_client;

typedef struct
{
    ma_IActivateAudioInterfaceAsyncOperationVtbl* lpVtbl;
    ULONG refs;
    ma_uint32 releases;
} hardening_async_operation;

static hardening_async_mode g_asyncMode;
static ma_bool32 g_processLoopbackSupported;
static ma_uint32 g_activationCalls;
static ma_bool32 g_sawVirtualDevice;
static ma_bool32 g_sawAudioClientIID;
static ma_bool32 g_sawActivationBlob;
static ma_uint32 g_targetProcessID;
static MA_PROCESS_LOOPBACK_MODE g_processLoopbackMode;
static ma_completion_handler* g_pendingHandler;
static hardening_audio_client g_audioClient;
static hardening_async_operation g_asyncOperation;

static void* hardening_malloc(size_t sz, void* pUserData)
{
    hardening_allocator_state* pState = (hardening_allocator_state*)pUserData;
    void* p = malloc(sz);
    if (p != NULL) {
        pState->allocations += 1;
    }
    return p;
}

static void* hardening_realloc(void* p, size_t sz, void* pUserData)
{
    hardening_allocator_state* pState = (hardening_allocator_state*)pUserData;
    void* pNew;

    if (p == NULL) {
        pNew = realloc(NULL, sz);
        if (pNew != NULL) {
            pState->allocations += 1;
        }
        return pNew;
    }

    return realloc(p, sz);
}

static void hardening_free(void* p, void* pUserData)
{
    hardening_allocator_state* pState = (hardening_allocator_state*)pUserData;
    if (p != NULL) {
        pState->frees += 1;
        free(p);
    }
}

static HRESULT STDMETHODCALLTYPE hardening_audio_query_interface(ma_IAudioClient* pThis, const IID* const riid, void** ppObject)
{
    hardening_audio_client* pClient = (hardening_audio_client*)pThis;

    if (g_asyncMode == hardening_async_query_failure || !ma_is_guid_equal(riid, &MA_IID_IAudioClient)) {
        *ppObject = NULL;
        return E_NOINTERFACE;
    }

    pClient->refs += 1;
    *ppObject = pThis;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE hardening_audio_add_ref(ma_IAudioClient* pThis)
{
    hardening_audio_client* pClient = (hardening_audio_client*)pThis;
    pClient->refs += 1;
    return pClient->refs;
}

static ULONG STDMETHODCALLTYPE hardening_audio_release(ma_IAudioClient* pThis)
{
    hardening_audio_client* pClient = (hardening_audio_client*)pThis;
    if (pClient->refs == 0) {
        return 0;
    }
    pClient->refs -= 1;
    pClient->releases += 1;
    return pClient->refs;
}

static ma_IAudioClientVtbl g_audioClientVtbl = {
    hardening_audio_query_interface,
    hardening_audio_add_ref,
    hardening_audio_release
};

static HRESULT STDMETHODCALLTYPE hardening_operation_query_interface(ma_IActivateAudioInterfaceAsyncOperation* pThis, const IID* const riid, void** ppObject)
{
    (void)pThis;
    (void)riid;
    *ppObject = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE hardening_operation_add_ref(ma_IActivateAudioInterfaceAsyncOperation* pThis)
{
    hardening_async_operation* pOperation = (hardening_async_operation*)pThis;
    pOperation->refs += 1;
    return pOperation->refs;
}

static ULONG STDMETHODCALLTYPE hardening_operation_release(ma_IActivateAudioInterfaceAsyncOperation* pThis)
{
    hardening_async_operation* pOperation = (hardening_async_operation*)pThis;
    if (pOperation->refs > 0) {
        pOperation->refs -= 1;
    }
    pOperation->releases += 1;
    return pOperation->refs;
}

static HRESULT STDMETHODCALLTYPE hardening_operation_get_result(ma_IActivateAudioInterfaceAsyncOperation* pThis, HRESULT* pActivateResult, ma_IUnknown** ppActivatedInterface)
{
    (void)pThis;
    *ppActivatedInterface = NULL;

    if (g_asyncMode == hardening_async_result_failure) {
        *pActivateResult = E_ACCESSDENIED;
        return S_OK;
    }

    *pActivateResult = S_OK;
    hardening_audio_add_ref((ma_IAudioClient*)&g_audioClient);
    *ppActivatedInterface = (ma_IUnknown*)&g_audioClient;
    return S_OK;
}

static ma_IActivateAudioInterfaceAsyncOperationVtbl g_asyncOperationVtbl = {
    hardening_operation_query_interface,
    hardening_operation_add_ref,
    hardening_operation_release,
    hardening_operation_get_result
};

static HRESULT WINAPI hardening_activate_audio_interface_async(
    const wchar_t* pDeviceInterfacePath,
    const IID* pIID,
    MA_PROPVARIANT* pActivationParams,
    ma_IActivateAudioInterfaceCompletionHandler* pCompletionHandler,
    ma_IActivateAudioInterfaceAsyncOperation** ppOperation)
{
    ma_completion_handler* pHandler = (ma_completion_handler*)pCompletionHandler;
    MA_AUDIOCLIENT_ACTIVATION_PARAMS* pParams = NULL;

    g_activationCalls += 1;
    g_sawVirtualDevice = ma_wcscmp(pDeviceInterfacePath, MA_VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK) == 0;
    g_sawAudioClientIID = ma_is_guid_equal(pIID, &MA_IID_IAudioClient);

    if (pActivationParams != NULL && pActivationParams->vt == MA_VT_BLOB &&
        pActivationParams->blob.cbSize == sizeof(MA_AUDIOCLIENT_ACTIVATION_PARAMS) &&
        pActivationParams->blob.pBlobData != NULL) {
        pParams = (MA_AUDIOCLIENT_ACTIVATION_PARAMS*)pActivationParams->blob.pBlobData;
        if (pParams->ActivationType == MA_AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK) {
            g_sawActivationBlob = MA_TRUE;
            g_targetProcessID = pParams->ProcessLoopbackParams.TargetProcessId;
            g_processLoopbackMode = pParams->ProcessLoopbackParams.ProcessLoopbackMode;
        }
    }

    *ppOperation = NULL;
    if (g_asyncMode == hardening_async_call_failure) {
        return E_ACCESSDENIED;
    }

    g_asyncOperation.refs = 1;
    *ppOperation = (ma_IActivateAudioInterfaceAsyncOperation*)&g_asyncOperation;

    ma_completion_handler_AddRef(pHandler);
    if (g_asyncMode == hardening_async_timeout) {
        g_pendingHandler = pHandler;
    } else {
        ma_completion_handler_ActivateCompleted(pHandler, *ppOperation);
        ma_completion_handler_Release(pHandler);
    }

    return S_OK;
}

static ma_result hardening_activation_wait(void* pHandler, DWORD timeoutInMilliseconds)
{
    if (g_asyncMode == hardening_async_timeout) {
        return MA_TIMEOUT;
    }
    return ma_completion_handler_wait((ma_completion_handler*)pHandler, timeoutInMilliseconds);
}

static ma_bool32 hardening_process_loopback_supported(ma_context* pContext)
{
    (void)pContext;
    return g_processLoopbackSupported;
}

static void hardening_reset(hardening_allocator_state* pAllocatorState, ma_context* pContext)
{
    MA_ZERO_OBJECT(pAllocatorState);
    MA_ZERO_OBJECT(pContext);
    MA_ZERO_OBJECT(&g_audioClient);
    MA_ZERO_OBJECT(&g_asyncOperation);

    g_asyncMode = hardening_async_immediate_success;
    g_processLoopbackSupported = MA_TRUE;
    g_activationCalls = 0;
    g_sawVirtualDevice = MA_FALSE;
    g_sawAudioClientIID = MA_FALSE;
    g_sawActivationBlob = MA_FALSE;
    g_targetProcessID = 0;
    g_processLoopbackMode = MA_PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    g_pendingHandler = NULL;

    g_audioClient.lpVtbl = &g_audioClientVtbl;
    g_asyncOperation.lpVtbl = &g_asyncOperationVtbl;

    pContext->allocationCallbacks.pUserData = pAllocatorState;
    pContext->allocationCallbacks.onMalloc = hardening_malloc;
    pContext->allocationCallbacks.onRealloc = hardening_realloc;
    pContext->allocationCallbacks.onFree = hardening_free;
    pContext->wasapi.ActivateAudioInterfaceAsync = (ma_proc)hardening_activate_audio_interface_async;
}

static int hardening_release_outputs(ma_IAudioClient* pAudioClient, ma_WASAPIDeviceInterface* pDeviceInterface)
{
    if (pAudioClient != NULL) {
        ma_IAudioClient_Release(pAudioClient);
    }
    if (pDeviceInterface != NULL) {
        ma_IUnknown_Release((ma_IUnknown*)pDeviceInterface);
    }
    return g_audioClient.refs == 0 ? 0 : 1;
}

static int hardening_test_include_exclude(void)
{
    hardening_allocator_state allocatorState;
    ma_context context;
    ma_IAudioClient* pAudioClient = NULL;
    ma_WASAPIDeviceInterface* pDeviceInterface = NULL;
    ma_result result;
    int failed = 0;

    hardening_reset(&allocatorState, &context);
    result = ma_context_get_IAudioClient__wasapi(&context, ma_device_type_loopback, NULL, 4321, MA_FALSE, &pAudioClient, &pDeviceInterface);
    if (result != MA_SUCCESS || !g_sawVirtualDevice || !g_sawAudioClientIID || !g_sawActivationBlob ||
        g_targetProcessID != 4321 || g_processLoopbackMode != MA_PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE) {
        printf("include activation contract failed: result=%d calls=%u pid=%u mode=%d\n", result, g_activationCalls, g_targetProcessID, g_processLoopbackMode);
        failed = 1;
    }
    failed += hardening_release_outputs(pAudioClient, pDeviceInterface);
    if (allocatorState.allocations != allocatorState.frees) {
        printf("include handler leak: alloc=%u free=%u\n", allocatorState.allocations, allocatorState.frees);
        failed = 1;
    }

    hardening_reset(&allocatorState, &context);
    pAudioClient = NULL;
    pDeviceInterface = NULL;
    result = ma_context_get_IAudioClient__wasapi(&context, ma_device_type_loopback, NULL, 9876, MA_TRUE, &pAudioClient, &pDeviceInterface);
    if (result != MA_SUCCESS || g_targetProcessID != 9876 || g_processLoopbackMode != MA_PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE) {
        printf("exclude activation contract failed: result=%d pid=%u mode=%d\n", result, g_targetProcessID, g_processLoopbackMode);
        failed = 1;
    }
    failed += hardening_release_outputs(pAudioClient, pDeviceInterface);
    if (allocatorState.allocations != allocatorState.frees) {
        printf("exclude handler leak: alloc=%u free=%u\n", allocatorState.allocations, allocatorState.frees);
        failed = 1;
    }

    return failed;
}

static int hardening_test_rejected_contracts(void)
{
    hardening_allocator_state allocatorState;
    ma_context context;
    ma_device_id deviceID;
    ma_IAudioClient* pAudioClient = NULL;
    ma_WASAPIDeviceInterface* pDeviceInterface = NULL;
    ma_result result;
    int failed = 0;

    hardening_reset(&allocatorState, &context);
    MA_ZERO_OBJECT(&deviceID);
    result = ma_context_get_IAudioClient__wasapi(&context, ma_device_type_loopback, &deviceID, 10, MA_FALSE, &pAudioClient, &pDeviceInterface);
    if (result != MA_INVALID_ARGS || g_activationCalls != 0) {
        printf("explicit endpoint was not rejected: result=%d calls=%u\n", result, g_activationCalls);
        failed = 1;
    }

    hardening_reset(&allocatorState, &context);
    g_processLoopbackSupported = MA_FALSE;
    result = ma_context_get_IAudioClient__wasapi(&context, ma_device_type_loopback, NULL, 10, MA_FALSE, &pAudioClient, &pDeviceInterface);
    if (result != MA_NOT_IMPLEMENTED || g_activationCalls != 0) {
        printf("unsupported OS was not rejected: result=%d calls=%u\n", result, g_activationCalls);
        failed = 1;
    }

    hardening_reset(&allocatorState, &context);
    context.wasapi.ActivateAudioInterfaceAsync = NULL;
    result = ma_context_get_IAudioClient__wasapi(&context, ma_device_type_loopback, NULL, 10, MA_FALSE, &pAudioClient, &pDeviceInterface);
    if (result != MA_NOT_IMPLEMENTED || g_activationCalls != 0) {
        printf("missing async symbol was not rejected: result=%d calls=%u\n", result, g_activationCalls);
        failed = 1;
    }

    return failed;
}

static int hardening_test_failure_cleanup(void)
{
    const hardening_async_mode modes[] = {
        hardening_async_call_failure,
        hardening_async_result_failure,
        hardening_async_query_failure
    };
    ma_uint32 i;
    int failed = 0;

    for (i = 0; i < ma_countof(modes); i += 1) {
        hardening_allocator_state allocatorState;
        ma_context context;
        ma_IAudioClient* pAudioClient = NULL;
        ma_WASAPIDeviceInterface* pDeviceInterface = NULL;
        ma_result result;

        hardening_reset(&allocatorState, &context);
        g_asyncMode = modes[i];
        result = ma_context_get_IAudioClient__wasapi(&context, ma_device_type_loopback, NULL, 55, MA_FALSE, &pAudioClient, &pDeviceInterface);
        if (result == MA_SUCCESS || pAudioClient != NULL || pDeviceInterface != NULL ||
            allocatorState.allocations != allocatorState.frees || g_audioClient.refs != 0) {
            printf("failure cleanup mode=%d result=%d audio=%p interface=%p refs=%lu alloc=%u free=%u\n",
                modes[i], result, (void*)pAudioClient, (void*)pDeviceInterface, (unsigned long)g_audioClient.refs,
                allocatorState.allocations, allocatorState.frees);
            failed = 1;
        }
    }

    return failed;
}

static int hardening_test_late_completion(void)
{
    hardening_allocator_state allocatorState;
    ma_context context;
    ma_IAudioClient* pAudioClient = NULL;
    ma_WASAPIDeviceInterface* pDeviceInterface = NULL;
    ma_result result;

    hardening_reset(&allocatorState, &context);
    g_asyncMode = hardening_async_timeout;
    result = ma_context_get_IAudioClient__wasapi(&context, ma_device_type_loopback, NULL, 77, MA_FALSE, &pAudioClient, &pDeviceInterface);
    if (result != MA_TIMEOUT || pAudioClient != NULL || pDeviceInterface != NULL || g_pendingHandler == NULL ||
        allocatorState.allocations != 1 || allocatorState.frees != 0) {
        printf("timeout ownership failed: result=%d pending=%p alloc=%u free=%u\n",
            result, (void*)g_pendingHandler, allocatorState.allocations, allocatorState.frees);
        return 1;
    }

    ma_completion_handler_ActivateCompleted(g_pendingHandler, (ma_IActivateAudioInterfaceAsyncOperation*)&g_asyncOperation);
    ma_completion_handler_Release(g_pendingHandler);
    g_pendingHandler = NULL;

    if (allocatorState.allocations != allocatorState.frees) {
        printf("late completion leak: alloc=%u free=%u\n", allocatorState.allocations, allocatorState.frees);
        return 1;
    }

    return 0;
}

static int hardening_test_public_config_validation(void)
{
    ma_device device;
    ma_device_config config;
    ma_device_descriptor playback;
    ma_device_descriptor capture;
    ma_device_id deviceID;
    ma_result result;
    int failed = 0;

    MA_ZERO_OBJECT(&device);
    MA_ZERO_OBJECT(&playback);
    MA_ZERO_OBJECT(&capture);
    config = ma_device_config_init(ma_device_type_loopback);
    capture.shareMode = ma_share_mode_exclusive;
    result = ma_device_init__wasapi(&device, &config, &playback, &capture);
    if (result != MA_INVALID_DEVICE_CONFIG) {
        printf("exclusive process loopback was not rejected: result=%d\n", result);
        failed = 1;
    }

    MA_ZERO_OBJECT(&device);
    MA_ZERO_OBJECT(&capture);
    MA_ZERO_OBJECT(&deviceID);
    config = ma_device_config_init(ma_device_type_loopback);
    config.wasapi.loopbackProcessID = 123;
    capture.pDeviceID = &deviceID;
    result = ma_device_init__wasapi(&device, &config, &playback, &capture);
    if (result != MA_INVALID_DEVICE_CONFIG) {
        printf("endpoint-specific process loopback was not rejected: result=%d\n", result);
        failed = 1;
    }

    return failed;
}

int main(void)
{
    int failed = 0;

    failed += hardening_test_include_exclude();
    failed += hardening_test_rejected_contracts();
    failed += hardening_test_failure_cleanup();
    failed += hardening_test_late_completion();
    failed += hardening_test_public_config_validation();

    if (failed != 0) {
        printf("WASAPI process-loopback hardening failures: %d\n", failed);
        return 1;
    }

    printf("WASAPI process-loopback hardening tests passed.\n");
    return 0;
}
