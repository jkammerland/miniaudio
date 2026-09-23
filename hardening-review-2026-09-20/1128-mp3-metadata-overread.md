# Issue #1128: truncated MP3 metadata heap over-read

## Assessment

- **Priority:** P0 memory safety
- **Exact checkout:** affected
- **Local reproduction:** confirmed with ASan
- **Platform:** backend-independent
- **Public path:** `ma_decoder_init_memory()` default MP3 backend

The original 35-byte input triggers a heap-buffer over-read in
`ma_dr_mp3_init_internal` at `miniaudio.h:94913`. Unlike the WAV BEXT and SMPL
issues, this reaches the ordinary decoder path without a metadata opt-in.

Evidence: [probe-1128-memory.log](evidence/audit/probe-1128-memory.log).

Only the report's source and data were inspected. Bundled executable/object
attachments were not run; the failure was reproduced using a locally compiled
ASan/UBSan probe.

## Suspect code

The parser derives `pTagData` from a decoded frame position and reads an
Xing/Info signature, flags, and optional fields without proving each is inside
the initialized input:

```c
pTagDataBeg = pFirstFrameData + MA_DR_MP3_HDR_SIZE + (bs.pos / 8);
pTagData = pTagDataBeg;

isXing = pTagData[0] == 'X' && pTagData[1] == 'i' &&
         pTagData[2] == 'n' && pTagData[3] == 'g';
isInfo = pTagData[0] == 'I' && pTagData[1] == 'n' &&
         pTagData[2] == 'f' && pTagData[3] == 'o';

flags = pTagData[7];
pTagData += 8;
```

Subsequent flag-controlled reads consume 4-byte counts, a 100-byte TOC,
quality data, and encoder delay/padding. A late comparison cannot make earlier
reads or pointer arithmetic safe.

## Potential minimal fix

Represent parsing positions as integer offsets and validate before forming or
dereferencing pointers:

```c
if (!checked_add_size(frameOffset, tagRelativeOffset, &tagOffset) ||
    tagOffset > dataSize) {
    goto ignore_optional_tag;
}

remaining = dataSize - tagOffset;
if (remaining < 8) {
    goto ignore_optional_tag;
}

tag = data + tagOffset;
flags = tag[7];
tagOffset += 8;

if ((flags & XING_FRAMES) && !consume_checked(&tagOffset, 4, dataSize)) {
    goto ignore_optional_tag;
}
if ((flags & XING_TOC) && !consume_checked(&tagOffset, 100, dataSize)) {
    goto ignore_optional_tag;
}
```

The actual frame extent and initialized input extent must both constrain reads.
Allocation capacity is not the same as bytes initialized from the file. Every
optional field needs its own `required <= end - offset` check, with `offset <=
end` proved first to avoid subtraction underflow.

Malformed optional metadata may be safely ignored if the audio frame itself is
valid; a truncated frame/file should return `MA_INVALID_FILE`. Tests should
define this distinction rather than relying on an incidental branch.

## Required regression tests

1. Original 35-byte fixture via memory, file, and custom callback decoders.
2. Truncation immediately before/within/after the signature, flags, frame
   count, byte count, TOC, quality, and encoder delay/padding.
3. All optional flag combinations, including multiple fields whose sum reaches
   the exact frame end.
4. Valid CBR `Info`, VBR `Xing`, LAME-tagged, and tag-free MP3 files.
5. Deliberate difference between allocation capacity, bytes read, and declared
   frame length.
6. ASan/UBSan and 32-bit size arithmetic coverage.

## Compatibility and upstream state

No public ABI change is expected. Invalid optional metadata may no longer
affect length/delay detection, while valid audio should still decode. The issue
was closed based on development-branch work, but the vulnerable reads remain
in this exact release checkout and reproduce through its default decoder.

## Implemented callback-allocation result

The metadata bounds and callback fault matrix are landed. A separate refill
guard prevents decode or callback pointer arithmetic when callback storage is
missing. Final review correctly observed that the original synthetic
non-null-capacity/null-pointer fixture was not proof of allocator failure.
`fee518b` therefore adds counted refusing callbacks for both the initial
allocation and a growth realloc while a previously allocated buffer remains
live. No read callback is entered, the retained pointer/capacity are unchanged
after growth failure, and final allocation/free/live counts balance exactly.
