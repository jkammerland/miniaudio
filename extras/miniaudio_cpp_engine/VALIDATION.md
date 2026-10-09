# Validation record

Date: 2026-10-09. All paths below refer to the dedicated
`feat/cpp-engine-contract` worktree. The implementation is opt-in; the existing
miniaudio build remains the default.

## Provenance

| Item | Value |
| --- | --- |
| miniaudio base | `ab4bd4bfb30e2321b37207fad7cc6c1eb906c096` |
| validated implementation tip | `1f49a2ff6ad1ae36b08b669562dd96244a9a4673` |
| timer_wheels integration | `392cab579f6c6556df16131bea878832a0b585ee` (clean worktree) |
| Steam Audio source | `4b37ab1aaff9cb7af3df531d0374112856ecc6ca` |
| Steam Audio release library SHA-256 | `fb8511340a25c1846e110952f5c7f2c5467293b30f1815b0e20fb418441ead28` |
| Steam Audio release ELF build ID | `10bf67cf9cb9187184faed347ea168318c90fd19` |
| Steam Audio GCC-ASan library SHA-256 | `3fde8d10a248ce19f552659b2e6229209a15f716984c9f3f40d1eaccfd1d490f` |
| `phonon.h` SHA-256 | `b71e88724121b0a83d70281a4c0ad75de61588915049bd8208963f17177ffebd` |
| `phonon_version.h` SHA-256 | `08d1f9e835533563dbf49036b69c52bbd41ef25e2be2efbefa0e8cc6f0efe436` |

Host: Linux `7.2.7-200.native.fc44.x86_64`, AMD Ryzen 9 7950X.
Tools: GCC `16.2.1 20260819`, Clang `22.1.8`, CMake `4.4.2`, and
Ninja `1.14.0.git`.

## Deterministic and repository checks

The release configuration enabled the C++ engine, all repository tests, the
allocation contract test, the benchmark, the exact timer_wheels source path,
and the source-built Steam Audio library. The final commands were:

```sh
cmake --build build/cpp-engine -j 16
ctest --test-dir build/cpp-engine --output-on-failure
```

Result: **26/26 passed**. Four engine executables and two CMake-script
regressions establish:

- graph composition, gain equivalence with `ma_gainer`, callback-size timeline,
  channel semantics, lock-free publication, and observed control-thread graph
  destruction;
- no host C/C++ allocation on first render, plan adoption, or 10,000 mixed-size
  steady-state pulls, plus allocation-failure ownership preservation;
- mono and stereo Steam Audio rendering, direct wet-output equivalence,
  dry/wet ramping, HRTF-setting rejection, direction edge cases, and zero host
  or Steam allocator calls during rendering;
- timer expiry, cancellation, `when_any(cancel_remaining)` cleanup, scheduled
  plan publication, and cancelled-candidate destruction on the owner thread;
- exact Steam Audio SDK platform selection, including negative Android,
  FreeBSD, iOS, and Windows ARM64 cases; and
- valid installed `pkg-config` metadata when the install prefix contains
  spaces.

## Sanitizers

All three sanitizer build directories were reconfigured after the timer_wheels
tip advanced. Each configure reported the exact revision
`392cab579f6c6556df16131bea878832a0b585ee` before the following tests were
rerun. Earlier seeded-cache checks independently exercised migration from each
project-owned historical pin rather than treating a stale default as a user
override.

Clang 22 ASan+UBSan used `-fsanitize=address,undefined
-fno-omit-frame-pointer`. The normal source-built Steam Audio library was used
to avoid mixing sanitizer runtimes:

```sh
cmake --build build/cpp-engine-clang-asan --target \
  miniaudio_cpp_engine_tests miniaudio_cpp_engine_allocation_tests \
  miniaudio_cpp_engine_steamaudio_tests miniaudio_cpp_engine_timer_wheels_tests
ctest --test-dir build/cpp-engine-clang-asan --output-on-failure \
  -R '^miniaudio_cpp_engine'
```

Result: **6/6 passed**, comprising the four engine executables and the two
CMake-script regressions.

The Steam Audio library instrumented with GCC ASan+UBSan was paired with a GCC
16 ASan+UBSan executable so the library and executable used the same runtime:

```sh
cmake --build build/cpp-engine-gcc-asan \
  --target miniaudio_cpp_engine_steamaudio_tests
ctest --test-dir build/cpp-engine-gcc-asan --output-on-failure \
  -R '^miniaudio_cpp_engine_steamaudio$'
```

Result: **1/1 passed**. This specifically covers the HRTF/frame-size boundary
that the independent review had reproduced as an out-of-bounds read before the
typed HRTF wrapper was added.

Clang 22 TSan used `-fsanitize=thread -fno-omit-frame-pointer`:

```sh
cmake --build build/cpp-engine-clang-tsan --target \
  miniaudio_cpp_engine_tests miniaudio_cpp_engine_timer_wheels_tests
ctest --test-dir build/cpp-engine-clang-tsan --output-on-failure \
  -R '^miniaudio_cpp_engine($|_timer_wheels$)'
```

Result: **2/2 passed**, including the cross-thread publication/reclamation
stress case. The allocation-interposition executable is intentionally separate
from TSan because replacing allocation entry points conflicts with the TSan
runtime; it remains covered by release and ASan+UBSan runs. The external Steam
Audio binary is not TSan-instrumented.

## Build and consumer matrix

Fresh focused configurations produced these results:

| Configuration | Result |
| --- | --- |
| `MINIAUDIO_FORCE_C89=ON` for miniaudio plus C++20 engine | 3/3 focused tests passed |
| `MINIAUDIO_NO_NULL=ON` | core engine test passed; null-only success case excluded |
| `BUILD_SHARED_LIBS=ON` | core engine test passed |
| static miniaudio and engine linked into a shared consumer | passed, no text relocations |
| deliberately wrong timer revision with Git unavailable | configuration failed closed |
| wrong timer revision with a pre-existing timer target | configuration failed closed |
| existing cache containing the immediately preceding `a46be19` project pin | migrated to `392cab5`; timer integration passed on MSVC |
| explicit `a46be19` revision override against `392cab5` source | configuration failed closed with both revisions in the diagnostic |
| dependency-free timer portability build, GCC 16 and Clang 22 | C++20 and C++23 targets passed 2/2 with each compiler |
| staged install and `pkg-config --static miniaudio-cpp-engine` consumer | built successfully |
| staged install below an absolute prefix containing spaces | metadata regression passed; a real CMake/pkg-config consumer built and ran |
| packaged Steam Audio SDK root (`include`, `lib/<platform>`) | GCC ASan+UBSan adapter test passed with the matching instrumented library |
| synthetic FreeBSD target with a Linux Steam Audio SDK root | SDK correctly reported unavailable; no Linux library was selected |

The install consumers exercised the public headers, both static archives, PIC,
quoted include/library paths, and the platform runtime flags emitted by
pkg-config. Optional Steam Audio and timer_wheels development dependencies
remain external and are never copied into the install tree.

The timer_wheels integration worktree independently passed its complete
**1695/1695** test suite at `392cab5`.

## Native Windows/MSVC

The exact committed sources were transferred as hash-verified complete and
incremental Git bundles and checked out detached on the Windows worker. The
validated revisions were miniaudio
`1f49a2ff6ad1ae36b08b669562dd96244a9a4673` and timer_wheels
`392cab579f6c6556df16131bea878832a0b585ee`.

The first MSVC run exposed a real portability defect in timer_wheels: MSVC did
not deduce the Boolean parameter in a fallback `move_only_function` partial
specialization from `noexcept(Noex)`. That production fix spells throwing and
non-throwing function types as separate specializations. Subsequent runs found
two test-infrastructure assumptions: the broad suite unnecessarily pulled in
Gentest's LLVM and Boost dependencies for this check, and taking the address of
an implementation-private inherited `std::move_only_function::operator()` is
not portable. The final dependency-free targets retain exact member-signature
checks for timer_wheels itself and compare the standard wrapper through its
specified cv/ref invocability, exact return type, and `noexcept` behavior.

The final closure used Windows `10.0.26200` x64, Visual Studio 2022 Build Tools,
MSVC `19.44.35228`, Windows SDK `10.0.26100.0`, CMake `4.4.3`, and Ninja
`1.12.1`. Fresh Debug builds produced these results:

| Gate | Result |
| --- | --- |
| final miniaudio architecture, core, allocation, and timer integration | 4/4 passed |
| cached `a46be19` project pin migration | migrated to `392cab5`; timer test passed |
| explicit historical override | configuration rejected it and named both exact revisions |
| dependency-free C++20/C++23 portability | 2/2 passed; all 12 throwing/noexcept cv/ref forms compiled and ran in both modes |

The final configure trace recorded zero dependency fetches and prohibited tool
invocations. Steam Audio was not installed, so Windows validates the portable
architecture-mapping regression but does not claim native compilation of the
optional adapter. The final Windows result is MSVC-only. MinGW/GCC is
permanently owner-rejected and was neither installed, probed, nor invoked; only
an explicit owner policy reversal can reopen that target.

The final exact-commit Astra/max continuation is `61ee7c3793564c1f` under
Batch Codex job `cc0e232fb5e12d4c`. The earlier continuation
`40a100864f87ff38` supplies the unchanged timer migration, fail-closed, and
C++20/C++23 portability evidence at the same timer revision. Compact results
and hashes of the retrieved transcripts, summaries, lifecycle records, and
test logs are in
[`validation/windows-msvc-native-2026-10-09.json`](validation/windows-msvc-native-2026-10-09.json).
All 99 final artifact hashes were independently verified after retrieval.

Historical task roots totaling 2,667,218,986 bytes were removed after evidence
verification. The two final exact-commit roots total 685,911,482 logical bytes
and remain retained: Windows denied the process-enumeration query needed to
exclude executable or command-line references, so the cleanup failed closed
without renaming or deleting them. The durable cleanup record is
[`validation/windows-msvc-native-cleanup-2026-10-09.json`](validation/windows-msvc-native-cleanup-2026-10-09.json).

## Native macOS/Apple Clang

The returned work Mac validated the same exact commits in detached clean
worktrees. macOS `26.5.2` arm64, Xcode `26.6`, Apple Clang `21.0.0`,
CMake `4.3.4`, Ninja `1.13.2`, and pkg-config `2.5.1` produced a fresh
Release result of **5/5 passed**:

- Steam Audio architecture mapping;
- core rendering and exchange behavior;
- allocation invariants;
- timer_wheels integration; and
- pkg-config prefixes containing spaces.

The three built executables were verified as arm64 Mach-O files. Steam Audio
was not installed on this host, so its adapter was not compiled; the
architecture-mapping regression still ran. CoreAudio was disabled and no
physical audio was attempted because this gate targets deterministic engine
and packaging behavior; the separate hardening record covers interactive
device testing.

The Astra/max validation job was `1415dacb35f1b348`. Its 196-file evidence
manifest was independently hash-verified. Cleanup continuation
`29167d4b6671e27d` used native process, command-line, cwd, open-file, mount,
identity, and source checks before atomically quarantining and deleting only
the exact validation root. Both original and quarantine paths are absent;
595,124,224 allocated bytes were removed. The compact evidence and hashes are
in [`validation/macos-native-2026-10-09.json`](validation/macos-native-2026-10-09.json).

## Cache benchmark

Command:

```sh
taskset -c 8 \
  build/cpp-engine/extras/miniaudio_cpp_engine/miniaudio_cpp_engine_cache_benchmark
```

Method: 21 paired samples, 50,000 repetitions per sample, rotating execution
order, fixed CPU affinity, compiler escape barrier, and an untimed complete
sample-by-sample equivalence check before measurement. The practical threshold
chosen before interpreting the final run was 3%.

| Adapter | Median ns/frame |
| --- | ---: |
| interleaved shifting cache | 1.2765 |
| planar shifting cache | 0.7507 |
| planar cursor cache | 0.7676 |

The paired planar-shift/cursor ratio was `0.9790`: the cursor implementation was
about 2.1% slower, below the 3% practical threshold. Therefore this evidence
does **not** support a throughput-speedup claim. The cursor design is retained
for bounded callback work (no remaining-sample `memmove`) and a planar layout
that composes directly with Steam Audio. The full raw rows are in
[`validation/cache-benchmark-linux-gcc16.csv`](validation/cache-benchmark-linux-gcc16.csv).

## Independent review

Three read-only `gpt-6-astra` / `max` Batch Codex passes reviewed real-time and
memory-model behavior, build/portability integration, and test/benchmark/spec
integrity. Their initial job was `f77368a16596236f`. It found mutable Steam
Audio HRTF scratch shared across concurrent renders and checkout-only SDK
discovery. Continuations `5f7330a09ec831d6` and `584eaeeca313fda6` then
found a scheduler-dependent contention test, unquoted pkg-config paths, and
over-broad platform-to-SDK mapping.

The repairs added a public nonblocking HRTF render lease and deterministic
contention test, packaged-SDK discovery, quoted metadata, and exact supported
system/architecture mapping with negative regressions. Final continuations
`e9c7781457904be9` and `9614495aec8229b8` closed the real-time and build
tracks with no P0-P3 findings. Reproduced findings were converted into
regression tests or focused configuration checks rather than waived.

## Remaining limits

- This is a playback vertical slice, not a complete replacement for
  `ma_engine`; capture, codecs/resource management, hotplug, resampling, and
  Steam Audio tail/EOF draining still need contracts and implementations.
- The exchange deliberately fails compilation where pointer or 64-bit atomics
  are not always lock-free. No 32-bit runtime was available for execution.
- The benchmark does not show a throughput win: the cursor cache was about
  2.1% slower than planar shifting, below the predefined 3% practical
  threshold. Its value is bounded callback work and native planar composition.
- Native Steam Audio compilation is validated on Linux only because neither
  native Windows nor macOS had that optional SDK installed. Platform discovery
  itself is deterministic and covered on all three hosts.
- Additional architecture-specific DSP and packing combinations remain
  untested. Windows x64 and macOS arm64 results are native and are not inferred
  from Linux.
