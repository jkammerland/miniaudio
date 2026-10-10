# Experimental miniaudio C++ engine

This opt-in prototype tests a smaller engine contract without replacing
miniaudio's mature device and codec backends. See [SPEC.md](SPEC.md) before using
the API.

The [2026-10-10 synchronization report](SYNC-REPORT-2026-10-10.md) records the
validated revisions, remaining work, and proposed Steam Audio tail/EOF contract.
That report is a historical snapshot; the implemented stream/tail API is
specified below and in [SPEC.md](SPEC.md).

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

## Finite playback and offline output

Use `compile_stream()` when the graph must end. Existing `compile()`,
`source_callback`, `callback_effect_node`, and `pull()` remain available with
their original continuous-render behavior. The stream compiler rejects
reachable opaque effects; use `tail_free_effect_node` only when the callback
really has no delayed output, or implement `stream_node` for a stateful effect.

A finite mono callback can expose an already-decoded clip without allocating:

```cpp
#include <miniaudio_cpp_engine/quantum_adapter.hpp>
#include <algorithm>
#include <span>

using namespace miniaudio::cpp_engine;

struct mono_clip {
    std::span<const float> samples;
    std::size_t cursor{}; // render-owned while the plan is active
};

auto read_clip(void* state, process_context, planar_view output) noexcept
    -> source_result {
    auto& clip = *static_cast<mono_clip*>(state);
    const auto count = static_cast<std::uint32_t>(
        std::min<std::size_t>(output.frames(), clip.samples.size() - clip.cursor));
    if (count != 0)
        std::copy_n(clip.samples.data() + clip.cursor, count, output.channel_data(0));
    clip.cursor += count;
    return {count, clip.cursor == clip.samples.size()};
}
```

Keep the clip and its samples alive until the containing plan is reclaimed.
The engine zero-pads the suffix, distinguishes temporary starvation from EOF,
and never calls a completed source again. Build and render it like this:

```cpp
const stream_contract contract{channel_layout::mono(), 48'000, 256};
mono_clip clip{pcm}; // pcm is an application-owned span<const float>
render_plan_builder builder{contract};
const auto source = builder.add(
    std::make_unique<finite_source_node>(contract.layout, &clip, read_clip));
auto plan = std::move(builder).compile_stream(source);
quantum_output_adapter adapter{contract};
planar_buffer block{contract.layout, 97}; // arbitrary writer/backend request
auto render = [&](process_context context, planar_view output) noexcept {
    return plan->process_stream(context, output);
};
for (;;) {
    const auto result = adapter.pull_stream(block.view(), render);
    // Write only block.view().channel(0).first(result.valid_frames) to the file.
    // The final complete (or failed) pull can still contain valid samples.
    if (result.state == stream_state::failed)
        throw std::runtime_error("stream rendering failed");
    if (result.state == stream_state::complete)
        break;
}
```

`valid_frames` is a frame count per channel. For interleaved output, write
`valid_frames * channels` samples. Backend padding is silence, not file content.
Do not mix legacy `pull()` and `pull_stream()` without resetting the adapter
while stopped. There is no decoder/resource-manager implementation in this
slice; finite callbacks consume application-supplied PCM.

For device playback, publish that stream plan normally. Natural EOF drains
its effects automatically. `request_drain()` also ends a continuous source at
the next quantum and drains the graph:

```cpp
// Control owner: the desired plan has been adopted; no replacement is pending.
const auto target = exchange.acknowledged_generation();
const auto request = device.request_drain();
// Later, on the same owner loop (not by blocking the render callback):
if (request == drain_request_status::queued) {
    if (device.failed_generation() == target) {
        // Report failure; this is not successful completion.
    } else if (device.completed_generation() == target) {
        // The final samples have been copied into a backend callback buffer.
        // The device remains running silently and can accept a new plan.
    }
}
```

Requests target the active generation observed inside `request_drain()`, not
pending or future publications. Keep publication serialized on the control
owner; a replacement interrupts the old graph instead of implicitly draining
it. `completed_generation()` and `failed_generation()` are exact last-result
IDs, not cumulative watermarks. Completion includes delivery of every cached
tail frame but does not prove hardware audibility. Existing `stop()` is still
immediate. The timer helper can schedule an owner-thread `request_drain()`;
cancelling that scheduled action does not end the stream.

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

In a `compile_stream()` graph, the binaural node renders the zero-padded final
input block, then uses Steam Audio's tail-size and GetTail APIs until the SDK
reports completion. The final GetTail call can contain valid output even when
its state is complete. Mono or stereo inputs, serial effects, partial final
tails, and mixed callback sizes follow the same stream contract above. Use a
stereo graph output contract and insert the binaural node after the finite
source. SDK-required padded output is retained; no amplitude-based trimming or
wall-clock timeout decides EOF. A suppressed HRTF lease fails the stream
without consuming its tail instead of silently reporting success.

With `MINIAUDIO_INSTALL=ON`, the core and optional Steam Audio archives, public
headers, and `miniaudio-cpp-engine.pc` metadata are installed. The timer and
Steam Audio adapter headers still require their respective development
dependencies; those dependencies are not copied or vendored by this project.
The standalone `tests/installed_consumer` CMake project exercises both legacy
and stream APIs using only a staged install. Set
`MINIAUDIO_TEST_INSTALL_PREFIX` to that absolute prefix and use a fresh consumer
build directory. Configure the library's `CMAKE_INSTALL_PREFIX` to the same
path before installation: an install-time `--prefix` alone does not relocate
the configured absolute pkg-config paths.

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
