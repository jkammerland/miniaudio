# Issue #1157: GCC LTO diagnostics around atomics and node buses

## Assessment

- **Priority:** P1 build/lifetime investigation
- **Exact checkout:** reported code is present
- **Local reproduction:** not reproduced
- **Local toolchain:** GCC 16.2.1, C++23, `-O3 -flto`
- **Reported platform:** Windows GCC/MinGW; compiler version omitted

The report describes `-Wstringop-overflow` diagnostics involving node-bus
`isAttached` and resource-manager atomic fields under LTO. A local build of
`tests/cpp/cpp.cpp` with:

```text
g++ -std=c++23 -O3 -flto -Wall -Wextra -Wpedantic \
    -Werror=stringop-overflow ...
```

succeeds. The only message says LTO uses serial LTRANS. Evidence:
`/tmp/miniaudio-audit-20260920/gcc-lto.log`.

This means the warning is toolchain/configuration-dependent or needs a more
specific instantiation. It does not prove the code safe, and it does not
justify suppressing the warning.

## Relevant code

Node attachment reads atomically at `miniaudio.h:73913`:

```c
if (ma_atomic_load_32(&pOutputBus->isAttached)) {
    ...
}
```

Detach/uninit marks attachment state, updates pointers, and waits for active
read/reference counters around `miniaudio.h:73994-74057`:

```c
ma_atomic_exchange_32(&pOutputBus->isAttached, MA_FALSE);
...
while (ma_atomic_load_32(&pOutputBus->refCount) != 0) {
    ma_yield();
}
```

The report also references atomics in resource-manager result and execution
pointer code around lines 69834 and 72990.

## Investigation plan

1. Obtain the exact GCC/MinGW version, target triple, flags, preprocessor
   defines, warning text, and complete inlined call stack.
2. Rebuild the same translation unit with and without LTO, unity/header-only
   mode, C versus C++, and each relevant feature define.
3. Compare object-size reasoning around embedded/flexible backend structures
   and any casts reaching atomic helpers.
4. Run node lifetime tests under ASan/UBSan/TSan and inspect generated IR or
   optimized dumps only after reproducing the warning.

## Potential fix criteria

Do not replace real atomics with plain loads, weaken assertions, or add a
warning suppression. A code change is warranted only if one of these is proven:

- the pointer can reference an object too small for the atomic field;
- an object is accessed after lifetime end;
- helper signatures/casts obscure valid object size from GCC and can be made
  type-correct without changing synchronization;
- layout/aliasing assumptions violate C or C++ rules under LTO.

If it is a GCC false positive, isolate a small upstream compiler reproducer and
use the narrowest documented workaround after checking generated code and
other compilers.

## Required regression tests

1. Empty graph; attach/detach/reattach; multiple input/output buses.
2. Concurrent offline processing versus control-thread detach only where the
   API permits it, using deterministic barriers.
3. Resource-manager result/execution atomics through success, failure,
   cancellation, and retirement.
4. Exact reporter toolchain with `-O3 -flto` and warnings-as-errors; current
   GCC and Clang/MSVC comparison builds.
5. Runtime sanitizers in addition to compiler diagnostics.

## Real-time and compatibility impact

Node detach can wait for audio readers and therefore must not run in the audio
callback. A future integration should quiesce or schedule graph mutation on a
control path. Any valid fix should preserve public ABI and atomic ordering; no
API change is currently justified.
