# Hardening implementation: unresolved work and validation limits

Status date: 2026-09-21
Implementation reconciled through: `f07c3f0`

This is the authoritative remaining-work record for
`IMPLEMENTATION-PROMPT.md`. A hardware, toolchain, or acoustic limitation is
not counted as a pass. Conversely, an issue is not kept open after a
deterministic regression and the required source invariant establish the fix.

## Current unresolved table

| Item | Current status | Evidence obtained | What is still missing / why it remains open |
|---|---|---|---|
| #1152 WASAPI exclusive-capture duplication | **Unresolved; not reproduced on the available USB microphone** | 27 bounded Windows runs, 25 packet-instrumented; 7,372 successful `GetBuffer` calls paired with 7,372 full releases; duration/QPC ratios 0.99350-1.00458; no duplicate packet tuple or approximately 1.44x duration. A later five-second shared/exclusive smoke test also initialized and shut down cleanly; exclusive input was nonzero. | Only `Microphone (USB Condenser Microphone)` was available. Shared input was silent in session 0, no acoustic/frame-index watermark reached this endpoint, and the reported Realtek/M-Audio devices were not tested. Changing the packet-draining loop without reproducing the fault would be speculative. |
| #1155 alleged optional-libopus UAF | **Unresolved as the alleged UAF; no valid owner-mediated UAF reproduced** | Pinned Opus/Vorbis dependencies; valid, malformed, truncated, chained, seek/EOF, nonseekable, allocator/VFS/open failure, callback-lifetime, and deterministic resource-manager cancellation tests under normal, C89-option, ASan/UBSan, and TSan builds. Three real callback-contract defects were fixed separately. | The report still lacks a supported call sequence and sanitizer stack. Concurrent public read/uninit is not assumed valid. The seek/tell and EOF fixes are not evidence for the alleged ownership UAF. |
| #1157 Windows GCC/MinGW LTO diagnostics | **Owner-rejected / outside the accepted deployment scope** | Native Linux GCC 16.2.1 C/C++ LTO checks did not reproduce the diagnostic. | The owner explicitly will not deploy or run GCC/MinGW on Windows. No MinGW toolchain was provisioned or executed. This is a documented scope rejection, not a pass or a fix. Reopening requires a new owner decision or an external exact compiler/triple/flags reproducer. |
| #484 process-specific WASAPI content isolation | **Implemented; acoustic include/exclude result unresolved on this host** | Desktop process-loopback activation, OS/symbol/config gates, timeout-safe completion ownership, public automatic-context error preservation, virtual metadata, and deterministic include/exclude/failure/timeout tests pass. Ten native six-second captures initialized and returned 288,000 frames each. | Whole-system controls and every process capture were bitwise silent, while producer reference WAVs contained the intended watermarks. Silence cannot prove filtering without a nonzero loopback control. There is also no public object to cancel while synchronous `ma_device_init()` is still activating. |
| CoreAudio real-device behavior | **Deterministic lifecycle and actual worker dispatch fixed; physical I/O remains unverified** | Transactional reroute/failure ownership tests, a barrier-driven pending-reroute dispatch through the real control worker, native Apple Clang Debug/C89/ASan/UBSan/TSan runs, and clean teardown all pass. | The isolated macOS account had no usable playback callback and capture permission was denied. Real playback/capture, hotplug, and duplex device-change behavior therefore remain hardware/permission limitations. |
| #1153 original audible spatialization report | **Related gain defects fixed; original audible scenario not reproduced** | Independent scalar/SSE2 mathematical-oracle tests cover split ramps, target changes, tails, guards, in/out-of-place operation, null advancement, and 1/2/6/8/33 channels. Steam Audio snapshot synchronization passes with the real SDK. | The report's Windows listener-rotation/spatialization symptom was not reproduced acoustically. The deterministic defects are closed, but they do not prove that every cause of the original audible report is gone. |
| 32-bit and additional architecture coverage | **Validation limitation** | Linux x86_64, Windows x64, and macOS arm64 ABI samples matched their baselines; scalar/SSE2 and available sanitizer configurations pass. Checked arithmetic covers the hostile metadata paths. | No usable 32-bit libc/toolchain was available, and not every architecture-specific DSP implementation or compiler packing mode was exercised. No public layout change was made, but this is not universal ABI proof. |
| Steam Audio checkout with its default C++14 mode | **External dependency/toolchain limitation; miniaudio integration validated with a C++17 override** | A fresh `phonon` build from tracked commit `902e09545ae6bfd06768bcd38980b009317641ac`, followed by miniaudio fixed/partial/tail, C89-option, ASan/UBSan, and TSan tests, passed. | Steam Audio's bundled FlatBuffers selects the host Abseil `string_view`, while Steam Audio requests C++14 and current Abseil requires C++17. The external checkout needs its own dependency/standard fix. No Steam Audio source was changed by this task. |

## Disposition of the final independent-review findings

Batch Codex job `db5bbdbcca10ee51` used `gpt-6-astra` to review the complete
series through `aa84825`. It found no critical issue, but identified additional
source and proof gaps. The following commits address them without weakening a
test:

| Finding | Disposition | Commit / proof |
|---|---|---|
| `CORE-001`: buffer/node completion preceded physical job retirement | **Fixed** | `3dd73e1`: buffer load/free and node load/page jobs advance their execution pointer before publishing completion; deterministic owner teardown checks the exact ordering. |
| `CORE-002` and `CORE-005`: nested WAV LIST records bypassed slot capacity; empty trailing INFO was rejected | **Fixed** | `fb932dd`: capacity is checked immediately before each emitted record, including LIST children; empty containers consume no slot. Undercount and valid trailing-empty tests pass. |
| `CORE-003` / `BR-003`: notification `Release` exposed quiescence before its final sidecar access | **Fixed** | `66e188f`: an atomic retirement state counts active `Release` calls separately from COM references; teardown waits through the final sidecar access. A barrier stops `Release` in the formerly unsafe interval. |
| `CORE-004` / `BR-004`: late WASAPI activation retained a context-scoped allocator | **Fixed** | `66e188f`: detached activation state uses `HeapAlloc`/`HeapFree`, independent of context allocator lifetime. Late completion after allocator retirement is tested. |
| `CORE-006`: public callback decoders falsely advertised tell support | **Fixed** | `aac73b2`: the bridge passes a tell adapter only when the caller supplied tell capability; public Opus/Vorbis callback tests pass. `bed44d0` applies the same rule to built-in MP3 and fixes the native null-call crash found by the full Windows suite. |
| `CORE-007`: nonseekable Opus mapped normal `MA_AT_END` to I/O error | **Fixed** | `aac73b2`: zero-byte `MA_AT_END` becomes opusfile EOF; a full nonseekable drain must finish as `MA_AT_END`. |
| `CORE-008` / `R03`: #1128 test lacked semantic, cleanup, path, and optional-field coverage | **Fixed** | `6f98e1c` forces MP3 selection and checks format, frames, and allocation cleanup. `f07c3f0` adds memory/file/callback paths, every flag combination, exact TOC/LAME boundaries, valid Xing/Info/LAME/tag-free controls, and commits tag state only after complete validation. |
| `R04`: EOF coverage omitted boundary, looping, resampling, and notification timing | **Fixed** | `b55d1fe`: exact sample/frame and notification-order tests cover direct, split, resampled, cache-boundary, and loop-transition cases. |
| `R05`: CoreAudio's real worker dispatch branch was untested | **Fixed** | `a39d5de`: a deterministic barrier publishes reroute work to the actual control worker and waits for acknowledgement. `0ecda7d` makes its allocator telemetry atomic; native TSan then passes. |
| `R06` / `BR-006`: automatic context selection overwrote process-loopback errors | **Fixed** | `66e188f`: process-loopback stops fallback after WASAPI; top-level `ma_device_init(NULL, ...)` tests preserve `MA_INVALID_DEVICE_CONFIG` and `MA_NOT_IMPLEMENTED` semantics. |
| `BR-001`: desktop `StringFromIID` introduced an Ole32 link dependency | **Fixed** | `66e188f`: fixed interface strings replace the call. Native standalone C/C++ implementation link checks require no explicit libraries and contain no `StringFromIID`/Ole32 import. |
| `BR-002`: stale UWP activation-helper identifier | **Fixed** | `66e188f`: the UWP branch calls the current async helper; Windows SDK C and C++ UWP-family object builds pass. Packaging/runtime deployment was not attempted. |
| `BR-005`: generic pre-worker init rollback could strand WASAPI state | **Fixed** | `66e188f`: a dedicated pre-worker rollback invokes backend teardown and releases staged generic resources without joining a worker that was never created. |
| `BR-007`: initial WASAPI client failure released COM services on the caller thread | **Fixed** | `66e188f`: initial render/capture service cleanup is dispatched through the WASAPI command worker; injected event failure checks exact release counts and thread identities. |
| `BR-008`: process-loopback metadata fell back to a physical endpoint | **Fixed** | `66e188f`: device-scoped info returns the virtual ID/name and negotiated native format without endpoint enumeration. |

The earlier Astra review findings F01-F12 remain fixed by `1c1efd5`,
`b25511c`, `3f2a48c`, `5ddd7c0`, `a4d0966`, `fcc0270`, `90e3bad`, and
`6a83e67`. Stream job retirement was separately fixed by `ee9b745`; the
remaining buffer/node variants are covered by `3dd73e1` above.

## Why the later fixes matter

Completion and physical retirement are different events. The safe ordering is:

```c
/* The worker performs its final object access first. */
ma_job_set_execution_pointer(...);

/* Only then may a waiter observe completion and destroy the owner. */
ma_async_notification_signal(...);
```

A callback capability must also remain absent all the way through adapters:

```c
backend_on_init(
    read_adapter,
    seek_adapter,
    decoder->onTell != NULL ? tell_adapter : NULL,
    decoder);
```

Finally, optional MP3 metadata is parsed into locals. Declared frame count,
delay, padding, and Xing/Info classification are published only after every
flag-selected field fits inside both the decoded frame and initialized bytes.
An incomplete later field therefore cannot leave a partially trusted length.

## Validation matrix through `f07c3f0`

| Environment | Result | Scope / qualification |
|---|---|---|
| Linux GCC 16.2.1 Debug | **13/13 pass** | Complete ordinary registered suite. |
| Linux GCC 16.2.1 Release | **13/13 pass** | Complete ordinary registered suite. |
| Linux GCC strict C89 option | **12/12 pass** | C++ test is intentionally not registered in C89 mode. |
| Linux GCC Release + LTO + warnings-as-errors | **13/13 pass** | Complete ordinary registered suite. This is not Windows MinGW validation. |
| Linux Clang 22.1.8 C90 + C++23 + warnings-as-errors | **13/13 pass** | Complete ordinary registered suite. |
| Forced C++23 implementation build | **3/3 focused pass** | MP3, resource-manager, and EOF hardening targets plus the library. |
| Pinned Opus/Vorbis Debug | **15/15 pass** | libogg 1.3.6, libopus 1.6.1, opusfile 0.12, libvorbis commit `1b75110b...`. |
| Pinned Opus/Vorbis C89 option | **14/14 pass** | C++ test not registered. |
| ASan/UBSan | **7/7 focused pass** | WAV, WAV metadata, resource manager, MP3, EOF, Opus, and Vorbis. |
| TSan | **4/4 focused pass** | Resource manager, generic device rollback, Opus, and Vorbis. |
| Steam Audio real SDK | **pass in normal, C89-option, ASan/UBSan, and TSan** | Uses the C++17 validation build described in the unresolved table. |
| macOS arm64 Apple Clang 21.0.0 | **CoreAudio focused pass in Debug, C89, ASan/UBSan, and follow-up TSan** | Deterministic lifecycle and actual worker dispatch; no physical I/O claim. Evidence manifests `86cd93b...` and `7edb6fc...`. |
| Windows x64 MSVC 19.51 / SDK 10.0.26100.0 | **WASAPI 3/3, lifecycle 100/100, UWP C/C++ objects, direct-link C/C++, and microphone shared/exclusive smoke pass at `66e188f`; full 15/15 pass at `bed44d0`** | The first broad run exposed the MP3 null-tell crash, which `bed44d0` fixed. Shared microphone data was silent; exclusive data was nonzero. Evidence manifests `6523c24...` and `7d57c796...`. A final exact-HEAD native pass is still required after the last MP3 matrix commit and final review. |

No test was skipped, retried, weakened, or given a wider semantic tolerance.
The Windows Debug generation test's original 30-second external bound was
retained as a failure; the same unmodified test completed in 36.76-40.55
seconds under a documented 90-second per-test bound.

## Audio evidence available for manual inspection

- #1152 exclusive microphone:
  `/tmp/miniaudio-1152-20260921-1-evidence/exclusive-reported-10.wav`
  and `exclusive-device-native-10.wav` contain nonzero USB microphone input.
- Latest five-second microphone smoke:
  `/tmp/miniaudio-wasapi-adfaebc4-evidence/microphone-exclusive.wav` is
  nonzero; `microphone-shared.wav` is valid but silent.
- #484 process-loopback captures and their watermarked producer references are
  under `/tmp/miniaudio-0484-2d76179-evidence`. The `live-*.wav` capture files
  are silent; the corresponding `*.reference.wav` files contain the tones.

## Evidence and lifecycle status

- #1152 evidence manifest: `bc3cb9dcab8632dd7cafbfbf5ea0cde2bc95f772dce45cbac4947ceb9163a5b1`.
- #484 evidence manifest: `ea28b648156ad4fe841f67f241169236d6cf9595d46d5a73265477e5ef73b335`.
- CoreAudio dispatch evidence manifests:
  `86cd93b00a1dd46ab84e4778ab23aca872ebd0fdb049ea8a0c360da3ebfe4923`
  and `7edb6fc6551cd53af8c270b2c844f6729fb2a170247869eb47e8da540ab4df26`.
- WASAPI evidence manifests:
  `6523c24feb934718efdd965ff2fac2e833a7f990c11e1278f4fc7baae91f450e`
  and `7d57c79648db7d116ee62d368269249223bd8860a44cee2e426dc023dd62356d`.
- The CoreAudio attempt-owned workspace was removed after evidence retrieval.
  The current Windows staging/build roots remain retained only for the
  authorized final exact-HEAD continuation; the clean source checkout is
  preserved.

No `timer_wheels` source was modified. Future coroutine integration must keep
logical cancellation separate from physical callback retirement, post
affinity-bound completion before inline resume, retain one generation-bound
resume token per suspension, and never retain or independently resume a raw
coroutine handle.

## Remaining release gate

The locally confirmed implementation defects and the later independent-review
findings above are fixed. Before final handoff, continue the three Astra-max
review conversations against the current series, resolve any evidence-backed
finding, run final exact-HEAD Windows/macOS validation justified by subsequent
changes, fetch and verify evidence, and close the retained Windows lifecycle
ledger. Do not convert the unresolved table's hardware, acoustic, owner-scope,
or external-dependency limits into passes.
