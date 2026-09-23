# Issue #1101 / CVE-2026-32837: BEXT coding-history over-read

## Assessment

- **Priority:** P0 memory safety
- **Exact checkout:** affected
- **Local reproduction:** confirmed with ASan
- **Platform:** backend-independent
- **Exposure:** dr_wav metadata-enabled initialization

The reproducer creates a BEXT coding-history field without a NUL byte. ASan
reports a one-byte heap-buffer over-read in `ma_dr_wav__strlen` at
`miniaudio.h:80659`, called from `ma_dr_wav__read_bext_to_metadata_obj` at
line 80808.

```text
READ of size 1 immediately after a 244-byte allocation
ma_dr_wav__strlen                  miniaudio.h:80659
ma_dr_wav__read_bext_to_metadata  miniaudio.h:80808
ma_dr_wav__metadata_process_chunk miniaudio.h:81042
```

Evidence: [repro-1101.log](evidence/audit/repro-1101.log).

Normal `ma_wav_init*` calls do not enable metadata parsing, so default engine
playback is less exposed than applications calling the embedded dr_wav metadata
APIs. The affected parser is still shipped and can be reached by metadata tools
or custom integrations.

## Suspect code

At `miniaudio.h:80803-80808`, storage is requested for the exact declared
history size, bytes are read, and an unbounded string scan follows:

```c
extraBytes = (size_t)(chunkSize - MA_DR_WAV_BEXT_BYTES);
if (extraBytes > 0) {
    pMetadata->data.bext.pCodingHistory =
        (char*)ma_dr_wav__metadata_get_memory(
            pParser, extraBytes, 1);
    bytesRead += ma_dr_wav__metadata_parser_read(
        pParser,
        pMetadata->data.bext.pCodingHistory,
        extraBytes,
        NULL);
    pMetadata->data.bext.codingHistorySize =
        (ma_uint32)ma_dr_wav__strlen(
            pMetadata->data.bext.pCodingHistory);
}
```

The allocation has no guaranteed terminator and `strlen` has no bound.

## Potential minimal fix

Allocate one checked extra byte, terminate at the number of bytes actually
read, and use a bounded scan if the API's historical meaning is “characters up
to the first NUL”:

```c
if (extraBytes == SIZE_MAX) {
    return metadata_error;
}

history = ma_dr_wav__metadata_get_memory(
    pParser, extraBytes + 1, 1);
if (history == NULL) {
    return metadata_error;
}

actual = ma_dr_wav__metadata_parser_read(
    pParser, history, extraBytes, NULL);
history[actual] = '\0';

/* Preserve first-NUL semantics without leaving the allocation. */
historySize = bounded_strlen(history, actual);
```

An upstream development change assigns `codingHistorySize` to the byte count
read. That prevents the over-read but may change behavior when the history
contains an embedded NUL. Before adopting it verbatim, decide and test whether
the public field means raw byte count or C-string length. Also define whether a
short read rejects the metadata or returns a safely truncated field; silent use
of uninitialized requested bytes is unacceptable.

The metadata allocator must use checked `size_t` addition and alignment for the
extra terminator in both the count and materialization passes.

## Required regression tests

1. Coding history with no NUL, a first-byte NUL, embedded NUL, final-byte NUL,
   and zero length.
2. Short reads at every boundary after the fixed BEXT header.
3. Declared lengths at `SIZE_MAX`, at alignment boundaries, and beyond a
   32-bit `size_t` build's capacity.
4. Allocation failure while counting, allocating, and filling metadata.
5. Valid BEXT metadata round-trip/inspection to lock down
   `codingHistorySize` semantics.
6. Metadata-disabled decoding of the same WAV to confirm normal playback is
   unaffected.

Run the original reproducer and all boundary cases under ASan/UBSan. The test
allocator should poison newly allocated bytes so accidental reliance on zeroed
memory cannot hide the defect.

## Compatibility and upstream state

No public layout change is required. Defining `codingHistorySize` differently
could be an observable API change, so the minimal compatible fix should retain
the existing first-NUL meaning unless project documentation says otherwise.

The issue is closed upstream and the CVE record references a later fix, but
this exact checkout still reproduces the over-read. GitHub closure is not patch
ancestry. The primary record is CVE-2026-32837; the relevant upstream miniaudio
commit is `1df46ae9a0eed5aa9f58b179d2cc4af5d23f8bde`.

## Implemented nested-LIST result

The BEXT termination/counting fix and semantic regressions are landed. Final
review also found that nested LIST child sizes could influence metadata counts
before the child payload and padding were proved to fit the parent LIST.
`fee518b` validates the child ID/size headers, payload, and internal pad before
counting, allocation, read, or seek. Known, unknown, and label children plus
truncated ID/size and missing-pad cases use a reject-all allocator and require
zero allocation attempts; a valid unknown INFO child still round-trips.
