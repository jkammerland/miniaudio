# Issue #484: Windows process-specific loopback capture

## Assessment

- **Priority:** P1 requested feature / platform correctness
- **Exact checkout:** incomplete experimental path
- **Local reproduction:** unavailable on Linux
- **Platform:** Windows, modern process-loopback support
- **Security classification:** not a demonstrated memory-safety issue

The public configuration contains process-loopback fields near
`miniaudio.h:7147-7148`, but the WASAPI initialization path does not provide a
verified desktop implementation. The issue remains open.

Historical discussion matters: the maintainer did test an implementation on
Windows 11 in 2023 and received silence. “Never verified” should mean never
verified working, not never tested.

## Suspect architecture

The path around `miniaudio.h:23382-23418` checks a nonzero process ID and builds
process-loopback activation parameters, but backend activation diverges:

```c
if (processID != 0 && pDeviceID == NULL) {
    /* Build AUDIOCLIENT_ACTIVATION_PARAMS for process loopback. */
    ...
}

#if desktop_path
    /* Existing endpoint/IMMDevice activation. */
#else
    /* ActivateAudioInterfaceAsync path. */
#endif
```

Process loopback is endpoint-independent and uses modern asynchronous audio
interface activation. Forcing the entire library into a UWP build mode is not
an acceptable desktop fix because it changes enumeration, compatibility, and
other backend behavior.

## Potential implementation

Keep the existing process-ID and include/exclude configuration, but select a
dedicated activation path when process loopback is requested:

```c
if (config.processID != 0) {
    if (!os_supports_process_loopback()) {
        return MA_NOT_IMPLEMENTED;
    }

    params.TargetProcessId = config.processID;
    params.ProcessLoopbackMode = config.excludeProcessTree
        ? PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE
        : PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    return activate_process_loopback_async(params, completionHandler);
}
```

The completion handler needs stable backend-owned state, an initialization
stage machine, cancellation/teardown coordination, and exact COM-threading
rules. It must report unsupported OS/configuration explicitly rather than
silently falling back to whole-system capture.

This feature should build on the safe WASAPI control/lifecycle executor
proposed for #1149. It should not introduce a second independent worker or
resume a coroutine from the activation callback.

## Required Windows tests

1. Supported OS build (Microsoft's sample specifies build 20348+) and an older
   unsupported build/compatibility check.
2. Two independent processes producing distinct watermarked tones.
3. Include target process tree, exclude target process tree, and child process
   creation after capture starts.
4. Target with no active streams: documented silence, not random endpoint
   capture.
5. Target exit/restart, invalid PID, access restrictions, activation failure,
   stop/cancel during activation, and device changes.
6. Exact frame content/duration and no capture from disallowed processes.

## API/ABI and integration impact

The existing fields appear sufficient, so no public API expansion should be
required. Internal asynchronous state should use a sidecar if necessary to
preserve public layout. Document OS requirements and return codes.

This feature belongs after parser/resource-manager and #1149 correctness work.
It should land as a separate `feat(wasapi)` commit rather than being described
as part of a security fix.
