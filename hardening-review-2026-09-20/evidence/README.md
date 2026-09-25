# Durable hardening evidence

This directory replaces the transient `/tmp` evidence locations used during
the 2026-09-20 hardening review. The repository documents should reference
these files and archives, not the deleted temporary paths.

## Contents

- `audit/` contains the original issue inputs and the directly cited parser,
  resource-manager, and DSP reproduction results. `PLAN.md` has durable
  relative links; `PLAN.original.md` is its byte-exact temporary-tree version.
- `native-windows-fee518b-primary.tar.zst` contains the final #1152 microphone
  and #484 process-loopback campaigns, including every final campaign WAV,
  metric, event record, helper source, command log, and report.
- `windows-1152-prior.tar.zst`, `windows-0484-prior.tar.zst`, and
  `native-windows-346acda-prior.tar.zst` retain the earlier campaigns cited by
  the issue notes.
- `native-windows-06e3a06-final.tar.zst` and
  `native-macos-e4658df-final.tar.zst` retain the final native validation
  reports and command-level evidence.
- `native-macos-835a14f-physical.tar.zst` retains the real-device CoreAudio
  built-in playback/capture, Bose duplex, and manual output-reroute/hotplug
  evidence. It also records the interactive runner stdin defect and preserves
  hashes plus bounded extracts for the omitted 5.74 GB prompt-spam logs.
- `native-macos-835a14f-physical-cleanup.md` records the exact allowlisted Mac
  cleanup performed only after that archive was committed and verified.
- `steam-audio-hardening.tar.zst` and `steam-audio-native-text.tar.zst` retain
  the Steam Audio integration evidence needed by this review.
- `native-cleanup-records.tar.zst` retains the native-host cleanup reports.
- `linux-cxx23-0f459ed-evidence.tar.zst` retains the compact GCC and Clang
  C++23 build/test receipts, negative conformance-guard outputs, and the final
  Clang tests/examples/tools compile-command inventory for exact commit
  `0f459ed`.
- `validation-logs.tar.zst` retains the small top-level lifecycle, manifest,
  optional-codec, and sanitizer logs that were otherwise only in `/tmp`.
- `cleanup-deleted.txt` enumerates the local temporary roots removed after the
  durable evidence commit; `cleanup-retained.txt` records the one live root
  that was deliberately left in place.

`SHA256SUMS` authenticates every durable file except itself. Verify the set
from this directory with:

```sh
sha256sum -c SHA256SUMS
for archive in *.tar.zst; do zstd --test "$archive"; done
```

Extract an archive with, for example:

```sh
tar --use-compress-program=unzstd -xf native-windows-fee518b-primary.tar.zst
```

## Retention policy

The archives intentionally omit rebuildable executables, object/debug files,
copied full miniaudio headers, build directories, and generic generated audio
suite output. The exact commits and source hashes recorded in the evidence are
the source of those rebuildable artifacts. The final Windows archive does
retain the unique physical microphone captures and process-loopback captures
and reference tones. Earlier campaigns retain their reports, event data,
metrics, and uniquely relevant captures but not redundant tool binaries.
Original offload manifests are retained as provenance inventories; because
the durable archives deliberately omit rebuildable files, those original
full-set manifests are not curated-archive manifests. `SHA256SUMS` is the
authoritative integrity record for the durable set.

The local cleanup was limited to top-level temporary entries owned by this
review whose names began with `miniaudio`, `steam-audio`, or `hardening`, plus
the review's named cleanup helpers. On 2026-09-23, 280 enumerated entries
(approximately 20.5 GiB) were removed after the evidence was committed at
`475b59e`. Nine additional top-level home build/evidence directories
(approximately 1.7 GiB) were removed after the Linux C++23 evidence was
committed at `28e2145`; all 289 exact paths are listed in
`cleanup-deleted.txt`. A still-running debugger tree was excluded and is
listed in `cleanup-retained.txt`.

On 2026-09-25, the exact physical CoreAudio source, build, staged bundle, and
remote evidence roots were removed after archive commit `d526814`. Their
pre-cleanup allocation was 6,216,464 KiB and the filesystem gained 6,200,020
KiB of available space. The retained cleanup record lists every selected path
and confirms the original and quarantine names are absent.
