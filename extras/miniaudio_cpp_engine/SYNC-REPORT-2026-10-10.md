# Miniaudio C++ engine synchronization report

Report date: 2026-10-10, Europe/Stockholm.
Repository snapshot checked at 08:57 UTC / 10:57 CEST.
Runtime validation dates below are 2026-10-09 unless explicitly stated otherwise.

## Summary

The opt-in C++20 playback vertical slice is implemented, tested, and reviewed.
It is not a complete C++ replacement for `ma_engine`. Existing miniaudio remains
the default and supplies the mature device and codec backends. No dependencies
or toolchain executables were vendored.

The dedicated implementation worktree was clean before this report. The final
three-track Astra/max Batch Codex review and its evidence-only closure have no
remaining P0-P3 findings in their reviewed scopes. Documented feature omissions,
hardware limitations, and cleanup restrictions are not converted into passes.

This report records the current status and explains the proposed tail/EOF work.
It does not implement that work or replace the normative [contract](SPEC.md).
No runtime suite or native-host job was rerun for this documentation change.

## Revisions and worktrees

| Component | Snapshot | Location and disposition |
| --- | --- | --- |
| Original miniaudio checkout | `ab4bd4bfb30e2321b37207fad7cc6c1eb906c096` | `/mnt/d2/agent-workspace/repos/miniaudio`, `master`; clean and unchanged by this report |
| C++ engine documentation baseline | `5226fdca6eda26e5595af6d3a937b075ad9077ee` | `/mnt/d2/agent-workspace/worktrees/miniaudio-cpp-engine`, `feat/cpp-engine-contract`; this report is added here |
| Runtime implementation validated natively | `75c81e7e3616cdd710f404e9d74198f7efb9a825` | The subsequent `5226fdc` commit updates evidence only |
| timer_wheels integration | `38d087f02dc4fb9f00a36d10fbf5ca97e02e2aa9` | `/home/ai-dev1/repos/timer_wheels-miniaudio-latest`, `fix/miniaudio-latest-pr-stack`; clean |
| Steam Audio source | `4b37ab1aaff9cb7af3df531d0374112856ecc6ca` | `/mnt/d2/agent-workspace/repos/steam-audio`; existing build artifacts left untouched |

The timer integration includes the latest PR-stack snapshot used for validation
(`077e8a4`), followed by the GCC/MSVC portability and callable-contract fixes
listed in [PROVENANCE.md](PROVENANCE.md). A read-only remote-ref check on
2026-10-10 timed out connecting to `gitea.home.arpa:22`. Therefore this report
does not claim that remote PR tips were refreshed today. It records the exact
locally available, validated stack. No push was performed.

## Implemented slice

| Area | Completed behavior |
| --- | --- |
| Processing and layout | Immutable graph topology; source, gain, mixer, and effect composition; semantic channel layouts; 64-byte-aligned planar float buffers |
| Callback adaptation | Fixed render quanta feed arbitrary callback sizes through a cursor cache without shifting remaining samples |
| Ownership | One control owner constructs and retires plans; generation acknowledgement separates publication from safe reclamation |
| Render contract | No engine-owned allocation, mutex, blocking, graph destruction, I/O, or coroutine resumption on the render path; application callbacks must satisfy the same restrictions |
| Device boundary | Playback adapter uses miniaudio's float32 interleaved callback boundary; the C device implementation remains in use |
| Steam Audio | Mono/stereo binaural node; typed HRTF settings validation; direction publication; dry/wet ramping; nonblocking exclusion of overlapping shared-HRTF renders |
| timer_wheels | Owner-thread ST scheduling; cancellable publication of an owned candidate; losing operations clean up without implying device or graph retirement |
| Build and packaging | Opt-in CMake targets; static/shared and PIC consumers; external dependency discovery; exact SDK platform mapping; pkg-config paths and absolute libraries containing spaces |

## Validation and review status

These are recorded executions, not tests performed on the report date. Commands,
tool versions, artifact hashes, and qualification limits are in
[VALIDATION.md](VALIDATION.md) and its linked evidence.

| Gate | Recorded result | Scope limit |
| --- | --- | --- |
| Linux Release repository suite | 26/26 passed | Enabled playback prototype and available optional integration tests |
| Clang 22 ASan+UBSan | 6/6 engine gates passed | Normal external Steam Audio library; not a claim that this library was instrumented |
| GCC 16 ASan+UBSan with instrumented Steam Audio | 1/1 adapter gate passed | Matching executable/library sanitizer runtime |
| Clang 22 TSan | 2/2 core and timer gates passed | External Steam Audio binary not TSan-instrumented |
| timer_wheels full Debug suite | 1697/1697 passed | Exact `38d087f` integration stack |
| Dependency-free timer portability | C++20/C++23 passed with GCC and Clang | Separate from the full Gentest-dependent suite |
| Native Windows 11 x64 | 4/4 passed with MSVC 19.44.35228 | Steam Audio SDK unavailable; MSVC-only, no MinGW |
| Native macOS arm64 | 5/5 passed with Apple Clang 21 | Deterministic engine/packaging checks; no new physical audio or native Steam Audio adapter check |

The final full review was Astra/max Batch Codex job `948283b4fa204a2e`.
Runtime/concurrency and build/portability tracks had no P0-P3 findings. The
evidence track found a P3: the sealed Windows manifest reported zero aggregate
payload bytes although its 379 individually verified entries total 3,338,548
bytes. The original was preserved and an explicit
[correction](validation/windows-msvc-native-manifest-correction-2026-10-09.json)
was added. Closure `16b0126753bde5f3` independently reread all 379 payloads,
checked the cross-hashes, and reported P0: 0, P1: 0, P2: 0, P3: 0 in the
evidence/specification/test scope. It did not rerun runtime suites.

The cache benchmark does not establish a throughput improvement. Across 21
paired samples, the cursor cache was about 2.1% slower than planar shifting,
below the predefined 3% practical threshold. Its supported advantages are
bounded callback work and direct planar composition, not a measured speedup.

## Remaining prototype work and operational limits

| Item | Status | Next valid step and reason |
| --- | --- | --- |
| Steam Audio tail/EOF | Deliberately outside this slice | Define explicit source EOF, effect draining, valid-frame accounting, completion, and retirement semantics; then implement and test them as described below |
| Capture, decoders/resource management, hotplug, resampling | Not implemented in the new engine | Specify each ownership and timing boundary before extending the playback prototype; existing C functionality is not a new-engine implementation |
| Native Steam Audio on Windows/macOS | Unvalidated | Supply a compatible normally installed SDK/library and run exact-revision adapter tests; architecture mapping alone does not prove the adapter works there |
| 32-bit and additional architectures | Unvalidated or intentionally unsupported by the exchange | Qualify a concrete target, including always-lock-free pointer/64-bit atomics; compilation is rejected when that requirement is false |
| Windows task-root cleanup | Four exact staging roots retained | Restore a conclusive host locker query before cleanup; both CIM and Restart Manager enumeration were access-denied, so no unlocked-state assumption is valid |
| Future evidence sealing guard | Recommended tooling follow-up | Check serialized row counts and 64-bit byte totals while sealing and verifying; the documented correction does not claim that the historic sealing script was repaired |

Windows cleanup details are in the
[durable cleanup record](validation/windows-msvc-native-cleanup-2026-10-09.json).
Historical roots totaling 2,667,218,986 bytes were removed. The four later roots
totaling 1,048,956,819 logical bytes remain unchanged in that record. The final
Mac job's two exact task roots were removed after evidence and safety checks,
as recorded in [macOS evidence](validation/macos-native-2026-10-09.json).
No cleanup was attempted for this report.

## Carried-forward hardening items

The authoritative detailed record remains
[UNRESOLVED.md](../../hardening-review-2026-09-20/UNRESOLVED.md). The following
table reconciles its current contents with this report; it does not date its
older hardware campaigns as new executions.

| Item | Current disposition | Missing evidence or next step |
| --- | --- | --- |
| #1152 WASAPI capture duplication | Open; not reproduced in 49 bounded USB-microphone cases | Known non-silent input with an acoustic/frame watermark or reporter hardware; exclusive WAVs were nonzero, but shared capture was silent in session 0 |
| #1155 alleged Opus UAF | Allegation unconfirmed; supported owner-mediated tests pass | A supported failing call sequence and sanitizer stack; concurrent public read/uninit is not assumed valid |
| #1157 Windows MinGW/GCC | Permanently owner-rejected deployment target | Do not install, probe, or invoke MinGW; reconsider only after explicit owner policy reversal |
| #484 process loopback isolation | Implemented; live acoustic proof open | A nonzero whole-system watermarked control before judging include/exclude output; current live captures were all silent |
| CoreAudio physical I/O | Playback, capture, duplex, and output rerouting/hotplug verified by the operator | Active-capture input switching remains untraced; `Q` was swallowed by the test wrapper, Control-C was used, and the operator declined a repeat |
| #1153 original audible spatialization | Related gain/synchronization defects fixed; original symptom unconfirmed | Reporter's scene/configuration or a captured rotating-listener comparison |
| Additional architectures | Validation limitation | Concrete 32-bit and other architecture-specific DSP/packing qualification; native x64/arm64 coverage is not universal ABI proof |
| Steam Audio default C++14 | External dependency limitation | Repair its FlatBuffers/Abseil standard/dependency selection in that repository; the older hardening integration passed using C++17 |

The old miniaudio/Steam Audio integration has fixed/partial/tail regression
coverage. That does not supply an EOF/draining contract for this new C++ graph.
They are different integration paths with different completion APIs.

## What Steam Audio tail and EOF mean

EOF means a source will never provide another input frame. A silent block or a
temporary decoder underrun is not EOF. A DSP effect may still owe output after
EOF because it retains state from earlier input.

For example, a toy FIR filter with coefficients `[1, 0.5, 0.25]` receives an
impulse as its last input sample. It produces `1` at that instant, then `0.5`
and `0.25` after the source ends. Dropping the effect at EOF loses the latter
two samples. This illustrates convolution; it is not a claim about the exact
length or coefficients of a Steam Audio HRTF. Binaural filtering can have a
tail even when no reverb effect is enabled.

Steam Audio already provides `iplBinauralEffectGetTailSize()` and
`iplBinauralEffectGetTail()`. After input ends, use `GetTail` rather than `Apply`
until the returned state is complete. The state describes what remains after
the call: the call that returns `TAILCOMPLETE` can still produce the final
audio block. Consume that output before declaring it delivered. See the
[official binaural effect API](https://valvesoftware.github.io/steam-audio/doc/capi/binaural-effect.html).

### What the current prototype lacks

The current node intentionally discards the effect state in
[steam_audio_binaural.cpp](src/steam_audio_binaural.cpp):

```cpp
static_cast<void>(iplBinauralEffectApply(
    effect_, &parameters, &input_buffer, &output_buffer));
```

The generic `node::process()` and source callback return `void`. They cannot
report terminal EOF, a short final input block, or whether an effect still owes
output. The [quantum adapter](include/miniaudio_cpp_engine/quantum_adapter.hpp)
also marks every rendered quantum as fully available. It has no terminal
valid-frame count or finished-stream marker.

Consequently the engine can render continuously supplied blocks, but it cannot
promise a complete finite stream with exactly accounted tail frames. Publishing
a replacement plan can be memory-safe while still cutting off unrendered audio.
The existing generation acknowledgement proves reclamation safety, not that a
finite stream has been drained.

### Recommended contract before implementation

| Boundary | Meaning | Required behavior |
| --- | --- | --- |
| Source EOF | No future source frames, possibly after a short final block | Record the final valid-frame count; zero-pad the fixed-size DSP input without mistaking padding for source data |
| DSP complete | No future effect output remains after this call | Preserve the last output, including the call returning `TAILCOMPLETE`; stop further effect calls once drained |
| Output delivered | The last valid cached output frame has been handed to the next consumer/device buffer | Emit finite-stream completion separately from DSP completion; this does not prove that hardware playback latency has elapsed |
| Safe reclamation | The render domain cannot access the old plan again | Use the existing acknowledgement/control-owner protocol; never destroy nodes in the render callback |

For the source boundary, a possible API shape is the following. It is a design
example, not an existing header or a finalized API:

```cpp
enum class source_state { active, starved, eof };
struct source_result {
    std::uint32_t valid_frames;
    source_state state;
};
```

`starved` must not trigger permanent drain. Its timeline/zero-fill policy needs
to be explicit. After a short final source block, process one zero-padded input
quantum, then switch to tail calls. For the inspected Steam Audio revision,
`GetTail` fills a full quantum, copies at most the remaining tail count, and
zeros the unused suffix. A single drain step could therefore look like this:

```cpp
// Illustrative render-domain step; output storage is already allocated.
const int remaining = iplBinauralEffectGetTailSize(effect);
if (remaining > 0) {
    const auto state = iplBinauralEffectGetTail(effect, &output);
    const int valid = std::min(frame_size, remaining);
    copy_to_preallocated_cache(output, valid);  // proposed helper, not current API
    dsp_complete = state == IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
} else {
    dsp_complete = true;
}
```

Implement at most the required bounded quantum work per render step, not a
callback loop that drains an arbitrarily long effect. The final-input padding
may contain real filtered output, so it cannot be discarded just because it
extends beyond source EOF. Specify that timeline and final valid-frame policy
against a direct SDK reference. An offline file writer should stop at the
defined finite output boundary; a running device callback must still fill its
requested buffer, using silence for any suffix after that boundary.

A final quantum may remain partly cached after the DSP reports completion. The
cache owns those samples, so the effect need not remain alive solely to store
them. Once all required tail is generated and the render domain releases the
plan, control-side reclamation can proceed independently of cache delivery.
Neither condition alone establishes that the device has physically played the
samples.

Graph composition also needs rules: a mixer ends only when every relevant input
has ended; a downstream effect consumes upstream tail as ordinary input before
draining its own state. During binaural tail rendering the dry branch has no
new source data, while wet output still needs the defined gain/dry-wet policy.
Choose whether parameters freeze at EOF or may continue to change. A graceful
stop should drain; an immediate stop or seek may explicitly discard/reset old
state instead. Those operations must not accidentally replay the previous tail.

All effect operations remain render-domain-owned. Preserve nonblocking shared
HRTF exclusion and preallocated storage. Define how suppression affects drain
progress: a rejected call must not consume state or signal completion. The
control owner observes completion through bounded publication; timer_wheels
may schedule a stop request but must not guess a tail duration with a timer or
infer retirement from cancellation.

### Focused validation for that extension

- Compare an impulse-at-EOF render against a direct SDK apply-plus-tail oracle;
  establish a nonzero-tail positive control rather than accepting all silence.
- Cover full and partial final source blocks, zero-length EOF, repeated EOF,
  final partial tail blocks, and calls after completion.
- Pull the same finite stream through awkward callback sizes and verify exact
  sample order, no lost/duplicated tail frames, valid counts, and offline length.
- Cover mixers and serial effects, wet settings of zero/fractional/one,
  graceful versus immediate stop, seek/reset, and nonblocking suppression.
- Prove zero render allocations, render/control ownership, and safe retirement
  even when the final output remains in the adapter cache.

The SDK call change is small. The graph-wide EOF, completion, and lifetime
contract is the substantive work. It is a bounded follow-on feature once those
rules are chosen; it should not be represented as already completed by the
current playback slice.
