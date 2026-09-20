# Issue #1152: WASAPI exclusive capture duplication/discontinuity

## Assessment

- **Priority:** P1 Windows correctness
- **Exact checkout:** suspect code present
- **Native reproduction:** pending; a Windows capture endpoint was connected on
  2026-09-20 but enumeration and exclusive-format support remain to be verified
- **Reported setup:** Windows 11, exclusive capture, Realtek and M-Audio,
  s16/48 kHz/stereo

The reporter observes captured duration around 1.44 times wall-clock duration,
duplicated periods, and repeated `AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY`.
This is serious for recording, voice processing, and acoustic input, but the
exact cause is not established from source inspection alone.

The capture loop at `miniaudio.h:24725` drains WASAPI packets, caches partial
frames, calls `ReleaseBuffer` around line 24761, and obtains subsequent packets
around line 24771. Discontinuity recovery near 24808 is conditional on duplex
behavior. Event pacing for exclusive mode also interacts with available-frame
calculation near 24488 and higher-level waits.

## Why the loop alone is not proof

WASAPI permits zero, one, or multiple packets to be ready when the client
wakes. Draining multiple packets is valid. `GetBuffer` and `ReleaseBuffer` must
alternate on the same thread; releasing zero frames repeats the packet, while
releasing its full frame count consumes it. The buffer remains valid only until
release.

Therefore a patch that simply limits the loop to one `GetBuffer` per wake could
hide duplication while causing backlog or discontinuities elsewhere.

## Diagnostic instrumentation

Add a bounded, preallocated trace record written in the capture path and
drained off the audio thread:

```c
struct capture_trace {
    uint64_t callbackIndex;
    uint32_t requestedFrames;
    uint32_t packetFrames;
    uint32_t consumedFrames;
    uint32_t cachedBefore;
    uint32_t cachedAfter;
    DWORD    flags;
    uint64_t devicePosition;
    uint64_t qpcPosition;
    uint64_t sampleHash;
};
```

Pass non-null device/QPC position outputs to `IAudioCaptureClient::GetBuffer`
during a diagnostic build. Correlate monotonic device positions, buffer hashes,
cache transitions, callback requests, and elapsed wall time. The trace must use
a bounded overwrite/drop policy; logging or allocating in the callback would
alter timing and violate the real-time constraint.

## Potential fix direction

Select a fix only after the trace identifies which invariant fails. Candidates
to test include:

- cached frames being presented again after already being released;
- `framesRead`/available-frame accounting mixing device frames with client
  frames;
- an exclusive-mode event being treated as packet-complete when it is only a
  wake hint;
- discontinuity recovery resetting the wrong state or being absent for
  capture-only mode;
- period negotiation disagreeing with the conversion buffer's accounting.

A valid fix must maintain exact `GetBuffer`/`ReleaseBuffer` pairing and consume
each packet once. Do not add sleeps, retries, blanket resets on every
discontinuity, or deduplicate samples by content; legitimate audio can repeat.

## Required Windows matrix

1. Shared and exclusive, capture-only and duplex.
2. Native and converted formats; 10 ms and 30 ms requested periods plus the
   negotiated native periods.
3. At least the reported Realtek and M-Audio classes, plus a virtual/loopback
   device where available.
4. A known nonrepeating input signal with frame-index watermarking, verifying
   monotonic indices and exact duration.
5. CPU load, delayed consumer, start/stop, device invalidation, and deliberate
   discontinuity.
6. Instrumented debug build and normal optimized build, with no callback-side
   dynamic allocation or blocking output.

Tests should assert frame positions/accounting, not merely compare audible
output. A native reproduction is required before an implementation commit or a
claim of resolution.

## API/ABI and integration impact

A packet-accounting fix should require no public API/ABI change. Diagnostic
telemetry can remain test-only or behind an internal compile flag. In a future
coroutine integration, copy captured frames into a bounded backend-owned queue;
do not post through a mutex/deque or resume a coroutine from the capture
callback. Cancellation must retire capture callbacks before releasing the
queue and operation state.
