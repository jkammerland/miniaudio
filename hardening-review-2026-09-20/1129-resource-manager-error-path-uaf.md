# Issue #1129: resource-manager error-path UAF

## Assessment

- **Priority:** P0-related memory safety
- **Exact checkout:** affected defect family
- **Local reproduction:** covered by the #1141 failed-load reproducer
- **Platform:** backend-independent

#1129 is an earlier report of the resource-manager data-buffer error-path
use-after-free. Upstream described a potential development-branch fix. The
release code reviewed here still frees a newly allocated node and then accesses
and publishes its pointer in
`ma_resource_manager_data_buffer_node_acquire` at
`miniaudio.h:70915-70933`.

The primary local evidence and implementation proposal are in
[the #1141 review](1141-resource-manager-failed-load-uaf.md). This file exists
separately so the upstream issue number remains traceable.

## Representative failing pattern

```c
if (result != MA_SUCCESS && nodeAlreadyExists == MA_FALSE) {
    ma_resource_manager_data_buffer_node_remove(pResourceManager,
                                                 pDataBufferNode);
    ma_free(pDataBufferNode, &allocationCallbacks);
}

/* pDataBufferNode is still dereferenced and returned below. */
if (pDataBufferNode->isDataOwnedByResourceManager) {
    ...
}
*ppDataBufferNode = pDataBufferNode;
```

## Potential fix and tests

Use one ownership-safe cleanup path that snapshots required flags before free,
sets the local pointer to null after free, returns a null output on failure, and
tracks notification/fence initialization explicitly. Do not add a second
issue-specific workaround beside the #1141 fix.

The regression matrix must identify #1129 as covered and include its original
trigger if an attachment is available, alongside missing/corrupt assets,
allocation failure, custom VFS errors, and sync/async/WAIT_INIT modes. Exact
free, close, fence, and completion counts are required under ASan/UBSan.

## Upstream state

Closed upstream means the development branch was changed. It does not mean the
0.11.25 fork contains the fix. The affected source remains present and the
same family is locally reproducible.
