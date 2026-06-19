# Playerbot Compute Offload Spec Suite

This directory is the authoritative implementation spec for reducing playerbot latency while
preserving AzerothCore game-loop semantics.

The intended executor is a less capable coding model. Keep every task narrow, default-off,
mechanically verifiable, and easy to revert. Do not combine tasks.

`execution-contract.md` is the single source of truth for reading order, go/stop status, forbidden
worker access, and the measurement gate.

## Objective

Reduce playerbot cost on hot maps by moving only isolated compute work off the map thread.

Do not parallelize the bot loop. Do not move action execution. Do not make core game objects
generally thread-safe.

## Non-Negotiable Requirements

See `execution-contract.md`. This README intentionally does not duplicate the full forbidden-worker
or go/stop tables.

## Key Code Facts

- The playerbot tick enters through `PlayerbotsPlayerScript::OnAfterUpdate` in
  `modules/mod-playerbots/src/Playerbots.cpp`.
- That call happens during per-player map update order in `Map::Update`, not at the start of the
  map tick.
- `Engine::DoNextAction` interleaves triggers, values, multipliers, prerequisites, action
  execution, continuers, and alternatives. It is not a safe low-invasive parallelization boundary.
- `PathGenerator` is not worker-safe as-is. Its six-argument path method still dereferences
  `_source` through deeper calls for unit state, map liquid checks, phase mask, collision height,
  Z normalization, movement capabilities, water checks, and diagnostics.
- `MMapMgr::GetNavMeshQuery` returns a non-thread-safe shared query and lazily mutates its
  instance-query map. Worker jobs must never use it.
- `MMapMgr::loadMap` and `MMapMgr::unloadMap` mutate shared `dtNavMesh` tile state at runtime.
  Worker Detour reads require an explicit MMAP read-safety solution before Phase 2 implementation.
- `Map::MMapLock` is declared/exposed but not acquired in current source review. Treat MMAP as
  having zero active synchronization until the manager-owned lease is implemented.
- `AiObjectContext` includes both per-bot value context and shared value context. Per-bot
  exclusivity does not protect shared values.

## Architectural Boundary

Allowed worker work:

- copied plain inputs;
- leased MMAP/navmesh reads only after the MMAP safety gate in `source-audit.md` is satisfied;
- private Detour query objects;
- local buffers;
- plain output structs.

Forbidden worker work is defined only in `execution-contract.md`.

The map thread remains authoritative. Worker output is advisory until validated on the map thread.

## Phases

Start with `START-HERE.md`. Then run phases in order.

1. `START-HERE.md`: mandatory execution path for a fresh session.
2. `execution-contract.md`: canonical reading order, go/stop status, forbidden access, measurement gate.
3. `COMPACTION-RECOVERY.md`: recovery context for compacted or restarted sessions.
4. `final-architecture-proposal.md`: final source-backed architecture and worker-pool rule.
5. `source-anchors.md`: exact source anchors that must be revalidated before work.
6. `source-audit.md`: source-grounded facts already known.
7. `phase-preimplementation-static-analysis.md`: produce deterministic callgraph, boundary, MMAP,
   path-equivalence, and proceed/stop documents.
8. `failure-mode-checklist.md`: explicit pass/stop gates for known failure modes.
9. `phase-0-measurement.md`: add default-off profiling only.
10. `phase-1-compute-pool.md`: add and test a playerbot compute pool, unused by gameplay.
11. `phase-2-path-compute.md`: add worker-safe Detour-only path compute only after MMAP safety is solved.
12. `phase-3-deferred-work.md`: document deferred work that must not be implemented in this pass.

Legacy filenames `phase-1-async-pathfinding.md`, `phase-2-parallel-values.md`, and
`phase-3-deferred.md` are kept as redirects or compatibility notes only. New work must follow the
phase files listed above.

## Execution Rules For A Less Capable Model

- Execute exactly one task item at a time.
- Edit only the files named by that task item.
- Do not add opportunistic refactors.
- Do not move to the next task after a failed build, test, or guard.
- Revalidate `source-anchors.md` before each implementation phase.
- Apply every relevant item in `failure-mode-checklist.md` before marking a task complete.
- Follow `execution-contract.md` when go/stop status appears duplicated or unclear.
- Do not begin implementation until all Phase -1 static documents exist and say `Decision: PROCEED`.
- Do not infer safety. If a worker path needs a live object or live map API, stop and report.
- If a task asks for judgment not explicitly covered by the docs, stop and request spec refinement.
- Do not implement skipped or deferred phases.
- Do not include code in documentation updates unless a task explicitly asks for code elsewhere.

## Standard Gate

Every implementation task must finish with:

- build succeeds;
- relevant unit tests pass;
- config defaults keep new behavior off;
- self-review confirms only allow-listed files changed;
- no worker job file contains forbidden live-object or mutation access.

## Runtime Gate

Any enabled offload must pass staging verification before ramping:

- warm the bot population for at least 20 minutes;
- run dry-run shadow comparison before consuming worker output;
- enable verification mode;
- require zero `PARALLEL_VERIFY_FAIL` log entries;
- run ThreadSanitizer and require zero warnings;
- ramp canary percentage only after clean verification.

## Acceptance Criteria

The project is successful only if:

- switch-off behavior is unchanged;
- no game-state mutation leaves the map thread;
- worker jobs use only approved copied inputs and lease-protected compute data;
- staging verification reports no mismatches and no races;
- hot-map playerbot AI time improves measurably;
- player-facing action order, movement commits, and game-loop consistency remain equivalent.
