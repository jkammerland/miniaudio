# Issue #1153: spatialized playback pops while rotating the listener

## Assessment

- **Priority:** P1 audio correctness
- **Exact checkout:** affected by independently reproducible gain-ramp defects
- **Original audible reproduction:** not run on Windows hardware
- **Platform:** gain bugs are portable; report was Windows/MSVC
- **Steam Audio:** original report uses miniaudio spatialization, not Steam Audio

The attached report rotates the listener at roughly 60 Hz and hears pops or
glitches. Disabling pitch and Doppler did not eliminate them; longer smoothing
helped somewhat. A fresh offline probe found multiple deterministic defects in
the gain smoother used by `ma_spatializer_process_pcm_frames` at
`miniaudio.h:52622-52623`.

For an eight-frame fade from 1 to 0, the independent expected first-channel
sequence is:

```text
1.000, 0.875, 0.750, 0.625, 0.500, 0.375, 0.250, 0.125, 0, 0, ...
```

Observed stereo SSE2 output for a seven-frame block is:

```text
1.000, 0.875, 0.875, 0.750, 0.750, 0.625, 1.000
```

Observed scalar output continues below zero after the eighth frame:

```text
..., 0.125, 0.000, -0.125, -0.250, -0.375
```

Evidence:

- `/tmp/miniaudio-audit-20260920/probe-dsp.c`
- `/tmp/miniaudio-audit-20260920/probe-dsp.log`
- `/tmp/miniaudio-audit-20260920/probe-dsp-scalar.log`

These defects are plausible contributors to #1153. They do not prove that all
reported Windows pops have one cause.

## Suspect code

### Reversed remaining-frame subtraction

At `miniaudio.h:50711`:

```c
if (pGainer->t >= pGainer->config.smoothTimeInFrames) {
    interpolatedFrameCount = 0;
} else {
    interpolatedFrameCount =
        pGainer->t - pGainer->config.smoothTimeInFrames;
    if (interpolatedFrameCount > frameCount) {
        interpolatedFrameCount = frameCount;
    }
}
```

The unsigned subtraction is reversed. It underflows, then is clamped to the
entire callback, so interpolation continues beyond its intended endpoint.

### Stereo and six-channel unrolled progression

The stereo SSE path processes two frames per vector but adds only one frame's
gain delta to all lanes per loop at `miniaudio.h:50755-50761`:

```c
__m128 runningGainDelta0 = _mm_set_ps(d1, d0, d1, d0);
__m128 runningGain0 = _mm_set_ps(g1+d1, g0+d0, g1, g0);

for (...) {
    output4 = input4 * runningGain0;
    runningGain0 = _mm_add_ps(runningGain0, runningGainDelta0);
}
```

The next vector should begin two frames later. The six-channel path has the
same two-frame unroll problem. After the vector loop, scalar `pRunningGain[]`
has not advanced, so an odd tail restarts at the initial gain.

### Channel-blind pointer offset

At `miniaudio.h:50902-50903`:

```c
pFramesOut = ma_offset_ptr(pFramesOut,
                           interpolatedFrameCount * sizeof(float));
pFramesIn  = ma_offset_ptr(pFramesIn,
                           interpolatedFrameCount * sizeof(float));
```

The offset omits `channels`. Once a callback correctly contains both the end of
the ramp and the constant-gain region, multichannel fast-path output can begin
at the wrong sample.

## Potential minimal fix

First make the scalar reference correct:

```c
remaining = smoothTime - t;
interpolatedFrameCount = ma_min(frameCount, remaining);

for (frame = 0; frame < interpolatedFrameCount; ++frame) {
    gain = old + (new - old) * ((t + frame) / (float)smoothTime);
    apply_frame(gain);
}

t = ma_min(t + interpolatedFrameCount, smoothTime);
offsetSamples = interpolatedFrameCount * channels;
```

Then make optimized paths exactly equivalent:

- build per-lane gains for frame `n` and `n+1`;
- add `2 * delta` after each two-frame vector;
- either extract the advanced vector state or calculate the odd-tail gain from
  the absolute frame index;
- use frame indexes, not loop-vector indexes, for all scalar handoff;
- multiply buffer offsets by the channel count;
- clamp completion at the target and use the new gain thereafter.

Calculating each tail from `t + frame` is less fragile than mutating a separate
stack gain array. Optimize only after tests prove bit/epsilon-equivalence to the
reference for all supported channel paths.

## Required regression tests

1. Smoothing lengths 0, 1, odd, and even; callback blocks shorter, equal, and
   longer than the ramp.
2. One block versus many split blocks must produce the same sequence.
3. Rising/falling ramps and target changes midway through a ramp.
4. Channels 1, 2, 6, 8, and greater than 32.
5. Scalar (`MA_NO_SSE2`) and SSE2 builds; other SIMD paths where supported.
6. In-place/out-of-place processing and null-buffer timer advancement.
7. Guarded buffers proving the constant-gain region begins at
   `frames * channels`.
8. Offline spatializer render while rotating the listener; then native Windows
   listening/measurement using the report's attachment and configuration.

The oracle should compute the documented linear ramp independently. Do not
widen tolerances to accept repeated, overshooting, or discontinuous gains.

## Real-time and Steam Audio implications

The fix uses no callback allocation or blocking. Parameter concurrency still
needs attention: `ma_atomic_vec3f_set/get` at 51562/51571 use spinlocks, so a
preempted writer can delay the mixer. The actual integration should publish
precomputed, bounded snapshots at block boundaries instead of updating
spinlocked listener vectors from arbitrary threads.

The repository's Steam Audio example has a separate race: `direction` is plain
floating-point state written by the main thread at lines 260-262 and read by
the audio callback at 105-107. It also uses nearest HRTF interpolation. Those
issues can produce artifacts but are not part of #1153's reproducer and should
be fixed/tested separately.

## Compatibility and upstream state

No API or ABI change is expected. Corrected sample output intentionally differs
from broken output. The upstream issue remains open and reportedly persists on
a development commit, so branch upgrade is not evidence of resolution.
