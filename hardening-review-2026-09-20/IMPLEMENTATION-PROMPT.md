# Implementation prompt: miniaudio backend hardening

> **Execution status (2026-09-23):** implementation and available native
> validation have reached `e4658df`. Exact-final Linux and deterministic macOS
> gates pass; Windows production code is exact-tested through `06e3a06`, after
> which only the macOS-only CoreAudio test translation unit changed.
> `UNRESOLVED.md` is the authoritative current disposition. In particular, the
> owner explicitly rejected Windows GCC/MinGW deployment; do not provision or
> run MinGW unless that decision is later reversed.

You are the lead engineer implementing and validating the miniaudio hardening
work described below. This prompt authorizes implementation, local testing,
native Windows/macOS offload validation, and scoped local commits. It does not
authorize pushing to a remote, weakening tests, making an undocumented ABI
break, or modifying unrelated repositories.

## Repositories and baseline

- Miniaudio fork: `/mnt/d2/agent-workspace/repos/miniaudio`
- Review baseline before this prompt: `6af26f4` (the original dossier commit
  `7e940fe` plus the follow-up upstream/source review)
- Production-source baseline: miniaudio 0.11.25 at
  `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`
- Issue archive:
  `/mnt/d2/agent-workspace/repos/miniaudio/issue-collection-2026-09-20`
- Issue dossiers and implementation proposals:
  `/mnt/d2/agent-workspace/repos/miniaudio/hardening-review-2026-09-20`
- Prior investigation evidence: `/tmp/miniaudio-audit-20260920`
- Coroutine framework, read-only unless a separate proven framework defect
  requires explicit approval: `/home/ai-dev1/repos/timer_wheels`
- Steam Audio source for the optional integration:
  `/mnt/d2/agent-workspace/repos/steam-audio`

The intended miniaudio/Steam Audio consumer is in another repository. Do not
invent an integration in `timer_wheels` or in the experimental repositories.
Use the framework documents only to preserve the future completion/lifetime
contract. Preserve all unrelated work in every checkout.

Before editing, verify HEAD, worktree/index status, submodules/LFS if any, and
the production-source diff from `9634bedb...`. Record unexpected changes and
work around them; never reset, clean, delete-sync, or overwrite user work.

Read every file in `hardening-review-2026-09-20`, then use the individual issue
file as the acceptance basis for that issue. Also inspect the cited current
source rather than copying a proposed sketch blindly. Upstream development
patches are references, not trusted wholesale replacements.

## Required outcome

Implement and test every locally confirmed defect:

1. #1151: malformed WAV decoder loop.
2. #1101 / CVE-2026-32837: BEXT coding-history over-read.
3. #1138: SMPL metadata can request a multi-gigabyte allocation.
4. #1123: `fact` chunk underflow drives excessive seeking.
5. #1128: truncated MP3 Xing/Info/LAME metadata over-read.
6. #1141, covering duplicates #1129 and #1139: resource-manager failed-load
   use-after-free and dangling output.
7. #1153-related confirmed gain-smoothing defects: duration underflow/overshoot,
   two-frame SIMD progression, odd tails, and multichannel pointer offsets.
8. #1125: valid converted tail frames are discarded at EOF.
9. #1117: reproduce the MP3 frame-count overflow, then widen/check the
   multiplication and prevent hostile length metadata from becoming allocation
   authority.

Investigate on the required native platform and fix when reproduced or when a
deterministic source-invariant test proves the defect:

1. #1149: WASAPI notification callback performs blocking rerouting and mishandles
   failed reinitialization. This is a mandatory Windows lifecycle fix, but do
   not claim it validated until the native matrix passes.
2. #1152: WASAPI exclusive capture duplication/discontinuity. Instrument first;
   do not infer that draining multiple packets is itself wrong.
3. #1155: incomplete optional-libopus UAF claim. Pin dependencies and establish
   a valid call sequence before changing ownership.
4. #1157: GCC/MinGW LTO atomic/node-bus diagnostics. **Owner-rejected Windows
   deployment scope.** Document it as unresolved/out of scope; do not install,
   invoke, or claim MinGW passed. Reopen only after a new owner decision or an
   external exact-toolchain reproducer, and never change atomics or layout for
   an unreproduced diagnostic.
5. #484: process-specific WASAPI loopback. Treat this as a separate feature
   commit after lifecycle correctness. Implement it only with explicit
   unsupported-OS behavior and native content tests; never silently fall back
   to whole-system capture.

In addition, run focused native audits of the touched Windows WASAPI and macOS
CoreAudio paths. Fix reproducible backend problems found in initialization
failure, device change, playback/capture accounting, callback real-time safety,
cancellation, or shutdown with red/green coverage. Keep unrelated feature work
out of scope and list it as a remaining issue. Native validation is broad
enough to find problems in the touched backend/lifecycle paths; it is not a
claim that every platform behavior has been exhaustively proven.

## Red/green discipline

For every fix, use this sequence whenever technically possible:

1. Re-run the original reproducer against the unchanged parent commit and save
   the command, exit status, sanitizer stack or bounded trace as **RED**.
2. Add a deterministic regression test that fails for the intended reason on
   the parent commit. Diagnose that failure before editing production code.
3. Make the smallest source change that restores the required invariant.
4. Run the exact same regression command and save its passing output as
   **GREEN**.
5. Run the narrow subsystem tests and sanitizers before creating the scoped
   commit.

Final commits should contain the regression test and fix together. Red evidence
may live in the implementation artifact directory; do not commit a deliberately
failing tree. Record the parent/fixed commit IDs with both logs.

For nontermination cases, do not make timeout the only assertion. Use an
isolated subprocess watchdog as a last line of defense, plus an instrumented
read/seek callback with a finite operation budget. Before the fix, the test
must demonstrate an excessive/repeating operation count; after the fix, the
same input must return a documented error or bounded result without exhausting
the budget. A killed or timed-out test is red, never green.

For memory errors, run the original input with ASan/UBSan before and after.
Also assert the semantic result and cleanup counts so a suppressed sanitizer or
early return cannot create a vacuous pass. For allocation issues, use telemetry
and a refusing allocator; never perform the hostile multi-gigabyte allocation.

For concurrency and platform callbacks, use deterministic barriers and fake
interfaces to force orderings. Do not use sleeps, retries, probabilistic loops,
or widened tolerances. Hardware tests supplement deterministic tests; they do
not replace them.

If a report cannot be reproduced, do not manufacture a fix or a green test.
Save the exact matrix and observations, explain what evidence is missing, and
leave the issue explicitly unresolved.

## Test integrity and source rules

- Never weaken an assertion, widen a tolerance, skip a failing test, add a
  retry/sleep, or catch-and-pass to obtain green.
- Tests must prove behavior using independent expectations. Avoid assertions
  derived from the implementation and loops over potentially empty data.
- Malformed input must return an error or another bounded documented failure.
- Check arithmetic before subtraction, addition, multiplication, conversion,
  allocation, pointer formation, reads, and seeks.
- Preserve assertions that express internal invariants. Add runtime error
  handling for hostile external input rather than disabling assertions.
- Preserve the public API and binary layout. Before and after changes, record
  `sizeof`, alignment, and relevant public-field offsets on Linux, Windows, and
  macOS. If a correct fix needs an ABI break, stop and present the exact change
  for approval; this prompt does not authorize it.
- Do not import all of upstream `dev` or `dev-0.12`. Port only understood fixes
  and test them against this fork.
- Keep optional decoder configurations independent: a missing dependency is an
  unvalidated configuration, not a pass.
- Add deterministic core tests to the root CMake test declarations. Keep tests
  that require actual devices in separately named native-platform targets.

## Issue-specific implementation gates

### WAV and MP3 parsers

Cover memory, file, and callback-backed decoders. Test exact boundaries,
failed/short reads, failed seeks, fallback decoders, valid large chunks, and
cleanup. Preserve valid RIFF/RIFX/RF64/W64/AIFF and PCM/ADPCM behavior.

For BEXT, terminate at the actual bytes read and retain the established
first-NUL meaning of `codingHistorySize` unless the API documentation proves a
different contract. For SMPL, share one structural validator between metadata
count and read passes, including valid nonempty sampler-specific data. Add
32-bit size-conversion coverage.

For MP3, validate integer offsets against both initialized input and frame
extent before creating pointers. Cover every optional Xing/Info/LAME field and
combine #1128 bounds work with #1117 length arithmetic without letting one test
mask the other.

### Resource manager

The immediate #1141 fix must remove/free a new failed node exactly once, never
publish it, track notification/fence initialization explicitly, and return a
null output on failure. Test sync, ASYNC, ASYNC|WAIT_INIT, job-post failure,
custom VFS failures, allocation failures, shared existing nodes, and registered
external data.

Treat the nearby publish-before-job-retirement risk separately. Force the
interleaving with barriers before adding any wait. Prove that a proposed wait
cannot deadlock on the only thread capable of advancing `executionPointer`,
including manual job-pump mode. Land it in a separate commit only if confirmed.

### Gain smoothing and EOF

Use an independent mathematical oracle for scalar and SIMD ramps. Test block
splits, mid-ramp target changes, rising/falling gains, in-place/out-of-place,
null-buffer time advancement, and 1/2/6/8/>32 channels. Run scalar, SSE2, and
other available SIMD builds. Verify buffer guards around the transition from
interpolated to constant gain.

For #1125, verify exact final samples and frame counts across source formats,
cache/period boundaries, looping, resampling tails, and end notification once
after all frames drain.

### WASAPI

`IMMNotificationClient` must perform bounded, nonblocking, allocation-free
notification only. Do not reuse the current four-entry command path if it can
lock, spin, allocate an event, or wait. Use preallocated sidecar state and a
serialized control worker/executor. Re-query the endpoint after coalescing;
never retain the borrowed device-ID pointer. Build new client state in
temporaries, commit only complete initialization, and restart only if reroute
succeeded and the user's latest state still requests running.

Teardown order must stop accepting work, unregister/quiesce notification,
drain/join control work, and only then free callback/device state. Distinguish
an internal reroute failure from caller misuse of `ma_device_uninit` after a
failed public init.

Use fake COM/audio interfaces for failure injection and ordering, then real
device tests. Instrument exclusive capture into a bounded preallocated trace
containing packet frames, consumed/cache frames, flags, device/QPC positions,
and a sample watermark/hash. Drain trace data off the callback thread.

For process loopback, test include/exclude target process trees with two
distinct watermarked tone producers, target exit, child creation, silence when
there are no target streams, invalid/unsupported OS behavior, and
cancel-during-activation.

### CoreAudio/macOS

Build and exercise CoreAudio with real native headers and frameworks. Review
the touched lifecycle equivalents for:

- device/property listener callbacks doing blocking or destructive work;
- init failure followed by partial or double teardown;
- callback access after device/context destruction;
- start/stop/device-change races;
- capture/playback frame accounting and converter tails;
- locks, allocation, logging, VFS/decoder work, or graph mutation in render
  callbacks.

Add deterministic callback/lifecycle tests where APIs can be abstracted. Run
real default-device playback/capture only when the isolated account has an
available device and permission; record unavailable hardware/permissions as a
limitation rather than skipping a core assertion. Any macOS-specific fix gets
its own conventional commit and issue/evidence note.

### Optional codecs and rejected MinGW scope

For Opus, record exact `opusfile`, `libopus`, and `libogg` versions. Cover open
failure, malformed/chained streams, seek/EOF, callback userdata lifetime,
resource-manager cancellation, and shutdown. Concurrent public read/uninit is
not assumed valid; prove the owner-mediated sequence first. Do not add a blind
reference count that moves final destruction onto an audio callback.

For #1157, preserve real atomics and assertions, but do not provision or run a
Windows MinGW/GCC toolchain. The owner has rejected that deployment target.
Keep the report explicitly unresolved/out of scope; only a future reversal of
that decision or a supplied external exact-toolchain reproducer can reopen the
compiler investigation.

## Audio callback and coroutine lifetime requirements

Audio callbacks must not perform blocking work, unbounded allocation, unsafe
locking, resource destruction, device rerouting, graph detach/uninit, or direct
coroutine resumption. Instrument callback builds for allocations and locks.
Measure worst-case callback cost, but do not claim universal hard real-time
safety from an average timing result.

Keep Steam Audio effect/HRTF/scene creation and simulation transfer separate
from device/backend lifecycle. Fix the repository example's unsynchronized
direction state with bounded preallocated snapshots adopted at block boundaries;
do not introduce a callback mutex, unbounded seqlock retry, or callback-side
last-owner destruction. Validate positive fixed frame sizes, partial blocks,
tails, and effect ownership with a real SDK before claiming the example fixed.

For future coroutine integration, follow `timer_wheels` contracts:

- use one generation-bound `resume_token` per suspension and copy it to
  competing completion sources;
- a token resumes inline, so external completion must first be posted to the
  owning event loop when affinity is required;
- never retain or independently resume a raw coroutine handle;
- frame pinning does not own audio state, operation storage, buffers, or loop;
- logical cancellation and physical callback retirement are separate;
- cancellation, duplicate completion, and shutdown are idempotent;
- late/stale completion still releases its owned payload safely;
- keep the loop/worker capable of servicing retirement until callbacks and
  pending operations are quiesced.

Do not modify `timer_wheels` unless a separate framework regression proves its
documented contract is wrong. If integration code is not in this repository,
record the requirements and leave it untouched.

## Native remote-offload workflow

Use `$native-remote-offload` and `$codex-mcp-server-usage`. Use
`$win11-ssh-bdrive` for Windows paths/toolchains and
`$mac-dev-homebrew-nonadmin` for the isolated macOS environment.

Remote jobs validate exact committed source. The local miniaudio checkout is
the authoritative implementation history. Do not let Windows and macOS jobs
edit divergent live copies concurrently. If a native job discovers a required
OS-specific change, have it return a patch and red/green evidence from the
exact base commit; review/apply it locally, run local checks, commit it, then
send the new exact commit back for native validation.

Never synchronize a live dirty checkout. Prefer an existing verified native
checkout at the exact commit; otherwise use the TrueNAS Git remote or a
manually prepared exact Git bundle plus required LFS objects. Do not push as a
transfer mechanism. Verify commit and source status on the native host before
building.

For each endpoint:

1. Inspect the live Batch Codex tool inventory.
2. Run named `native-preflight` profiles when workspace, compiler, CMake/Ninja,
   SDK/framework, dependency, Git LFS, or Python paths are uncertain.
3. Record free bytes and peak storage needs before creating build/dependency
   trees.
4. Name all attempt-owned source staging, build, extraction, install, and tool
   roots and their retention policy.
5. Write a lifecycle manifest outside removable roots containing endpoint,
   job/task IDs, exact commit, owned roots, shared caches, capacity snapshot,
   evidence/hash state, retention reason/review date, and cleanup result.
6. Submit each concrete job once with an absolute working directory,
   `environmentManifest`, acceptance checks, and evidence paths under
   `${artifactDir}`. Omit model and execution-policy fields.
7. Retain the job ID and make exactly one blocking wait call. Never poll,
   resubmit, cancel for slowness, or move work to `tmux`/`nohup`. After a real
   disconnect, reconnect and make one replacement wait for the same job ID.
8. Fetch terminal evidence with `codex-artifact-fetch`, verify hashes, then
   remove only proven attempt-owned build/install/extraction/staging roots.
   Preserve source worktrees, user data, baselines, and shared verified caches.
   Record removed/retained paths and bytes recovered. If evidence fetch fails,
   retain the roots as `retained_pending_evidence` with the next action.

### Windows job

Use `windows-offload` through `WIN11-offload` as the standard-medium
`codex-worker` account. Use roots under `C:\codex-worker\repos` and
`C:\codex-worker\offload`; never access or broaden permissions on the separate
protected `C:\offload` namespace. Use PowerShell 7 for scripts. Initialize MSVC
and run Ninja in the same `cmd.exe` process. Test at least:

- MSVC Debug and optimized builds;
- Clang/clang-cl when present;
- do not install or run MinGW GCC/LTO under the current owner-rejected scope;
- scalar/SIMD variants and all deterministic core tests;
- ASan/UBSan configurations supported by the selected compiler;
- fake WASAPI lifecycle/failure tests;
- real WASAPI playback/capture, default-device reroute, exclusive capture, and
  process-loopback matrices where hardware/OS support permits;
- public structure size/alignment/offset checks before and after.

Fetch evidence through `WIN11-offload` using
`C:\Users\codex-worker\codex-artifact-fetch.cmd`. Do not use the administrator
`WIN11` alias or the legacy `windows-11` host.

### macOS job

Use `mac-offload` on `work-computer-ai-dev` and verify the isolated user is
`ai-dev`. Run relevant `native-preflight` profiles such as workspace and
`cmake-metal` to discover CMake/Ninja/Xcode/Homebrew paths, even though this is
a CoreAudio rather than Metal change. Test at least:

- Apple Clang using the active macOS SDK;
- keg-only Homebrew LLVM with explicit compiler paths when already available;
- Debug and optimized builds, scalar/SIMD variants, complete deterministic
  core tests, ASan/UBSan, and static analysis where supported;
- CoreAudio initialization/failure, default-device change, playback/capture,
  callback quiescence, and teardown tests;
- public structure size/alignment/offset checks before and after.

Do not install, upgrade, relink, modify shell startup files, or switch
Xcode/Homebrew components without separate authorization. Record unavailable
tools/devices/permissions precisely.

## Validation cadence

After each issue fix, run the narrow failing test and applicable sanitizer.
After each parser/resource-manager/DSP group, run the complete local CMake test
suite. Before final delivery, run:

- complete Linux CMake suite in Debug and optimized builds;
- ASan/UBSan parser and resource-manager suite; TSan for valid concurrency;
- GCC and Clang C/C++ builds, C++23 consumer build, C89 compatibility where
  supported, warnings-as-errors, optimized/LTO, and static analysis;
- 32-bit metadata arithmetic configuration where available;
- optional Opus/Vorbis configurations with pinned dependencies;
- Windows and macOS matrices above against the exact final commit;
- relevant `timer_wheels` Debug/sanitizer/async-interoperability tests only if
  integration or framework code is actually changed.

Do not repeat broad suites after a passing exact commit unless new changes or a
failure justify it. Do not mark unavailable native validation as passing.

## Commit sequence

Use scoped conventional local commits, staging only explicit task files. A
suggested sequence is:

1. `fix(wav): validate chunk bounds and forward progress`
2. `fix(wav): bound BEXT and SMPL metadata parsing`
3. `fix(resource-manager): make failed-load cleanup ownership-safe`
4. `fix(resource-manager): retire pending jobs before freeing resources`, only
   if separately reproduced
5. `fix(mp3): validate frame metadata before reading fields`
6. `fix(gainer): keep scalar and SIMD ramps continuous`
7. `fix(engine): drain converted frames before signaling end`
8. `fix(wasapi): defer notification rerouting to a control worker`
9. Separate `fix(coreaudio): ...` commits for each reproduced macOS defect
10. `fix(examples): synchronize Steam Audio state updates`, only with SDK
    validation
11. Conditional fixes for #1152 and #1155 after reproduction; leave #1157
    unresolved under the owner-rejected Windows MinGW scope
12. `feat(wasapi): support process-scoped loopback capture`, after correctness

Reorder only when dependencies require it. Do not combine unrelated platform,
parser, DSP, and feature changes. Never push.

## Deliverables

Provide:

- local commit list and issue-to-commit mapping;
- per-issue RED and GREEN commands/results, including parent/fixed commit IDs;
- regression-test inventory and why each test proves the behavior;
- complete Linux, Windows, and macOS validation matrices with exact compilers,
  SDK/OS versions, dependency versions, and hardware/configuration limits;
- sanitizer/static-analysis results;
- API/ABI layout comparison;
- callback real-time audit and remaining bounded risks;
- coroutine cancellation/shutdown assumptions for the future integration;
- explicit unresolved issues and why they remain unresolved;
- remote job IDs, artifact locations/hashes, lifecycle manifests, fetched
  evidence, and cleanup/retention results;
- confirmation that no test was weakened/skipped/retried, no unrelated work
  was changed, and no remote was pushed.

Persist until all locally confirmed fixes are implemented and validated and all
native jobs have reached terminal state with evidence fetched. If Windows or
macOS access is genuinely unavailable, finish every independent local task and
report the exact native blocker; do not claim the corresponding behavior fixed.

## Completed execution record

The implementation sequence is complete through `54a9ef5`. The late
saturation review added `7dc7c8e` (retained resource-manager admission records)
and `75158b8` (serialized continuation/cancellation/retirement). Strict native
builds then drove `6681823`, `a99cdc0`, `205e334`, and `06e3a06`; those commits
remove ambiguous or unreachable code and fix a real Win32 long-path cleanup
leak without changing public layouts. Exact-final macOS warnings-as-errors
coverage exposed one test-hook-only unused-function warning; `e4658df` keeps a
typed reference to the production CoreAudio initializer while preserving the
runtime injection seam. The whole-series Astra-max pass then found that the
forced-C++ CMake option changed the compiler driver's interpretation without
changing CMake's source language, so its C++23/`-Werror` matrix was
misattributed. `54a9ef5` assigns `.c` sources to CXX while retaining the
driver-extension flag, adds an actual C++23 compile guard, and removes the
test-only escaped stack pointer exposed by the corrected policy.

Final available gates are recorded in [UNRESOLVED.md](UNRESOLVED.md): exact
`54a9ef5` Linux GCC/Clang C++23 passes 14/14 with all intended compile commands
audited for C++23 and warnings-as-errors; pinned optional codecs and the
adjacent Steam Audio integration pass; Windows `06e3a06` passes strict
Debug/Release 17/17, focused clang-cl, 100/100 stress, and ABI comparison;
macOS exact `e4658df` passes Apple Clang Debug/Release and Homebrew LLVM Release
13/13, with every selected deterministic C target under `-Werror` and zero
warnings in the complete build logs. Focused C89/sanitizers, CoreAudio
lifecycle, 100/100 stress, and field-layout comparison also pass.
Physical/acoustic reports remain unresolved only where a valid signal or
permitted endpoint was unavailable. Windows MinGW/GCC remains an explicit
owner rejection, not a pass.
