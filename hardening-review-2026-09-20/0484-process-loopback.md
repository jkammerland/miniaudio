# Issue #484: Windows process-specific loopback capture

## Assessment

- **Priority:** P1 requested feature / platform correctness
- **Implementation:** `0b4727e` plus lifetime/test corrections through
  `0ab5917` and `a1229bf`
- **Deterministic validation:** passed on native Windows MSVC
- **Live activation:** passed on Windows build 26200
- **Content isolation:** unresolved because the host's whole-system positive
  control and every process capture were digital silence
- **Security classification:** not a demonstrated memory-safety issue

The existing public fields were sufficient, so the implementation does not
change public layout. A nonzero `wasapi.loopbackProcessID` now selects a
dedicated desktop asynchronous activation path rather than sending the virtual
process-loopback ID through `IMMDeviceEnumerator::GetDevice`.

## Implemented behavior

The old desktop path built valid activation parameters but then tried to treat
`VAD\Process_Loopback` as a physical endpoint. The implemented selection is:

```c
if (deviceType == ma_device_type_loopback && processID != 0) {
    if (explicitDeviceID != NULL) {
        return MA_INVALID_DEVICE_CONFIG;
    }
    if (!os_supports_process_loopback() || activateAsync == NULL) {
        return MA_NOT_IMPLEMENTED;
    }

    params.TargetProcessId = processID;
    params.ProcessLoopbackMode = exclude
        ? PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE
        : PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    return activate_audio_interface_async(
        L"VAD\\Process_Loopback", params);
}
```

Process loopback is endpoint-independent and shared-mode only. Combining a
nonzero process ID with an explicit capture device or exclusive mode is rejected
instead of silently becoming ordinary loopback. Unsupported OS/symbol cases
return `MA_NOT_IMPLEMENTED`; activation failure is returned directly. There is
no PID-zero retry.

Desktop contexts load `ActivateAudioInterfaceAsync` opportunistically, so an
older desktop still has ordinary WASAPI. `RtlGetVersion` performs the process
feature gate without manifest-dependent version reporting. The implementation
uses the review's Windows 10 build 20348 minimum.

## Async lifetime hardening

The prior UWP helper kept its completion object on the stack and waited
forever. That cannot safely support a bounded failure path: a late COM callback
would access dead stack storage. The shared desktop/UWP helper now allocates a
COM-reference-counted completion object:

```c
result = wait_for_activation(handler, 30000);
if (result != MA_SUCCESS) {
    operation->Release();
    handler->Release();       /* Callback's COM reference keeps it alive. */
    return result;
}
```

The final `Release` closes the event and frees the handler. Activation-result
and query-interface failures release every interface they obtained. The helper
also fixes the earlier UWP error path that could free an explicit device-ID
string it did not allocate or leak an activated interface after failed
`QueryInterface`.

Public `ma_device_init()` remains synchronous; there is no initialized public
device object to cancel while activation is pending. The bounded timeout and
late-callback test prove safe physical retirement, not a public cancellation
API.

Process mode also skips endpoint property access, endpoint rerouting, hardware
offload probing, and IAudioClient3 initialization. It publishes a virtual name
and ID and uses the normal capture service after shared `IAudioClient`
initialization.

## Deterministic Windows tests

`hardening_wasapi_process_loopback.c` covers:

1. Include/exclude activation type, target PID, and virtual interface path.
2. Explicit endpoint, exclusive mode, unsupported OS, and missing symbol.
3. Activation-call, activation-result, and interface-query failures with exact
   output/ref/allocation cleanup.
4. Timeout followed by a simulated late callback; one allocation must remain
   live until the late COM reference releases it.
5. Proof that process requests do not call the ordinary endpoint activator.

At exact commit `2d76179`, native MSVC Debug passed the complete configured
suite 15/15. A separate `MINIAUDIO_FORCE_C89=ON` WASAPI run passed 2/2; MSVC
does not enforce strict C89, so this is a configuration check rather than a
language-conformance claim.

The final MSVC run at `a1229bf` again passed 15/15. The process-loopback test
passed directly and in 100/100 independent processes. In every invocation,
nine non-vacuous checkpoints required exactly one handler allocation and one
event creation, exact live ownership across the simulated late callback, and
matching free/close counts after retirement.

## Live watermark attempt

Evidence is in `/tmp/miniaudio-0484-2d76179-evidence`; artifact manifest
SHA-256 is
`ea28b648156ad4fe841f67f241169236d6cf9595d46d5a73265477e5ef73b335`.
The host was Windows 10.0.26200.9457 and exported
`ActivateAudioInterfaceAsync`.

An evidence-only probe launched independent target and sibling render
processes. The target created a child only after capture reached 48,000 frames.
Named events and captured frame counts—not arbitrary sleeps—synchronized the
matrix. Producer callback reference files contain their assigned tones:

- target: 697 + 941 Hz;
- sibling: 1201 + 1601 Hz; and
- late child: 2003 + 2503 Hz.

Whole-system controls, include, exclude, no-stream target, nonexistent PID,
target exit, and explicit producer endpoint all initialized. Ten populated
captures each received non-null buffers for exactly 288,000 frames (six
seconds). Every captured sample was zero. The whole-system positive controls
were also zero even though producer callback references measured approximately
0.08 amplitude.

This means activation, lifecycle, frame delivery, target exit, and bounded
teardown were exercised, but audio separation was not. Declaring target
rejection from universal silence would be a vacuous test. A future run needs a
Windows environment where the whole-system control first captures a nonzero
watermark. The current host has only one render endpoint, `Realtek Digital
Output (Realtek(R) Audio)`, so cross-endpoint/default-switch evidence is also
unavailable.

The saved `live-*.wav` files permit manual inspection, but they are bitwise
silent; the `.reference.wav` files are producer callback data, not loopback
captures.

The final live continuation repeated the matrix against byte-identical
production code. Whole-system, include, exclude, target-exit, no-stream,
nonexistent-PID, and explicit-endpoint cases each delivered 288,000 frames.
The whole-system positive control and every capture were again bitwise silent;
producer references remained nonzero. One optional child-only calibration did
not meet its frame-trigger precondition and is retained as a failed calibration,
not counted as isolation evidence. Only one active render endpoint was
available, so cross-endpoint/default-switch behavior remains untested.
Evidence is in `/tmp/miniaudio-native-win-346acda-evidence` with transport
manifest SHA-256
`b70ae331733eddcee85200a1acec3d0fdb06dfda54fc77c20c5178c04aa09502`.

## Remaining acceptance boundary

The source feature and deterministic lifetime/configuration contract are
implemented. The issue remains unresolved only for live include/exclude content
isolation, multiple-endpoint changes, and any future decision to expose public
activation cancellation. No whole-system fallback or ABI change is present.
