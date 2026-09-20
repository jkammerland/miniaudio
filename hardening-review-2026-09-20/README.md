# Miniaudio 0.11.25 hardening review

This directory contains the phase-one investigation for the miniaudio fork at
commit `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` (version 0.11.25). Each
upstream issue has its own review file with checkout applicability, evidence,
suspect code, a potential fix, regression tests, compatibility considerations,
and validation limits.

The independent follow-up review at [follow-up-review.md](follow-up-review.md)
refreshes the upstream issue status and reviews the current development-branch
patches against this checkout. It is still an audit note: production source is
unchanged.

These documents are proposals. No production source has been changed, and none
of the proposed fixes should be treated as implemented or validated yet.

The copy/paste-ready implementation handoff is in
[IMPLEMENTATION-PROMPT.md](IMPLEMENTATION-PROMPT.md). It requires per-issue
red/green evidence and durable native Windows/macOS validation through the
offload endpoints.

## Issue index

| Priority | Issue | Checkout result | Platform |
|---|---|---|---|
| P0 | [#1151: malformed WAV decode loop](1151-malformed-wav-loop.md) | Reproduced | All |
| P0 | [#1101 / CVE-2026-32837: BEXT over-read](1101-bext-coding-history-overread.md) | Reproduced with ASan | All, metadata opt-in |
| P0 | [#1141: failed resource load UAF](1141-resource-manager-failed-load-uaf.md) | Reproduced with ASan | All |
| P0-related | [#1129: resource-manager duplicate report](1129-resource-manager-error-path-uaf.md) | Same defect family | All |
| P0-related | [#1139: resource-manager duplicate report](1139-resource-manager-error-path-uaf.md) | Same defect family | All |
| P0 | [#1138: SMPL metadata allocation](1138-smpl-unbounded-allocation.md) | Reproduced with capped allocator | All, metadata opt-in |
| P0 | [#1123: fact-chunk underflow](1123-fact-chunk-seek-underflow.md) | Reproduced | All |
| P0 | [#1128: MP3 metadata over-read](1128-mp3-metadata-overread.md) | Reproduced with ASan | All |
| P0 | [#1149: WASAPI callback deadlock](1149-wasapi-notification-deadlock.md) | Confirmed in source; native validation pending | Windows |
| P1 | [#1152: exclusive capture duplication](1152-wasapi-exclusive-capture.md) | Not reproduced | Windows |
| P1 | [#1153: spatialization glitches](1153-spatialization-gain-smoothing.md) | Related DSP defects reproduced | All; original report Windows |
| P1 | [#1155: optional Opus UAF report](1155-libopus-uaf-report.md) | Insufficient report; dependency unavailable | Optional Opus |
| P1 | [#1157: GCC LTO diagnostics](1157-gcc-lto-atomics.md) | Not reproduced with local GCC 16 | Primarily Windows/MinGW report |
| P1 | [#484: process-specific loopback](0484-process-loopback.md) | Feature remains unverified | Windows |
| P1 | [#1125: short sound truncation](1125-short-sound-truncation.md) | Reproduced offline | All |
| P1 | [#1117: MP3 length overflow](1117-mp3-length-overflow.md) | Suspect code present; fixture reproduction pending | All |

## Shared evidence

The full investigation plan and command-level evidence are stored outside the
working tree:

- `/tmp/miniaudio-audit-20260920/PLAN.md`
- `/tmp/miniaudio-audit-20260920/repro-results.json`
- `/tmp/miniaudio-audit-20260920/parser-probe-results.json`
- `/tmp/miniaudio-audit-20260920/probe-dsp.log`
- `/tmp/miniaudio-audit-20260920/probe-dsp-scalar.log`
- `/tmp/miniaudio-audit-20260920/ctest.log`
- `/tmp/miniaudio-audit-20260920/upstream-dev.diff`

The existing CMake test suite passed 5/5 before any fix, so it does not cover
the confirmed parser, resource-manager, or DSP failures.

## Cross-cutting integration rules

The future coroutine integration must keep backend operation storage alive
independently of coroutine-frame lifetime. A `resume_token` prevents stale
frame resumption; it does not own an audio device, callback userdata, pending
OS operation, result buffer, or event loop. External completion should be
posted to the owning event loop before the token is invoked whenever loop
affinity is required, because token invocation resumes inline.

Audio and OS callbacks must use bounded, preallocated communication. They must
not perform blocking rerouting, resource destruction, unbounded allocation,
unsafe graph mutation, or direct coroutine resumption. Cancellation and
shutdown must retire the external callback source before freeing its userdata.

## Proposed commit order

1. `fix(wav): validate chunk bounds and forward progress`
2. `fix(wav): bound BEXT and SMPL metadata parsing`
3. `fix(resource-manager): make failed-load cleanup ownership-safe`
4. `fix(resource-manager): retire pending jobs before freeing resources`, only
   after deterministic reproduction of the separate retirement race
5. `fix(mp3): validate frame metadata before reading fields`
6. `fix(gainer): keep scalar and SIMD ramps continuous`
7. `fix(engine): drain converted frames before signaling end`
8. `fix(wasapi): defer notification rerouting to a control worker`
9. Optional decoder, compiler, capture, and process-loopback work after each is
   reproduced on its required platform/configuration
