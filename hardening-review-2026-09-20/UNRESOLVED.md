# Hardening implementation: remaining work and validation limits

Status date: 2026-09-21
Implementation reviewed through: `2d7617964c567982366b0c7408e26f6a3d46a0de`

This is the authoritative remaining-work record for
`IMPLEMENTATION-PROMPT.md`. A platform or hardware limitation is never counted
as a pass. Conversely, an issue is not kept open after deterministic tests and
the required source invariant establish the fix. The original Astra-max review
findings are retained below with their disposition so the history is auditable.

## Current unresolved table

| Item | Current status | Evidence obtained | What is still missing / why it remains open |
|---|---|---|---|
| #1152 WASAPI exclusive-capture duplication | **Unresolved; not reproduced on the available USB microphone** | 27 bounded Windows runs; 25 packet-instrumented; 7,372 successful `GetBuffer` packets paired with 7,372 full releases; duration/QPC ratios 0.99350–1.00458; no duplicate packet tuple or ~1.44× duration | Only `Microphone (USB Condenser Microphone)` was available. Shared input was silent, no acoustic/frame-index watermark path existed, and the reported Realtek/M-Audio devices were not tested. No production patch is justified without reproduction. |
| #1155 alleged optional-libopus UAF | **Unresolved as an alleged UAF; no valid owner-mediated UAF reproduced** | Hash-pinned Opus/Vorbis dependencies; valid, malformed, truncated, chained, seek/EOF, nonseekable, allocator/VFS/open failure, callback lifetime, and deterministic resource-manager cancellation tests under normal, C89-option, ASan/UBSan, and TSan builds | The public report still lacks a valid call sequence and sanitizer stack. Concurrent public read/uninit is not assumed supported. Two real nonseekable callback-contract bugs were fixed, but they are not evidence of the alleged UAF. |
| #1157 Windows GCC/MinGW LTO diagnostics | **Owner-rejected / outside accepted deployment scope** | Native GCC 16.2.1 C/C++ LTO checks outside Windows did not reproduce the warning | The owner explicitly will not deploy or run GCC/MinGW on Windows. This is documented scope rejection, not a pass or fix. Reopening requires a new owner decision or an external exact compiler/triple/flags diagnostic and reproducer. |
| #484 process-specific WASAPI content isolation | **Implemented; acoustic include/exclude result unresolved on this host** | Dedicated desktop `ActivateAudioInterfaceAsync` path; OS/symbol/config gates; heap-owned late-callback-safe completion state; deterministic include/exclude/failure/timeout tests; Windows Debug 15/15 and C89-option WASAPI 2/2; live initialization and ten 288,000-frame captures succeeded | Every whole-system positive control and process capture was bitwise silent even though producer callback references contained the intended tones. Without a nonzero whole-system control, silence cannot prove include/exclude filtering. Multiple-endpoint switching and a public cancel API are also unavailable; device initialization is synchronous. |
| CoreAudio real-device behavior | **Source/lifecycle fixes complete; hardware exercise limited** | Transactional deterministic lifecycle/failure tests and native macOS builds passed | The isolated macOS account had no usable playback callbacks and capture permission was denied. This limits hardware confirmation; it does not invalidate the deterministic ownership fixes. |
| 32-bit and additional architecture coverage | **Validation limitation** | Linux x86_64, Windows x64, and macOS arm64 ABI measurements matched their baselines; scalar/SSE2 and available sanitizer configurations passed | A real 32-bit libc/toolchain build and every architecture-specific DSP implementation were not available. No ABI change was made, but this is not proof for every compiler and packing mode. |
| Steam Audio checkout with the host's default C++14 mode | **External dependency/toolchain limitation; miniaudio integration validated with a C++17 build override** | A fresh `phonon` build from tracked-clean commit `902e09545ae6bfd06768bcd38980b009317641ac`, followed by the miniaudio hardening and TSan tests, passed | Steam Audio's bundled FlatBuffers selects the host's Abseil `string_view` while Steam Audio requests C++14; current Abseil requires C++17. The external checkout needs its own permanent dependency/standard fix. No Steam Audio source was changed here. |

## Disposition of the independent Astra-max findings

Batch Codex job `cde2b7f425041dd3` reviewed the earlier implementation through
`ca92a4b` with `gpt-6-astra` at `max` reasoning. It reported 0 critical, 6
high, and 7 medium findings. The following scoped commits address every source
or mandatory-test finding from that review:

| Finding | Disposition | Commit and proof focus |
|---|---|---|
| F01 SMPL count/read disagreement | **Fixed** | `1c1efd5`: acceptance is separate from bytes consumed; materialization is capacity checked; mixed valid/invalid metadata and short reads are tested. |
| F02 WASAPI reroute publishes before converter setup | **Fixed** | `fcc0270`: candidate clients and generic post-init state are completed before publication; injected failures preserve the old state or an empty destroyable state. |
| F03 failed synchronous load frees a shared node | **Fixed** | `b25511c`: failure is published and only the creator reference is retired through the locked unacquire path; a barrier test holds a second acquisition. |
| F04 MP3 declared length controls allocation | **Fixed** | `a4d0966`: Xing/Info length is a hint; the actual stream is scanned before exposing an allocation length; resource-manager refusing-allocation coverage is included. |
| F05 failed CoreAudio reroute leaves stale ownership | **Fixed** | `90e3bad`: staged replacement/retirement and failure teardown have exact ownership tests. |
| F06 failed CoreAudio duplex init remains tracked | **Fixed** | `90e3bad`: tracking is published only for complete initialization and partial failures untrack deterministically. |
| F07 valid RF64 metadata regression | **Fixed** | `1c1efd5`: `ds64` resolves the RF64 sentinel before bounds checks; writer/metadata-reader round trips pass. |
| F08 WASAPI internal reroute emits public stop/start | **Fixed** | `fcc0270`: internal notification suppression and ordering tests distinguish reroute lifecycle from public state requests. |
| F09 WAIT_INIT ignores notification failure | **Fixed** | `5ddd7c0`: notification/fence stages are checked and unwound exactly; failure injection covers signal/wait/uninit counts. |
| F10 failed decoded loads leak decoder/supply | **Fixed** | `3f2a48c`: staged cleanup covers length-query, PAGE-post, decode, VFS, and allocation failures. |
| F11 Steam Audio direction data race | **Fixed** | `6a83e67`: bounded block-boundary snapshots replace ordinary cross-thread floats; Steam Audio 4.8.1 from `../steam-audio` passed fixed/partial/tail and sanitizer/TSan tests. |
| F12 incomplete WASAPI lifecycle proof | **Fixed** | `fcc0270`: expanded fake-interface/barrier coverage exercises coalescing, full queues, candidate/post-init failures, public-state races, quiescence, worker join, and exact release accounting. |

The follow-up also confirmed and fixed a separate resource-manager retirement
race in `ee9b745`: a stream-load job previously signalled initialization before
its final `executionPointer` write. The waiter could destroy/zero the stream,
after which the worker wrote into retired storage. The job now advances its
execution pointer before publishing completion; deterministic notification
interleaving and TSan pass without adding a wait that would deadlock a manual
job pump.

## New callback-contract defects found during optional-codec validation

The #1155 investigation did not reproduce its alleged UAF, but stronger tests
found two independent defects in the optional wrappers. A callback source may
be nonseekable only when the wrapper advertises that consistently:

```c
/* Wrong: a seek callback without tell advertises an unusable partial pair. */
callbacks.seek = onSeek;
callbacks.tell = NULL;

/* Fixed: supply both, or advertise neither. */
if (onSeek != NULL && onTell != NULL) {
    callbacks.seek = wrapper_seek;
    callbacks.tell = wrapper_tell;
}
```

Vorbis could call a null tell function and crash at address zero. Opus rejected
the same valid nonseekable source as `MA_INVALID_FILE`. Commit `9d7e8a3` makes
seek/tell an atomic capability pair in both wrappers and adds independently
selectable dependency modes:

- `MINIAUDIO_FETCH_LIBVORBIS` / `MINIAUDIO_FETCH_LIBOPUS` fetch hash-pinned
  sources;
- `MINIAUDIO_REQUIRE_LIBVORBIS` / `MINIAUDIO_REQUIRE_LIBOPUS` fail configure
  visibly when a requested development dependency is absent;
- the ordinary configuration remains offline and does not download anything.

Pinned inputs are libogg 1.3.6, libopus 1.6.1, opusfile 0.12, and libvorbis
commit `1b75110b5a2754ba1931d82dd83cb822b266a21d`. Released libvorbis 1.3.7 was
not selected because UBSan found a signed-shift defect and it predates the
upstream `ov_pcm_page_seek` security fix.

## #1152 evidence and exact limitation

Windows evidence is in `/tmp/miniaudio-1152-20260921-1-evidence`. Its artifact
manifest SHA-256 is
`bc3cb9dcab8632dd7cafbfbf5ea0cde2bc95f772dce45cbac4947ceb9163a5b1`.

The tested endpoint was:

```text
Microphone (USB Condenser Microphone)
{0.0.1.00000000}.{9e87ea05-2f7d-4660-a61d-caf0b5c2d0e2}
```

Shared f32/48 kHz/stereo, exclusive native s24/48 kHz/mono, and miniaudio's
reported s16/48 kHz/stereo conversion all initialized. Exclusive WAVs contained
nonzero input around 0.03 peak. Shared WAVs were silent in the worker session.
The packet accounting was internally exact, including induced discontinuity
stress; the reported ordinary repeated-discontinuity/duplicated-period/~1.44×
pattern did not occur. Useful manual-listening files include
`exclusive-reported-10.wav` and `exclusive-device-native-10.wav`.

This closes the requested reproduction attempt, not the upstream report. It
would be wrong to change the packet-draining loop based only on the report.

## #484 implementation and evidence boundary

Commits `0b4727e` and `2d76179` implement and validate the desktop path without
changing public layout:

```c
if (processID != 0) {
    if (explicitDeviceID != NULL) return MA_INVALID_DEVICE_CONFIG;
    if (!supportedOS || activateAsync == NULL) return MA_NOT_IMPLEMENTED;
    return activate_process_loopback_async(L"VAD\\Process_Loopback", params);
}
```

The async completion handler is heap-owned and COM-reference-counted. A
30-second bounded wait may return `MA_TIMEOUT`; a later callback retains valid
state and performs the final release. Query-interface and activation failures
release every obtained interface. Process mode skips endpoint properties,
hardware offload, IAudioClient3, and endpoint rerouting, and it never retries
with PID zero.

Native evidence is in `/tmp/miniaudio-0484-2d76179-evidence`; artifact manifest
SHA-256 is
`ea28b648156ad4fe841f67f241169236d6cf9595d46d5a73265477e5ef73b335`.
Windows 10.0.26200.9457 exposed `ActivateAudioInterfaceAsync`. The following
all initialized and ran through the exact public API: whole-system controls,
include, exclude, target child created after capture start, no-stream target,
nonexistent PID, and target exit while capture continued. Ten captures each
contained exactly 288,000 frames (six seconds), but all samples were zero. The
producer reference WAVs contain the requested watermarks. Therefore the
content filter remains unverified until a Windows audio environment produces a
nonzero whole-system loopback control.

The API has no public object to cancel while synchronous `ma_device_init()` is
still running. The deterministic timeout/late-callback test proves safe
retirement, not user-visible cancel-during-activation semantics.

## Other validation limits

- The MSVC `MINIAUDIO_FORCE_C89=ON` configuration passes but CMake explicitly
  notes that MSVC cannot enforce C89. Strict C89 is covered on compilers that
  support the language mode; the Windows run is a configuration check only.
- Optional codec tests are independently selectable so one installed codec
  cannot conceal the other's absence. Ordinary builds remain download-free.
- A fresh Steam Audio build used C++17 through a CMake compiler-rule override
  because its C++14 FlatBuffers snapshot detects a host Abseil version that no
  longer supports C++14. This was a validation-only workaround in `/tmp`; the
  tracked `../steam-audio` source was not modified.
- The callback and packet probes cover miniaudio allocation hooks, not every
  allocation a Windows driver or COM implementation might perform internally.
- The macOS real-device limitation remains recorded above; deterministic
  lifecycle ownership, not an unavailable physical callback, is the regression
  gate for the CoreAudio fixes.
- No `timer_wheels` source was modified. Future coroutine integration must keep
  physical callback retirement separate from logical cancellation and must not
  retain or independently resume raw coroutine handles.

## Post-fetch cleanup receipts

All fetched evidence was hash-verified before removing attempt-owned paths.
For #484 attempt 1, Windows staging/build removal recovered 679,677,920 file
bytes; attempt 2 recovered 463,045,218 file bytes. Clean exact source checkouts
at `C:\codex-worker\repos\miniaudio-hardening-0b4727e` and
`C:\codex-worker\repos\miniaudio-hardening-2d76179` were retained. The #1152
owned staging/build roots are absent and its exact source checkout is retained;
an earlier cleanup-reporting typo prevented a trustworthy recovered-byte count,
which is recorded rather than guessed.

## Release gate

The original F01–F12 blockers are addressed. Release language must still keep
#1152, the alleged #1155 UAF, owner-rejected #1157, #484 acoustic isolation,
and the platform limits in the table explicit. A final Astra-max whole-series
review and the repository's complete safe/practical checks remain required
before calling the implementation handoff complete.
