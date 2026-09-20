# Issue #1139: duplicate resource-manager failed-load UAF report

## Assessment

- **Priority:** P0-related memory safety
- **Exact checkout:** affected defect family
- **Local reproduction:** covered by #1141
- **Platform:** backend-independent

Upstream closed #1139 as a duplicate of #1129. It reports the same lifecycle
family later demonstrated by #1141: a failed data-buffer load reaches cleanup,
frees the node, and then uses or exposes the stale node pointer.

See [the #1141 review](1141-resource-manager-failed-load-uaf.md) for the ASan
stack, source locations, fix invariants, and full test matrix. See
[the #1129 review](1129-resource-manager-error-path-uaf.md) for upstream
traceability.

## Code example

The dangerous invariant is visible at `miniaudio.h:70915-70933`:

```c
ma_free(pDataBufferNode, &pResourceManager->config.allocationCallbacks);

/* Invalid after the free. */
if (pDataBufferNode->isDataOwnedByResourceManager) {
    ...
}

/* Invalid output on failure. */
*ppDataBufferNode = pDataBufferNode;
```

## Potential fix and regression attribution

One shared fix should cover #1129, #1139, and #1141. The change must preserve
ownership state before cleanup, remove/free only newly created nodes, clear the
pointer, publish null on failure, and balance notifications/fences. The test
name or metadata should reference all three issue numbers so later audits do
not mistake duplicate closure for missing coverage.

This duplicate does not justify three divergent patches. It does justify a
single comprehensive lifecycle regression suite.
