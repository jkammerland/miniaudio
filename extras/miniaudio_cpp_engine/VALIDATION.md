# Validation record

Date: 2026-10-09. All paths below refer to the dedicated
`feat/cpp-engine-contract` worktree. The implementation is opt-in; the existing
miniaudio build remains the default.

## Provenance

| Item | Value |
| --- | --- |
| miniaudio base | `ab4bd4bfb30e2321b37207fad7cc6c1eb906c096` |
| timer_wheels integration | `f5853d075ba011efc5bd872ce892f1162d51db8a` (clean worktree) |
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

Result: **24/24 passed**. The four new test executables establish:

- graph composition, gain equivalence with `ma_gainer`, callback-size timeline,
  channel semantics, lock-free publication, and observed control-thread graph
  destruction;
- no host C/C++ allocation on first render, plan adoption, or 10,000 mixed-size
  steady-state pulls, plus allocation-failure ownership preservation;
- mono and stereo Steam Audio rendering, direct wet-output equivalence,
  dry/wet ramping, HRTF-setting rejection, direction edge cases, and zero host
  or Steam allocator calls during rendering; and
- timer expiry, cancellation, `when_any(cancel_remaining)` cleanup, scheduled
  plan publication, and cancelled-candidate destruction on the owner thread.

## Sanitizers

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

Result: **4/4 passed**.

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
| staged install and `pkg-config --static miniaudio-cpp-engine` consumer | built successfully |

The install consumer exercised the public headers, both static archives, PIC,
and the platform runtime flags emitted by pkg-config. Optional Steam Audio and
timer_wheels development dependencies remain external and are never copied into
the install tree.

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
integrity. Their initial job was `ca9f276fe311651d`; closure continuations were
used after each repair pass. Reproduced findings were converted into regression
tests or focused configuration checks rather than waived.

## Remaining limits

- This is a playback vertical slice, not a complete replacement for
  `ma_engine`; capture, codecs/resource management, hotplug, resampling, and
  Steam Audio tail/EOF draining still need contracts and implementations.
- The exchange deliberately fails compilation where pointer or 64-bit atomics
  are not always lock-free. No 32-bit runtime was available for execution.
- Native Windows and macOS builds are recorded separately when those worktrees
  contain this exact implementation commit; the Linux evidence above does not
  stand in for them.
