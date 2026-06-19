# Execution Contract

This is the single source of truth for execution order, go/stop status, and worker-forbidden APIs.
Other documents may add local task details, but they must not redefine this contract.

## Required Reading Order

1. `START-HERE.md`
2. `execution-contract.md`
3. `COMPACTION-RECOVERY.md`
4. `final-architecture-proposal.md`
5. `source-anchors.md`
6. `source-audit.md`
7. `static-implementation-decision.md`
8. `mmap-safety-decision.md`
9. `failure-mode-checklist.md`
10. phase file for the current task

## Current Go/Stop Status

- Bot loop boundary: `GO` only for keeping it serial on the map/game thread.
- Action/value offload: `STOP`.
- Phase 0 measurement: `GO`.
- Phase 1 compute pool infrastructure: `GO` only as unused/default-off infrastructure.
- MMAP worker read safety: `STOP` until Tasks 2.0A through 2.0E pass.
- Path helper without live objects: `STOP` until MMAP worker read safety is `GO`.
- `MoveToLOS` dry-run: `STOP` until path helper and job types are complete.
- Worker output consumption: `STOP` until dry-run has zero mismatches under staging load.
- Expansion to `ReachCombatTo` or `SearchForBestPath`: `STOP` until `MoveToLOS` consumption is
  verified and Phase 0/updated measurement confirms path work is still the target.

## Forbidden Worker Access

Worker code must not contain or access:

- `Player*`
- `Unit*`
- `Creature*`
- `GameObject*`
- `WorldObject*`
- `Map*`
- `MotionMaster`
- `Spell`
- `AiObjectContext`
- `Action`
- `Trigger`
- `Value`
- `PathGenerator`
- `ObjectAccessor`
- `WorldSession`
- `WorldPacket`
- database APIs
- script callbacks
- live grid visitors
- live LOS, liquid, height, Z-normalization, movement, spell, aura, threat, inventory, group, quest,
  packet, session, or DB APIs
- `MMapMgr::loadMapData`
- `MMapMgr::loadMap`
- raw `MMapMgr::GetNavMesh`
- `MMapMgr::GetNavMeshQuery`
- singleton managers unless the current task explicitly proves immutable read-only safety

If a task appears to need any forbidden access, the task is `STOP`.

## Measurement Gate

Phase 0 measures before path offload is implemented.

Path offload may proceed past compute-pool infrastructure only if measurement shows that path or
movement-path-adjacent work is a material contributor to playerbot latency. If profiling points to
another dominant cost, stop and revise the architecture before implementing Phase 2.

`MoveToLOS` is a low-blast-radius correctness canary. It is not expected to prove the final latency
win for non-ranged movement. The larger payoff must be earned by measurement and later verified when
converting repeated path work such as `SearchForBestPath`.

## Source Revalidation

Before each implementation phase:

1. Revalidate `source-anchors.md` with `rg` and direct source inspection.
2. Record whether each relevant anchor still matches.
3. If a source fact changed, update the source document or stop.
4. Do not adapt architecture silently.

