# Steam Audio fixed-frame adapter benchmark

`benchmark_steamaudio_frames.c` times the example adapter's offline node
callback for mono and stereo input. It uses a real miniaudio node graph and
Steam Audio context/HRTF/effect initialization, but substitutes a deterministic
copy for `iplBinauralEffectApply` inside the timed callback. This isolates frame
staging and FIFO work from HRTF convolution. The effect substitute asserts
256-sample input and output descriptors; each trial checks exact input/output
accounting, every output sample against a 256-sample-latency oracle, nonzero
finite output, and zero callback allocations/frees through the Steam Audio and
miniaudio allocator hooks.

The callback periods are fixed 256, fixed 240, and a repeated sequence of
`1, 17, 255, 256, 257, 511` frames. Each of seven trials per configuration
processes 4,194,304 input frames after 1,024 warmup frames. The CSV records
every trial and its median, callback count, effect-call count, time per input
frame/callback, and output checksum. Timing covers the adapter callback loop;
input generation and output verification happen outside the timer.

From an exact Steam Audio and miniaudio checkout, with an already built Steam
Audio library and its generated headers:

```sh
core_root=/absolute/path/to/steam-audio
miniaudio_root=/absolute/path/to/miniaudio
core_build=/absolute/path/to/steam-audio-build
cmake -S "$miniaudio_root" -B /tmp/miniaudio-fifo-bench -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DMINIAUDIO_BUILD_TESTS=ON \
  -DMINIAUDIO_BUILD_STEAMAUDIO_BENCHMARKS=ON \
  "-DSTEAMAUDIO_INCLUDE_DIR=$core_root/core/src/core" \
  "-DSTEAMAUDIO_LIBRARY=$core_build/src/core/libphonon.so" \
  "-DCMAKE_C_FLAGS=-I$core_build/src/core"
cmake --build /tmp/miniaudio-fifo-bench --target miniaudio_benchmark_steamaudio_frames
/tmp/miniaudio-fifo-bench/miniaudio_benchmark_steamaudio_frames > fifo.csv
```

On Linux with GCC 16, the Release run was pinned to CPU 8 with `taskset -c 8`.
The measured medians in nanoseconds per input frame were:

| Input channels | 256 period | 240 period | Fragmented |
| --- | ---: | ---: | ---: |
| 1 | 0.345 | 0.358 | 0.388 |
| 2 | 0.846 | 0.839 | 0.853 |

The Release CSV is retained at
`../steam-audio/.hardening-artifacts/final-gate/miniaudio-fifo-bench-linux-gcc16.csv`
with SHA-256
`d43585dad9b376373b6fc36d9f0c750e2749d8166dfbccda3398684233319860`.
The companion ASan/UBSan/LSan run used:

```sh
cmake -S "$miniaudio_root" -B /tmp/miniaudio-fifo-bench-sanitized -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DMINIAUDIO_BUILD_TESTS=ON \
  -DMINIAUDIO_BUILD_STEAMAUDIO_BENCHMARKS=ON \
  "-DSTEAMAUDIO_INCLUDE_DIR=$core_root/core/src/core" \
  "-DSTEAMAUDIO_LIBRARY=$core_build/src/core/libphonon.so" \
  "-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -I$core_build/src/core" \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined
cmake --build /tmp/miniaudio-fifo-bench-sanitized --target miniaudio_benchmark_steamaudio_frames
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  /tmp/miniaudio-fifo-bench-sanitized/miniaudio_benchmark_steamaudio_frames > fifo-sanitized.csv
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir /tmp/miniaudio-fifo-bench-sanitized --output-on-failure \
  -R '^miniaudio_hardening_steamaudio'
```

It completed all 48 CSV rows with empty diagnostic output. Its CSV SHA-256 is
`a01ea5ac193498b9b4694d5ee8387bad43bc1176d63a8f9639fd3b1e0827ffdd`.
The five related Steam Audio adapter CTests also passed under these sanitizers.

These are single-host baseline measurements, not a before/after optimization
comparison. They exclude HRTF DSP and do not separately quantify graph
scheduling, system allocator calls outside the configured hooks, or callback
locks. The first timed trial includes cold-cache effects; retain the individual
trial rows when comparing changes. Run the same source, compiler, CPU affinity,
frame patterns, and trial count for a meaningful comparison. The benchmark
does not establish a portable latency or throughput target.
