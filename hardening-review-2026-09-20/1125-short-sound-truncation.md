# Issue #1125: short sounds are truncated at the end

## Assessment

- **Priority:** P1 audio correctness
- **Exact checkout:** affected
- **Local reproduction:** confirmed with deterministic offline rendering
- **Platform:** backend-independent

A generated 3,036-frame s16 mono sound played through a device-less engine
produces only 2,560 nonzero output frames; the final 476 source frames are
lost. No hardware or auditory judgment is involved in this result.

Evidence is in [probe-dsp.log](evidence/audit/probe-dsp.log) and
[probe-dsp-scalar.log](evidence/audit/probe-dsp-scalar.log).

## Suspect code

The converted-data refill loop at `miniaudio.h:76985-77006` can accumulate
valid frames, then receive `MA_AT_END` on a later read:

```c
while (totalFramesConverted < pSound->processingCacheCap) {
    result = ma_data_source_read_pcm_frames(
        pSound->pDataSource, temp, framesToConvertThisIteration,
        &framesConverted);
    if (result != MA_SUCCESS) {
        break;
    }

    ma_convert_pcm_frames_format(..., framesConverted, ...);
    totalFramesConverted += framesConverted;
}

framesJustRead = totalFramesConverted;
pSound->processingCacheFramesRemaining = (ma_uint32)framesJustRead;

if (result == MA_AT_END) {
    ma_sound_set_at_end(pSound, MA_TRUE);
}
if (result != MA_SUCCESS || ma_sound_at_end(pSound)) {
    break;
}
```

The code stores valid converted frames in the processing cache but marks the
sound ended and leaves before draining them.

## Potential minimal fix

Normalize aggregate status when this refill produced frames, then declare end
only after the cache is drained:

```c
framesJustRead = totalFramesConverted;

if (result == MA_AT_END && framesJustRead > 0) {
    result = MA_SUCCESS;
    reachedEndAfterCache = MA_TRUE;
}

pSound->processingCacheFramesRemaining = (ma_uint32)framesJustRead;

/* Process cached frames. When remaining reaches zero: */
if (reachedEndAfterCache) {
    ma_sound_set_at_end(pSound, MA_TRUE);
}
```

The actual design may not need a new field if the lower-level data-source
contract guarantees `MA_AT_END` only with zero newly returned frames and a
subsequent refill naturally observes it. Tests must establish the contract for
all built-in and custom decoders. The key invariant is that every successfully
read/converted frame is consumed exactly once before end state or end callback.

## Required regression tests

1. s16 and f32 sources shorter than, equal to, and longer than processing cache
   and requested output sizes.
2. Lengths one frame below/at/above each boundary, including 0 and 1 frame.
3. One large render call versus many small calls; exact final sample identity.
4. Looping sources, stop/start, and seek near EOF.
5. End notification fires once and only after all cached/tail frames are
   output.
6. Other format conversions and resampling tails where enabled.
7. Custom sources returning full data then EOF, short success reads, and the
   exact documented partial-data status combinations.

Assertions should compare frame counts and samples, not merely whether a sound
was audible.

## Compatibility and real-time impact

No API/ABI change is expected. Output becomes complete. The correction must not
allocate, lock, or call user lifecycle functions in the mixer. Sound-end
callbacks remain audio-thread callbacks and must not restart/uninitialize a
sound or resume a coroutine directly.

The issue was closed after a development-branch fix, but the release checkout
still reproduces the loss.
