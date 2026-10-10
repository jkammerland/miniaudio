# Finite-stream and Steam Audio tail validation

Date: 2026-10-10 (Europe/Stockholm). Implementation:
`2437bea582ef085621adf5b50e628f8ecede8f90`, dedicated
`feat/cpp-engine-contract` worktree. This record concerns the opt-in C++ engine,
not a change to the existing C engine or its Steam Audio node.

## Implemented contract

Finite callbacks report `{valid_frames, eof}`; `compile_stream()` carries that
metadata through reachable stream-aware nodes. Stateful effects finish their
own tails before completing. The output adapter preserves the exact final
prefix and reports completion only after its cache is consumed. Invalid node
results and suppressed Steam Audio HRTF leases fail the stream, not silently
complete it. Legacy callbacks, custom nodes, `compile()`, and `pull()` remain
available; an opaque effect must explicitly declare tail-free behavior or
implement the stream interface before joining an EOF-aware graph.

Steam Audio uses its Apply, GetTailSize, and GetTail APIs, including the samples
returned by a final `TAILCOMPLETE` call. A nonempty partial source block is
zero-padded and applied as a full SDK frame. SDK padding is retained rather
than trimmed by amplitude or guessed duration. An empty stream does not Apply
an artificial data block. Serial effects and unequal mixer inputs drain in
dependency order.

`request_drain()` targets the active generation observed by the control owner;
cached audio is delivered before source EOF is injected at the next quantum.
Requests do not carry over to replacements. Completion/failure IDs are exact
last-result IDs, not cumulative watermarks. `stop()` is still immediate, and
completion does not establish physical hardware audibility. See [SPEC.md](SPEC.md)
for normative details and [README.md](README.md) for callback/renderer examples.

## Linux gates

| Gate | Result |
| --- | --- |
| GCC 16 Release, complete repository CTest suite | 27/27 passed |
| Clang 22 ASan+UBSan, engine/stream/allocation/Steam/timer/architecture/pkg-config | 7/7 passed |
| Clang 22 TSan, engine/stream/timer | 3/3 passed |
| GCC 16 ASan+UBSan with matching instrumented Steam Audio | 1/1 passed |
| Standalone installed legacy/stream consumer, prefix containing spaces | 1/1 passed |
| pkg-config space regression after configured install-prefix change | 1/1 passed |

The stream suite covers zero, partial, exact-quantum, and multi-quantum source
lengths; source starvation versus EOF; unequal mixer lengths; tail-free and
serial FIR effects against an independent convolution oracle; malformed node
metadata; final-prefix cache delivery; repeated post-EOF pulls; graceful-stop
idempotence; stale requests and interrupted replacements; separate success and
failure generations; and observed owner-thread reclamation after render join.

Allocation checks exercise first adoption, a graceful request and drain,
replacement adoption, final partial output, and 10,000 mixed-size/post-EOF
pulls with no new host allocations. Timer checks establish that expiry sends
the request on the control owner but cancellation does not.

Steam Audio tests compare the stream graph with independently driven direct
SDK Apply-plus-GetTail output. They cover mono/stereo inputs, quanta 40/256,
source lengths 0/1/half/quantum-minus-one/quantum/quantum-plus-one, three wet
mixes, serial binaural nodes, and varied request sizes. An impulse positive
control requires nonzero tail samples; the 40-frame case requires a partial
final tail. Further tests cover a tail dry/wet ramp and a held HRTF lease that
fails without consuming tail state. Render calls are allocation-instrumented.
No audible spatialization or hardware shutdown claim follows from these tests.

The complete-suite command was:

```sh
cmake --build build/cpp-engine -j 16
ctest --test-dir build/cpp-engine --output-on-failure
```

The focused sanitizer builds include the new
`miniaudio_cpp_engine_stream_tests` target. ASan+UBSan uses
`-fsanitize=address,undefined -fno-omit-frame-pointer`; TSan uses
`-fsanitize=thread -fno-omit-frame-pointer`. The GCC-instrumented SDK is paired
with GCC's sanitizer runtime; the Clang adapter gate uses the normal SDK to
avoid mixing runtimes. TSan does not cover the external Steam Audio binary or
the allocation-interposition executable.

Installed-consumer setup initially failed because an install-time `--prefix`
does not rewrite already-configured absolute `.pc` paths; the failed consumer
configuration then retained those discovery results in its CMake cache.
Configuring the library with the actual staged `CMAKE_INSTALL_PREFIX` and using
a fresh consumer build fixed the setup. No assertion, tolerance, or library
behavior was changed to bypass those diagnostics. The committed standalone
fixture consumes only installed headers and archives.

The full release rebuild emitted one warning from unchanged C miniaudio code,
`miniaudio.h:82882`: an always-false size comparison on this 64-bit target.
No new C++ engine warning was observed; this EOF implementation does not alter
that unrelated C path.

## Native gates and evidence lifecycle

Both native gates validated the exact implementation and timer revisions in
fresh, clean task-owned staging clones. No test was retried, weakened, or
skipped to obtain these results.

| Native gate | Engine tests | Installed legacy/stream consumer |
| --- | --- | --- |
| Windows 11 x64, MSVC `19.51.36257.0`, Debug/NMake | 5/5 passed | 1/1 passed |
| macOS `26.5.2` arm64, Apple Clang `21.0.0`, Release/Ninja | 6/6 passed | 1/1 passed |

Windows used the normally installed VS 2026 Build Tools, Windows SDK
`10.0.26100.0`, and CMake `4.4.3`; this is not the earlier baseline's VS/MSVC
version. macOS used Xcode `26.6`, CMake `4.3.4`, Ninja `1.13.2`, and pkg-config
`2.5.1`. Both installed consumers used only installed miniaudio headers/archives
under a prefix containing a space. The timer override remained empty and its
exact revision gate remained enabled. Windows's expanded configure trace
recorded zero dependency-fetch invocations.

The controller retrieved the complete gate and cleanup evidence, independently
matched every payload's size and SHA-256, checked unique/sorted inventories and
aggregate byte counts, and compared the durable text archives with those
verified files. No executable or dependency snapshot is archived. Windows's
gate manifest is sorted case-insensitively by PowerShell and uses a JSON seal;
the controller's initial ordinal/text-seal assumption was corrected only after
examining that producer. Hash, size, count, inventory, and uniqueness checks
remained strict. Its cleanup manifest explicitly uses ordinal sorting.

Compact results, tool hashes, complete archived evidence, and cleanup seals:

- [Windows native record](validation/tail-eof-windows-msvc-native-2026-10-10.json)
  and [complete text archive](validation/tail-eof-windows-msvc-native-2026-10-10.tar.xz);
- [macOS native record](validation/tail-eof-macos-native-2026-10-10.json)
  and [complete text archive](validation/tail-eof-macos-native-2026-10-10.tar.gz); and
- [dated lifecycle ledger](validation/tail-eof-native-lifecycle-2026-10-10.json).

The incremental miniaudio bundle advertises its branch rather than `HEAD`, so
acquisition used that explicit ref. Windows also lacked historical timer
benchmark LFS observations; their exact committed pointers were retained using
process-local smudge suppression. These observations are not consumed by the
engine/header gates, and no benchmark/data-asset validation is claimed. This
was source acquisition diagnosis, not a test retry or relaxed source revision.

No Windows MinGW/GCC tooling is installed, probed, or invoked. Physical I/O is
not attempted on either host, and the owner's completed Mac audio test is not
repeated. Native SDK compilation remains outside this gate when Steam Audio
is not normally installed.

### Cleanup unresolved, separate from passing engine validation

Neither native cleanup renamed or deleted anything. Their old sealed gate
evidence and original repositories remained unchanged.

| Host | Task-owned material retained | Reason / next valid action |
| --- | --- | --- |
| Windows | 113,364,205 logical bytes | Required CIM process query denied access (`0x80041003`); establish conclusive process/locker visibility through authorized host maintenance before fresh exact-path checks. |
| macOS | 55,875,578 logical bytes | Exact-target `lsof` queries warned of incomplete results from an unrelated OrbStack NFS mount; resolve that warning or provide an authorized conclusive open-file query before fresh checks. |

Exact retained roots are `C:\codex-worker\offload\miniaudio-eof-2437bea` and
`/Users/ai-dev/offload/miniaudio-eof-2437bea`, plus each host's two named incoming
bundle files recorded in the lifecycle ledger. Original source repositories,
user data, mounts, shared caches, and the four historical Windows retained
roots were outside cleanup scope. Volume-wide free-byte changes were not
misreported as recovered space. Review date `2026-10-11` is a reconciliation
trigger, not authority to delete. No worker permission escalation or host
configuration change was attempted.

## Provenance

The timer pin remains `38d087f02dc4fb9f00a36d10fbf5ca97e02e2aa9` with a clean
dependency worktree. Steam Audio remains
`4b37ab1aaff9cb7af3df531d0374112856ecc6ca`; its checkout and build directories
were consumed in place, not modified or vendored. Linux is
`7.2.7-200.native.fc44.x86_64`, AMD Ryzen 9 7950X, using GCC
`16.2.1 20260819`, Clang `22.1.8`, CMake `4.4.2`, Ninja `1.14.0.git`, and
pkg-config `2.5.1`.

Complete CTest text logs are preserved in the small
[Linux evidence archive](validation/tail-eof-linux-2026-10-10.tar.gz). Every
member's bytes, SHA-256, and passing-test count were independently checked; the
inventory and tool/SDK hashes are in
[validation/tail-eof-linux-2026-10-10.json](validation/tail-eof-linux-2026-10-10.json).
The archive contains no executables, dependency copies, or toolchain snapshots.

| Normally installed executable/artifact | SHA-256 |
| --- | --- |
| `/usr/bin/g++` | `579d25c2d1e45abeed0c63a517b5c52f6d09ea811a787bf7a7c677a7fd0726f6` |
| `/usr/bin/clang++` | `4ea85504401d9f5c05109d6cb05778b241aa8d56fde1da2e5bc7ba46cffd44f5` |
| `/usr/bin/cmake` | `6ada98c4f457916b95b0a9b3fdd1d5618b3fbe0ee777c6c5aee8203763eb4068` |
| `/usr/bin/ninja` | `0108fce2fda36061dd0b1b3c35ba14f28a5034abfbaee54799abcfc9fff29826` |
| Release `libphonon.so` | `fb8511340a25c1846e110952f5c7f2c5467293b30f1815b0e20fb418441ead28` |
| GCC ASan+UBSan `libphonon.so` | `3fde8d10a248ce19f552659b2e6229209a15f716984c9f3f40d1eaccfd1d490f` |
| `phonon.h` | `b71e88724121b0a83d70281a4c0ad75de61588915049bd8208963f17177ffebd` |

Hashes are provenance for these results, not a requirement to freeze old
executables. Ownership, API compatibility, dependency integrity, and account
isolation remain independently required. No throughput improvement is claimed;
the preceding cache benchmark was not rerun for this behavior change.
