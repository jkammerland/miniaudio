# Issue #1155: incomplete optional-libopus UAF report

## Assessment

- **Priority:** P1 investigation gate
- **Exact checkout:** optional wrapper is present; alleged failure unconfirmed
- **Local reproduction:** the alleged UAF was not reproduced under a valid
  owner-mediated sequence
- **Build coverage:** hash-pinned Opus and Vorbis configurations now pass
  normal, C89-option, ASan/UBSan, and TSan tests
- **Report quality:** tested-version placeholder, no public PoC or full stack

The issue claims a 168-byte use-after-free read involving the optional Opus
decoder and calls it a race, but it does not identify a tested miniaudio
version, reproducible call sequence, dependency version, ownership thread, or
public input. This is not enough evidence to choose a correct patch.

The report remains actionable as an audit/test requirement because decoder
callback and resource-manager lifetimes are security-sensitive.

## 2026-09-21 pinned investigation

Commit `9d7e8a3` adds opt-in FetchContent support and visible `REQUIRE` modes. The
ordinary build remains offline. The tested inputs are:

- libogg 1.3.6;
- libopus 1.6.1;
- opusfile 0.12; and
- libvorbis commit `1b75110b5a2754ba1931d82dd83cb822b266a21d`.

The matrix covers valid, malformed, truncated, chained, seek/EOF and
nonseekable inputs; allocator, VFS, callback, and open failures; callback
userdata and close counts; and resource-manager cancel-before-start and
cancel-during-read with deterministic events. No sanitizer established the
reported UAF under the supported owner-mediated lifecycle. Concurrent public
read and uninit remains outside the assumed contract.

Stronger tests did find two different callback-contract defects. Both wrappers
advertised seek independently of tell. Vorbis could then call a null tell
function and crash at address zero; Opus rejected the same valid nonseekable
source as `MA_INVALID_FILE`. The fix supplies seek and tell only as a pair, or
supplies neither. These bugs are fixed and tested, but must not be mislabeled
as the alleged UAF.

## Relevant code

The wrapper stores miniaudio callbacks and passes the `ma_libopus` object as
`opusfile` callback userdata around
`extras/decoders/libopus/miniaudio_libopus.c:167`:

```c
libopusCallbacks.read  = ma_libopus_of_callback__read;
libopusCallbacks.seek  = ma_libopus_of_callback__seek;
libopusCallbacks.close = NULL;
libopusCallbacks.tell  = ma_libopus_of_callback__tell;

pOpus->of = op_open_callbacks(pOpus, &libopusCallbacks,
                              NULL, 0, &libopusResult);
```

Reading calls `op_read_float`/`op_read` at lines 272/274. Uninitialization calls
`op_free` at line 223, then tears down the miniaudio data-source base. Heap
wrapper destruction calls `ma_libopus_uninit` then frees `pOpus` around lines
519-520:

```c
ma_libopus_uninit(pOpus, pAllocationCallbacks);
ma_free(pOpus, pAllocationCallbacks);
```

These are audit locations, not proof of a defect. Concurrent read and uninit of
the same decoder is not assumed to be a supported public contract.

## Investigation before any fix

Pin the dependency versions and obtain or reconstruct a valid owner-mediated
sequence:

1. Build `opusfile`, `libopus`, and `libogg` with ASan/UBSan and symbols.
2. Test direct wrapper initialization and resource-manager/custom-decoder use.
3. Use deterministic barriers around callback entry, EOF/error return,
   cancellation, and uninitialization.
4. Determine whether the alleged freed object is `ma_libopus`, `OggOpusFile`,
   callback userdata/VFS storage, resource-manager operation state, or an
   application-owned buffer.
5. Verify whether the call sequence obeys the library's concurrency and
   initialization contracts.

Do not run unknown bundled executables to obtain a stack. A data fixture and
source reproducer are required.

## Potential fixes by confirmed cause

Only apply the branch matching a reproduced valid use:

- If async resource cancellation can free callback userdata before
  `op_free`/the last callback returns, add backend-owned operation retirement
  and join/acknowledgment before free.
- If failed initialization leaves a partially initialized `of`, track stages
  explicitly and uninitialize only completed stages.
- If wrapper destruction is legitimately concurrent with public reads, either
  serialize at the owner layer or document/reject the unsupported operation;
  do not add a mutex in the audio callback without a real-time analysis.
- If the issue is in a pinned upstream dependency, update/patch that dependency
  with its own regression and version note rather than obscuring it here.

A reference count added blindly can convert a UAF into a leak or make final
destruction occur on the audio callback. Coroutine-frame pinning is not decoder
ownership.

## Required regression matrix

1. Valid, truncated, chained, and malformed Ogg Opus streams.
2. Open failure and allocation/VFS failures at each stage.
3. EOF, seekable/nonseekable sources, repeated seek/read, and close counts.
4. Resource-manager cancel-before-start, cancel-during-read,
   completion-versus-cancel, and shutdown with pending work.
5. ASan/UBSan, and TSan for concurrency explicitly supported by the owner
   contract.
6. Optional configuration in CI that fails visibly if dependencies are
   expected but missing; also build Vorbis independently to avoid conflating
   the two wrappers.

## Disposition

Do not claim #1155 fixed or affected as a confirmed UAF. Record it as unresolved
pending a reproducible valid call sequence. The optional decoder integration
and the two independently reproduced nonseekable-source bugs are fixed; that
evidence narrows the remaining report but does not establish its ownership
claim.
