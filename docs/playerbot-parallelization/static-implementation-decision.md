# Static Implementation Decision

Decision: PROCEED TO PHASE 0 AND PHASE 1 ONLY. Phase 2 is blocked until the MMAP read/write lease is
implemented and reviewed.

## Summary

Proceed:

- measurement;
- compute pool infrastructure;
- static guard updates;
- MMAP lease design/implementation as a prerequisite for path compute.

Do not proceed yet:

- worker Detour path reads;
- path job submission;
- dry-run path comparison;
- worker output consumption.

## Required Order

1. Read `START-HERE.md`.
2. Revalidate `source-anchors.md`.
3. Phase 0 measurement.
4. Record `PATH_OFFLOAD_PROCEED` or `PATH_OFFLOAD_STOP` in the baseline.
5. Phase 1 compute pool.
6. Phase 2 Task 2.0A MMAP source-anchor revalidation, only if baseline says `PATH_OFFLOAD_PROCEED`.
7. Phase 2 Task 2.0B MMAP lease design.
8. Phase 2 Task 2.0C MMAP lease implementation, only when implementation is explicitly requested.
9. Phase 2 Task 2.0D static review of MMAP lease.
10. Phase 2 Task 2.0E runtime verification of MMAP lease.
11. Detour-only path helper.
12. Path job.
13. `MoveToLOS` dry-run shadow mode.
14. Staging dry-run verification.
15. Worker output consumption only after dry-run is clean.

## Rejected Paths

- owner-free `PathGenerator` constructor as the primary design;
- map-start value precompute;
- worker-side generic values;
- worker-side action execution;
- action intent batching in this pass;
- using raw `GetNavMesh`, `GetNavMeshQuery`, `loadMapData`, or `loadMap` in workers;
- worker Detour reads without MMAP synchronization.

## Confidence After Static Analysis

Measurement and compute-pool work are low risk.

Path compute is feasible only if MMAP shared navmesh access is synchronized first. Without the MMAP
lease, the design must stop before Phase 2 worker reads.
