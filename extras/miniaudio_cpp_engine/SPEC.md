# Experimental C++ engine contract

Status: vertical-slice contract. Normative terms (`MUST`, `MUST NOT`, `SHOULD`)
apply to this directory only. This is not a compatibility specification for the
existing miniaudio engine.

## Scope

The engine provides a small C++20 rendering core while miniaudio continues to
provide device and codec backends. The first slice covers immutable graph
topology, planar floating-point processing, fixed render quanta, arbitrary
device callback sizes, graph publication, and owner-loop scheduling.

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

## Timeline and callback adaptation

`first_frame` is a monotonically increasing stream-frame index. It advances by
one quantum each time a plan is rendered, regardless of device callback size.
The callback adapter may satisfy a request from an already-rendered block. It
MUST retain a cursor into that block and MUST NOT shift remaining samples with
`memmove`.

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
non-finite vectors. Tail and explicit EOF draining are not yet part of this
vertical slice.

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
- zero dynamic allocations during first render, graph adoption, repeated core
  rendering, and tested Steam Audio rendering;
- generation acknowledgement and control-thread reclamation;
- timer expiry, action cancellation, and scheduled-publication cancellation on
  the pinned timer_wheels stack; and
- deterministic equivalence between cursor and shift-cache reference output.

Native device and acoustic Steam Audio checks are additional evidence, not a
replacement for deterministic tests.
