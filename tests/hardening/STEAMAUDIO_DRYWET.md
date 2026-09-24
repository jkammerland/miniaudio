# Adjustable Steam Audio dry/wet policy

Issue #407 is an integration-policy decision, not a demonstrated Steam Audio
core defect. The miniaudio example now supplies a conventional, engine-owned
dry/wet control. The Steam Audio binaural effect always processes at
`spatialBlend = 1`; the node combines its stereo wet output with the original
dry input:

```text
outL = (1 - wet) * dryL + wet * binauralL
outR = (1 - wet) * dryR + wet * binauralR
```

For mono input, `dryL = dryR = mono`. For stereo input, each dry channel is
preserved. This linear law is not Steam Audio's HRTF-domain `spatialBlend`
semantics. It has unity dry gain at `wet = 0` and unity wet gain at `wet = 1`;
correlated signals may have different intermediate power. The default is
`wet = 1`, preserving the example's previous fully spatialized output.

Run `engine_steamaudio <input-file> [wet 0..1]` to set an initial value.
`ma_steamaudio_binaural_node_set_dry_wet()` accepts a finite value in `[0, 1]`
from a control thread. It publishes one atomic target; the audio thread samples
it once per fixed Steam Audio block and ramps from the previous target to the
new one across that block. A concurrent publication affects the next block,
not half of the current one. Existing queued output still plays before the
new block. Reset requires the established detach/quiesce contract and starts
at the most recently published target. During tail processing the dry signal
is zero, so the same gain law fades or mutes the effect tail.

The node uses its existing preallocated input/output storage for mixing. The
new setter and mixing path do not allocate or lock; the deterministic test hook
that pauses a callback exists only in the regression test translation unit.

`hardening_steamaudio_drywet.c` compares the adapter with a separate public
Steam Audio effect used only to generate the fully spatialized wet reference.
It calculates the mix independently from distinct mono/stereo samples. The
matrix covers impulse and tone, wet values 0/0.5/1, nearest and bilinear HRTF
interpolation, and front/side/back/elevated directions (96 cases). Every case
uses fragmented 17/47-frame callbacks and checks exact frame accounting,
finite, nonzero wet output, exact endpoints, the sample-level gain/phase law,
and zero callback allocations. Other cases check invalid values, a one-block
runtime ramp, tail attenuation, reset, and a deterministic control-thread
publication while the callback holds a target snapshot. The existing
`miniaudio_hardening_steamaudio_frames` test continues to cover 240/256
periods, scheduled starts, EOF, and seek behavior.

The original example has no dry/wet control: adding the regression before
implementation failed to compile because the config had no `dryWet` or
`interpolation` field and the setter did not exist. The exact RED compiler
output is retained in Steam Audio's
`.hardening-artifacts/mono-drywet/red-build.log` (SHA-256
`0f47a4ea4772cc7408dd36d2fdcfae5f10335704a131354a7f27cc3e7b46b2ff`).
This is missing-feature evidence, not a claim of a core defect.

Linux validation (from the two local source checkouts):

```sh
cmake --build /tmp/steam-audio-miniaudio-teardown-debug -j 4
ctest --test-dir /tmp/steam-audio-miniaudio-teardown-debug --output-on-failure
cmake --build /tmp/steam-audio-miniaudio-frames-release -j 4
ctest --test-dir /tmp/steam-audio-miniaudio-frames-release --output-on-failure
cmake --build /tmp/steam-audio-miniaudio-teardown-asan-ubsan -j 4
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir /tmp/steam-audio-miniaudio-teardown-asan-ubsan --output-on-failure
cmake --build /tmp/steam-audio-miniaudio-teardown-tsan -j 4
TSAN_OPTIONS=halt_on_error=1 ctest --test-dir /tmp/steam-audio-miniaudio-teardown-tsan --output-on-failure -R '^miniaudio_hardening_steamaudio'
```

The full Debug, Release, and ASan/UBSan suites passed 20/20; the related TSan
suite passed 6/6. Native Windows and macOS exact-commit results are recorded
with the issue assessment after they complete.

Limitations: the gain law is linear amplitude, not constant-power; it is not
equivalent to Steam Audio's stereo spatial-blend path. The example still uses
one audio processing lane. A later coroutine engine must choose its own
worker/HRTF ownership and control-publishing architecture.
