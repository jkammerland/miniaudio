# Hardening implementation: unresolved work and validation limits

Status date: 2026-09-21

Implementation code validated through: `fee518b`

This is the authoritative remaining-work record for
`IMPLEMENTATION-PROMPT.md`. A hardware, toolchain, permission, or acoustic
limitation is not a pass. Conversely, an implementation defect is closed when
the source invariant and a deterministic regression establish the fix.

## Current unresolved table

| Item | Current status | Evidence obtained | What is still missing / why it remains open |
|---|---|---|---|
| #1152 WASAPI exclusive-capture duplication | **Unresolved; not reproduced on the available USB microphone** | The endpoint was explicitly selected while plugged in and accessible. Across 49 bounded cases (47 packet-instrumented), 13,816 successful `GetBuffer` calls had 13,816 full releases. The exact-`fee518b` campaign added 12 shared/exclusive native/reported, 10/30 ms, and stop/start cases; every duration ratio was 0.977349-1.000927, with no outstanding acquisition, position regression, duplicate position/frame/hash tuple, cache violation, trace drop, or callback/read allocation. Exclusive WAVs were nonzero. | The reported approximately 1.44x duration did not occur. Shared-mode input remained silent in session 0, no known acoustic/frame-index watermark reached the microphone, and the reporter's Realtek/M-Audio capture devices were unavailable. Changing the packet-draining loop without a failed invariant would be speculative. |
| #1155 alleged optional-libopus UAF | **Unresolved as the alleged UAF; no valid owner-mediated UAF reproduced** | Pinned Opus/Vorbis builds cover valid, malformed, truncated, chained, seek/EOF, nonseekable, allocator/VFS/open failure, callback lifetime, and deterministic resource-manager cancellation under normal, C89-option, ASan/UBSan, and TSan configurations. Separate callback-contract defects were fixed. | The report still lacks a supported call sequence and sanitizer stack. Concurrent public read/uninit is not assumed valid. The seek/tell and EOF fixes do not prove or disprove an unrelated ownership UAF. |
| #1157 Windows GCC/MinGW LTO diagnostics | **Owner-rejected / outside accepted deployment scope** | Linux GCC 16.2.1 C/C++ Release+LTO with warnings-as-errors passes. | The owner explicitly will never deploy GCC/MinGW on Windows. No MinGW toolchain was provisioned or invoked. This is a scope rejection, not a pass or fix. Reopening requires a new owner decision or an external exact compiler/triple/flags reproducer. |
| #484 process-specific WASAPI content isolation | **Implemented; live acoustic include/exclude remains unresolved on this host** | Desktop process-loopback activation, feature/config gates, timeout-safe completion ownership, public error preservation, virtual metadata, and deterministic include/exclude/failure/timeout tests pass. At exact commit `fee518b`, the test passed directly plus 100/100 independent runs. Nine named-event live scenarios exercised whole-system control, include, exclude, descendant inclusion, target exit, nonexistent/no-stream targets, and explicit-render selection at 288,000 frames per case; all producers crossed their thresholds and reaped cleanly. | The whole-system positive control and every process capture were bitwise silent while distinct producer reference WAVs were nonzero. Silence cannot prove filtering. Only one active render endpoint existed, so cross-endpoint/default-switch behavior is also unverified. Synchronous public initialization still exposes no object that can be canceled while activation is pending. |
| CoreAudio real-device behavior | **Deterministic teardown/reroute lifetime fixed; physical I/O remains unverified** | The started-device overlap test uses the actual control worker, an allocated old converter, an in-flight reroute barrier, injected restart failure, and exact old/candidate endpoint teardown. Debug, C89, supported ASan/UBSan, TSan, and 100/100 independent native stress runs pass. | Both macOS playback endpoints initialized but delivered zero callbacks in bounded probes; capture permission was denied. The full portable suite therefore timed out in two physical-device tests (12/14), and real playback/capture, hotplug, duplex, and default switching remain unverified. Apple LeakSanitizer is unsupported in this environment. |
| #1153 original audible spatialization report | **Related gain and Steam Audio synchronization defects fixed; original audible scenario not reproduced** | Independent scalar/SSE2 oracles cover split ramps, target changes, tails, guards, in/out-of-place operation, null advancement, and 1/2/6/8/33 channels. Steam Audio snapshot synchronization passes against the SDK built from adjacent source commit `902e09545ae6bfd06768bcd38980b009317641ac`. | The reported Windows listener-rotation/spatialization symptom was not reproduced acoustically. The deterministic fixes do not prove that every possible cause of the original audible report is gone. |
| 32-bit and additional architecture coverage | **Validation limitation** | Linux x86_64, Windows x64, and macOS arm64 were exercised; scalar/SSE2 and available sanitizers pass. Checked arithmetic covers the hostile metadata paths and no public layout change was made. | No usable 32-bit libc/toolchain was available, and not every architecture-specific DSP path, compiler, or packing mode was exercised. This is not universal ABI proof. |
| Steam Audio checkout in its default C++14 mode | **External dependency/toolchain limitation; miniaudio integration validated with C++17 override** | `phonon` was built from the adjacent `../steam-audio` checkout at commit `902e09545ae6bfd06768bcd38980b009317641ac`. The miniaudio fixed/partial/tail test passes in normal, C89-option, ASan/UBSan, and TSan configurations. | Steam Audio's bundled FlatBuffers selects the host Abseil `string_view`, while the checkout requests C++14 and current Abseil requires C++17. That external checkout needs its own dependency/standard fix. No Steam Audio source was changed here. |

## Optional Opus and Vorbis dependencies

These are development dependencies only for miniaudio's optional external
decoder adapters. **You do not need them unless you intend to build or test
those two adapters.** They are not audio-device, WAV/MP3, or Steam Audio
dependencies:

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

The hash-pinned FetchContent path is used for reproducible development and CI
coverage; system packages found through normal CMake discovery remain valid,
so vcpkg or Conan can also supply the libraries without changing miniaudio.
The `REQUIRE` switches deliberately fail configuration if a requested adapter
cannot be tested. Leaving both fetch options off performs no download and does
not add either codec to consumers that do not use it.

## Disposition of independent-review findings

The first three-way Astra review was Batch Codex job `db5bbdbcca10ee51`.
Follow-up conversations `4a5e8abb12539c3c` (core),
`bdef102d50a76bde` (backends), and `59b6aaafd9b4440d` (release/tests) found
additional lifetime and proof gaps. No assertion was weakened to close them.
The pre-`fee518b` final continuations `084376fdca0e83bd` (core),
`172e0a7282c3d69b` (backends), and `966c74b232430999` (release/tests)
then found the remaining items called out below.

| Finding | Why it was a problem | Disposition |
|---|---|---|
| Resource-manager result publication still preceded final job access (`FR01` / later `CORE-001`) | A waiter could observe a terminal result, release the last owner, and race the worker's execution-pointer access. | **Fixed in `26ad922`.** Buffer, node-load, and node-page jobs capture their last object-dependent decision, advance the execution pointer, and only then signal notifications/fences. Barrier tests destroy the last owner at the former gap. |
| WAIT_INIT direct-job failure could over-release its init fence (`CORE-009`, reopened) | Connector initialization had consumed the init-stage ownership, but a failed continuation post left the done stage with the caller. Treating both stages as one boolean released the init fence twice. | **Fixed in `fee518b`.** Init and done stages have separate ownership. The saturated inline test keeps a sentinel init-fence reference and proves the failed repost releases only the still-owned done stage, with exact notification counts. |
| Queue-full posts consumed execution order before publication (`CORE-010`) | Initial and continuation page jobs incremented an object's execution counter before asking the bounded queue for a slot. A failed post left an order that no job could retire, so teardown could wait forever. | **Fixed in `fee518b`.** The queue reserves a slot first and only then atomically assigns the order to the queued copy. Initial/continuation post-failure tests and the `UINT32_MAX` wrap test prove no hole is created. |
| Result-poll regression could join before the free job was published (`CORE-011`) | Its barrier acknowledged a pre-post hook, so the only manual pump could run on an empty queue and then join an uninit thread waiting for that pump. | **Fixed in `fee518b`.** A separate post-publication acknowledgment now orders the one manual pump after enqueue; an explicit barrier still forces the former gap without sleeps or retries. |
| Full resource-manager queues could orphan last-reference retirement (`FR07`) | Once a node/buffer was removed and marked unavailable, failure to enqueue its FREE job could discard the only cleanup path. Propagating an error was insufficient because public ownership had already been surrendered. | **Fixed in `fee518b`.** Retirement posts retain ownership and make a slot in manual-pump mode or yield to workers until publication succeeds; order is allocated only on success. Saturated unregister and buffer-uninit regressions require exact final frees and zero live supply. |
| Nested WAV LIST children were counted before their parent bounds were proved (`CORE-012`) | A child could declare a huge payload inside a tiny LIST. The count pass could inflate metadata allocation before later traversal noticed the enclosing extent was impossible. | **Fixed in `fee518b`.** Child ID, size, payload, and internal pad must all fit the parent before counting, allocating, reading, or seeking. A reject-all allocator proves malformed known, unknown, label, truncated-header, and missing-pad inputs make zero allocation attempts. |
| WASAPI notification `Release` published zero active releases too early (`FR02`) | Teardown could free the sidecar after seeing zero while `Release` still executed its hook/final sidecar access. | **Fixed in `0ab5917`.** The retirement counter decrement is the final sidecar access; teardown waits through it. A barrier holds `Release` in the formerly unsafe interval. |
| WASAPI cleanup command acceptance was not transactional (`BR-009`, reopened) | The ring stored a stack-backed command before releasing its semaphore. If semaphore release failed, the caller saw rejection while the ring retained its pointers. A wait error could also return before worker retirement, and quit failure allowed generic cleanup to destroy synchronization under a live worker. | **Fixed in `fee518b`.** The wake token is released under the queue lock before the ring commit; failure leaves count/index unchanged. Accepted waits retain stack storage through worker completion, release/quit retry publication, and quit always joins before sync teardown. Injected native counters prove the failure, retry, and exact COM-release paths; lifecycle passes directly and 100/100. |
| Process-loopback handler cleanup oracle was vacuous (`FR03`) | Comparing only allocator totals could pass without proving that a handler/event was ever created. | **Fixed in `0ab5917`.** Tests require exactly one allocation and event, exact live ownership before late completion, and exact frees/closes afterward. Native execution passed CTest, direct, and 100/100 stress runs. |
| CoreAudio public uninit could publish terminal state during reroute reconstruction (`BR-010`) | The worker could still reconstruct or restart a device after public teardown had made it appear uninitialized. | **Fixed in `8350759`; strengthened in `959e3ab`.** Public uninit removes the device under the same tracking mutex held through reconstruction before publishing terminal state. The native test starts the device, allocates the old converter, injects restart failure, and requires exact teardown with zero tracked/live objects. |
| Generic backend-init rejection leaked `stopEvent` (`BR-011`) | `startEvent` and `wakeupEvent` were destroyed, but the already-created stop event was stranded. | **Fixed in `4970632`.** The rollback destroys stop/start/wakeup in exact order; a failure-path test counts every cleanup. |
| Preallocated job queue accepted null backing storage | Initialization could succeed with a nonzero capacity and null external storage, deferring a crash. | **Fixed in `ab9de9c`.** Nonzero preallocated capacity now requires non-null data and has a direct regression. |
| MP3 callback refill could decode through null storage, and its release record overclaimed OOM proof (`FR08`) | `df9ed4c` added a valid missing-storage guard, but its original synthetic test did not actually invoke a failing allocator or prove cleanup after retained-buffer growth failure. | **Completed in `fee518b`.** The synthetic invariant remains distinct. New counted callbacks reject the initial allocation and a real growth realloc while one buffer is live; no read occurs, the original pointer/capacity survive, and final allocation/free counts balance exactly. |
| MP3/WAV/EOF proof omitted boundary and callback-fault cases (`FR04`) | Earlier coverage still omitted the named RIFX/W64 fact boundaries, W64 totals below 24, short AIFF fixed headers, very large `fmt` remainders, and a valid large unknown chunk. | **Completed in `fee518b` after the earlier `cfb4e89`, `cd840c6`, and `e9c62c3` work.** Sparse callbacks cover `INT_MAX`, `INT_MAX+1`, and `UINT32_MAX` without huge allocation. RIFX fact 0-4, W64 fact 0-8 plus padded/extended valid forms, W64 total 0-23, AIFF COMM/SSND short headers, and a valid large unknown chunk are asserted. The independent W64 fixtures also exposed and fixed the padding formula to round up to the next 8-byte boundary. |
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

Publication itself must be transactional. The WASAPI consumer cannot observe
the ring while the producer holds `commandLock`, so the wake token can be
committed first; only success makes the command and its stack pointers visible:

```c
ma_mutex_lock(&context->wasapi.commandLock);
result = ma_semaphore_release(&context->wasapi.commandSem);
if (result == MA_SUCCESS) {
    context->wasapi.commands[index] = command;
    context->wasapi.commandCount += 1;
}
ma_mutex_unlock(&context->wasapi.commandLock);
```

Cleanup also cannot abandon an accepted stack-backed command after a wait
error. Release retains its payload until the worker's final access:

```c
do {
    result = ma_context_enqueue_command__wasapi(context, &releaseCommand);
} while (result != MA_SUCCESS);
while (!ma_atomic_load_32(&releaseData.isComplete)) {
    ma_yield();
}
```

The resource-manager queue follows the same commit rule. It reserves a slot
before consuming an execution order, so a full queue cannot create a number
that no job will ever retire:

```c
slot = ma_job_queue_allocate_slot(queue);
if (slot == MA_JOB_ID_NONE) {
    return MA_OUT_OF_MEMORY;
}
queue->pJobs[index] = job;
queue->pJobs[index].order = ma_atomic_fetch_add_32(counter, 1);
/* publication cannot fail after this point */
```

For CoreAudio, removal from the worker's tracked set is the quiescence point.
It happens before public terminal state, under the mutex the worker holds
through reconstruction:

```c
ma_device__untrack__coreaudio(device); /* waits for / excludes reroute */
ma_device__set_state(device, ma_device_state_uninitialized);
```

## Validation matrix through `fee518b`

| Environment | Result | Scope / qualification |
|---|---|---|
| Linux GCC 16.2.1 Debug | **13/13 pass** | Complete ordinary registered suite. |
| Linux GCC 16.2.1 Release and strict C89 option | **13/13 and 14/14 pass** | The C89-option matrix includes pinned Opus/Vorbis adapters; C++ tests are intentionally absent. |
| Linux GCC Release + LTO + warnings-as-errors | **13/13 pass** | Not Windows MinGW validation. |
| Linux Clang 22.1.8 C90 + C++23 + warnings-as-errors | **13/13 pass** | Complete ordinary registered suite. |
| Forced C++23 implementation | **13/13 pass** | Complete ordinary registered suite with implementation compiled as C++23. |
| Pinned Opus/Vorbis Debug and C89 option | **15/15 and 14/14 pass** | libogg 1.3.6, libopus 1.6.1, opusfile 0.12, libvorbis commit `1b75110b...`. |
| Core ASan/UBSan | **6/6 focused pass** | WAV, WAV metadata, resource manager, MP3, EOF, and generic device rollback. |
| Optional-codec ASan/UBSan and TSan | **2/2 + 2/2 pass** | Opus and Vorbis adapters. |
| Core TSan | **2/2 focused pass** | Resource manager and engine EOF. |
| Steam Audio real SDK | **pass in normal, C89-option, ASan/UBSan, and TSan** | Adjacent source build with C++17 override; no Steam Audio source edit. |
| macOS arm64 Apple Clang 21 | **focused CoreAudio pass in Debug, C89, supported ASan/UBSan, TSan, and 100/100 stress** | Complete Debug suite was 12/14 because physical `cpp`/`deviceio` tests timed out with zero playback callbacks. Capture permission denied. LeakSanitizer unsupported. |
| Windows x64 MSVC 19.51 / SDK 10.0.26100.0 | **15/15 pass at exact `fee518b`; lifecycle 100/100; process-loopback 100/100; UWP C/C++ objects and desktop C/C++ context links pass** | Twelve explicitly selected USB microphone cases passed ownership/duration invariants; exclusive WAVs were nonzero and shared WAVs silent. Nine synchronized loopback scenarios delivered exact frames but all captures, including whole-system control, were silent. |

No assertion, tolerance, or semantic test condition was weakened. Unsupported
Apple leak detection and unavailable physical audio are recorded as limits,
not converted to passes.

## Audio evidence available for manual inspection

- Exact-`fee518b` #1152 microphone captures are under
  `/tmp/miniaudio-native-win-fee518b-evidence/microphone/`. Start with
  `exclusive-reported-10ms.wav` (nonzero) and
  `shared-reported-10ms.wav` (silent); all 12 WAVs and per-case metrics remain
  available.
- Exact-`fee518b` #484 captures and producer references are under
  `/tmp/miniaudio-native-win-fee518b-evidence/loopback/`. Compare
  `control.wav.role0.reference.wav` (nonzero producer data) with
  `control.wav` (silent capture), then the equivalent `include*`, `exclude*`,
  and `child*` files. Manual listening can confirm file content, but cannot
  establish isolation until the whole-system capture control is nonzero.
- Earlier #1152 and #484 evidence remains in
  `/tmp/miniaudio-1152-20260921-1-evidence` and
  `/tmp/miniaudio-0484-2d76179-evidence`.

## Evidence and lifecycle status

- Exact-`fee518b` Windows transport manifest:
  `4f40ae7b301cf21f43436efd99e41d97f21465652cf8631d2c0304ba5911e52a`
  (571 files; internal manifest verified all 570 entries; 110 WAVs). Cleanup
  continuation manifest:
  `5ca812773b3a4987b3f4415db6d3dadce61bdda0407cb437ff36970abd452f06`.
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
717,677,709 logical bytes from the exact-`fee518b` Windows attempt,
864,251,904 bytes from the prior final Windows attempts, 723,202,048 bytes from
the earlier WASAPI build/staging roots, and 833,662,976 bytes from the final
macOS attempt. Clean source worktrees and server artifact roots were preserved.

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
