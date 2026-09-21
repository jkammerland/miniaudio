# Miniaudio 0.11.25 hardening review

> **Current implementation status (2026-09-21):** the original Astra-max source
> and mandatory-test findings have been addressed through `2d76179`. See
> [UNRESOLVED.md](UNRESOLVED.md) for the authoritative table of reports that
> were not reproducible or could not receive a valid hardware/content verdict.
> The proposal language below describes the earlier review phase and is retained
> as historical context.

This directory contains the phase-one investigation for the miniaudio fork at
commit `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` (version 0.11.25). Each
upstream issue has its own review file with checkout applicability, evidence,
suspect code, a potential fix, regression tests, compatibility considerations,
and validation limits.

The independent follow-up review at [follow-up-review.md](follow-up-review.md)
refreshes the upstream issue status and reviews the current development-branch
patches against this checkout. That file remains a historical audit note; the
later scoped commits contain the production changes.

These documents began as proposals. The later scoped commits implement the
confirmed fixes. `UNRESOLVED.md` is authoritative when an original issue note
still describes its historical pre-implementation state.

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
| P0 | [#1149: WASAPI callback deadlock](1149-wasapi-notification-deadlock.md) | Fixed with transactional control-worker lifecycle tests | Windows |
| P1 | [#1152: exclusive capture duplication](1152-wasapi-exclusive-capture.md) | Not reproduced in 27 USB-endpoint runs; report remains open | Windows |
| P1 | [#1153: spatialization glitches](1153-spatialization-gain-smoothing.md) | Related DSP defects reproduced | All; original report Windows |
| P1 | [#1155: optional Opus UAF report](1155-libopus-uaf-report.md) | Alleged UAF unconfirmed; pinned matrix found/fixed separate callback bugs | Optional Opus |
| P1 | [#1157: GCC LTO diagnostics](1157-gcc-lto-atomics.md) | Unresolved; Windows MinGW validation owner-declined | Primarily Windows/MinGW report |
| P1 | [#484: process-specific loopback](0484-process-loopback.md) | Implemented; live content isolation unverified because all controls were silent | Windows |
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

## Implemented commit sequence

The implementation followed the proposed dependency order: parser/DSP and
resource-manager fixes first, then WASAPI/CoreAudio lifecycle work, Steam Audio,
the separately reproduced resource-manager retirement race, optional decoders,
and finally process loopback. The exact conventional commits and the disposition
of every independent-review finding are listed in `UNRESOLVED.md`.
