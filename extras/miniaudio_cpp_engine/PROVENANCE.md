# Prototype provenance

The dependency revisions below define the initial integration baseline. They
are provenance, not vendored snapshots.

| Component | Revision | Role |
| --- | --- | --- |
| miniaudio | `ab4bd4bfb30e2321b37207fad7cc6c1eb906c096` | Worktree base |
| timer_wheels integration tip | `e9272b5d263859abbe15c4dfd6d123c542e9e9c0` | Latest PR stack plus the GCC constraint-parser compatibility fix |
| Steam Audio source checkout | `4b37ab1aaff9cb7af3df531d0374112856ecc6ca` | Optional binaural implementation reference |

The timer stack, oldest to newest, is:

1. `c14ba06` — cancellable timer waits;
2. `25c1a22` — current cancellation usage contract;
3. `7efaead` — fanouts, optional operation startup, direct callback operations;
4. `f47830f` — preserved comparison evidence;
5. `e9272b5` — parenthesized the inline-input constraint so GCC 16 can parse the public C++20 fanout header; Clang 22 already accepted the original spelling.

Initial Linux validation environment:

- Fedora kernel `7.2.7-200.native.fc44.x86_64`;
- AMD Ryzen 9 7950X;
- GCC `16.2.1 20260819`;
- CMake `4.4.2`;
- Ninja `1.14.0.git`.

Exact command output and benchmark results are recorded after validation in
`VALIDATION.md`.
