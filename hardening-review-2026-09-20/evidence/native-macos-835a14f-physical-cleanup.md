# CoreAudio physical attempt cleanup record

- Attempt: `coreaudio-physical-835a14f-20260925`
- Host/account: `work-computer-ai-dev` / `ai-dev` (UID 502)
- Durable archive commit: `d526814`
- Durable archive: `native-macos-835a14f-physical.tar.zst`
- Archive SHA-256: `ce0b91a906cf7a35ea1ca8e1babab75f57f40eee6d8a23143a3ca92cd8e971c0`
- Internal archive verification: 35/35 entries passed
- Selected transport verification: 24/24 complete files matched
- State before cleanup: evidence committed; cleanup authorized by lifecycle

## Exact attempt-owned targets

- `/Users/ai-dev/offload/work/miniaudio-coreaudio-835a14f-physical-src`
- `/Users/ai-dev/offload/work/miniaudio-coreaudio-835a14f-physical-build`
- `/Users/ai-dev/offload/staging/miniaudio-835a14f-physical.bundle`
- `/Users/ai-dev/offload/evidence/miniaudio-coreaudio-835a14f-physical-20260925`

The source was clean at exact commit
`835a14fc28a14d96c54f6932ee6121acfcd58a90`; the bundle matched SHA-256
`22e6dbe3da75e39b16263e2f79137ccd80a82f0e768c16f81231d26a5f7dfcb6`.
All targets were owned by `ai-dev`, were not symlinks, and no process referred
to the attempt name during the final preflight. Pre-cleanup allocation was
6,216,464 KiB across the four targets.

## Cleanup result

- Completed: `2026-09-25T16:42:40Z`
- Result: all four original paths were quarantined under exact same-parent
  names, deleted, and verified absent; all four quarantine paths were also
  verified absent.
- Pre-cleanup allocation reported by `du -sk`: 6,216,464 KiB.
- Filesystem available-space increase: 6,200,020 KiB (from 212,699,340 KiB to
  218,899,360 KiB).
- Retained remote ledger:
  `/Users/ai-dev/offload/evidence/miniaudio-coreaudio-835a14f-physical-cleanup.md`.
- Unrelated repositories, caches, bundles, evidence, and offload attempts were
  not selected.
