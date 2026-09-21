# Issue #1152: WASAPI exclusive capture duplication/discontinuity

## Assessment

- **Priority:** P1 Windows correctness
- **Exact checkout:** packet-accounting path instrumented at `6a83e67`
- **Native reproduction:** not reproduced on the available USB condenser
  microphone; the upstream report remains unresolved
- **Reported setup:** Windows 11, exclusive capture, Realtek and M-Audio,
  s16/48 kHz/stereo

The reporter observes captured duration around 1.44 times wall-clock duration,
duplicated periods, and repeated `AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY`.
This is serious for recording, voice processing, and acoustic input, but the
exact cause is not established from source inspection alone.

## 2026-09-21 native result

The connected endpoint was `Microphone (USB Condenser Microphone)`, ID
`{0.0.1.00000000}.{9e87ea05-2f7d-4660-a61d-caf0b5c2d0e2}`. Twenty-seven
bounded cases completed; 25 used callback-safe packet instrumentation. Across
7,372 successful packets there were exactly 7,372 full releases on the same
thread. No outstanding acquisition, cache-consumption mismatch,
device-position regression, duplicate position/frame/hash tuple, trace drop,
or miniaudio callback/read allocation was observed. Ordinary duration ratios
were 0.99350–1.00458, not approximately 1.44.

Shared f32/48 kHz/stereo, exclusive native s24/48 kHz/mono, and converted
s16/48 kHz/stereo client input all initialized. Exclusive WAVs contain nonzero
audio at about 0.03 peak. Shared-mode packets and WAVs were silent in the
session-0 environment. Induced worker delay produced discontinuity flags but
did not violate packet ownership or duplicate a packet. The ordinary reported
pattern did not occur, so no production change was made.

Evidence is in `/tmp/miniaudio-1152-20260921-1-evidence`; its artifact manifest
SHA-256 is
`bc3cb9dcab8632dd7cafbfbf5ea0cde2bc95f772dce45cbac4947ceb9163a5b1`.
`exclusive-reported-10.wav` and `exclusive-device-native-10.wav` are suitable
for manual inspection.

### Final plugged-microphone continuation

The final native campaign explicitly selected the same plugged-in USB endpoint
at production commit `346acda` (production `miniaudio.h` is byte-identical at
the final `a1229bf` test-only commit). Ten additional five-second cases covered
shared/exclusive, native/converted input, 10/30 ms requested periods, and
five-segment stop/start cycles. They added 3,109 successful packet acquisitions
and 3,109 full releases with no ownership, cache, position, tuple, trace-drop,
or callback-allocation violation. Ratios were 0.97991-1.00104; exclusive WAV
peaks were 0.028-0.033 and shared WAVs were silent.

Combined with the earlier campaign, 37 bounded cases (35 packet-instrumented)
account for 10,481 successful packets and releases. The approximately 1.44x
duration defect still did not reproduce. Evidence and listenable WAVs are in
`/tmp/miniaudio-native-win-346acda-evidence`; transport manifest SHA-256 is
`b70ae331733eddcee85200a1acec3d0fdb06dfda54fc77c20c5178c04aa09502`.
This increases confidence in the tested USB endpoint but does not substitute
for the reporter's Realtek/M-Audio hardware or a known acoustic/frame-index
watermark reaching the microphone.

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

## Remaining Windows matrix

1. Shared and exclusive, capture-only and duplex.
2. Native and converted formats; 10 ms and 30 ms requested periods plus the
   negotiated native periods.
3. The reported Realtek and M-Audio capture classes, plus a virtual/loopback
   device where available. Only the USB condenser microphone has been tested.
4. A known nonrepeating input signal with frame-index watermarking, verifying
   monotonic indices and exact duration.
5. CPU load, delayed consumer, start/stop, device invalidation, and deliberate
   discontinuity.
6. Longer/repeated campaigns, unplug/invalidation, and a positive acoustic or
   frame-index watermark path. The current playback default is a Realtek
   digital output with no established speaker-to-microphone route.

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
