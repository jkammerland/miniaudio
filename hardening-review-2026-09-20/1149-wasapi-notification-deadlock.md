# Issue #1149: WASAPI device-notification callback can deadlock

## Assessment

- **Priority:** P0 Windows lifecycle/correctness
- **Original checkout:** affected in source
- **Current implementation:** fixed by transactional control-worker and
  teardown commits through `fee518b`
- **Native validation:** MSVC lifecycle tests and 100/100 stress runs pass
- **Platform:** Windows WASAPI
- **Real-time concern:** OS callback performs blocking lifecycle work

`IMMNotificationClient::OnDefaultDeviceChanged` stops and reroutes a device
directly on the Windows notification thread while taking miniaudio locks. This
violates the Windows callback contract and can deadlock with device/audio
threads or teardown. The path also ignores reroute failure and restarts the
device even if reinitialization cleared its clients and failed.

The upstream issue remains open, but this fork no longer performs rerouting in
the notification callback. The callback publishes bounded coalesced work to a
pre-created control worker. Candidate backend state is transactional; teardown
stops publication, serializes terminal state, quiesces callbacks and `Release`,
and can release COM services/quit even when local event creation is injected to
fail. The source discussion below describes the original affected path and the
constraints that shaped the implemented fix.

## Suspect code

The callback starts at `miniaudio.h:22475`. Its effective sequence is:

```c
ma_device_stop(pDevice);                    /* line 22531 */
ma_mutex_lock(&pDevice->wasapi.rerouteLock);/* line 22536 */

if (flow == eRender) {
    ma_device__reroute__wasapi(pDevice, ma_device_type_playback);
} else {
    ma_device__reroute__wasapi(pDevice, ma_device_type_capture);
}

ma_mutex_unlock(&pDevice->wasapi.rerouteLock);
ma_device_start(pDevice);                   /* line 22570 */
```

The reroute result is ignored. Reinitialization around
`miniaudio.h:24124-24179` releases or clears audio clients and acknowledges
that COM objects cannot safely be finalized from the notification callback. If
new client initialization then fails, the unconditional start can operate on
null/invalid backend state.

There is an existing command queue at `miniaudio.h:22636`, but it is not a
safe drop-in deferral path. Posting creates an event, locks a queue, spins when
the four-entry queue is full, and waits for completion. Its consumer needs the
same queue mutex. Calling it from `IMMNotificationClient` would retain the
blocking/deadlock hazard.

## Required callback behavior

The Windows `IMMNotificationClient` contract requires callbacks to be
nonblocking. They must not wait, unregister/register notification callbacks,
or perform final releases of MMDevice API objects in the callback.

The callback should perform only bounded operations using already allocated
state:

```c
HRESULT OnDefaultDeviceChanged(flow, role, deviceId)
{
    if (!notification_applies(flow, role)) {
        return S_OK;
    }

    atomic_fetch_or(&sidecar->pendingFlows, flow_bit(flow));
    signal_precreated_control_event(sidecar); /* no wait, allocation, or lock */
    return S_OK;
}
```

Do not retain `deviceId`; the pointer is borrowed for the callback duration.
The worker should query the current default endpoint after coalescing changes.

## Potential minimal design

Add one backend control worker or equivalent serialized non-audio executor per
device/context sidecar:

1. Allocate its state and wake primitive before registering notifications.
2. The OS callback sets coalesced render/capture bits and signals the worker.
3. The worker serializes stop, client replacement, routing notification, and
   conditional restart with normal control-thread lifecycle calls.
4. Build new client state in temporaries. Commit it only after complete
   initialization; otherwise leave the device coherently stopped/detached.
5. Preserve the user's latest requested started/stopped state. A pending
   reroute must not restart a device that was stopped after the notification.
6. On teardown, stop accepting notifications, unregister/quiesce the callback,
   signal and join/drain the worker, then release sidecar/device state.

`ma_device` and `ma_context` publicly expose embedded backend fields around
`miniaudio.h:7872-7903` and `7347-7399`. To preserve ABI, prefer an internal
heap sidecar reachable through an existing opaque backend pointer, after
auditing all direct uses. Appending public fields is not automatically ABI-safe.
If no correct sidecar design fits the existing representation, stop for
explicit approval of an ABI change.

## Failed initialization distinction

The issue discussion also reports a crash after calling `ma_device_uninit()` on
an object whose `ma_device_init()` failed. Current API rules do not permit
uninitializing a failed object. That caller misuse is separate from the valid
internal problem: reroute can begin with a valid initialized device, fail
internally, and then unconditionally restart cleared state. The fix must not
claim that every arbitrary failed-init object becomes uninitializable.

## Required tests

Use deterministic fake COM/audio interfaces for failure and interleaving tests,
then native hardware/virtual-device validation:

1. Default render/capture endpoint changes while playing, capturing, duplex,
   and stopped.
2. Role/flow filtering, rapid repeated changes, coalescing, and callback
   reentrancy.
3. Failure at every event/thread/COM/client initialization step.
4. Stop or uninit racing a pending reroute; no restart after a user stop.
5. Queue/event saturation and repeated init/destroy cycles.
6. No callback after sidecar free, no COM leak, and no wait/lock/allocation in
   the OS callback, verified with instrumentation.
7. Native MSVC builds and Windows x86/x64 size, alignment, and offset checks,
   with other supported architectures covered in CI. Windows MinGW is an
   explicit owner-rejected deployment target for this effort and is recorded
   as an exclusion, not a pass.

Do not claim the Windows problem fixed based on compilation or Linux tests.

## Coroutine and Steam Audio impact

The control worker may publish completion to a future integration operation,
but it should post to the owning event loop first. Invoking a `resume_token`
directly resumes inline and could run arbitrary coroutine work on the WASAPI
worker. Callback-side notification must not resume a coroutine.

Steam Audio DSP state should remain independent of device replacement. Stop
and quiesce mixing before changing device/backend clients; retain or rebuild
DSP state on a control thread according to the integration's own lifetime
contract.

## Implemented command-retirement result

Notification deferral, reroute replacement, COM release affinity, and teardown
quiescence are landed. Final review reopened the control command's exceptional
path: the ring copied a stack-backed command before releasing its semaphore,
so semaphore failure returned rejection while retaining the pointers; event
wait failure could similarly return before worker retirement; failed quit
could let generic context cleanup destroy synchronization under the worker.

`fee518b` makes command acceptance transactional. While holding the ring lock,
it releases the semaphore first; the consumer wakes but cannot inspect the
ring until the successful command copy/count commit and unlock. Failure leaves
the ring unchanged. Accepted release waits through the worker's final payload
access even if the event wait reports an error, and release/quit retry failed
publication so quit always joins before synchronization teardown.

Native fault seams prove: one failed wake-token release leaves count/index
unchanged; the retry publishes exactly once; an injected accepted-command wait
error returns only after all three mock COM interfaces release exactly once on
the worker; and a failed first quit publication retries and joins. The exact
MSVC target passes directly and in 100/100 independent launches.
