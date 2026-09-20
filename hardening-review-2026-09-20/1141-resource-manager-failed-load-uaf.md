# Issue #1141: resource-manager failed-load use-after-free

## Assessment

- **Priority:** P0 memory safety/lifecycle correctness
- **Exact checkout:** affected
- **Local reproduction:** confirmed with ASan
- **Platform:** backend-independent
- **Trigger:** an ordinary failed synchronous resource load is sufficient

Initializing a resource-manager data source from a nonexistent file frees a
new data-buffer node and then reads from it. ASan reports:

```text
free: miniaudio.h:70918
read: miniaudio.h:70926
ma_resource_manager_data_buffer_node_acquire
ma_resource_manager_data_buffer_init_ex_internal
ma_resource_manager_data_source_init
```

Evidence: `/tmp/miniaudio-audit-20260920/repro-1141.log`.

This is not limited to hostile media. Missing files, corrupt files, custom VFS
errors, or allocation failures can enter failed-load cleanup.

## Suspect code

The common `done` path around `miniaudio.h:70915-70933` removes and frees a new
node but continues to inspect it and publishes its stale pointer:

```c
done:
    if (result != MA_SUCCESS) {
        if (nodeAlreadyExists == MA_FALSE) {
            ma_resource_manager_data_buffer_node_remove(
                pResourceManager, pDataBufferNode);
            ma_free(pDataBufferNode,
                    &pResourceManager->config.allocationCallbacks);
        }
    }

    if (nodeAlreadyExists == MA_FALSE &&
        pDataBufferNode->isDataOwnedByResourceManager &&
        (flags & MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC) != 0) {
        ...
    }

    if (ppDataBufferNode != NULL) {
        *ppDataBufferNode = pDataBufferNode;
    }
```

Simply reversing the boolean operands is not a complete fix: it may avoid this
read in one failing mode while still publishing a dangling pointer, leaking or
double-destroying notification state, and leaving fence completion ambiguous.

## Potential minimal fix

Capture cleanup facts only while the node is valid, track each initialized
resource explicitly, and clear the pointer immediately after freeing:

```c
isOwned = pDataBufferNode != NULL &&
          pDataBufferNode->isDataOwnedByResourceManager;

if (result != MA_SUCCESS && !nodeAlreadyExists) {
    if (pDataBufferNode != NULL) {
        ma_resource_manager_data_buffer_node_remove(
            pResourceManager, pDataBufferNode);
        ma_free(pDataBufferNode, &allocs);
        pDataBufferNode = NULL;
    }
}

if (!nodeAlreadyExists && isOwned && async && initNotificationInitialized) {
    ma_resource_manager_inline_notification_uninit(&initNotification);
}

if (ppDataBufferNode != NULL) {
    *ppDataBufferNode = result == MA_SUCCESS ? pDataBufferNode : NULL;
}
```

The real patch must audit every exit from
`ma_resource_manager_data_buffer_node_acquire_critical_section` and the outer
acquire function. Required invariants are:

1. A new node is either inserted and returned with one owned reference, or
   removed/freed exactly once and never published.
2. An existing shared node is not removed due to another caller's failure.
3. Inline notification init/uninit happens exactly once when initialization
   actually succeeded.
4. Init/done fences receive exactly the documented acquire/release/signal
   sequence on success, synchronous failure, posted asynchronous failure, and
   job-post failure.
5. No caller is asked to uninitialize an object whose initialization failed.

## Separate asynchronous retirement risk

This issue's immediate UAF occurs in failed initialization. Nearby release
paths have another source-supported risk: load jobs may publish `MA_SUCCESS`
before incrementing their `executionPointer`. Current code at
`miniaudio.h:71033` and `71337` can free based only on the result. Development
code adds waits for `executionPointer == executionCounter`.

That change should not be folded into this fix without a deterministic test. A
wait can deadlock if executed on the only thread capable of retiring the job.
Use barriers to force publish-before-retire and prove worker/manual-pump
behavior first; if reproduced, land it as a separate commit.

## Required regression tests

1. Nonexistent and corrupt files in synchronous, ASYNC, and
   ASYNC|WAIT_INIT modes.
2. Injected VFS open/read/seek/close failure and allocation failure at each
   node, path-copy, notification, decoder, and decoded-buffer allocation.
3. Job queue full/post failure, with and without resource-manager workers.
4. Existing shared nodes and caller-supplied registered data.
5. Assertions for null output on failure, exact node/path/decoder close/free
   counts, balanced fences, and no late completion using freed storage.
6. ASan/UBSan for the original path; deterministic ASan/TSan load/uninit
   barriers for the separate retirement case.

Tests must use explicit synchronization barriers, not sleeps or retries.

## Compatibility and integration impact

No public API or layout change is expected. Error outputs become reliably null,
which is the safe interpretation of failed initialization. Resource-manager
uninitialization remains control-thread work and may wait for workers; it must
not run in an audio callback.

For coroutine integration, operation storage must be registered before calling
an API that can complete during initialization. A stale `resume_token` protects
only the coroutine frame; resource-manager jobs and callback userdata still
need independent ownership until physical retirement.

The issue was closed upstream as already fixed in development. That fix is not
present in this checkout, as the fresh ASan reproduction demonstrates.
