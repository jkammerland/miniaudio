# Hardening implementation: unresolved work and validation limits

Status date: 2026-09-20  
Reviewed implementation HEAD: `ca92a4b1627a52b1ad865f5a97381014a13686b4`

This is the authoritative follow-up queue for the implementation requested by
`IMPLEMENTATION-PROMPT.md`. It records source-established defects, reproduced
regressions, missing deterministic tests, and environment-limited work. An
unavailable environment is not a pass. Existing fixes and passing tests remain
useful evidence, but the implementation is **not acceptance-ready** while the
delivery blockers below remain.

## Independent Astra-max review

Batch Codex job `cde2b7f425041dd3` reviewed the complete prompt, issue dossiers,
commits `47c2eef8..ca92a4b`, production and test diffs, local RED/GREEN artifacts,
and fetched Windows/macOS evidence. It used `gpt-6-astra` at `max` reasoning and
made no repository changes.

Review outputs:

- `/home/ai-dev1/.codex/codex-mcp-server/job-artifacts/cde2b7f425041dd3/tasks/task-0/REVIEW.md`
- `/home/ai-dev1/.codex/codex-mcp-server/job-artifacts/cde2b7f425041dd3/tasks/task-0/FINDINGS.json`
- `/home/ai-dev1/.codex/codex-mcp-server/job-artifacts/cde2b7f425041dd3/tasks/task-0/review-checks`

The review reported 0 critical, 6 high, and 7 medium findings. One medium
finding was the accuracy of the earlier draft of this document; this rewrite
addresses that documentation finding. The remaining source and coverage
findings are unresolved unless explicitly stated otherwise below.

## Delivery-blocking source defects and regression

### F01 — SMPL count/read disagreement can overrun metadata storage (high)

Status: **source-proven conditional defect; not fixed**.  
Snapshot location: `miniaudio.h:81425`.

The count pass reserves an SMPL record only when structural validation accepts
the chunk. The read helper can consume the complete fixed header on rejection,
and the read pass interprets complete consumption as materialization success
and advances `metadataCursor`. In a mixed-metadata file this can write a record
that the count pass did not reserve. The current isolated invalid-SMPL test has
zero accepted metadata and therefore bypasses the vulnerable materialization
pass.

Required work:

- return acceptance separately from bytes consumed;
- advance/publish only fully initialized accepted records;
- enforce record and pool capacity in the materialization pass; and
- add mixed-record, invalid-SMPL, and short-read allocation-accounting tests.

### F02 — WASAPI reroute publishes clients before converter setup is safe (high)

Status: **source-proven conditional defect; not fixed**.  
Snapshot location: `miniaudio.h:24767`.

The reroute path retires old clients and publishes replacement COM state before
`ma_device__post_init_setup()` rebuilds converters. Post-init first uninitializes
the published converter and can then fail allocation before replacing it. The
object may retain freed ownership and be uninitialized again during ordinary
device teardown. Candidate publication is transactional for a COM subset, not
for the complete device state.

Required work:

- construct clients, converters, caches, and related state in temporary owned
  storage before publication;
- leave either the valid old state or an explicitly empty destroyable state on
  every failure; and
- inject failures at each post-init stage with exact release-count assertions.

### F03 — Failed synchronous resource load can free a shared node (high)

Status: **source-proven concurrency defect; not fixed**.  
Snapshot location: `miniaudio.h:71315`.

A newly inserted BUSY node becomes visible after the BST lock is released.
Another acquisition can increment its reference before the first synchronous
load fails. The creator failure path removes/frees the node based on creator
ownership without retiring the second acquisition and performs the tree removal
without the BST lock. The landed missing-file tests have no second acquirer or
barrier.

Required work:

- publish failure to existing holders and retire only the failing caller's
  ownership;
- serialize tree mutation and preserve the node until references/jobs retire;
  and
- add a deterministic shared-acquisition custom-VFS failure test, separately
  from the publish-before-job-retirement investigation.

### F04 — MP3 declared length still controls resource-manager allocation (high)

Status: **source-proven required outcome incomplete; not fixed**.  
Snapshot location: `miniaudio.h:95530`.

The widened multiplication prevents the original 32-bit wrap, but the Xing
declared frame count is still returned as decoder length without validation
against actual decoded input. The resource-manager known-length decoded path
then allocates and clears that declared size when it fits `size_t`. The current
bounded allocation regression uses `ma_decode_memory()`, which grows storage by
actual decoded frames and therefore does not exercise this consumer.

Required work:

- treat unverified metadata length as a hint rather than allocation authority;
- use a validated bound or incremental decoded storage; and
- test the resource-manager decoded path with a refusing/recording allocator
  and an independent decoded-length expectation.

### F05 — Failed CoreAudio reroute leaves stale published ownership (high)

Status: **source-proven conditional defect; not fixed or runtime-injected**.  
Snapshot location: `miniaudio.h:36487`.

CoreAudio rerouting disposes published capture/playback units and frees the
published capture `AudioBufferList` without clearing the owners. If replacement
initialization then fails, ordinary teardown can dispose/free those stale fields
again.

Required work:

- stage the complete replacement before retiring old state, or clear every
  retired owner and enter a coherent stopped state;
- include generic post-init/converter failures in the transaction; and
- add deterministic failure/teardown tests plus permitted native confirmation.

### F06 — Failed CoreAudio duplex initialization remains globally tracked (high)

Status: **source-proven conditional lifetime defect; not fixed**.  
Snapshot location: `miniaudio.h:36649`.

Default capture is added to CoreAudio's global tracking list before duplex
playback initialization. The playback-failure branch disposes capture but does
not untrack the device; generic public-init failure cleanup does not call backend
untrack. Caller storage can therefore be released while a later property
callback retains its pointer.

Required work:

- publish tracking only after complete device initialization or unwind it from
  every partial-init failure; and
- test capture-success/playback-failure followed by a property notification,
  with exact cleanup accounting.

### F07 — Valid RF64 with metadata enabled is rejected (medium)

Status: **reproduced compatibility regression; not fixed**.  
Snapshot location: `miniaudio.h:81830`.

The new known-file-size guard checks the RF64 data chunk's raw sentinel size
before resolving it through `ds64`. The independent reviewer generated a valid
one-sample RF64 file with miniaudio's own writer. Normal and metadata-enabled
reads both pass on baseline `47c2eef`; on `ca92a4b`, normal read passes and the
metadata-enabled read fails.

Reproducer and logs:

- `review-checks/valid-rf64-roundtrip.c`
- `review-checks/valid-rf64-47c2eef.log` — exit 0
- `review-checks/valid-rf64-ca92a4b.log` — exit 1

Required work: resolve RF64 sentinel lengths from `ds64` before bounds/traversal
checks in both passes and land a valid writer/metadata-reader round-trip test.

### F08 — Internal WASAPI reroute emits a public stopped notification (medium)

Status: **source-proven contract violation; not fixed**.  
Snapshot location: `miniaudio.h:24755`.

The documented contract says stopped notifications are not emitted for
rerouting. The control worker calls the normal internal stop path; its boolean
controls requested-state bookkeeping but does not suppress the audio worker's
public stopped notification. A restart can then emit started before the separate
rerouted notification.

Required work: represent internal routing separately from externally requested
stop/start notifications and test successful/failed reroute notification order,
including concurrent public state changes.

### F09 — WAIT_INIT ignores notification initialization failure (medium)

Status: **source-proven failure-path defect; not fixed**.  
Snapshot location: `miniaudio.h:71118`.

`ma_resource_manager_inline_notification_init()` can fail, but the acquisition
path ignores its result and may pass, signal, wait on, or uninitialize an invalid
notification object. The prompt explicitly required tracking notification and
fence initialization.

Required work: check initialization before publication, track every initialized
or acquired stage, unwind only completed stages, and inject notification/fence
failures with exact signal/wait/uninit counts.

### F10 — Failed decoded loads can leak decoder/data-supply ownership (medium)

Status: **source-proven failure-path leak; not fixed**.  
Snapshot location: `miniaudio.h:71167`.

Decoded supply storage can be allocated before PAGE-job posting. PAGE-post
failure destroys only the decoder, and an inline WAIT_INIT failure can then
remove/free the node without releasing the supply. A decoder length-query
failure also returns after successful decoder initialization without complete
decoder cleanup. The current missing-file regression fails before these owners
exist.

Required work: centralize staged decoder/supply cleanup and test length-query,
mid-decode, PAGE-post, custom-VFS, and allocation failures without freeing
shared or externally registered storage.

### F11 — Steam Audio example direction state has a data race (medium)

Status: **inherited source-proven example defect; not fixed or SDK-validated**.  
Snapshot location: `examples/engine_steamaudio.c:260`.

The audio thread reads three ordinary direction floats while the main/control
thread writes them after playback starts. No bounded block-boundary snapshot is
present. The implementation prompt required this example change only with real
SDK validation; that SDK environment was unavailable, so the example was left
unchanged rather than claiming a fix.

Required work: implement a bounded preallocated snapshot adopted at audio-block
boundaries, then validate fixed/partial/tail frame handling and effect ownership
with the real Steam Audio SDK.

## Mandatory lifecycle coverage still absent

### F12 — WASAPI test covers publication bits, not lifecycle/quiescence (medium)

Status: **architecture implemented, mandatory deterministic validation absent**.

`hardening_wasapi_lifecycle.c` tests role/flow filtering, bit coalescing, and the
closed publication gate. Source inspection shows that the callback does not
store the borrowed ID pointer, but mutating the test buffer without a later
consumer/assertion does not prove non-retention or lifetime safety. The test
also does not start the control worker, initialize the operation lock, use fake
COM/audio clients, process a full command ring, inject worker/candidate/post-init
failures, race public start/stop with reroute, or prove
unregister/callback/Release retirement. Ordinary native playback supplements
but does not replace those tests.

Required work:

- fake-interface barriers for callback entry, unregister, final Release, worker
  drain/join, and free;
- full-ring command processing and payload-lifetime checks;
- COM-worker initialization and every candidate/post-init failure; and
- successful/failed reroute ordering with concurrent public state changes and
  shutdown.

Until then, describe #1149 as an implemented nonblocking publication/control
worker design with incomplete lifecycle proof, not as a completed lifecycle fix.

## Open platform reports and feature work

### #1152: WASAPI exclusive-capture duplication/discontinuity

Status: **unresolved; capture endpoint connected, reproduction pending**.

The earlier Windows worker run exposed no capture endpoint. On 2026-09-20 the
owner reported that a capture device had been connected; enumeration, exclusive
mode, and format support have not yet been verified. No packet/cache/flag/
device-QPC trace or watermark evidence exists. Do not infer that draining
multiple packets itself is wrong. Reproduction requires the connected endpoint
to support exclusive capture and the bounded preallocated trace specified by
the implementation prompt.

### #1155: optional-libopus use-after-free report

Status: **unresolved; ownership sequence not established**.

Observed runtime packages were `opusfile-0.12-18.fc44.x86_64`,
`opus-1.6-2.fc44.x86_64`, and `libogg-1.3.6-2.fc44.x86_64`. Development headers
and pkg-config metadata were unavailable. A valid owner-mediated sequence and a
sanitizer trace are required before changing ownership; concurrent public
read/uninit is not assumed valid.

### #1157: GCC/MinGW LTO atomic or node-bus diagnostics

Status: **unresolved; Windows GCC/MinGW validation explicitly owner-declined**.

Historical local runs reported GCC 16.2.1 C and C++23 success with and without
LTO, but the retained `gcc-lto-*` binaries do not preserve complete commands,
exact-source identifiers, and exit records, so that matrix is not independently
certified. Windows had no MinGW. On 2026-09-20 the owner explicitly rejected
deploying or running GCC/MinGW on Windows. Do not provision, install, or execute
MinGW on Windows for this effort. The report therefore remains unresolved and
outside the accepted validation scope; this decision is not evidence of a pass
or a fix. Reopening it requires a new owner decision or an externally supplied
exact compiler/triple/flags/diagnostic and reproducer.

### #484: process-scoped WASAPI loopback

Status: **feature incomplete**.

Include/exclude initialization returned `MA_INVALID_ARGS`. The desktop path has
not completed the dedicated asynchronous activation flow, and no two-watermark
content-isolation environment was available. Required coverage includes
unsupported-OS behavior, activation cancellation/late completion, two process
trees including children and exit, no-target silence, and proof of no fallback
to ordinary whole-system loopback.

## Other acceptance and validation gaps

The following are mandatory or materially relevant gaps, not passing results:

- Resource manager: plain `ASYNC`, custom-VFS, allocation, job-post, shared-node,
  registered-external-data, notification/fence, and manual-job-pump cases are
  absent. The separate publish-before-job-retirement interleaving remains
  unforced and unchanged.
- WAV/BEXT/SMPL: file/callback short reads, failed seeks, mixed metadata, and a
  real 32-bit size-conversion build remain incomplete. Valid RF64 is currently
  regressed as F07.
- MP3: callback/file paths, optional fields, fallback decoders, and the
  resource-manager allocation consumer remain incomplete.
- EOF: the regression asserts one overall end callback, but not notification
  timing after the final converted frame across looping, resampling, cache, and
  period boundaries.
- Gain: scalar/SSE2 tests are meaningful and include split blocks and 33
  channels. Other architecture-specific implementations require native runners;
  arm64 success does not imply a separate NEON gainer implementation was tested.
- Callback real-time behavior: source review was performed, but there is no
  allocation/lock instrumentation or worst-case callback-cost evidence.
- Local 32-bit libc development headers and optional Opus/Vorbis development
  configurations were unavailable.

## Residual diagnostics

Focused final test sources are clean for the targeted C89, format, and
uninitialized-output checks. Aggressive clang-cl builds still report inherited
header diagnostics including `-Wunsafe-buffer-usage`, reserved identifiers,
C-style casts in C++ consumers, and null-pointer style. macOS analysis reports
four inherited FLAC `runningOrder` dead stores. They were preserved rather than
suppressed and were not established as runtime defects by this work.

## Native evidence and provenance limits

- Full Windows matrices ran on `6923868`; exact final `ca92a4b` ran the three
  changed targets under MSVC Debug and clang-cl ASan, 6/6.
- Full macOS matrices ran on production-equivalent predecessors; exact final
  `ca92a4b` ran the three changed targets under Apple Debug and LLVM ASan/UBSan,
  6/6.
- Windows playback passed with 55 callbacks and 26,400 frames. There were zero
  capture endpoints and no MinGW.
- macOS device tests timed out after start with zero callbacks on baseline and
  final source; capture permission was denied. Those are failures/environment
  limits, not device passes.
- Measured Linux x86_64, Windows x64, and macOS arm64 ABI values matched their
  baselines. This is not proof for every compiler, packing mode, or public type.
- Historical RED artifacts support several original failures, but complete
  command/exit/revision-scoped parent/fixed pairs were not preserved for every
  issue. Existing binaries alone are not accepted as durable proof.

Independent review verified all fetched artifact manifests: 904 listed entries
with zero mismatches, and 897 native `SHA256SUMS` entries with zero mismatches.

## Post-fetch cleanup receipts

The native artifact snapshots were written before caller cleanup and therefore
say `retained_pending_fetch`. Later endpoint lifecycle receipts record actual
cleanup:

- `/tmp/miniaudio-hardening-implementation/lifecycle-post-fetch-windows-ca92a4b.json`
  (`aa88d25c2371ff97c8c650f235bdb3c937ca759eba08a9e7f9e17427b1798e0d`):
  Windows staging/build removed, 50,998,127 file bytes recovered; exact source
  worktrees retained.
- `/tmp/miniaudio-hardening-implementation/lifecycle-post-fetch-mac-ca92a4b.json`
  (`0653de7e3297c9d4bdbf4250c0972f4356b432f1d0084b5542a96dc05833a86d`):
  macOS staging/build removed, 41,127,936 allocated bytes recovered; exact source
  worktrees retained.

The earlier fetched snapshots remain immutable historical evidence; these two
post-fetch receipts substantiate the later cleanup action.

## Future coroutine integration constraints

No `timer_wheels` integration code was changed. A future bridge must use one
generation-bound resume token per suspension, post completion to the owning
loop before inline resumption when affinity is required, separate logical
cancellation from physical callback retirement, keep retirement progress
serviceable through shutdown, and idempotently release owned payloads for
duplicate or late completion. Raw coroutine handles must not be retained or
independently resumed.

## Release gate

Do not describe this implementation as complete until F01–F10 are fixed with
focused regressions and F12's deterministic lifecycle coverage exists. F11 can
remain explicitly unresolved only while real Steam Audio SDK validation is
unavailable; do not claim the example fixed. #1152, #1155, #1157, and #484 may
remain unresolved under the prompt's evidence gates, provided their limitations
stay explicit.
