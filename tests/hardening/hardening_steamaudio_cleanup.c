#include <stdint.h>
#include <stdio.h>
#include "../../miniaudio.c"
#include <phonon.h>

enum hardening_failure_step {
    HARDENING_FAIL_ENGINE,
    HARDENING_FAIL_CONTEXT,
    HARDENING_FAIL_HRTF,
    HARDENING_FAIL_SOUND,
    HARDENING_FAIL_EFFECT,
    HARDENING_FAIL_HEAP,
    HARDENING_FAIL_NODE_ATTACHMENT,
    HARDENING_FAIL_SOUND_ATTACHMENT,
    HARDENING_FAIL_SOUND_START,
    HARDENING_NORMAL_EXIT
};

enum hardening_event {
    HARDENING_SOUND_STOP = 1,
    HARDENING_SOUND_DETACH,
    HARDENING_SOUND_UNINIT,
    HARDENING_NODE_UNINIT,
    HARDENING_EFFECT_RELEASE,
    HARDENING_ENGINE_UNINIT,
    HARDENING_OWNER_HRTF_RELEASE,
    HARDENING_OWNER_CONTEXT_RELEASE
};

typedef struct {
    int engineAttempts, contextAttempts, hrtfAttempts, soundAttempts;
    int nodeAttempts, effectAttempts, heapAttempts;
    int engineCreate, engineRelease;
    int contextCreate, contextRetain, contextRelease;
    int hrtfCreate, hrtfRetain, hrtfRelease;
    int soundCreate, soundStop, soundDetach, soundRelease;
    int nodeCreate, nodeRelease;
    int effectCreate, effectRelease;
    int heapCreate, heapRelease;
    int attachmentAttempts, startAttempts, quitReads;
    int events[32], eventCount;
} hardening_counts;

static enum hardening_failure_step g_failStep;
static hardening_counts g_count;
static ma_waveform g_waveform;

static void hardening_record(int event)
{
    if (g_count.eventCount < (int)(sizeof(g_count.events) / sizeof(g_count.events[0]))) {
        g_count.events[g_count.eventCount++] = event;
    }
}

static ma_result hardening_engine_init(const ma_engine_config*, ma_engine*);
static void hardening_engine_uninit(ma_engine*);
static IPLerror IPLCALL hardening_context_create(IPLContextSettings*, IPLContext*);
static IPLContext IPLCALL hardening_context_retain(IPLContext);
static void IPLCALL hardening_context_release(IPLContext*);
static IPLerror IPLCALL hardening_hrtf_create(IPLContext, IPLAudioSettings*, IPLHRTFSettings*, IPLHRTF*);
static IPLHRTF IPLCALL hardening_hrtf_retain(IPLHRTF);
static void IPLCALL hardening_hrtf_release(IPLHRTF*);
static ma_result hardening_sound_init(ma_engine*, const ma_sound_config*, ma_sound*);
static void hardening_sound_uninit(ma_sound*);
static ma_result hardening_sound_stop(ma_sound*);
static ma_result hardening_sound_start(ma_sound*);
static ma_result hardening_node_init(ma_node_graph*, const ma_node_config*, const ma_allocation_callbacks*, ma_node*);
static void hardening_node_uninit(ma_node*, const ma_allocation_callbacks*);
static ma_result hardening_node_attach(ma_node*, ma_uint32, ma_node*, ma_uint32);
static ma_result hardening_node_detach(ma_node*);
static IPLerror IPLCALL hardening_effect_create(IPLContext, IPLAudioSettings*, IPLBinauralEffectSettings*, IPLBinauralEffect*);
static void IPLCALL hardening_effect_release(IPLBinauralEffect*);
static void* hardening_malloc(size_t, const ma_allocation_callbacks*);
static void hardening_free(void*, const ma_allocation_callbacks*);
static int hardening_getchar(void);

#define ma_engine_init hardening_engine_init
#define ma_engine_uninit hardening_engine_uninit
#define iplContextCreate hardening_context_create
#define iplContextRetain hardening_context_retain
#define iplContextRelease hardening_context_release
#define iplHRTFCreate hardening_hrtf_create
#define iplHRTFRetain hardening_hrtf_retain
#define iplHRTFRelease hardening_hrtf_release
#define ma_sound_init_ex hardening_sound_init
#define ma_sound_uninit hardening_sound_uninit
#define ma_sound_stop hardening_sound_stop
#define ma_sound_start hardening_sound_start
#define ma_node_init hardening_node_init
#define ma_node_uninit hardening_node_uninit
#define ma_node_attach_output_bus hardening_node_attach
#define ma_node_detach_all_output_buses hardening_node_detach
#define iplBinauralEffectCreate hardening_effect_create
#define iplBinauralEffectRelease hardening_effect_release
#define ma_malloc hardening_malloc
#define ma_free hardening_free
#define getchar hardening_getchar
#define main hardening_example_main
#include "hardening_steamaudio_cleanup_copy.c"
#undef main
#undef getchar
#undef ma_free
#undef ma_malloc
#undef iplBinauralEffectRelease
#undef iplBinauralEffectCreate
#undef ma_node_detach_all_output_buses
#undef ma_node_attach_output_bus
#undef ma_node_uninit
#undef ma_node_init
#undef ma_sound_start
#undef ma_sound_stop
#undef ma_sound_uninit
#undef ma_sound_init_ex
#undef iplHRTFRelease
#undef iplHRTFRetain
#undef iplHRTFCreate
#undef iplContextRelease
#undef iplContextRetain
#undef iplContextCreate
#undef ma_engine_uninit
#undef ma_engine_init

static ma_result hardening_engine_init(const ma_engine_config* pConfig, ma_engine* pEngine)
{
    ma_engine_config config = *pConfig;
    ma_result result;
    g_count.engineAttempts++;
    if (g_failStep == HARDENING_FAIL_ENGINE) return MA_ERROR;
    config.noDevice = MA_TRUE;
    result = ma_engine_init(&config, pEngine);
    if (result == MA_SUCCESS) g_count.engineCreate++;
    return result;
}

static void hardening_engine_uninit(ma_engine* pEngine)
{
    g_count.engineRelease++;
    hardening_record(HARDENING_ENGINE_UNINIT);
    ma_engine_uninit(pEngine);
}

static IPLerror IPLCALL hardening_context_create(IPLContextSettings* pSettings, IPLContext* pContext)
{
    IPLerror result;
    g_count.contextAttempts++;
    if (g_failStep == HARDENING_FAIL_CONTEXT) return IPL_STATUS_FAILURE;
    result = iplContextCreate(pSettings, pContext);
    if (result == IPL_STATUS_SUCCESS) g_count.contextCreate++;
    return result;
}

static IPLContext IPLCALL hardening_context_retain(IPLContext context)
{
    IPLContext retained = iplContextRetain(context);
    if (retained != NULL) g_count.contextRetain++;
    return retained;
}

static void IPLCALL hardening_context_release(IPLContext* pContext)
{
    if (pContext != NULL && *pContext != NULL) {
        if (pContext != &g_binauralNode.iplContext) hardening_record(HARDENING_OWNER_CONTEXT_RELEASE);
        g_count.contextRelease++;
    }
    iplContextRelease(pContext);
}

static IPLerror IPLCALL hardening_hrtf_create(IPLContext context, IPLAudioSettings* pAudio,
    IPLHRTFSettings* pSettings, IPLHRTF* pHRTF)
{
    IPLerror result;
    g_count.hrtfAttempts++;
    if (g_failStep == HARDENING_FAIL_HRTF) return IPL_STATUS_FAILURE;
    result = iplHRTFCreate(context, pAudio, pSettings, pHRTF);
    if (result == IPL_STATUS_SUCCESS) g_count.hrtfCreate++;
    return result;
}

static IPLHRTF IPLCALL hardening_hrtf_retain(IPLHRTF hrtf)
{
    IPLHRTF retained = iplHRTFRetain(hrtf);
    if (retained != NULL) g_count.hrtfRetain++;
    return retained;
}

static void IPLCALL hardening_hrtf_release(IPLHRTF* pHRTF)
{
    if (pHRTF != NULL && *pHRTF != NULL) {
        if (pHRTF != &g_binauralNode.iplHRTF) hardening_record(HARDENING_OWNER_HRTF_RELEASE);
        g_count.hrtfRelease++;
    }
    iplHRTFRelease(pHRTF);
}

static ma_result hardening_sound_init(ma_engine* pEngine, const ma_sound_config* pConfig, ma_sound* pSound)
{
    ma_sound_config config = *pConfig;
    ma_result result;
    g_count.soundAttempts++;
    if (g_failStep == HARDENING_FAIL_SOUND) return MA_ERROR;
    config.pFilePath = NULL;
    config.pDataSource = (ma_data_source*)&g_waveform;
    result = ma_sound_init_ex(pEngine, &config, pSound);
    if (result == MA_SUCCESS) g_count.soundCreate++;
    return result;
}

static void hardening_sound_uninit(ma_sound* pSound)
{
    g_count.soundRelease++;
    hardening_record(HARDENING_SOUND_UNINIT);
    ma_sound_uninit(pSound);
}

static ma_result hardening_sound_stop(ma_sound* pSound)
{
    g_count.soundStop++;
    hardening_record(HARDENING_SOUND_STOP);
    return ma_sound_stop(pSound);
}

static ma_result hardening_sound_start(ma_sound* pSound)
{
    g_count.startAttempts++;
    if (g_failStep == HARDENING_FAIL_SOUND_START) return MA_ERROR;
    return ma_sound_start(pSound);
}

static ma_result hardening_node_init(ma_node_graph* pGraph, const ma_node_config* pConfig,
    const ma_allocation_callbacks* pAllocator, ma_node* pNode)
{
    ma_result result = ma_node_init(pGraph, pConfig, pAllocator, pNode);
    g_count.nodeAttempts++;
    if (result == MA_SUCCESS) g_count.nodeCreate++;
    return result;
}

static void hardening_node_uninit(ma_node* pNode, const ma_allocation_callbacks* pAllocator)
{
    g_count.nodeRelease++;
    hardening_record(HARDENING_NODE_UNINIT);
    ma_node_uninit(pNode, pAllocator);
}

static ma_result hardening_node_attach(ma_node* pNode, ma_uint32 bus, ma_node* pOther, ma_uint32 inputBus)
{
    g_count.attachmentAttempts++;
    if ((g_count.attachmentAttempts == 1 && g_failStep == HARDENING_FAIL_NODE_ATTACHMENT) ||
        (g_count.attachmentAttempts == 2 && g_failStep == HARDENING_FAIL_SOUND_ATTACHMENT)) return MA_ERROR;
    return ma_node_attach_output_bus(pNode, bus, pOther, inputBus);
}

static ma_result hardening_node_detach(ma_node* pNode)
{
    g_count.soundDetach++;
    hardening_record(HARDENING_SOUND_DETACH);
    return ma_node_detach_all_output_buses(pNode);
}

static IPLerror IPLCALL hardening_effect_create(IPLContext context, IPLAudioSettings* pAudio,
    IPLBinauralEffectSettings* pSettings, IPLBinauralEffect* pEffect)
{
    IPLerror result;
    g_count.effectAttempts++;
    if (g_failStep == HARDENING_FAIL_EFFECT) return IPL_STATUS_FAILURE;
    result = iplBinauralEffectCreate(context, pAudio, pSettings, pEffect);
    if (result == IPL_STATUS_SUCCESS) g_count.effectCreate++;
    return result;
}

static void IPLCALL hardening_effect_release(IPLBinauralEffect* pEffect)
{
    if (pEffect != NULL && *pEffect != NULL) g_count.effectRelease++;
    hardening_record(HARDENING_EFFECT_RELEASE);
    iplBinauralEffectRelease(pEffect);
}

static void* hardening_malloc(size_t size, const ma_allocation_callbacks* pAllocator)
{
    void* pMemory;
    g_count.heapAttempts++;
    if (g_failStep == HARDENING_FAIL_HEAP) return NULL;
    pMemory = ma_malloc(size, pAllocator);
    if (pMemory != NULL) g_count.heapCreate++;
    return pMemory;
}

static void hardening_free(void* pMemory, const ma_allocation_callbacks* pAllocator)
{
    if (pMemory != NULL) g_count.heapRelease++;
    ma_free(pMemory, pAllocator);
}

static int hardening_getchar(void)
{
    g_count.quitReads++;
    return '\n';
}

static int hardening_event_before(int first, int second)
{
    int firstPosition = -1;
    int secondPosition = -1;
    int i;
    for (i = 0; i < g_count.eventCount; i++) {
        if (g_count.events[i] == first && firstPosition < 0) firstPosition = i;
        if (g_count.events[i] == second && secondPosition < 0) secondPosition = i;
    }
    return firstPosition >= 0 && secondPosition > firstPosition;
}

static int hardening_run_case(enum hardening_failure_step step)
{
    char* args[] = {(char*)"steamaudio-cleanup", (char*)"in-memory-waveform", NULL};
    int expectedEngine = step > HARDENING_FAIL_ENGINE;
    int expectedContext = step > HARDENING_FAIL_CONTEXT;
    int expectedHRTF = step > HARDENING_FAIL_HRTF;
    int expectedSound = step > HARDENING_FAIL_SOUND;
    int expectedNode = step > HARDENING_FAIL_SOUND;
    int expectedEffect = step > HARDENING_FAIL_EFFECT;
    int expectedHeap = step > HARDENING_FAIL_HEAP;
    int expectedAttachments = step <= HARDENING_FAIL_HEAP ? 0 :
        (step == HARDENING_FAIL_NODE_ATTACHMENT ? 1 : 2);
    int expectedStarts = step >= HARDENING_FAIL_SOUND_START ? 1 : 0;
    int result;

    MA_ZERO_OBJECT(&g_count);
    g_failStep = step;
    result = hardening_example_main(2, args);
    if ((step == HARDENING_NORMAL_EXIT && result != MA_SUCCESS) ||
        (step != HARDENING_NORMAL_EXIT && result == MA_SUCCESS)) {
        printf("step %d returned %d\n", step, result);
        return 1;
    }
    if (g_count.engineAttempts != 1 || g_count.contextAttempts != expectedEngine ||
        g_count.hrtfAttempts != expectedContext || g_count.soundAttempts != expectedHRTF ||
        g_count.nodeAttempts != expectedSound || g_count.effectAttempts != expectedNode ||
        g_count.heapAttempts != expectedEffect ||
        g_count.attachmentAttempts != expectedAttachments || g_count.startAttempts != expectedStarts) {
        printf("step %d did not reach intended injected API: engine=%d context=%d HRTF=%d sound=%d node=%d effect=%d heap=%d attach=%d start=%d\n",
            step, g_count.engineAttempts, g_count.contextAttempts, g_count.hrtfAttempts,
            g_count.soundAttempts, g_count.nodeAttempts, g_count.effectAttempts,
            g_count.heapAttempts, g_count.attachmentAttempts, g_count.startAttempts);
        return 1;
    }
    if (g_count.engineCreate != expectedEngine || g_count.engineRelease != expectedEngine ||
        g_count.contextCreate != expectedContext || g_count.hrtfCreate != expectedHRTF ||
        g_count.soundCreate != expectedSound || g_count.soundStop != expectedSound ||
        g_count.soundDetach != expectedSound || g_count.soundRelease != expectedSound ||
        g_count.nodeCreate != expectedNode || g_count.nodeRelease != expectedNode ||
        g_count.effectCreate != expectedEffect || g_count.effectRelease != expectedEffect ||
        g_count.heapCreate != expectedHeap || g_count.heapRelease != expectedHeap) {
        printf("step %d owner counts engine %d/%d context %d HRTF %d sound %d/%d/%d/%d node %d/%d effect %d/%d heap %d/%d\n",
            step, g_count.engineCreate, g_count.engineRelease, g_count.contextCreate,
            g_count.hrtfCreate, g_count.soundCreate, g_count.soundStop,
            g_count.soundDetach, g_count.soundRelease, g_count.nodeCreate,
            g_count.nodeRelease, g_count.effectCreate, g_count.effectRelease,
            g_count.heapCreate, g_count.heapRelease);
        return 1;
    }
    if (g_count.contextCreate + g_count.contextRetain != g_count.contextRelease ||
        g_count.hrtfCreate + g_count.hrtfRetain != g_count.hrtfRelease) {
        printf("step %d leaked owner references: context %d+%d/%d HRTF %d+%d/%d\n",
            step, g_count.contextCreate, g_count.contextRetain, g_count.contextRelease,
            g_count.hrtfCreate, g_count.hrtfRetain, g_count.hrtfRelease);
        return 1;
    }
    if (expectedSound && (!hardening_event_before(HARDENING_SOUND_STOP, HARDENING_SOUND_DETACH) ||
        !hardening_event_before(HARDENING_SOUND_DETACH, HARDENING_SOUND_UNINIT))) {
        printf("step %d violated sound stop/detach/uninit order\n", step);
        return 1;
    }
    if (expectedHeap && expectedNode && expectedEffect &&
        !hardening_event_before(HARDENING_NODE_UNINIT, HARDENING_EFFECT_RELEASE)) {
        printf("step %d released effect before retiring its graph node\n", step);
        return 1;
    }
    if (expectedHeap && expectedNode && expectedSound &&
        !hardening_event_before(HARDENING_SOUND_UNINIT, HARDENING_NODE_UNINIT)) {
        printf("step %d retired the effect node before its sound\n", step);
        return 1;
    }
    if (expectedHRTF && expectedEngine &&
        (!hardening_event_before(HARDENING_ENGINE_UNINIT, HARDENING_OWNER_HRTF_RELEASE) ||
         !hardening_event_before(HARDENING_OWNER_HRTF_RELEASE, HARDENING_OWNER_CONTEXT_RELEASE))) {
        printf("step %d violated engine/HRTF/context release order\n", step);
        return 1;
    }
    if (g_count.quitReads != (step == HARDENING_NORMAL_EXIT)) {
        printf("step %d unexpected normal-exit prompt count %d\n", step, g_count.quitReads);
        return 1;
    }
    return 0;
}

int main(void)
{
    ma_waveform_config waveformConfig = ma_waveform_config_init(ma_format_f32, 2, 48000,
        ma_waveform_type_sine, 0.125, 440);
    int step;
    int failures = 0;
    if (ma_waveform_init(&waveformConfig, &g_waveform) != MA_SUCCESS) return 1;
    for (step = HARDENING_FAIL_ENGINE; step <= HARDENING_NORMAL_EXIT; step++) {
        failures += hardening_run_case((enum hardening_failure_step)step);
    }
    ma_waveform_uninit(&g_waveform);
    if (failures == 0) printf("Steam Audio example cleanup matrix passed\n");
    return failures ? 1 : 0;
}
