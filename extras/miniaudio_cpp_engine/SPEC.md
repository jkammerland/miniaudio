# Experimental C++ engine contract

Status: vertical-slice contract. Normative terms (`MUST`, `MUST NOT`, `SHOULD`)
apply to this directory only. This is not a compatibility specification for the
existing miniaudio engine.

## Scope

The engine provides a small C++20 rendering core while miniaudio continues to
provide device and codec backends. The first slice covers immutable graph
topology, planar floating-point processing, fixed render quanta, arbitrary
device callback sizes, graph publication, and owner-loop scheduling.
The opt-in stream path additionally covers explicit source EOF, effect tails,
cache-aware completion, and generation-scoped graceful stopping.

It does not promise source or ABI compatibility with `ma_engine`, dynamic graph
mutation in place, a resource manager, decoder ownership, device hotplug, or
sample-rate conversion. Those features require separate contracts before being
implemented.

## Execution domains

The application has three explicit domains:

1. The **render domain** is the device callback or offline renderer. It owns
   mutable DSP state and MUST NOT allocate, block, acquire a mutex, destroy a
   graph, or resume a coroutine.
2. The **control domain** is a single owner thread. It constructs graphs,
   publishes plans, retires acknowledged plans, and drives timers and
   continuations.
3. **Worker domains** may decode or perform I/O. They MUST publish completion to
   the control domain; they do not mutate a live render plan.

Thread-safe objects do not erase affinity. In particular, a timer continuation
is permission to resume work; it does not own device state, graph storage, audio
buffers, or backend registrations.

## Stream contract

A stream contract consists of a sample rate, a positive fixed render quantum,
and an ordered channel layout. The initial sample representation is finite or
non-finite IEEE-754 `float`; processors MUST preserve ordinary C++ floating-point
semantics and MUST NOT enable reassociation as part of this contract.

Internally, a block is planar and contiguous:

```text
channel 0: [frame 0 ... frame quantum-1]
channel 1: [frame 0 ... frame quantum-1]
...
```

The backing allocation is at least 64-byte aligned. Channel roles and order are
semantic; equal channel counts with different roles are not interchangeable.
Interleaving occurs only at an explicit boundary such as a miniaudio device.
An all-unknown layout delegates positional choice to the device backend. A
layout mixing unknown and named roles is rejected at device initialization;
silently replacing its named roles would violate this contract.

Every node processes exactly one quantum. A node MUST fully initialize its
output block. Node callbacks are `noexcept`; an error that can occur while
rendering must be represented in preallocated state and converted to a defined
audio result, normally silence. Construction and graph validation may throw.
Callback state pointers are non-owning. The pointed-to state MUST outlive every
plan containing that callback and may be destroyed only after the plan is
eligible for control-domain reclamation.

### Opt-in finite streams

`compile()` and the original `void` callbacks retain their continuous-render
contract. `compile_stream()` compiles only nodes reachable from the selected
output and MUST reject a reachable node whose EOF/tail behavior is unknown.
It resolves each node's optional `stream_interface()` on the control domain;
rendering MUST NOT use RTTI or dynamically discover stream capabilities.

`finite_source_callback` returns `source_result{valid_frames, eof}`. A source
MUST write the indicated prefix in every channel, with a count no greater than
the quantum. The engine clears the remaining frames. A short read without EOF
is starvation, not completion: its zero-padded quantum advances stream time
and remains valid audio. `{0, false}` therefore renders one silent quantum;
`{0, true}` ends an empty source without rendering an artificial data block.
Once a source reports EOF, the plan MUST NOT call it again. An original
`callback_source_node` is continuous until a graceful-stop request ends it.

Stream-aware nodes implement `stream_node::process_stream()` and receive
`stream_input{samples, result}` per input. Physical input/output buffers remain
one full quantum; metadata identifies the meaningful prefix and whether more
output can follow. The per-node result contract is:

| State | Valid prefix | Meaning |
| --- | --- | --- |
| `running` | exactly one quantum | More output may follow, including an effect tail. |
| `complete` | zero through one quantum | This prefix is the final output, not an instruction to discard it. |
| `failed` | zero | No output from this quantum is usable. |

Invalid metadata or any reachable node failure MUST silence that quantum and
latch the plan as failed. A terminal plan returns an empty result with the same
terminal state on subsequent calls without executing nodes. Buffers beyond a
node's valid prefix MUST be zero, independently of the node implementation.

Gain preserves its input result. A mixer ends only when all its inputs end,
uses their longest valid prefix, and treats ended inputs as silence. A
`tail_free_effect_node` explicitly promises no delayed output of its own and
propagates its input result; ordinary `callback_effect_node` does not make that
promise and is rejected by the stream compiler. Stateful custom effects MUST
continue draining after receiving empty complete input until their own output
is complete. Serial effects receive upstream tails as ordinary input before
draining their own state. These rules apply to the C++ prototype only; the
existing C engine and its callbacks are unchanged.

## Timeline and callback adaptation

`first_frame` is a monotonically increasing stream-frame index. It advances by
one quantum each time a plan is rendered, regardless of device callback size.
The callback adapter may satisfy a request from an already-rendered block. It
MUST retain a cursor into that block and MUST NOT shift remaining samples with
`memmove`.

`pull_stream()` accepts the same planar/interleaved output boundaries as
`pull()` but returns the valid frame count actually copied into this request.
It caches the exact final prefix, zero-fills the unused backend buffer, and
MUST NOT report completion until the cached prefix is fully consumed. A failed
pull may still contain valid frames copied from an earlier quantum; its
returned count identifies that prefix. Per-node metadata validation does not
apply to this aggregated pull result, whose count can exceed one quantum.
Repeated pulls after completion/failure MUST NOT execute DSP. An offline file
writer MUST write only the returned valid prefix, including on a final complete
or failed pull, instead of writing backend padding.

Do not mix `pull()` and `pull_stream()` on one adapter without `reset()` while
rendering is stopped. `restart_stream()` is render-domain-only and requires an
empty cache; it clears terminal metadata when adopting a replacement without
resetting frame indices or counters. `rendered_frames` counts whole processed
quanta, including a padded final block; `delivered_frames` counts requested
backend frames, including silence after EOF. Neither counter measures physical
audibility. After terminal output, frame indices do not advance merely because
the backend requests silence.

Wall-clock timers schedule control work. Sample-accurate DSP changes use frame
indices inside the render domain; a wall-clock timer alone does not establish a
sample-accurate boundary.

## Graph publication and lifetime

Topology is immutable after a plan is compiled. DSP state inside the active plan
is render-domain-owned.

The control domain publishes at most one pending plan. The render domain adopts
it immediately before rendering a new quantum and then release-publishes the
adopted generation. Publication never destroys the previous plan. The control
domain may destroy generation `N` only after observing an acknowledged generation
greater than `N`.

`request_drain()` is a control-domain operation targeting the acknowledged
active generation observed by that request, never a pending replacement. It
returns `no_active_plan`, `unsupported_plan` for a legacy plan, or `queued`.
At the next render quantum, the matching stream plan stops invoking its
zero-input source nodes, exposes their empty EOF, and drains downstream
effects. Already cached audio is delivered first. Repeated requests are
idempotent; a request for a superseded generation MUST NOT drain a replacement.
Publication is still a replacement/interrupt operation, not an implicit
request to gracefully finish the previous graph. The application MUST wait for
completion before publishing if it wants all of that graph's tails delivered.

`playback_device::completed_generation()` reports the last successful generation
whose final cache was copied to a backend callback. `failed_generation()`
separately reports the last failed generation. These are exact IDs, not
cumulative completion watermarks: a later success does not prove an interrupted
earlier graph completed. Adoption acknowledgement permits reclamation but is
not completion. Retained graphs are still destroyed only by the control owner
after adoption of a later generation, or after rendering has stopped/joined.

The exchange is available only on targets where pointer and 64-bit atomics are
always lock-free. This is a compile-time requirement: an implementation that
could route either operation through a runtime lock is not conforming.

The exchange and all retained plans MUST outlive the render domain. Shutdown is:

1. stop and join the device/render callback;
2. retire or reset retained plans on the control thread;
3. destroy the exchange and device state.

## Device boundary

The miniaudio playback adapter requests `ma_format_f32` with the stream's sample
rate and channel count. It converts the planar cache to miniaudio's interleaved
callback buffer without allocation. Device initialization, start, stop, and
uninitialization are control-domain operations. The adapter prevents miniaudio
from changing denormal handling around the callback so online and offline DSP
retain the same ordinary C++ floating-point semantics.

`stop()` remains an immediate backend stop and does not drain. A graceful
request only ends the current stream graph; the device continues servicing
callbacks with silence and can adopt another published plan. A control owner
may call `stop()` after observing its target's completion, but copying audio
into the callback buffer is not proof the hardware has played it. Backend
latency or physically audible shutdown requires a separate device contract.

Backend callback timing, hotplug, capture, duplex operation, and default-device
switching remain miniaudio contracts in this slice.

## Steam Audio boundary

The optional binaural node accepts mono or stereo planar input and produces
stereo planar output. Its quantum and sample rate MUST equal
`IPLAudioSettings::frameSize` and `samplingRate`, and its frame size MUST be at
least eight. A `steam_audio_hrtf` creates the HRTF with those settings and binds
the metadata to the handle; the node rejects a wrapper created for different
settings. This prevents Steam Audio from reading an HRTF spectrum at an
incompatible FFT size. The node retains its Steam Audio context and HRTF and
owns its binaural effect. Graph retirement therefore releases these objects on
the control domain, never inside the callback.

Direction publication is single-producer/single-consumer. Dry/wet changes use a
single atomic scalar and are ramped over a quantum. Direction updates are
normalized in double precision on the control domain and reject zero or
non-finite vectors. Steam Audio uses mutable interpolation scratch inside an
HRTF, so nodes that share one `steam_audio_hrtf` may execute only in one active
render domain at a time. Sequential nodes in one plan are valid. If two calls
overlap, the later entrant clears its output without waiting or allocating and
increments `suppressed_overlap_count()`; independent concurrent render domains
should use independent HRTF objects. Direct callers of `native_handle()` use
`try_acquire_render()` around every render operation and keep the HRTF wrapper
alive until the returned lease is destroyed.

In a stream plan, the final nonempty source block is zero-padded and passed to
`iplBinauralEffectApply()` once as a full SDK frame. That block can contain
filtered output beyond source EOF and is retained in full. On subsequent
quanta, `iplBinauralEffectGetTailSize()` and `iplBinauralEffectGetTail()` determine
the remaining output. A tail call returning `TAILCOMPLETE` may still contain
valid samples, which MUST be delivered. Its valid count is bounded by the tail
size reported before the call; a partial final tail is not replaced with a
whole silent quantum. Empty EOF on a fresh effect completes without an Apply
call. SDK tail sizes, not a timer or amplitude threshold, define completion;
SDK-required padding is retained even when it happens to be silent.

Tail output has zero dry input and uses the existing per-quantum dry/wet ramp.
Direction changes do not re-spatialize convolution history. Nonblocking HRTF
lease suppression in the stream path is a failure, not successful completion;
it increments the existing overlap counter and MUST NOT consume DSP state.
The graph latches that failure rather than retrying on the render thread.
Legacy direct `process()` calls retain their original silence-and-recovery
behavior and do not opt into EOF semantics.

## timer_wheels boundary

The validated integration target is the stacked revision recorded in
`PROVENANCE.md`. It includes cancellable `awaitable_timer`, optional startup,
fanouts, and direct external-operation subscriptions.

The integration helper uses an ST timer queue on its owner thread. Cancellation
returns `awaitable_timer_status::cancelled`; it does not run the scheduled action.
The queue, stop source, captured action, and any referenced engine object MUST
outlive the pending operation. Stop requests and polling for an ST queue occur
on the owner thread.

`schedule_plan_publish` owns its candidate plan inside the coroutine frame. On
expiry it attempts one publication; if the exchange is busy, the candidate is
destroyed when the completed coroutine is destroyed on the control domain. On
cancellation it never publishes. The coroutine object itself MUST therefore be
owned and destroyed by the control domain.

Future device/load operations SHOULD implement the stack's `await_subscribe`
protocol using stable operation storage. `when_any(cancel_remaining, ...)`
requests cancellation and waits for losing operations to finish cleanup; it is
not physical retirement by itself.

## Conformance requirements

The vertical slice is conforming only when tests establish:

- channel-role and planar/interleaved behavior;
- source, gain, mixer, and fixed-quantum effect composition;
- arbitrary callback sizes without output discontinuity;
- empty, partial, exact-quantum, and multi-quantum EOF; unequal mixer inputs;
  serial effect tails; malformed-result rejection; and final-prefix caching;
- zero dynamic allocations during first render, graph adoption, repeated core
  rendering, draining, post-EOF pulls, and tested Steam Audio rendering;
- generation acknowledgement and control-thread reclamation;
- generation-scoped graceful requests, interrupted replacements, separate
  completion/failure reporting, and cached-tail delivery before completion;
- timer expiry, action cancellation, and scheduled-publication cancellation on
  the pinned timer_wheels stack, including owner-thread drain requests; and
- optional Steam Audio Apply-plus-GetTail reference equivalence with nonzero
  impulse tails, partial final tail blocks, dry/wet changes, and suppression;
- deterministic equivalence between cursor and shift-cache reference output.

Native device and acoustic Steam Audio checks are additional evidence, not a
replacement for deterministic tests.
