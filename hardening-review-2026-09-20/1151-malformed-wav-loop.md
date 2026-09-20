# Issue #1151: malformed WAV can loop during decoder initialization

## Assessment

- **Priority:** P0 availability/correctness
- **Exact checkout:** affected
- **Local reproduction:** confirmed
- **Platform:** backend-independent
- **Public path:** `ma_decode_memory()` and `ma_decoder_init_memory()`

A crafted 69-byte WAV prevents the public memory decoder from returning. The
process was killed after a three-second watchdog. A bounded diagnostic callback
recorded a repeating seek cycle:

```text
position 38, seek +39 -> clamped to 69
position 69, seek -58 -> 11
position 11, seek +1  -> 12
... parse reaches 38 and repeats
```

The exact fixture does **not** hang every entry point: direct dr_wav memory
initialization rejects it, and public file decoding rejects it promptly. The
failure occurs when decoder fallback reaches the generic memory callback,
whose clamped seeks allow the malformed parser state to revisit prior bytes.

Evidence:

- `/tmp/miniaudio-audit-20260920/repro-1151.log`
- `/tmp/miniaudio-audit-20260920/probe-1151-bounded.log`
- `/tmp/miniaudio-audit-20260920/parser-probe-results.json`

## Suspect code

The WAV parser reads a 64-bit chunk length but narrows a derived remainder to
the signed `int` accepted by its seek callback at `miniaudio.h:81411`:

```c
if (pWav->onSeek(
        pWav->pUserData,
        (int)(header.sizeInBytes - bytesReadSoFar),
        MA_DR_WAV_SEEK_CUR) == MA_FALSE) {
    return MA_FALSE;
}
cursor += (header.sizeInBytes - bytesReadSoFar);
```

The generic memory seek callback at `miniaudio.h:66412-66452` clamps an
out-of-range request and reports success:

```c
if (pDecoder->data.memory.currentReadPos + byteOffset >
    pDecoder->data.memory.dataSize) {
    byteOffset = (ma_int64)(pDecoder->data.memory.dataSize -
                            pDecoder->data.memory.currentReadPos);
}
pDecoder->data.memory.currentReadPos += (size_t)byteOffset;
return MA_SUCCESS;
```

The parser advances its logical cursor by the requested chunk size while the
callback may move only to the end of the real buffer. A later narrowed seek can
be negative, creating the observed cycle.

## Potential minimal fix

Validate the format chunk before reading or subtracting:

```c
if (header.sizeInBytes < 16) {
    return MA_FALSE;
}

/* After the 16-byte base format and optional 2-byte cbSize. */
if (bytesReadSoFar > header.sizeInBytes) {
    return MA_FALSE;
}

leftover = header.sizeInBytes - bytesReadSoFar;
if (leftover > INT_MAX) {
    return MA_FALSE;
}

if (leftover != 0 &&
    !pWav->onSeek(pWav->pUserData, (int)leftover,
                  MA_DR_WAV_SEEK_CUR)) {
    return MA_FALSE;
}
cursor += leftover;
```

Before this sketch becomes a patch, the extension parser also needs to prove
that `cbSize`, extensible fields, and every intermediate addition remain within
the chunk. The fix should reject an impossible `fmt` description rather than
depend on callback clamping or a global iteration watchdog.

A generic memory seek outside `[0, dataSize]` should also be reviewed. Changing
it to fail could affect callers that currently rely on clamping, so the parser
must be made correct independently and compatibility tests should precede any
callback semantic change.

## Required regression tests

1. Run the original 69-byte fixture through `ma_decode_memory`,
   `ma_decoder_init_memory`, file initialization, direct dr_wav memory, and a
   custom callback decoder. Every path must return success or a documented
   error within a bounded number of reads/seeks.
2. Exercise `fmt` sizes 0 through 15, 16, 17, and extension sizes that end
   exactly at or beyond the chunk boundary.
3. Cover remainders at `INT_MAX`, `INT_MAX + 1`, and `UINT32_MAX`.
4. Include valid PCM, ADPCM, RIFX, RF64, W64, and large unknown chunks so the
   hardening does not reject legitimate files.
5. Verify allocation and decoder cleanup on every error path under ASan/UBSan.

The permanent test must not pass merely because a timeout kills the process.
An exhausted operation budget is a test failure.

## Compatibility and integration impact

No public API or object-layout change is expected. Malformed files previously
accepted or stalled may now return `MA_INVALID_FILE`. The issue can block a
resource-manager worker or application thread before Steam Audio sees PCM, so
Steam Audio does not mitigate it.

The upstream issue remains open. Development-branch guards are useful
references but are not present in this fork and have not been imported or
validated as a unit.
