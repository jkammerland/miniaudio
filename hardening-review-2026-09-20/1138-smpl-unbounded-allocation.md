# Issue #1138: attacker-controlled SMPL metadata allocation

## Assessment

- **Priority:** P0 resource exhaustion/correctness
- **Exact checkout:** affected
- **Local reproduction:** confirmed with a capped allocator
- **Platform:** backend-independent
- **Exposure:** dr_wav metadata-enabled initialization

An 88-byte WAV causes this checkout to request an allocation of
**2,147,483,766 bytes**. The reproducer's allocator records and refuses the
request, so no multi-gigabyte allocation was performed. The reproducer exiting
normally means the dangerous request was intercepted; it is not a passing
library result.

Evidence: [repro-1138.log](evidence/audit/repro-1138.log).

## Suspect code

Both metadata passes infer the loop count from a declared chunk size and then
request storage using fields controlled by the file. The materialization path
around `miniaudio.h:80938-80969` includes logic equivalent to:

```c
loopCount = ma_dr_wav_bytes_to_u32(smplHeaderData + 28);
calculatedLoopCount =
    (pChunkHeader->sizeInBytes - MA_DR_WAV_SMPL_BYTES) /
    MA_DR_WAV_SMPL_LOOP_BYTES;

if (loopCount == calculatedLoopCount) {
    pMetadata->data.smpl.pLoops =
        ma_dr_wav__metadata_get_memory(
            pParser,
            sizeof(ma_dr_wav_smpl_loop) * loopCount,
            MA_DR_WAV_METADATA_ALIGNMENT);
    pMetadata->data.smpl.pSamplerSpecificData =
        ma_dr_wav__metadata_get_memory(
            pParser, samplerSpecificDataSizeInBytes, 1);
}
```

Problems include unchecked subtraction when the chunk is shorter than the
36-byte SMPL header, unchecked loop-count multiplication, and no proof that
the sampler-specific payload fits after the loop records. The equality check
also treats all trailing bytes as loop records, so it mishandles legitimate
SMPL chunks with sampler-specific data.

## Potential minimal fix

Use one validator in both metadata passes:

```c
if (chunkSize < SMPL_HEADER_BYTES) {
    return MA_FALSE;
}

remaining = chunkSize - SMPL_HEADER_BYTES;

if (!checked_mul_u64(loopCount, SMPL_LOOP_BYTES, &loopBytes) ||
    loopBytes > remaining) {
    return MA_FALSE;
}
remaining -= loopBytes;

if (samplerSpecificBytes > remaining) {
    return MA_FALSE;
}

if (!checked_mul_size(loopCount, sizeof(ma_dr_wav_smpl_loop),
                      &loopAllocation)) {
    return MA_FALSE;
}
```

The count pass should reserve exactly the validated object, loop-array, and
sampler payload sizes using checked addition and alignment. The read pass must
re-run the same validation before consuming bytes. If stream length is known,
also ensure the chunk lies within the remaining stream; a declared internally
consistent multi-gigabyte chunk in an 88-byte input must fail before allocation.

Do not substitute an arbitrary small global metadata cap for structural
validation. Applications may intentionally process large legitimate metadata;
policy caps belong in an opt-in resource policy if later required.

## Required regression tests

1. The original 88-byte reproducer with allocation telemetry asserting that no
   oversized request is made.
2. Chunk shorter than 36 bytes; loop count multiplication overflow; loop bytes
   one byte beyond the chunk; sampler bytes one byte beyond the remainder.
3. Zero, one, and multiple valid loops with zero and nonzero sampler-specific
   data.
4. A valid chunk whose trailing sampler bytes mean
   `(chunkSize - 36) / 24 != loopCount`, proving the old heuristic is removed.
5. Short reads between every loop record and in sampler data.
6. Metadata count/read pass consistency, allocation failure, alignment
   overflow, and 32-bit `size_t` builds.

## Compatibility and upstream state

No public ABI change is expected. Malformed files that previously caused a
huge request will fail metadata initialization. Valid SMPL files with sampler
payloads may become more compatible because they no longer need to satisfy the
incorrect equality heuristic.

The upstream issue was closed as a duplicate of a dr_libs report. That closure
did not update this fork; the allocation request is reproducible here. Default
engine decoding does not request this metadata, but metadata importers and
asset tools remain exposed before Steam Audio processing begins.
