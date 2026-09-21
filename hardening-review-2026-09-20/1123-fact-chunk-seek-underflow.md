# Issue #1123: RIFF fact-chunk subtraction underflow

## Assessment

- **Priority:** P0 availability/correctness
- **Exact checkout:** affected
- **Local reproduction:** confirmed
- **Platform:** backend-independent
- **Public path:** file and memory decoder initialization

A 160-byte malformed ADPCM WAV declares a zero-byte `fact` chunk. The parser
reads/subtracts the fixed-size sample count without first checking the chunk
length. Unsigned underflow produces an enormous remainder, which
`ma_dr_wav__seek_forward` splits into repeated `INT_MAX` seeks. Public memory
and file decoder initialization were both killed after three seconds.

The bounded diagnostic trace records repeated requests at EOF:

```text
position 52, seek +2147483647 -> EOF 160
position 160, seek +2147483647 -> EOF 160
position 160, seek +2147483647 -> EOF 160
...
```

This is a practically excessive finite loop, not the repeating parse-state
cycle seen in #1151.

Evidence:

- `/tmp/miniaudio-audit-20260920/probe-1123-bounded.log`
- `/tmp/miniaudio-audit-20260920/parser-probe-results.json`

## Suspect code

At `miniaudio.h:81442-81463`, the RIFF/RIFX path reads four bytes and then
subtracts four; the W64 path does the same with eight bytes:

```c
if (pWav->container == ma_dr_wav_container_riff ||
    pWav->container == ma_dr_wav_container_rifx) {
    ma_uint8 sampleCount[4];
    if (ma_dr_wav__on_read(..., sampleCount, 4, &cursor) != 4) {
        return MA_FALSE;
    }
    chunkSize -= 4;
} else if (pWav->container == ma_dr_wav_container_w64) {
    if (ma_dr_wav__on_read(..., &sampleCountFromFactChunk, 8,
                           &cursor) != 8) {
        return MA_FALSE;
    }
    chunkSize -= 8;
}

ma_dr_wav__seek_forward(pWav->onSeek, chunkSize, pWav->pUserData);
```

## Potential minimal fix

Validate before both the read and subtraction:

```c
fixedSize = isW64 ? 8 : 4;
if (chunkSize < fixedSize) {
    return MA_FALSE;
}

if (ma_dr_wav__on_read(..., fixedSize, &cursor) != fixedSize) {
    return MA_FALSE;
}
chunkSize -= fixedSize;
```

Apply the same pattern to every fixed-header subtraction in the parser rather
than fixing only the reported line. During review, this includes W64 chunk
headers whose total size is reduced by 24, AIFF COMM/SSND fixed fields, padding
addition, and metadata `declared - bytesRead` calculations. Each check belongs
before pointer movement, read, allocation, or seek.

Known-length streams should reject chunks extending beyond the input. Generic
callbacks must still support legitimate streaming sources, so failure must be
based on structural impossibility or a callback error, not a small arbitrary
file-size cap.

## Required regression tests

1. Original fixture through public memory, file, direct dr_wav, and custom
   callbacks.
2. RIFF/RIFX fact sizes 0 through 4 and W64 sizes 0 through 8.
3. Exact fixed-size fact chunks and valid chunks with trailing extension bytes.
4. Failed/short read and failed seek behavior with bounded callback counts.
5. W64 total chunk sizes below 24; AIFF SSND/COMM sizes below required headers;
   padding and cursor overflow boundaries.
6. Valid ADPCM fact sample counts and valid large chunks.

Every malformed case must return an error or other bounded documented failure.
A watchdog may detect a regression, but timeout is a failed test.

## Compatibility and upstream state

No public API or layout change is expected. Only structurally invalid files
should change behavior. The issue was closed after a development-branch fix,
but the unchecked subtraction remains in this release checkout. Steam Audio
does not affect the parser and cannot mitigate a decoder-initialization stall.

## Implemented result

The fixed parser now rejects RIFF/RIFX fact payloads shorter than four bytes,
W64 fact payloads shorter than eight bytes, W64 total chunk sizes below its
24-byte header, and short AIFF COMM/SSND headers before reading or subtracting.
`fee518b` completes the requested independent matrix: RIFX fact sizes 0-4,
W64 sizes 0-8, valid trailing extensions (including a non-aligned W64 child),
W64 totals 0-23, and AIFF fixed-header boundaries.

The independent W64 fixture exposed a separate alignment defect. Wave64 pads
to the *next* 8-byte boundary, so the correct calculation is:

```c
padding = (8 - (chunkSize % 8)) % 8;
```

The former `chunkSize % 8` happened to agree only for already aligned and
half-aligned sizes. Correcting it makes both reading and writing conform to the
container alignment rule. The full strict, sanitizer, optional-codec, Steam
Audio, and exact MSVC 15/15 matrices pass after the change.
