# Experimental miniaudio C++ engine

This opt-in prototype tests a smaller engine contract without replacing
miniaudio's mature device and codec backends. See [SPEC.md](SPEC.md) before using
the API.

The current playback slice has no decoder or resource-manager layer, so it does
not require Opus or Vorbis development packages. Those remain optional future
decoder-adapter dependencies and should be supplied by the consuming build
when that contract exists; they are not fetched or vendored here.

Configure against the current timer_wheels PR-stack worktree:

```sh
cmake -S . -B build/cpp-engine -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DMINIAUDIO_BUILD_CPP_ENGINE=ON \
  -DMINIAUDIO_BUILD_TESTS=ON \
  -DMINIAUDIO_CPP_ENGINE_TIMER_WHEELS_SOURCE_DIR="$HOME/repos/timer_wheels-miniaudio-latest"
cmake --build build/cpp-engine
ctest --test-dir build/cpp-engine --output-on-failure \
  -R miniaudio_cpp_engine
```

The source-path dependency is consumed in place as headers. It is not copied
into this repository. Configuration verifies the Git revision and fails closed
if it differs or cannot be read. The project-owned revision pin is not stored
as a user cache value, so an existing build directory follows pin updates. An
intentional alternate revision must use
`MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_REVISION_OVERRIDE`; this is separate from
the tested default. For an independently verified exported tree, revision
checking can be explicitly disabled with
`MINIAUDIO_CPP_ENGINE_VERIFY_TIMER_WHEELS_REVISION=OFF`.
The supplied scheduling helper can own and publish a prepared render plan after
a cancellable timer expires; both the timer queue and coroutine object remain
control-thread-owned. Consumers of that header link the optional
`miniaudio::cpp_engine_timer_wheels` target rather than the core target alone.

To validate the optional Steam Audio node against the sibling checkout and a
library built from that checkout:

```sh
cmake -S . -B build/cpp-engine \
  -DMINIAUDIO_STEAMAUDIO_SOURCE_DIR=/path/to/steam-audio \
  -DMINIAUDIO_STEAMAUDIO_LIBRARY=/path/to/steam-audio/build/src/core/libphonon.so
```

The exact library location is build-specific. The source checkout is not added
with `add_subdirectory()` yet because its dependency selection currently does
not satisfy its own default C++14 configuration. Keeping it as an independently
built dependency avoids changing Steam Audio's compile contract from here.
The C++ adapter creates HRTFs through `steam_audio_hrtf`, which binds the sample
rate and frame size to the handle. Raw HRTF handles are intentionally not
accepted by a binaural node because Steam Audio cannot validate a mismatched FFT
size at that boundary. A checkout root (`unity/include/phonon` and
`unity/lib/<platform>`) or a packaged SDK root (`include` and
`lib/<platform>`) is accepted. Nodes may share an HRTF when their renders are
serialized; overlapping use is suppressed without blocking because Steam
Audio's HRTF interpolation scratch is mutable. Direct native-handle rendering
participates in the same exclusion contract through `try_acquire_render()`.

With `MINIAUDIO_INSTALL=ON`, the core and optional Steam Audio archives, public
headers, and `miniaudio-cpp-engine.pc` metadata are installed. The timer and
Steam Audio adapter headers still require their respective development
dependencies; those dependencies are not copied or vendored by this project.

The cache benchmark is opt-in and requires GCC or a GNU-compatible Clang mode:

```sh
cmake -S . -B build/cpp-engine -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DMINIAUDIO_BUILD_CPP_ENGINE=ON \
  -DMINIAUDIO_CPP_ENGINE_BUILD_BENCHMARKS=ON
cmake --build build/cpp-engine --target miniaudio_cpp_engine_cache_benchmark
taskset -c 8 build/cpp-engine/extras/miniaudio_cpp_engine/miniaudio_cpp_engine_cache_benchmark
```

The benchmark first compares every output sample across implementations. Its
current result is evidence about this workload, not a general speed claim; see
[VALIDATION.md](VALIDATION.md).
