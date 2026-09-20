# Follow-up code and upstream review — 2026-09-20

## Scope

Reviewed the local `master` checkout at `7e940fe`, whose production source is
still the upstream 0.11.25 commit `9634bedb`. Compared it with upstream `dev`
at `dc3b628` and checked the current GitHub state of the issue set in the
index. No production source was changed by this follow-up.

## Upstream status snapshot

The following issues are currently closed upstream, but their fixes are not in
this checkout: [#1101](https://github.com/mackron/miniaudio/issues/1101),
[#1117](https://github.com/mackron/miniaudio/issues/1117),
[#1123](https://github.com/mackron/miniaudio/issues/1123),
[#1125](https://github.com/mackron/miniaudio/issues/1125),
[#1128](https://github.com/mackron/miniaudio/issues/1128),
[#1129](https://github.com/mackron/miniaudio/issues/1129),
[#1138](https://github.com/mackron/miniaudio/issues/1138),
[#1139](https://github.com/mackron/miniaudio/issues/1139), and
[#1141](https://github.com/mackron/miniaudio/issues/1141). The resource-manager
fix is visible in upstream commit
[`87206fc`](https://github.com/mackron/miniaudio/commit/87206fca67b449685da294e9e3e6c4269d3c1273).

These remain open: [#484](https://github.com/mackron/miniaudio/issues/484),
[#1149](https://github.com/mackron/miniaudio/issues/1149),
[#1151](https://github.com/mackron/miniaudio/issues/1151),
[#1152](https://github.com/mackron/miniaudio/issues/1152),
[#1153](https://github.com/mackron/miniaudio/issues/1153),
[#1155](https://github.com/mackron/miniaudio/issues/1155), and
[#1157](https://github.com/mackron/miniaudio/issues/1157).

GitHub closure is not patch ancestry. The local branch is still vulnerable to
the confirmed parser and lifetime findings, and it still contains the affected
DSP and WASAPI paths.

## Independent source review

### Findings confirmed in the local checkout

- The BEXT coding-history path allocates `extraBytes + 1`, reads only
  `extraBytes`, and then calls an unbounded `ma_dr_wav__strlen()` without
  writing a terminator (`miniaudio.h:80803-80808`). The local ASan reproducer
  still fails.
- The resource-manager failure path frees a newly-created node and then reads
  `pDataBufferNode->isDataOwnedByResourceManager` (`miniaudio.h:70913-70929`).
  The local ASan reproducer still fails. Upstream's fix caches this state and
  clears the local pointer, but that does not by itself prove all asynchronous
  retirement paths safe.
- The SMPL metadata count pass trusts
  `samplerSpecificDataSizeInBytes` as an allocation size without checking the
  remaining chunk bytes (`miniaudio.h:80938-80958`). The capped allocator
  records a 2,147,483,766-byte request from a tiny input.
- The MP3 Xing/Info path dereferences tag fields before proving that the tag
  fits in the decoded frame (`miniaudio.h:94911-94935`), and the frame-count
  product is widened only after 32-bit multiplication (`miniaudio.h:94973-94974`).
  The local truncated-input ASan probe still fails.
- The gainer computes the remaining ramp as `t - smoothTime` instead of
  `smoothTime - t`, advances stereo/six-channel unrolled SIMD state by one
  frame, and offsets interleaved buffers without multiplying by channel count
  (`miniaudio.h:50708-50715`, `50749-50837`, `50902-50903`). Both SSE2 and
  scalar probes fail the independent ramp oracle.
- The conversion/cache path can return `MA_AT_END` with frames still in the
  conversion cache; the local short-sound probe returns 2,560 nonzero frames
  where the fixture contains 3,036 (`miniaudio.h:76978-77006`).
- The WASAPI default-device notification callback calls stop/reroute/start
  work directly from `OnDefaultDeviceChanged` (`miniaudio.h:22475-22575`).
  The same file documents those operations as unsafe inside callbacks and
  comments that releasing the audio client there can deadlock
  (`miniaudio.h:24121-24129`). This remains a source-confirmed Windows issue,
  not a Linux reproduction.

### Upstream development patches requiring review before transplant

1. The development WAV patch adds a known-size check and rejects large signed
   seeks, but computes
   `leftoverBytes = header.sizeInBytes - bytesReadSoFar` before proving the
   subtraction is non-negative. It also reads the 2-byte extension size when a
   `fmt ` chunk declares only 17 bytes. A safe transplant must first require
   the fixed fields, then require `extendedSize <= header.sizeInBytes - 18`,
   and only then seek the remainder.

2. The development MP3 patch calculates a pointer difference to the buffer
   end and later subtracts the tag offset from `frameBytes`. The replacement
   must use validated integer offsets: prove `tagOffset <= frameExtent`, then
   check each required field with `required <= frameExtent - tagOffset` before
   forming or dereferencing a pointer. Do not adopt the patch solely because
   the issue is closed.

3. The development resource-manager branch adds waits for execution-pointer
   retirement. Those waits need deterministic tests for worker-thread callers,
   manual job pumping, queue-post failure, and counter wraparound; a wait that
   depends on the current caller processing the queued job can self-deadlock.

## Validation performed

- Fresh Debug CMake configure/build and CTest: **5/5 passed**. The suite does
  not exercise the confirmed parser, resource-manager, MP3, gainer, or WASAPI
  failures.
- Fresh smoke runs of the archived probes: BEXT and resource-manager ASan
  failures reproduced; the SMPL capped allocation was reproduced; malformed
  WAV #1151 still exceeded the 4-second bounded run; both DSP variants failed.
- Optional `opusfile`/`libvorbisfile` and Windows hardware tests were not
  available in this environment, so #1155, #1152, #1149, and the audible part
  of #1153 remain platform/dependency-limited.

## Disposition

Keep the existing issue notes as the phase-one investigation. Before applying
upstream code, land focused fixes in the proposed order from the main README,
with independent regression tests for every confirmed failure and separate
Windows validation for WASAPI. Do not label this 0.11.25-based checkout fixed
until those tests pass against the actual source used by the consumer.
