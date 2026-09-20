# Issue #1117: MP3 frame-count multiplication overflows before widening

## Assessment

- **Priority:** P1 correctness and allocation-safety input
- **Exact checkout:** suspect arithmetic is present
- **Local reproduction:** original fixture not freshly reproduced in phase one
- **Platform:** backend-independent

The Xing/Info metadata parser stores the detected MP3 frame count in a 32-bit
value. At `miniaudio.h:94974`, the multiplication occurs in the operands'
32-bit type before assignment to the 64-bit total:

```c
if (detectedMP3FrameCount != 0xFFFFFFFF) {
    pMP3->totalPCMFrameCount =
        detectedMP3FrameCount * firstFramePCMFrameCount;
}
```

A malformed count can therefore wrap and report an incorrect length. Length
consumers may under-allocate, over-allocate, seek incorrectly, or make bad
playback decisions. This investigation has not demonstrated downstream heap
corruption from this issue by itself.

The nearby metadata parser also has the confirmed #1128 out-of-bounds reads.
Bounds must be fixed first or together so a length-only regression does not
exercise undefined memory access.

## Potential minimal fix

After validating the Xing field bounds, widen before multiplication:

```c
ma_uint64 frameCount = (ma_uint64)detectedMP3FrameCount;
ma_uint64 samplesPerFrame = (ma_uint64)firstFramePCMFrameCount;

if (frameCount > UINT64_MAX / samplesPerFrame) {
    return MA_INVALID_FILE;
}

pMP3->totalPCMFrameCount = frameCount * samplesPerFrame;
```

For 32-bit-by-32-bit inputs the 64-bit product itself will fit, but the explicit
check documents the invariant and protects future type changes. More
importantly, treat the metadata count as an untrusted hint: validate it against
file/byte counts where meaningful and do not use it as sole authority for a
large allocation.

## Required reproduction and tests

1. Re-run the original `bad-length.mp3` attachment under ASan/UBSan before
   changing source and capture the incorrect old length.
2. Counts 0, 1, maximum valid, values that overflow 32-bit multiplication, and
   `0xFFFFFFFF` sentinel behavior.
3. MPEG versions/layers with different samples per frame.
4. Valid CBR/VBR/Info/Xing files and files whose declared count disagrees with
   bytes actually available.
5. Decoder length query, seek near reported end, full decode, and helper APIs
   that allocate from reported length.
6. Memory, file, and custom callback routes after all #1128 field bounds are in
   place.

## Compatibility and upstream state

No ABI change is expected. Correct 64-bit length values may differ from the
wrapped values returned by this release. The issue was closed after an upstream
dr_mp3/development change, but this checkout still has the pre-widening
multiplication. It should be marked source-confirmed and reproduction-pending
until the original fixture is rerun.
