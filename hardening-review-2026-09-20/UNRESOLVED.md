# Hardening implementation: unresolved work and validation limits

Status date: 2026-09-23

Implementation and test/build code validated through: `54a9ef5`

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
| CoreAudio real-device behavior | **Deterministic teardown/reroute lifetime fixed; physical I/O remains unverified** | At exact final commit `e4658df`, Apple Clang Debug and optimized Release plus Homebrew LLVM optimized Release each pass 13/13 deterministic tests with warnings-as-errors and zero warnings. Strict C89 3/3, ASan+UBSan 3/3, TSan 1/1, the deterministic CoreAudio lifecycle, 100/100 independent resource-manager runs, and an arm64 field-layout comparison also pass. | The isolated account's prior playback endpoints initialized but delivered zero callbacks in bounded probes; capture permission was denied. Real playback/capture, hotplug, duplex, and default switching therefore remain unverified. Apple LeakSanitizer is unsupported in this environment. |
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

A later saturation-focused pass found that retrying an admitted cleanup was
still unsafe when the sole worker itself encountered a full queue. Commits
`7dc7c8e` and `75158b8` replace that retry model with retained admission
records and serialize cancellation, continuation publication, and last-owner
retirement. Astra-max continuations `399b8c7d33d2e6ef` (core) and
`74cea9479359e715` (backend/release) independently approved exact `75158b8`
with no Critical, High, or Medium findings. Native strict builds subsequently
exposed the warning and Win32 filesystem items also recorded below.

The whole-series Astra-max continuation `c34d44bdf141b742` then reviewed exact
`754f852`. It found no production correctness defect, but blocked the release
record on one Medium build-attribution error and two Low evidence-scope errors.
Commit `54a9ef5` fixes the language-mode implementation and its newly exposed
strict diagnostic; this reconciliation narrows the two documentation claims.

| Finding | Why it was a problem | Disposition |
|---|---|---|
| Resource-manager result publication still preceded final job access (`FR01` / later `CORE-001`) | A waiter could observe a terminal result, release the last owner, and race the worker's execution-pointer access. | **Fixed in `26ad922`.** Buffer, node-load, and node-page jobs capture their last object-dependent decision, advance the execution pointer, and only then signal notifications/fences. Barrier tests destroy the last owner at the former gap. |
| WAIT_INIT direct-job failure could over-release its init fence (`CORE-009`, reopened) | Connector initialization had consumed the init-stage ownership, but a failed continuation post left the done stage with the caller. Treating both stages as one boolean released the init fence twice. | **Fixed in `fee518b`.** Init and done stages have separate ownership. The saturated inline test keeps a sentinel init-fence reference and proves the failed repost releases only the still-owned done stage, with exact notification counts. |
| Queue-full posts consumed execution order before publication (`CORE-010`) | Initial and continuation page jobs incremented an object's execution counter before asking the bounded queue for a slot. A failed post left an order that no job could retire, so teardown could wait forever. | **Fixed in `fee518b`.** The queue reserves a slot first and only then atomically assigns the order to the queued copy. Initial/continuation post-failure tests and the `UINT32_MAX` wrap test prove no hole is created. |
| Result-poll regression could join before the free job was published (`CORE-011`) | Its barrier acknowledged a pre-post hook, so the only manual pump could run on an empty queue and then join an uninit thread waiting for that pump. | **Fixed in `fee518b`.** A separate post-publication acknowledgment now orders the one manual pump after enqueue; an explicit barrier still forces the former gap without sleeps or retries. |
| Full resource-manager queues could orphan last-reference retirement (`FR07`) | Once a node/buffer was removed and marked unavailable, failure to enqueue its FREE job could discard the only cleanup path. Propagating an error was insufficient because public ownership had already been surrendered. | **Fixed in `fee518b`.** Retirement posts retain ownership and make a slot in manual-pump mode or yield to workers until publication succeeds; order is allocated only on success. Saturated unregister and buffer-uninit regressions require exact final frees and zero live supply. |
| A sole worker could deadlock while retrying cleanup into its own full queue | The worker that had to consume the next slot could spin/yield inside a FREE/continuation post, so no thread remained able to make capacity. | **Fixed in `7dc7c8e`.** Resource-manager queues use private retained admission records. A running record transitions directly back to READY for a deferral/continuation, while externally initiated retirement carries its own retained record. Saturation tests cover sole-worker nested cleanup without sleeps or retries. |
| An accepted deferral or continuation could be lost under saturation | The current job's admission was retired before the follow-up had durable capacity. A failed post could therefore return an error after the operation had already been accepted, with no job left to complete it. | **Fixed in `7dc7c8e`.** The accepted operation keeps or transfers the same stable record under the queue lock; it cannot fall through the bounded generic queue. The all-site saturation matrix proves retained record/code/order transport; separate live-owner tests execute terminal handlers and prove balanced ownership and eventual cleanup. |
| Last-reference FREE could be ordered before a later page continuation (`CORE-014`) | The node state check, continuation order allocation, and FREE publication were separate operations. A page job could observe BUSY, lose the race to retirement, and then allocate an order after FREE, allowing the node to be freed first. | **Fixed in `75158b8`.** The queue lock now atomically checks required state and publishes the continuation/order, or publishes `MA_UNAVAILABLE` with the FREE order. A losing continuation destroys its decoder locally and never touches the retired node again. |
| Shutdown cancellation returned stale caller job data (`CORE-015` / `BR-012`) | `ma_job_queue_next()` returned `MA_CANCELLED` after draining but left the caller's previous `ma_job` contents intact, so code inspecting the output could see and potentially reuse a stale operation. | **Fixed in `75158b8`.** The shutdown path writes a freshly initialized QUIT job before returning cancellation. Direct and worker-thread tests seed hostile sentinels and require the QUIT output. |
| Invalid dequeued dispatch could strand a retained admission (`CORE-016`) | A corrupted or feature-disabled job code returned before the tracked completion path, leaving its private record RUNNING forever and reducing queue capacity permanently. | **Fixed in `75158b8`.** Dispatchability is rejected before admission, and a dequeued job mutated afterward explicitly releases its retained record before returning `MA_INVALID_OPERATION`. |
| `MA_NO_RESOURCE_MANAGER` strict builds saw resource-manager-only helpers (`BR-013`) | Private continuation/retirement helpers were compiled even when the resource manager was disabled, producing unused-static diagnostics under warnings-as-errors. | **Fixed in `75158b8`.** Resource-manager-only helpers are feature-guarded, and a dedicated `MA_NO_RESOURCE_MANAGER` implementation test compiles and runs in C and forced C++ modes. |
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
| Validation timing text looked like retry-to-green (`FR06`) | It did not state which bound applied to the original failure, diagnostic run, and later complete suite. | **Corrected.** The original 30-second failure is retained; a 36.76-second diagnostic used 180 seconds, a 40.55-second complete run used 90 seconds, exact `a1229bf` completed generation in 39.228 seconds, and exact `fee518b` completed it in 38.415 seconds under the same 90-second bound. |
| New Windows test named `HANDLE` before Windows declarations | MSVC could not compile the process-loopback test even though production code built. | **Fixed in `a1229bf`.** Hooks use the already-public `ma_handle`. A fresh native build then passed 15/15 and the test passed directly plus 100/100 stress runs. |
| Strict optimized WAV test warned about an uninitialized diagnostic output | The compatibility helper's error path left a local used by an assertion indeterminate. | **Fixed in `346acda`.** The diagnostic is initialized and passes C89 warnings-as-errors. |
| WAV implementation shadowed its outer stream-size variable under MSVC (`C4456`) | A nested signed `fileSize` declaration hid the outer stream size. The scopes were behaviorally distinct, but `/W4 /WX` correctly made the ambiguity release-blocking. | **Fixed in `6681823`.** The inner value is named `streamSizeSigned`; behavior and public layout are unchanged. |
| Optimized MSVC exposed assertion-elided and potentially uninitialized diagnostics | `pContext` was used only by an assertion removed under `NDEBUG`; short-circuit test diagnostics could read uninitialized sentinels; and `external/fs` reused the name `result` in a nested scope. | **Fixed in `a99cdc0`.** The release-only parameter is explicitly consumed, test sentinels are initialized, and the nested filesystem result is named `mkdirResult`. Assertions and warning levels were not weakened. |
| Successful Win32 removal leaked long-path conversion storage | `fs_remove()` returned immediately after `RemoveDirectoryW()` succeeded, bypassing `fs_win32_path_uninit()`. Paths longer than the stack buffer therefore leaked their heap-backed UTF-16 conversion on every successful removal. | **Fixed in `205e334`.** Success now flows through the common cleanup label. A Windows-only counted-allocator regression creates/removes a path longer than 255 characters and requires the live allocation count to return to zero; the parent leaks one allocation. The same commit initializes the standard-path `HANDLE` and removes a now-unreachable fallback that strict MSVC diagnosed. |
| Path comparison ended with unreachable code under strict Debug MSVC (`C4702`) | `fs_path_compare()` has an unconditional `for (;;)` whose branches all return, so the trailing fallback return could never execute. | **Fixed in `06e3a06`.** The unreachable statement was removed; semantics are unchanged, and both Debug and optimized Release build with `/W4 /WX` and zero warnings. |
| The CoreAudio lifecycle hook hid the production initializer from unused-function analysis | The test replaces the initializer call site to inject failures. The real static initializer was still compiled but had no reference in that translation unit, producing an Apple/Homebrew Clang warning; simply compiling it out exposed five dependent helpers as unused and would have reduced implementation coverage. | **Fixed in `e4658df`.** The test keeps a typed reference to the production initializer while still routing runtime calls through the hook. Exact-final Apple Clang Debug/Release and Homebrew LLVM Release now build the full deterministic suite with `-Werror` and zero warnings. |
| Forced-C++ CMake mode did not apply the requested C++ standard or C++ warning policy (`FINAL-01`) | `-x c++` changed the compiler's interpretation of `.c` files, but CMake still modeled them as C. Consequently, `CMAKE_CXX_STANDARD=23` and `CMAKE_CXX_FLAGS=-Werror` reached only the two native `.cpp` files; 19 `.c` commands used the compiler's default C++20 mode without `-Werror`, even though all 14 runtime tests passed. | **Fixed in `54a9ef5`.** Every miniaudio-owned `.c` source is now assigned CMake language `CXX` while retaining the driver flag needed for `.c` extensions. Exact-commit GCC 16 and Clang 22 each pass 14/14; both command inventories prove 19/19 `.c` and 2/2 `.cpp` actions use C++23 and `-Werror`. A compile-time guard rejects C and C++20. |
| The corrected C++23 policy exposed an escaped stack address in a resource-manager test hook | The fault injector stored a stack-backed notification address in global state solely to recognize invalid wait/uninit calls. GCC's C++23 dangling-pointer analysis diagnosed the escape under the newly effective `-Werror` policy. | **Fixed in `54a9ef5`.** The hook writes a poison callback into the still-live notification object and recognizes that in-object marker instead. The same invalid-use counters and assertions remain; C, C89, C++23, ASan/UBSan, and TSan resource-manager checks pass. |
| The macOS matrix said every full-build target used `-Werror` (`FINAL-02`) | The three exact `e4658df` compile databases show `-Werror` on all 20 C translation units, including every selected deterministic test, but not on the two `.cpp` translation units. Complete logs still contain zero warnings. | **Documentation corrected.** The native result is now stated at its proven scope: all selected deterministic C targets used `-Werror`, and the complete builds emitted zero warnings. No physical-device result is inferred. |
| The resource-manager dossier attributed terminal cleanup ownership to every saturated-deferral case (`FINAL-03`) | The all-site matrix deliberately tests admission/guard transport with synthetic, order-mismatched jobs and retires their records through the queue helper. Those cases do not construct decoder/VFS/fence owners or execute every terminal handler. | **Documentation corrected.** The transport matrix is credited only with retained record/code/order preservation. Separate live-owner continuation, cancellation, node/buffer retirement, fence, and shutdown tests carry the end-to-end cleanup claims. |

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

The resource-manager queue follows the same commit rule, but its final design
also keeps accepted work alive when the bounded queue is saturated. A running
record can become the next READY record under one lock, so the sole worker
never waits for itself to create capacity:

```c
ma_spinlock_lock(&queuePrivate->lock);
if (requiredState != NULL && *requiredState != expectedState) {
    ma_spinlock_unlock(&queuePrivate->lock);
    return MA_SUCCESS; /* retirement won; caller owns local cleanup */
}
ma_job_queue_prepare_record_job(record, &nextJob, executionCounter);
ma_job_queue_append_ready_record_locked(queuePrivate, record);
currentJob.next = 0; /* ownership transferred to the READY record */
ma_spinlock_unlock(&queuePrivate->lock);
```

Last-reference retirement uses the same lock to publish `MA_UNAVAILABLE` and
allocate the FREE order. That prevents a page continuation from receiving an
order after FREE. Invalid codes are rejected before admission; if a dequeued
job is corrupted later, its retained record is still retired before the error
is returned.

For CoreAudio, removal from the worker's tracked set is the quiescence point.
It happens before public terminal state, under the mutex the worker holds
through reconstruction:

```c
ma_device__untrack__coreaudio(device); /* waits for / excludes reroute */
ma_device__set_state(device, ma_device_state_uninitialized);
```

## Validation matrix through `54a9ef5`

| Environment | Result | Scope / qualification |
|---|---|---|
| Linux GCC 16.2.1 with pinned Opus/Vorbis | **16/16 Debug pass; 15/15 strict-C89 option pass** | libogg 1.3.6, libopus 1.6.1, opusfile 0.12, and libvorbis commit `1b75110b...`; the C89-option build intentionally omits C++ tests. |
| Linux GCC Release + LTO + warnings-as-errors | **exact `54a9ef5`: 14/14 pass** | This is native Linux GCC, not Windows MinGW validation. |
| Linux Clang 22.1.8 warnings-as-errors | **exact `54a9ef5`: 14/14 pass** | Complete registered non-optional suite. |
| Forced C++23 implementation | **exact `54a9ef5`: GCC 14/14 and Clang 14/14 pass** | Compile-command audits prove all 19 `.c` actions use the C++ driver, `-x c++`, `-std=c++23`, and `-Werror`; both `.cpp` actions use C++23 and `-Werror`. The conformance guard fails under C and under GCC/Clang C++20. Evidence is retained in `/home/ai-dev1/miniaudio-cxx23-language-evidence-20260923` and the two exact-commit build trees. |
| Linux ASan/UBSan and TSan | **exact `54a9ef5` focused pass** | ASan/UBSan covers resource-manager, no-resource-manager, and WAV; TSan covers the resource-manager scheduler. Earlier focused WAV/no-resource-manager and full SteamAudio-suite evidence is retained; the latter also reports an unrelated pre-existing `tests/deviceio/deviceio.c` race, so no universal TSan-clean claim is made. |
| Optional-codec ASan/UBSan and TSan | **2/2 + 2/2 pass** | Opus and Vorbis adapter tests from the pinned dependency matrix. |
| Steam Audio real SDK | **15/15 final normal suite pass; focused normal/C89-option/ASan+UBSan/TSan passes retained** | The final suite links the existing adjacent `../steam-audio/unity` binary. The authoritative clean SDK build was made earlier from adjacent source commit `902e09545ae6bfd06768bcd38980b009317641ac` with a C++17 override. No Steam Audio source or user build output was edited. |
| macOS 26.5.2 arm64, Apple Clang 21 / Homebrew LLVM 22.1.8 | **exact `e4658df`: Debug 13/13, Apple Release 13/13, LLVM Release 13/13; all selected deterministic C targets used `-Werror`; complete builds emitted zero warnings** | Strict C89 3/3, ASan+UBSan 3/3, TSan 1/1, deterministic CoreAudio 1/1, and resource-manager stress 100/100 pass. Arm64 size/alignment and listed offsets for six types match `75158b8`. The two compiled `.cpp` targets did not receive `-Werror`; physical `cpp`/`deviceio` remain excluded for the documented permission/callback limitation. LeakSanitizer is unsupported. |
| Windows 11 x64, MSVC 19.44 / SDK 10.0.26100.0 | **exact `06e3a06`: Debug 17/17 and Release 17/17; 0 warnings under `/W4 /WX`; resource-manager stress 100/100** | Release also used `/O2 /GL /LTCG`. Direct Win32 filesystem Debug/Release, standalone C/C++latest 2/2, and six-type MSVC/clang-cl ABI comparisons pass. No retries or skipped registered tests. |
| Windows clang-cl 23.1.1 | **4/4 focused pass** | Resource manager, no-resource-manager, WAV, and Win32 filesystem compile/link/run with strict warnings. The first standalone filesystem link omitted required `shell32.lib`; the corrected command passed and the harness error is retained. |
| Windows USB capture and process loopback | **exact `fee518b` deterministic/live campaigns pass their code invariants** | Twelve selected microphone cases did not reproduce #1152. Nine synchronized loopback scenarios delivered exact frames, but all captured WAVs—including the whole-system control—were silent, so content isolation remains unresolved. |

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
- Post-review Windows evidence was fetched after each exact commit: `75158b8`
  transport `51d2959682e06c13a3bf4f5a1c5cd44da7b4480fa58a4194e20cec3b8b00e73f`
  (215 files), `6681823` transport
  `5bae9f9380f5cbaf3bf97857322f1ae6f918aa85d1333235850703dd894f839d`
  (767 files), `a99cdc0` transport
  `d479994ff77511d7c8e42f01151a84cf14c29d1d2dd85b1b2ac402b9617998af`
  (739 files), and `205e334` transport
  `65d7b40810dea3abf24d855b499f0f5928c7f0a6a7b22134d7afc7a3446326b1`
  (608 files). Each failed strict build was retained as evidence and drove the
  next source fix; no warning was suppressed.
- Exact-final Windows Batch job `b3261e86fa57e276` tested `06e3a06`. Its
  input bundle SHA-256 is
  `3c20ebde0327a234b7e4d51bd689e975607a6ea471c9800455bfd3d28f9be951`;
  transport manifest
  `2f4c34eee86f16ec6d87ff0d2f42e28a97bc6c8773d89b1cb10394f8d7995e12`
  fetched 543 files / 5,589,029 bytes. The controller independently matched
  all 542 internal manifest rows by size and SHA-256.
- Exact-`7dc7c8e` macOS transport manifest
  `8a4e0859c8fcba26d6767d067d0d02ef07f894d9a5708bb7dfd666b0b405715e`
  fetched 200 files / 1,012,769 bytes and all 198 internal evidence hashes
  were verified. After the host returned, exact-final `e4658df` was validated
  from input bundle SHA-256
  `1fb3bd400d44862cb04310ebd37c7c41049061a8550542966f9e76da35160d6f`.
  The controller fetched the preserved evidence over hardened SSH and
  independently matched all 358 files by size and SHA-256. The scoped cleanup
  report SHA-256 is
  `c060861daf4fd2d9ef5aaafdf934f07802f5465dbb5196e25d0f1dd9e2c06e75`:
  18 exact roots were renamed/deleted, six never-created failed-attempt roots
  were already absent, zero failed, and 2,964,736,633 logical bytes were
  recovered. Incoming bundles, evidence, repositories, and all non-allowlisted
  paths were preserved.
- Windows cleanup Batch job `2bd73fb5b57c7d5d` verified the exact source HEAD
  and non-reparse ownership of all five post-review attempt roots, renamed
  each to a unique same-parent quarantine, deleted it, and verified all five
  original and all five quarantine paths absent. Its evidence transport
  manifest is
  `cb2c59d47e2091f9915501cb03538da64644be514ef0adf6909d030c9a043c0c`
  (4 files / 61,994 bytes, all entries independently matched). Incoming
  bundles, shared caches, server artifacts, and unrelated attempts were
  preserved.
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
macOS attempt. The five later Windows roots through `06e3a06` were also removed
by the allowlisted cleanup above. Clean source worktrees, incoming bundles,
shared caches, and server artifact roots were preserved.

No `timer_wheels` source was modified. Future coroutine integration must keep
logical cancellation separate from physical callback retirement, post
affinity-bound completion before inline resume, retain one generation-bound
resume token per suspension, and never retain or independently resume a raw
coroutine handle.

## Remaining release gate

The exact-`754f852` Astra-max review reported zero Critical/High findings, one
Medium build-attribution finding, and two Low evidence-scope findings. Code
commit `54a9ef5` and this documentation reconciliation address all three. The
final gate is a continuation of that same reviewer conversation over the
remediated tree. The hardware, acoustic, owner-scope, and external-dependency
limits above remain explicit.
