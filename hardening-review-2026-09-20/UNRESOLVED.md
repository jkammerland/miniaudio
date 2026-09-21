# Hardening implementation: unresolved work and validation limits

Status date: 2026-09-21

Implementation code validated through: `a1229bf`

This is the authoritative remaining-work record for
`IMPLEMENTATION-PROMPT.md`. A hardware, toolchain, permission, or acoustic
limitation is not a pass. Conversely, an implementation defect is closed when
the source invariant and a deterministic regression establish the fix.

## Current unresolved table

| Item | Current status | Evidence obtained | What is still missing / why it remains open |
|---|---|---|---|
| #1152 WASAPI exclusive-capture duplication | **Unresolved; not reproduced on the available USB microphone** | The endpoint was explicitly selected while plugged in and accessible. Across 37 bounded cases (35 packet-instrumented), 10,481 successful `GetBuffer` calls had 10,481 full releases. The final ten-case run covered native/converted formats, 10/30 ms requested periods, and stop/start. Ratios were 0.97991-1.00458, with no outstanding acquisition, position regression, duplicate position/frame/hash tuple, cache violation, trace drop, or callback/read allocation. Exclusive WAVs were nonzero. | The reported approximately 1.44x duration did not occur. Shared-mode input remained silent in session 0, no known acoustic/frame-index watermark reached the microphone, and the reporter's Realtek/M-Audio capture devices were unavailable. Changing the packet-draining loop without a failed invariant would be speculative. |
| #1155 alleged optional-libopus UAF | **Unresolved as the alleged UAF; no valid owner-mediated UAF reproduced** | Pinned Opus/Vorbis builds cover valid, malformed, truncated, chained, seek/EOF, nonseekable, allocator/VFS/open failure, callback lifetime, and deterministic resource-manager cancellation under normal, C89-option, ASan/UBSan, and TSan configurations. Separate callback-contract defects were fixed. | The report still lacks a supported call sequence and sanitizer stack. Concurrent public read/uninit is not assumed valid. The seek/tell and EOF fixes do not prove or disprove an unrelated ownership UAF. |
| #1157 Windows GCC/MinGW LTO diagnostics | **Owner-rejected / outside accepted deployment scope** | Linux GCC 16.2.1 C/C++ Release+LTO with warnings-as-errors passes. | The owner explicitly will never deploy GCC/MinGW on Windows. No MinGW toolchain was provisioned or invoked. This is a scope rejection, not a pass or fix. Reopening requires a new owner decision or an external exact compiler/triple/flags reproducer. |
| #484 process-specific WASAPI content isolation | **Implemented; live acoustic include/exclude remains unresolved on this host** | Desktop process-loopback activation, feature/config gates, timeout-safe completion ownership, public error preservation, virtual metadata, and deterministic include/exclude/failure/timeout tests pass. The final test executes nine exact handler-ownership checkpoints and passed directly plus 100/100 independent runs. The live matrix exercised whole-system, include, exclude, target exit, nonexistent/no-stream targets, and explicit endpoint capture at 288,000 frames per case. | The whole-system positive control and every process capture were bitwise silent while producer reference WAVs were nonzero. Silence cannot prove filtering. Only one active render endpoint existed, so cross-endpoint/default-switch behavior is also unverified. Synchronous public initialization still exposes no object that can be canceled while activation is pending. |
| CoreAudio real-device behavior | **Deterministic teardown/reroute lifetime fixed; physical I/O remains unverified** | The started-device overlap test uses the actual control worker, an allocated old converter, an in-flight reroute barrier, injected restart failure, and exact old/candidate endpoint teardown. Debug, C89, supported ASan/UBSan, TSan, and 100/100 independent native stress runs pass. | Both macOS playback endpoints initialized but delivered zero callbacks in bounded probes; capture permission was denied. The full portable suite therefore timed out in two physical-device tests (12/14), and real playback/capture, hotplug, duplex, and default switching remain unverified. Apple LeakSanitizer is unsupported in this environment. |
| #1153 original audible spatialization report | **Related gain and Steam Audio synchronization defects fixed; original audible scenario not reproduced** | Independent scalar/SSE2 oracles cover split ramps, target changes, tails, guards, in/out-of-place operation, null advancement, and 1/2/6/8/33 channels. Steam Audio snapshot synchronization passes against the SDK built from adjacent source commit `902e09545ae6bfd06768bcd38980b009317641ac`. | The reported Windows listener-rotation/spatialization symptom was not reproduced acoustically. The deterministic fixes do not prove that every possible cause of the original audible report is gone. |
| 32-bit and additional architecture coverage | **Validation limitation** | Linux x86_64, Windows x64, and macOS arm64 were exercised; scalar/SSE2 and available sanitizers pass. Checked arithmetic covers the hostile metadata paths and no public layout change was made. | No usable 32-bit libc/toolchain was available, and not every architecture-specific DSP path, compiler, or packing mode was exercised. This is not universal ABI proof. |
| Steam Audio checkout in its default C++14 mode | **External dependency/toolchain limitation; miniaudio integration validated with C++17 override** | `phonon` was built from the adjacent `../steam-audio` checkout at commit `902e09545ae6bfd06768bcd38980b009317641ac`. The miniaudio fixed/partial/tail test passes in normal, C89-option, ASan/UBSan, and TSan configurations. | Steam Audio's bundled FlatBuffers selects the host Abseil `string_view`, while the checkout requests C++14 and current Abseil requires C++17. That external checkout needs its own dependency/standard fix. No Steam Audio source was changed here. |

## Optional Opus and Vorbis dependencies

These are development dependencies only for miniaudio's optional external
decoder adapters:

- Opus audio in an Ogg container uses `libogg`, `libopus`, and `opusfile`.
- Vorbis audio in an Ogg container uses `libogg`, `libvorbis`, and
  `vorbisfile`.

Core miniaudio, its built-in WAV/FLAC/MP3 decoders, device I/O, and Steam Audio
do not require them. They remain opt-in. A developer who needs those adapters
can request hash-pinned sources with:

```sh
cmake -S . -B build \
  -DMINIAUDIO_FETCH_LIBOPUS=ON \
  -DMINIAUDIO_FETCH_LIBVORBIS=ON \
  -DMINIAUDIO_REQUIRE_LIBOPUS=ON \
  -DMINIAUDIO_REQUIRE_LIBVORBIS=ON
```

Leaving both fetch options off preserves normal system-package discovery and
does not add either codec to consumers that do not use it.

## Disposition of independent-review findings

The first three-way Astra review was Batch Codex job `db5bbdbcca10ee51`.
Follow-up conversations `4a5e8abb12539c3c` (core),
`bdef102d50a76bde` (backends), and `59b6aaafd9b4440d` (release/tests) found
additional lifetime and proof gaps. No assertion was weakened to close them.

| Finding | Why it was a problem | Disposition |
|---|---|---|
| Resource-manager result publication still preceded final job access (`FR01` / later `CORE-001`) | A waiter could observe a terminal result, release the last owner, and race the worker's execution-pointer access. | **Fixed in `26ad922`.** Buffer, node-load, and node-page jobs capture their last object-dependent decision, advance the execution pointer, and only then signal notifications/fences. Barrier tests destroy the last owner at the former gap. |
| WAIT_INIT direct-job failure leaked fence acquisitions (`CORE-009`) | The direct worker path had already balanced the fences, but generic error cleanup could apply the wrong ownership rule. | **Fixed in `26ad922`.** Direct and queued paths have separate cleanup ownership; exact fence counts are asserted. |
| WASAPI notification `Release` published zero active releases too early (`FR02`) | Teardown could free the sidecar after seeing zero while `Release` still executed its hook/final sidecar access. | **Fixed in `0ab5917`.** The retirement counter decrement is the final sidecar access; teardown waits through it. A barrier holds `Release` in the formerly unsafe interval. |
| WASAPI release/quit depended on creating a fresh local event (`BR-009`) | Out-of-memory/event-creation failure could make cleanup unable to release COM services or stop its command thread. | **Fixed in `0ab5917`.** Release enqueues directly and waits on a caller event or atomic completion; quit enqueues without a local event and joins. Injected local-event failure proves ordinary commands fail while release/quit still complete exactly once. |
| Process-loopback handler cleanup oracle was vacuous (`FR03`) | Comparing only allocator totals could pass without proving that a handler/event was ever created. | **Fixed in `0ab5917`.** Tests require exactly one allocation and event, exact live ownership before late completion, and exact frees/closes afterward. Native execution passed CTest, direct, and 100/100 stress runs. |
| CoreAudio public uninit could publish terminal state during reroute reconstruction (`BR-010`) | The worker could still reconstruct or restart a device after public teardown had made it appear uninitialized. | **Fixed in `8350759`; strengthened in `959e3ab`.** Public uninit removes the device under the same tracking mutex held through reconstruction before publishing terminal state. The native test starts the device, allocates the old converter, injects restart failure, and requires exact teardown with zero tracked/live objects. |
| Generic backend-init rejection leaked `stopEvent` (`BR-011`) | `startEvent` and `wakeupEvent` were destroyed, but the already-created stop event was stranded. | **Fixed in `4970632`.** The rollback destroys stop/start/wakeup in exact order; a failure-path test counts every cleanup. |
| Preallocated job queue accepted null backing storage | Initialization could succeed with a nonzero capacity and null external storage, deferring a crash. | **Fixed in `ab9de9c`.** Nonzero preallocated capacity now requires non-null data and has a direct regression. |
| MP3 callback refill could decode through null storage after allocation failure | The callback path could continue after a failed buffer allocation/reallocation. | **Fixed in `df9ed4c`.** The decoder returns before pointer arithmetic or decode when storage is null; injected allocation failure verifies cleanup. |
| MP3/WAV/EOF proof omitted boundary and callback-fault cases (`FR04`) | Earlier tests did not distinguish before/exact/after metadata boundaries, short/failing callbacks, partial-success EOF statuses, or the full supported WAV container/codec set. | **Closed by `cfb4e89`, `cd840c6`, and `e9c62c3`.** Tests now cover Xing byte/frame boundaries, short/read/seek faults, one-frame/zero-frame/near-EOF lifecycle, full-data-plus-EOF, RIFF/RIFX/RF64/W64/AIFF, PCM/IMA/MS ADPCM, fact sizes 0-3/4/8, and bounded failed seeks. |
| Documentation conflated private and public process-loopback errors (`FR05`) | Private unsupported activation returns `MA_NOT_IMPLEMENTED`, while invalid public configurations return `MA_INVALID_DEVICE_CONFIG`; treating them as one claim obscured API behavior. | **Corrected.** The two layers and results are documented and tested separately. |
| Validation timing text looked like retry-to-green (`FR06`) | It did not state which bound applied to the original failure, diagnostic run, and later complete suite. | **Corrected.** The original 30-second failure is retained; a 36.76-second diagnostic used 180 seconds, a 40.55-second complete run used 90 seconds, and the final exact-Windows run completed generation in 39.228 seconds under 90 seconds. |
| New Windows test named `HANDLE` before Windows declarations | MSVC could not compile the process-loopback test even though production code built. | **Fixed in `a1229bf`.** Hooks use the already-public `ma_handle`. A fresh native build then passed 15/15 and the test passed directly plus 100/100 stress runs. |
| Strict optimized WAV test warned about an uninitialized diagnostic output | The compatibility helper's error path left a local used by an assertion indeterminate. | **Fixed in `346acda`.** The diagnostic is initialized and passes C89 warnings-as-errors. |

Earlier findings remain fixed by the commits listed in Git history: validated
WAV traversal/metadata bounds, resource-manager failed-load ownership, MP3
metadata arithmetic and capability propagation, gainer continuity, EOF cache
draining, transactional WASAPI/CoreAudio rerouting, Steam Audio snapshots,
optional nonseekable decoders, and process-loopback activation.

## Why the lifetime fixes matter

Completion is a public ownership boundary, not merely a status update. The
worker must make its final owner-dependent access before waking code that may
destroy that owner:

```c
/* All decisions that read pNode happen above this point. */
ma_atomic_fetch_add_32(&pNode->executionPointer, 1);

/* A waiter may release the last reference immediately after this signal. */
ma_async_notification_signal(pDone);
```

The WASAPI notification callback needs the same rule. A reference count of
zero is not sufficient if `Release` still touches the sidecar:

```c
ma_atomic_fetch_add_32(&state->releaseRetirementState, 1);
newRefCount = ma_atomic_fetch_sub_32(&client->counter, 1) - 1;
/* hook and every other state access */
ma_atomic_fetch_sub_32(&state->releaseRetirementState, 1); /* final access */
```

Cleanup must also remain possible when allocation is already failing. WASAPI
release therefore bypasses the ordinary helper that creates a temporary event:

```c
ma_context_enqueue_command__wasapi(context, &releaseCommand);
while (!ma_atomic_load_32(&releaseData.isComplete)) {
    ma_yield();
}
```

For CoreAudio, removal from the worker's tracked set is the quiescence point.
It happens before public terminal state, under the mutex the worker holds
through reconstruction:

```c
ma_device__untrack__coreaudio(device); /* waits for / excludes reroute */
ma_device__set_state(device, ma_device_state_uninitialized);
```

## Validation matrix through `a1229bf`

| Environment | Result | Scope / qualification |
|---|---|---|
| Linux GCC 16.2.1 Debug | **13/13 pass** | Complete ordinary registered suite. |
| Linux GCC 16.2.1 Release and strict C89 option | **13/13 and 12/12 pass** | C++ test is intentionally absent in C89 mode. |
| Linux GCC Release + LTO + warnings-as-errors | **13/13 pass** | Not Windows MinGW validation. |
| Linux Clang 22.1.8 C90 + C++23 + warnings-as-errors | **13/13 pass** | Complete ordinary registered suite. |
| Forced C++23 implementation | **3/3 focused pass** | Resource manager, MP3, and EOF hardening targets. |
| Pinned Opus/Vorbis Debug and C89 option | **15/15 and 14/14 pass** | libogg 1.3.6, libopus 1.6.1, opusfile 0.12, libvorbis commit `1b75110b...`. |
| Core ASan/UBSan | **6/6 focused pass** | WAV, WAV metadata, resource manager, MP3, EOF, and generic device rollback. |
| Optional-codec ASan/UBSan and TSan | **2/2 + 2/2 pass** | Opus and Vorbis adapters. |
| Core TSan | **2/2 focused pass** | Resource manager and generic device rollback. |
| Steam Audio real SDK | **pass in normal, C89-option, ASan/UBSan, and TSan** | Adjacent source build with C++17 override; no Steam Audio source edit. |
| macOS arm64 Apple Clang 21 | **focused CoreAudio pass in Debug, C89, supported ASan/UBSan, TSan, and 100/100 stress** | Complete Debug suite was 12/14 because physical `cpp`/`deviceio` tests timed out with zero playback callbacks. Capture permission denied. LeakSanitizer unsupported. |
| Windows x64 MSVC 19.51 / SDK 10.0.26100.0 | **15/15 pass at `a1229bf`; process-loopback 100/100; lifecycle, UWP C/C++ objects, and desktop C/C++ links pass** | At byte-identical production code `346acda`, ten microphone cases passed packet invariants and exclusive WAVs were nonzero; live loopback control stayed silent. The first `346acda` build exposed the test-only premature-`HANDLE` defect, retained as red evidence and fixed by `a1229bf`. |

No assertion, tolerance, or semantic test condition was weakened. Unsupported
Apple leak detection and unavailable physical audio are recorded as limits,
not converted to passes.

## Audio evidence available for manual inspection

- Final #1152 microphone captures:
  `/tmp/miniaudio-native-win-346acda-evidence/exclusive-reported.wav`,
  `exclusive-native.wav`, `shared-reported.wav`, and `shared-native.wav`.
  The exclusive files are nonzero; the shared files are silent.
- Final #484 captures and producer references are in
  `/tmp/miniaudio-native-win-346acda-evidence/live-*.wav`. Capture files are
  silent; `*.reference.wav` files contain the generated tone pairs.
- Earlier #1152 and #484 evidence remains in
  `/tmp/miniaudio-1152-20260921-1-evidence` and
  `/tmp/miniaudio-0484-2d76179-evidence`.

## Evidence and lifecycle status

- Final Windows live/native transport manifest:
  `b70ae331733eddcee85200a1acec3d0fdb06dfda54fc77c20c5178c04aa09502`
  (392 files; internal manifest verified 391 entries).
- Final Windows corrected-test transport manifest:
  `61ff5271b54a6e6ac941b9837b10c813d08876afd86fc3db3afe97ca9ce80423`
  (172 files; internal manifest verified 171 entries).
- Final macOS transport manifest:
  `57a64d7d9750045e8c975da788fb8b6d65136bb0df5f65c212ee49308f0ad9bc`
  (168 files; internal manifest verified 167 entries).
- Earlier #1152, #484, and WASAPI transport manifests:
  `bc3cb9dcab8632dd7cafbfbf5ea0cde2bc95f772dce45cbac4947ceb9163a5b1`,
  `ea28b648156ad4fe841f67f241169236d6cf9595d46d5a73265477e5ef73b335`,
  `6523c24feb934718efdd965ff2fac2e833a7f990c11e1278f4fc7baae91f450e`,
  and `7d57c79648db7d116ee62d368269249223bd8860a44cee2e426dc023dd62356d`.

After retrieval and hash verification, exact attempt-owned roots were removed:
864,251,904 bytes from the final Windows attempts, 723,202,048 bytes from the
earlier WASAPI build/staging roots, and 833,662,976 bytes from the final macOS
attempt. Clean source worktrees and server artifact roots were preserved.

No `timer_wheels` source was modified. Future coroutine integration must keep
logical cancellation separate from physical callback retirement, post
affinity-bound completion before inline resume, retain one generation-bound
resume token per suspension, and never retain or independently resume a raw
coroutine handle.

## Remaining release gate

The implementation and native matrices above are complete. The last gate is
the owner-requested Astra-max review of the entire current series and this
reconciled evidence. Any evidence-backed finding from that review must be
fixed and revalidated before handoff; the unresolved table's hardware,
acoustic, owner-scope, and external-dependency limits must remain explicit.
