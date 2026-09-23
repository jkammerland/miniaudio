/*
Demonstrates integration of Steam Audio with miniaudio's engine API.

In this example a HRTF effect from Steam Audio will be applied. To do this a custom node will be
implemented which uses Steam Audio's IPLBinauralEffect and IPLHRTF objects.

By implementing this as a node, it can be plugged into any position within the graph. The output
channel count of this node is always stereo.

Steam Audio requires fixed sized processing, the size of which must be specified at initialization
time of the IPLBinauralEffect and IPLHRTF objects. The node below stages input into fixed Steam
Audio blocks, so miniaudio callbacks may use any frame count. This adds one Steam Audio block of
latency. Missing input is staged as silence; signal end-of-input explicitly to flush a short
final block and the effect tail.
*/
#include "../miniaudio.c"

#include <stdint.h> /* Required for uint32_t which is used by STEAMAUDIO_VERSION, and a random use of uint8_t. If there's a Steam Audio maintainer reading this, that needs to be fixed to use IPLuint32 and IPLuint8. */

/* Need to silence some warnings from the Steam Audio headers. */
#if defined(__clang__) || (defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)))
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wlong-long"
    #pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <phonon.h> /* Steam Audio */
#if defined(__clang__) || (defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)))
    #pragma GCC diagnostic pop
#endif

#define FORMAT      ma_format_f32   /* Must be floating point. */
#define CHANNELS    2               /* Must be stereo for this example. */
#define SAMPLE_RATE 48000


static ma_result ma_result_from_IPLerror(IPLerror error)
{
    switch (error)
    {
        case IPL_STATUS_SUCCESS:     return MA_SUCCESS;
        case IPL_STATUS_OUTOFMEMORY: return MA_OUT_OF_MEMORY;
        case IPL_STATUS_INITIALIZATION:
        case IPL_STATUS_FAILURE:
        default: return MA_ERROR;
    }
}


typedef struct
{
    ma_node_config nodeConfig;
    ma_uint32 channelsIn;
    IPLAudioSettings iplAudioSettings;
    IPLContext iplContext;
    IPLHRTF iplHRTF;   /* There is one HRTF object to many binaural effect objects. */
} ma_steamaudio_binaural_node_config;

MA_API ma_steamaudio_binaural_node_config ma_steamaudio_binaural_node_config_init(ma_uint32 channelsIn, IPLAudioSettings iplAudioSettings, IPLContext iplContext, IPLHRTF iplHRTF);


#define MA_STEAMAUDIO_DIRECTION_INDEX_MASK  0x00000003U
#define MA_STEAMAUDIO_DIRECTION_DIRTY       0x80000000U

#ifndef MA_STEAMAUDIO_DIRECTION_PUBLISH_AFTER_WRITE
    #define MA_STEAMAUDIO_DIRECTION_PUBLISH_AFTER_WRITE()  ((void)0)
#endif
#ifndef MA_STEAMAUDIO_DIRECTION_CONSUME_AFTER_LOAD
    #define MA_STEAMAUDIO_DIRECTION_CONSUME_AFTER_LOAD()   ((void)0)
#endif
#ifndef MA_STEAMAUDIO_DIRECTION_CONSUME_AFTER_EXCHANGE
    #define MA_STEAMAUDIO_DIRECTION_CONSUME_AFTER_EXCHANGE() ((void)0)
#endif

typedef struct
{
    ma_vec3f snapshots[3];
    ma_uint32 frontIndex;              /* Audio thread only. */
    ma_uint32 backIndex;               /* Control thread only. */
    volatile ma_uint32 middleState;    /* Shared index plus dirty bit. */
} ma_steamaudio_direction_state;

static void ma_steamaudio_direction_state_init(ma_steamaudio_direction_state* pState, ma_vec3f direction)
{
    pState->snapshots[0] = direction;
    pState->snapshots[1] = direction;
    pState->snapshots[2] = direction;
    pState->frontIndex = 0;
    pState->backIndex = 2;
    ma_atomic_store_32(&pState->middleState, 1);
}

static void ma_steamaudio_direction_state_publish(ma_steamaudio_direction_state* pState, ma_vec3f direction)
{
    ma_uint32 previousMiddleState;

    /* This side is single-publisher. The audio thread is the only consumer. */
    pState->snapshots[pState->backIndex] = direction;
    MA_STEAMAUDIO_DIRECTION_PUBLISH_AFTER_WRITE();
    previousMiddleState = ma_atomic_exchange_32(&pState->middleState, pState->backIndex | MA_STEAMAUDIO_DIRECTION_DIRTY);
    pState->backIndex = previousMiddleState & MA_STEAMAUDIO_DIRECTION_INDEX_MASK;
}

static ma_vec3f ma_steamaudio_direction_state_consume(ma_steamaudio_direction_state* pState)
{
    ma_uint32 middleState = ma_atomic_load_32(&pState->middleState);

    MA_STEAMAUDIO_DIRECTION_CONSUME_AFTER_LOAD();
    if ((middleState & MA_STEAMAUDIO_DIRECTION_DIRTY) != 0) {
        middleState = ma_atomic_exchange_32(&pState->middleState, pState->frontIndex);
        pState->frontIndex = middleState & MA_STEAMAUDIO_DIRECTION_INDEX_MASK;
        MA_STEAMAUDIO_DIRECTION_CONSUME_AFTER_EXCHANGE();
    }

    return pState->snapshots[pState->frontIndex];
}


typedef struct
{
    ma_node_base baseNode;
    IPLAudioSettings iplAudioSettings;
    IPLContext iplContext;
    IPLHRTF iplHRTF;
    IPLBinauralEffect iplEffect;
    ma_steamaudio_direction_state directionState;
    ma_uint32 inputFrameCount;
    ma_uint32 outputFrameOffset;
    ma_uint32 outputFrameCount;
    ma_bool32 tailComplete;
    volatile ma_uint32 inputEnded;
    float* ppBuffersIn[2];      /* Each buffer is an offset of _pHeap. */
    float* ppBuffersOut[2];     /* Each buffer is an offset of _pHeap. */
    void* _pHeap;
} ma_steamaudio_binaural_node;

MA_API ma_result ma_steamaudio_binaural_node_init(ma_node_graph* pNodeGraph, const ma_steamaudio_binaural_node_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_steamaudio_binaural_node* pBinauralNode);
MA_API void ma_steamaudio_binaural_node_uninit(ma_steamaudio_binaural_node* pBinauralNode, const ma_allocation_callbacks* pAllocationCallbacks);
MA_API ma_result ma_steamaudio_binaural_node_set_direction(ma_steamaudio_binaural_node* pBinauralNode, float x, float y, float z);
/* Call after the upstream source stops. A later NULL input then drains the final block and tail. */
MA_API ma_result ma_steamaudio_binaural_node_end_input(ma_steamaudio_binaural_node* pBinauralNode);
/* For a seek/restart, detach the output bus and quiesce readers before resetting and reattaching. */
MA_API ma_result ma_steamaudio_binaural_node_reset(ma_steamaudio_binaural_node* pBinauralNode);


MA_API ma_steamaudio_binaural_node_config ma_steamaudio_binaural_node_config_init(ma_uint32 channelsIn, IPLAudioSettings iplAudioSettings, IPLContext iplContext, IPLHRTF iplHRTF)
{
    ma_steamaudio_binaural_node_config config;

    MA_ZERO_OBJECT(&config);
    config.nodeConfig       = ma_node_config_init();
    config.channelsIn       = channelsIn;
    config.iplAudioSettings = iplAudioSettings;
    config.iplContext       = iplContext;
    config.iplHRTF          = iplHRTF;

    return config;
}


static void ma_steamaudio_binaural_node_process_block(ma_steamaudio_binaural_node* pBinauralNode)
{
    IPLBinauralEffectParams binauralParams;
    IPLAudioBuffer inputBufferDesc;
    IPLAudioBuffer outputBufferDesc;
    ma_vec3f direction;
    IPLAudioEffectState effectState;

    direction = ma_steamaudio_direction_state_consume(&pBinauralNode->directionState);
    MA_ZERO_OBJECT(&binauralParams);
    binauralParams.direction.x   = direction.x;
    binauralParams.direction.y   = direction.y;
    binauralParams.direction.z   = direction.z;
    binauralParams.interpolation = IPL_HRTFINTERPOLATION_NEAREST;
    binauralParams.spatialBlend  = 1.0f;
    binauralParams.hrtf          = pBinauralNode->iplHRTF;

    inputBufferDesc.numChannels  = (IPLint32)ma_node_get_input_channels(&pBinauralNode->baseNode, 0);
    inputBufferDesc.numSamples   = pBinauralNode->iplAudioSettings.frameSize;
    inputBufferDesc.data         = pBinauralNode->ppBuffersIn;
    outputBufferDesc.numSamples  = pBinauralNode->iplAudioSettings.frameSize;
    outputBufferDesc.numChannels = 2;
    outputBufferDesc.data        = pBinauralNode->ppBuffersOut;

    effectState = iplBinauralEffectApply(pBinauralNode->iplEffect, &binauralParams, &inputBufferDesc, &outputBufferDesc);
    pBinauralNode->inputFrameCount = 0;
    pBinauralNode->outputFrameOffset = 0;
    pBinauralNode->outputFrameCount = (ma_uint32)pBinauralNode->iplAudioSettings.frameSize;
    pBinauralNode->tailComplete = (effectState == IPL_AUDIOEFFECTSTATE_TAILCOMPLETE);
}

static void ma_steamaudio_binaural_node_process_tail_block(ma_steamaudio_binaural_node* pBinauralNode)
{
    IPLAudioBuffer outputBufferDesc;
    IPLAudioEffectState effectState;

    outputBufferDesc.numSamples  = pBinauralNode->iplAudioSettings.frameSize;
    outputBufferDesc.numChannels = 2;
    outputBufferDesc.data        = pBinauralNode->ppBuffersOut;

    effectState = iplBinauralEffectGetTail(pBinauralNode->iplEffect, &outputBufferDesc);
    pBinauralNode->outputFrameOffset = 0;
    pBinauralNode->outputFrameCount = (ma_uint32)pBinauralNode->iplAudioSettings.frameSize;
    pBinauralNode->tailComplete = (effectState == IPL_AUDIOEFFECTSTATE_TAILCOMPLETE);
}

static void ma_steamaudio_binaural_node_copy_input(ma_steamaudio_binaural_node* pBinauralNode, const float* pFramesIn, ma_uint32 frameOffset, ma_uint32 frameCount)
{
    ma_uint32 channelsIn = ma_node_get_input_channels(&pBinauralNode->baseNode, 0);

    if (channelsIn == 1) {
        ma_copy_pcm_frames(pBinauralNode->ppBuffersIn[0] + pBinauralNode->inputFrameCount, pFramesIn + frameOffset, frameCount, ma_format_f32, 1);
    } else {
        void* ppDeinterleaved[2];
        ppDeinterleaved[0] = pBinauralNode->ppBuffersIn[0] + pBinauralNode->inputFrameCount;
        ppDeinterleaved[1] = pBinauralNode->ppBuffersIn[1] + pBinauralNode->inputFrameCount;
        ma_deinterleave_pcm_frames(ma_format_f32, channelsIn, frameCount, ma_offset_pcm_frames_const_ptr_f32(pFramesIn, frameOffset, channelsIn), ppDeinterleaved);
    }

    pBinauralNode->inputFrameCount += frameCount;
}

static void ma_steamaudio_binaural_node_copy_output(ma_steamaudio_binaural_node* pBinauralNode, float* pFramesOut, ma_uint32 frameOffset, ma_uint32 frameCount)
{
    const void* ppDeinterleaved[2];

    if (pFramesOut != NULL) {
        ppDeinterleaved[0] = pBinauralNode->ppBuffersOut[0] + pBinauralNode->outputFrameOffset;
        ppDeinterleaved[1] = pBinauralNode->ppBuffersOut[1] + pBinauralNode->outputFrameOffset;
        ma_interleave_pcm_frames(ma_format_f32, 2, frameCount, ppDeinterleaved, ma_offset_pcm_frames_ptr_f32(pFramesOut, frameOffset, 2));
    }

    pBinauralNode->outputFrameOffset += frameCount;
    pBinauralNode->outputFrameCount -= frameCount;
    if (pBinauralNode->outputFrameCount == 0) {
        pBinauralNode->outputFrameOffset = 0;
    }
}

static void ma_steamaudio_binaural_node_flush_partial_input(ma_steamaudio_binaural_node* pBinauralNode)
{
    ma_uint32 iChannel;
    ma_uint32 channelsIn = ma_node_get_input_channels(&pBinauralNode->baseNode, 0);
    ma_uint32 frameSize = (ma_uint32)pBinauralNode->iplAudioSettings.frameSize;

    for (iChannel = 0; iChannel < channelsIn; iChannel += 1) {
        ma_silence_pcm_frames(pBinauralNode->ppBuffersIn[iChannel] + pBinauralNode->inputFrameCount, frameSize - pBinauralNode->inputFrameCount, ma_format_f32, 1);
    }
    pBinauralNode->inputFrameCount = frameSize;
    ma_steamaudio_binaural_node_process_block(pBinauralNode);
}

static void ma_steamaudio_binaural_node_process_pcm_frames(ma_node* pNode, const float** ppFramesIn, ma_uint32* pFrameCountIn, float** ppFramesOut, ma_uint32* pFrameCountOut)
{
    ma_steamaudio_binaural_node* pBinauralNode = (ma_steamaudio_binaural_node*)pNode;
    const float* pFramesIn = (ppFramesIn != NULL) ? ppFramesIn[0] : NULL;
    float* pFramesOut = (ppFramesOut != NULL) ? ppFramesOut[0] : NULL;
    ma_uint32 inputFrameCount = (pFramesIn != NULL && pFrameCountIn != NULL) ? *pFrameCountIn : 0;
    ma_uint32 outputFrameCapacity = *pFrameCountOut;
    ma_uint32 inputFramesConsumed = 0;
    ma_uint32 outputFramesProduced = 0;
    ma_uint32 frameSize = (ma_uint32)pBinauralNode->iplAudioSettings.frameSize;
    ma_bool32 inputEnded = ma_atomic_load_32(&pBinauralNode->inputEnded);

    while (outputFramesProduced < outputFrameCapacity) {
        ma_uint32 framesThisIteration = outputFrameCapacity - outputFramesProduced;

        if (pBinauralNode->inputFrameCount == frameSize && pBinauralNode->outputFrameCount == 0) {
            ma_steamaudio_binaural_node_process_block(pBinauralNode);
        }

        if (pFramesIn == NULL && inputEnded && pBinauralNode->outputFrameCount == 0) {
            if (pBinauralNode->inputFrameCount > 0) {
                ma_steamaudio_binaural_node_flush_partial_input(pBinauralNode);
                continue;
            }
            if (!pBinauralNode->tailComplete) {
                ma_steamaudio_binaural_node_process_tail_block(pBinauralNode);
                continue;
            }

            if (pFramesOut != NULL) {
                ma_silence_pcm_frames(ma_offset_pcm_frames_ptr_f32(pFramesOut, outputFramesProduced, 2), outputFrameCapacity - outputFramesProduced, ma_format_f32, 2);
            }
            outputFramesProduced = outputFrameCapacity;
            break;
        }

        if (pBinauralNode->outputFrameCount > 0 && framesThisIteration > pBinauralNode->outputFrameCount) {
            framesThisIteration = pBinauralNode->outputFrameCount;
        }

        if (inputFramesConsumed < inputFrameCount) {
            ma_uint32 inputFramesRemaining = inputFrameCount - inputFramesConsumed;
            ma_uint32 inputFrameSpace = frameSize - pBinauralNode->inputFrameCount;
            if (framesThisIteration > inputFramesRemaining) {
                framesThisIteration = inputFramesRemaining;
            }
            if (framesThisIteration > inputFrameSpace) {
                framesThisIteration = inputFrameSpace;
            }
        } else if (pFramesIn == NULL && inputEnded) {
            /* An explicit EOF drains queued output without extending the input stream. */
        } else {
            ma_uint32 inputFrameSpace = frameSize - pBinauralNode->inputFrameCount;
            if (framesThisIteration > inputFrameSpace) {
                framesThisIteration = inputFrameSpace;
            }
        }

        if (framesThisIteration == 0) {
            break;
        }

        if (inputFramesConsumed < inputFrameCount) {
            ma_steamaudio_binaural_node_copy_input(pBinauralNode, pFramesIn, inputFramesConsumed, framesThisIteration);
            inputFramesConsumed += framesThisIteration;
        } else if (!(pFramesIn == NULL && inputEnded)) {
            ma_uint32 iChannel;
            ma_uint32 channelsIn = ma_node_get_input_channels(&pBinauralNode->baseNode, 0);
            for (iChannel = 0; iChannel < channelsIn; iChannel += 1) {
                ma_silence_pcm_frames(pBinauralNode->ppBuffersIn[iChannel] + pBinauralNode->inputFrameCount,
                    framesThisIteration, ma_format_f32, 1);
            }
            pBinauralNode->inputFrameCount += framesThisIteration;
        }

        if (pBinauralNode->outputFrameCount > 0) {
            ma_steamaudio_binaural_node_copy_output(pBinauralNode, pFramesOut, outputFramesProduced, framesThisIteration);
        } else if (pFramesOut != NULL) {
            ma_silence_pcm_frames(ma_offset_pcm_frames_ptr_f32(pFramesOut, outputFramesProduced, 2), framesThisIteration, ma_format_f32, 2);
        }
        outputFramesProduced += framesThisIteration;
    }

    /* Prepare a completed fixed block for the next callback without adding callback-time latency. */
    if (pBinauralNode->inputFrameCount == frameSize && pBinauralNode->outputFrameCount == 0) {
        ma_steamaudio_binaural_node_process_block(pBinauralNode);
    }

    if (pFrameCountIn != NULL) {
        *pFrameCountIn = inputFramesConsumed;
    }
    *pFrameCountOut = outputFramesProduced;
}

static ma_node_vtable g_ma_steamaudio_binaural_node_vtable =
{
    ma_steamaudio_binaural_node_process_pcm_frames,
    NULL,
    1,  /* 1 input channel. */
    1,  /* 1 output channel. */
    MA_NODE_FLAG_CONTINUOUS_PROCESSING | MA_NODE_FLAG_ALLOW_NULL_INPUT
};

MA_API ma_result ma_steamaudio_binaural_node_init(ma_node_graph* pNodeGraph, const ma_steamaudio_binaural_node_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_steamaudio_binaural_node* pBinauralNode)
{
    ma_result result;
    ma_node_config baseConfig;
    ma_uint32 channelsIn;
    ma_uint32 channelsOut;
    IPLBinauralEffectSettings iplBinauralEffectSettings;
    size_t heapSizeInBytes;
    size_t frameSize;
    ma_vec3f initialDirection;

    if (pBinauralNode == NULL) {
        return MA_INVALID_ARGS;
    }

    MA_ZERO_OBJECT(pBinauralNode);

    if (pConfig == NULL || pConfig->iplAudioSettings.frameSize <= 0 || pConfig->iplAudioSettings.samplingRate <= 0 || pConfig->iplContext == NULL || pConfig->iplHRTF == NULL) {
        return MA_INVALID_ARGS;
    }

    /* Steam Audio only supports mono and stereo input. */
    if (pConfig->channelsIn < 1 || pConfig->channelsIn > 2) {
        return MA_INVALID_ARGS;
    }

    channelsIn  = pConfig->channelsIn;
    channelsOut = 2;    /* Always stereo output. */

    baseConfig = pConfig->nodeConfig;
    baseConfig.vtable          = &g_ma_steamaudio_binaural_node_vtable;
    baseConfig.pInputChannels  = &channelsIn;
    baseConfig.pOutputChannels = &channelsOut;
    result = ma_node_init(pNodeGraph, &baseConfig, pAllocationCallbacks, &pBinauralNode->baseNode);
    if (result != MA_SUCCESS) {
        return result;
    }

    pBinauralNode->iplAudioSettings = pConfig->iplAudioSettings;
    pBinauralNode->iplContext       = iplContextRetain(pConfig->iplContext);
    pBinauralNode->iplHRTF          = iplHRTFRetain(pConfig->iplHRTF);
    if (pBinauralNode->iplContext == NULL || pBinauralNode->iplHRTF == NULL) {
        iplHRTFRelease(&pBinauralNode->iplHRTF);
        iplContextRelease(&pBinauralNode->iplContext);
        ma_node_uninit(&pBinauralNode->baseNode, pAllocationCallbacks);
        return MA_ERROR;
    }

    initialDirection.x = 0;
    initialDirection.y = 0;
    initialDirection.z = -1;
    ma_steamaudio_direction_state_init(&pBinauralNode->directionState, initialDirection);
    pBinauralNode->tailComplete = MA_TRUE;

    MA_ZERO_OBJECT(&iplBinauralEffectSettings);
    iplBinauralEffectSettings.hrtf = pBinauralNode->iplHRTF;

    result = ma_result_from_IPLerror(iplBinauralEffectCreate(pBinauralNode->iplContext, &pBinauralNode->iplAudioSettings, &iplBinauralEffectSettings, &pBinauralNode->iplEffect));
    if (result != MA_SUCCESS) {
        iplHRTFRelease(&pBinauralNode->iplHRTF);
        iplContextRelease(&pBinauralNode->iplContext);
        ma_node_uninit(&pBinauralNode->baseNode, pAllocationCallbacks);
        return result;
    }

    frameSize = (size_t)pBinauralNode->iplAudioSettings.frameSize;
    if (frameSize > MA_SIZE_MAX / sizeof(float) / (channelsIn + channelsOut)) {
        iplBinauralEffectRelease(&pBinauralNode->iplEffect);
        iplHRTFRelease(&pBinauralNode->iplHRTF);
        iplContextRelease(&pBinauralNode->iplContext);
        ma_node_uninit(&pBinauralNode->baseNode, pAllocationCallbacks);
        return MA_TOO_BIG;
    }
    heapSizeInBytes = sizeof(float) * frameSize * (channelsIn + channelsOut);

    /*
    Unfortunately Steam Audio uses deinterleaved buffers for everything so we'll need to use some
    intermediary buffers. We'll allocate one big buffer on the heap and then use offsets. We'll
    use the frame size from the IPLAudioSettings structure as a basis for the size of the buffer.
    */
    pBinauralNode->_pHeap = ma_malloc(heapSizeInBytes, pAllocationCallbacks);
    if (pBinauralNode->_pHeap == NULL) {
        iplBinauralEffectRelease(&pBinauralNode->iplEffect);
        iplHRTFRelease(&pBinauralNode->iplHRTF);
        iplContextRelease(&pBinauralNode->iplContext);
        ma_node_uninit(&pBinauralNode->baseNode, pAllocationCallbacks);
        return MA_OUT_OF_MEMORY;
    }

    pBinauralNode->ppBuffersOut[0] = (float*)pBinauralNode->_pHeap;
    pBinauralNode->ppBuffersOut[1] = (float*)ma_offset_ptr(pBinauralNode->_pHeap, sizeof(float) * pBinauralNode->iplAudioSettings.frameSize);

    {
        ma_uint32 iChannelIn;
        for (iChannelIn = 0; iChannelIn < channelsIn; iChannelIn += 1) {
            pBinauralNode->ppBuffersIn[iChannelIn] = (float*)ma_offset_ptr(pBinauralNode->_pHeap, sizeof(float) * pBinauralNode->iplAudioSettings.frameSize * (channelsOut + iChannelIn));
        }
    }

    return MA_SUCCESS;
}

MA_API void ma_steamaudio_binaural_node_uninit(ma_steamaudio_binaural_node* pBinauralNode, const ma_allocation_callbacks* pAllocationCallbacks)
{
    if (pBinauralNode == NULL) {
        return;
    }

    /* The base node is always uninitialized first. */
    ma_node_uninit(&pBinauralNode->baseNode, pAllocationCallbacks);

    /*
    The Steam Audio objects are deleted after the base node. This ensures the base node is removed from the graph
    first to ensure these objects aren't getting used by the audio thread.
    */
    iplBinauralEffectRelease(&pBinauralNode->iplEffect);
    iplHRTFRelease(&pBinauralNode->iplHRTF);
    iplContextRelease(&pBinauralNode->iplContext);
    ma_free(pBinauralNode->_pHeap, pAllocationCallbacks);
}

MA_API ma_result ma_steamaudio_binaural_node_set_direction(ma_steamaudio_binaural_node* pBinauralNode, float x, float y, float z)
{
    if (pBinauralNode == NULL) {
        return MA_INVALID_ARGS;
    }

    {
        ma_vec3f direction;
        direction.x = x;
        direction.y = y;
        direction.z = z;
        ma_steamaudio_direction_state_publish(&pBinauralNode->directionState, direction);
    }

    return MA_SUCCESS;
}

MA_API ma_result ma_steamaudio_binaural_node_end_input(ma_steamaudio_binaural_node* pBinauralNode)
{
    if (pBinauralNode == NULL) {
        return MA_INVALID_ARGS;
    }

    ma_atomic_store_32(&pBinauralNode->inputEnded, 1);
    return MA_SUCCESS;
}

MA_API ma_result ma_steamaudio_binaural_node_reset(ma_steamaudio_binaural_node* pBinauralNode)
{
    if (pBinauralNode == NULL) {
        return MA_INVALID_ARGS;
    }
    if (ma_node_output_bus_is_attached(&pBinauralNode->baseNode.pOutputBuses[0])) {
        return MA_INVALID_OPERATION;
    }

    iplBinauralEffectReset(pBinauralNode->iplEffect);
    pBinauralNode->inputFrameCount = 0;
    pBinauralNode->outputFrameOffset = 0;
    pBinauralNode->outputFrameCount = 0;
    pBinauralNode->tailComplete = MA_TRUE;
    ma_atomic_store_32(&pBinauralNode->inputEnded, 0);
    return MA_SUCCESS;
}



#ifndef MA_STEAMAUDIO_NO_MAIN
static ma_engine g_engine;
static ma_sound g_sound;            /* This example will play only a single sound at once, so we only need one `ma_sound` object. */
static ma_steamaudio_binaural_node g_binauralNode;   /* The echo effect is achieved using a delay node. */

int main(int argc, char** argv)
{
    ma_result result;
    ma_engine_config engineConfig;
    IPLAudioSettings iplAudioSettings;
    IPLContextSettings iplContextSettings;
    IPLContext iplContext;
    IPLHRTFSettings iplHRTFSettings;
    IPLHRTF iplHRTF;

    if (argc < 2) {
        printf("No input file.");
        return -1;
    }

    /* The engine needs to be initialized first. */
    engineConfig = ma_engine_config_init();
    engineConfig.channels   = CHANNELS;
    engineConfig.sampleRate = SAMPLE_RATE;

    /*
    Steam Audio requires processing in fixed sized chunks. Matching the engine period size avoids extra
    staging delay in this example, but the node also supports callbacks that are shorter or longer.
    */
    engineConfig.periodSizeInFrames = 256;

    result = ma_engine_init(&engineConfig, &g_engine);
    if (result != MA_SUCCESS) {
        printf("Failed to initialize audio engine.");
        return -1;
    }

    /*
    Now that we have the engine we can initialize the Steam Audio objects.
    */
    MA_ZERO_OBJECT(&iplAudioSettings);
    iplAudioSettings.samplingRate = ma_engine_get_sample_rate(&g_engine);

    /*
    If there's any Steam Audio developers reading this, why is the frame size needed? This needs to
    be documented. If this is for some kind of buffer management with FFT or something, then this
    need not be exposed to the public API. There should be no need for the public API to require a
    fixed sized update.

    This example uses the engine period size to keep the relationship easy to understand. The node stages
    arbitrary miniaudio callback sizes into this fixed Steam Audio frame size.
    */
    iplAudioSettings.frameSize = engineConfig.periodSizeInFrames;


    /* IPLContext */
    MA_ZERO_OBJECT(&iplContextSettings);
    iplContextSettings.version = STEAMAUDIO_VERSION;
    
    result = ma_result_from_IPLerror(iplContextCreate(&iplContextSettings, &iplContext));
    if (result != MA_SUCCESS) {
        ma_engine_uninit(&g_engine);
        return result;
    }


    /* IPLHRTF */
    MA_ZERO_OBJECT(&iplHRTFSettings);
    iplHRTFSettings.type   = IPL_HRTFTYPE_DEFAULT;
    iplHRTFSettings.volume = 1;

    result = ma_result_from_IPLerror(iplHRTFCreate(iplContext, &iplAudioSettings, &iplHRTFSettings, &iplHRTF));
    if (result != MA_SUCCESS) {
        iplContextRelease(&iplContext);
        ma_engine_uninit(&g_engine);
        return result;
    }


    /*
    The binaural node will need to know the input channel count of the sound so we'll need to load
    the sound first. We'll initialize this such that it'll be initially detached from the graph.
    It will be attached to the graph after the binaural node is initialized.
    */
    {
        ma_sound_config soundConfig;

        soundConfig = ma_sound_config_init();
        soundConfig.pFilePath   = argv[1];
        soundConfig.flags       = MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT;  /* We'll attach this to the graph later. */

        result = ma_sound_init_ex(&g_engine, &soundConfig, &g_sound);
        if (result != MA_SUCCESS) {
            return result;
        }

        /* We'll let the Steam Audio binaural effect do the directional attenuation for us. */
        ma_sound_set_directional_attenuation_factor(&g_sound, 0);

        /* Loop the sound so we can get a continuous sound. */
        ma_sound_set_looping(&g_sound, MA_TRUE);
    }


    /*
    We'll build our graph starting from the end so initialize the binaural node now. The output of
    this node will be connected straight to the output. You could also attach it to a sound group
    or any other node that accepts an input.

    Creating a node requires a pointer to the node graph that owns it. The engine itself is a node
    graph. In the code below we can get a pointer to the node graph with `ma_engine_get_node_graph()`
    or we could simple cast the engine to a ma_node_graph* like so:
    
        (ma_node_graph*)&g_engine

    The endpoint of the graph can be retrieved with `ma_engine_get_endpoint()`.
    */
    {
        ma_steamaudio_binaural_node_config binauralNodeConfig;

        /*
        For this example we're just using the engine's channel count, but a more optimal solution
        might be to set this to mono if the source data is also mono.
        */
        binauralNodeConfig = ma_steamaudio_binaural_node_config_init(CHANNELS, iplAudioSettings, iplContext, iplHRTF);

        result = ma_steamaudio_binaural_node_init(ma_engine_get_node_graph(&g_engine), &binauralNodeConfig, NULL, &g_binauralNode);
        if (result != MA_SUCCESS) {
            printf("Failed to initialize binaural node.");
            return -1;
        }

        /* Connect the output of the delay node to the input of the endpoint. */
        ma_node_attach_output_bus(&g_binauralNode, 0, ma_engine_get_endpoint(&g_engine), 0);
    }


    /* We can now wire up the sound to the binaural node and start it. */
    ma_node_attach_output_bus(&g_sound, 0, &g_binauralNode, 0);
    ma_sound_start(&g_sound);

#if 1
    {
        /*
        We'll move the sound around the listener which we'll leave at the origin. We'll then get
        the direction to the listener and update the binaural node appropriately.
        */
        float stepAngle = 0.002f;
        float angle = 0;
        float distance = 2;

        for (;;) {
            double x = ma_cosd(angle) - ma_sind(angle);
            double y = ma_sind(angle) + ma_cosd(angle);
            ma_vec3f direction;

            ma_sound_set_position(&g_sound, (float)x * distance, 0, (float)y * distance);
            direction = ma_sound_get_direction_to_listener(&g_sound);

            /* Update the direction of the sound. */
            ma_steamaudio_binaural_node_set_direction(&g_binauralNode, direction.x, direction.y, direction.z);
            angle += stepAngle;

            ma_sleep(1);
        }
    }
#else
    printf("Press Enter to quit...");
    getchar();
#endif

    ma_sound_uninit(&g_sound);
    ma_steamaudio_binaural_node_uninit(&g_binauralNode, NULL);
    ma_engine_uninit(&g_engine);

    return 0;
}
#endif
