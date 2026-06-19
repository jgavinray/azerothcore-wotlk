# Phase -1: Static Architecture Proof

Goal: prove the architecture from source before implementation begins.

This phase is mandatory. It exists because this is production work and the desired behavior must be
designed from known source facts, not discovered by trial-and-error rollout.

No implementation files may be edited in this phase. The only outputs are analysis documents.

## Required Outputs

Create these files before Phase 0 begins:

- `docs/playerbot-parallelization/static-callgraph.md`;
- `docs/playerbot-parallelization/static-forbidden-boundaries.md`;
- `docs/playerbot-parallelization/static-mmap-thread-safety.md`;
- `docs/playerbot-parallelization/static-path-equivalence.md`;
- `docs/playerbot-parallelization/static-implementation-decision.md`.

The implementation model must not proceed until all five exist and say `Decision: PROCEED`.

## Task -1.1: Bot Loop Call Graph

Allowed file:

- `docs/playerbot-parallelization/static-callgraph.md`.

Task items:

1. Trace the call path from `Map::Update` to `PlayerbotsPlayerScript::OnAfterUpdate`.
2. Trace from `OnAfterUpdate` to `PlayerbotAI::UpdateAI`.
3. Trace from `PlayerbotAI::UpdateAIInternal` to `Engine::DoNextAction`.
4. Trace `Engine::DoNextAction` through trigger processing, queue operations, multipliers,
   prerequisites, action execution, continuers, alternatives, and queue cleanup.
5. Record each source file and function involved.
6. Mark each function as map-thread-only, worker-forbidden, or pure/unknown.

Required conclusion:

- `PlayerbotAI::UpdateAI`, `Engine::DoNextAction`, triggers, actions, and action execution remain
  map-thread-only.

Stop if:

- any proposed worker boundary would move this call flow or change per-player update ordering.

## Task -1.2: Forbidden Worker Boundary Table

Allowed file:

- `docs/playerbot-parallelization/static-forbidden-boundaries.md`.

Task items:

1. Search candidate worker files and helper designs for forbidden concepts.
2. Record every forbidden token category:
   `Player`, `Unit`, `WorldObject`, `Map`, `MotionMaster`, `Spell`, `ObjectAccessor`, `Session`,
   `WorldPacket`, `Database`, `ScriptMgr`, `Visit`, `LoadGrid`, `GetGrid`, singleton managers, and
   mutation verbs.
3. Define the exact worker-allowed API surface.
4. Define the exact map-thread-only API surface.
5. Define the static guard regex that must block worker job files.

Required conclusion:

- Worker files may contain copied structs, Detour query operations under an MMAP read lease, local
  buffers, and plain outputs only.

Stop if:

- a candidate worker helper needs any forbidden category.

## Task -1.3: MMAP Thread-Safety Proof

Allowed file:

- `docs/playerbot-parallelization/static-mmap-thread-safety.md`.

Task items:

1. Trace `MMapMgr::GetNavMesh`.
2. Trace `MMapMgr::GetNavMeshQuery`.
3. Trace `MMapMgr::loadMapData`.
4. Trace `MMapMgr::loadMap`.
5. Trace `MMapMgr::unloadMap(map,x,y)`.
6. Trace `MMapMgr::unloadMap(map)`.
7. Trace `MMapMgr::unloadMapInstance`.
8. Trace all call sites that can load or unload MMAP tiles at runtime.
9. Trace whether `Map::MMapLock` has any acquisition sites.
10. Record every mutation of `loadedMMaps`, `loadedTileRefs`, `navMeshQueries`, and `dtNavMesh`.
11. Classify `unloadMapInstance` as shared-query cleanup, not `dtNavMesh` tile mutation, unless
    source changes prove otherwise.
12. Decide whether the read/write lease in `mmap-safety-decision.md` is sufficient.

Required conclusion:

- Worker Detour reads are forbidden until shared `dtNavMesh` mutation is protected by an explicit
  read/write lease.
- raw `GetNavMesh`, `GetNavMeshQuery`, `loadMapData`, and `loadMap` are always
  worker-forbidden.

Stop if:

- MMAP mutation cannot be isolated to narrow read/write lease changes.

## Task -1.4: Path Equivalence Design

Allowed file:

- `docs/playerbot-parallelization/static-path-equivalence.md`.

Task items:

1. Trace `MovementAction::MoveToLOS` linearly.
2. Trace `PathGenerator::CalculatePath` and every method it calls.
3. Classify every `PathGenerator` source dependency as:
   - copied scalar input;
   - map-thread validation;
   - intentionally omitted from worker Detour compute;
   - disqualifier.
4. Define the exact worker Detour subset to compute.
5. Define resolved Detour include/exclude filter flags as copied map-thread input.
6. Define the worker output stage as pre-live-post-processing Detour point path.
7. Define the exact map-thread post-processing and validation after worker output.
8. Define stale-result rejection rules.
9. Define dry-run comparison rules.

Required conclusion:

- Worker output is advisory and cannot be consumed until dry-run equivalence is proven.

Stop if:

- the Detour-only subset cannot be separated from live source behavior.

## Task -1.5: Implementation Decision

Allowed file:

- `docs/playerbot-parallelization/static-implementation-decision.md`.

Task items:

1. Summarize decisions from tasks -1.1 through -1.4.
2. State `Decision: PROCEED` or `Decision: STOP`.
3. If proceeding, list the exact phase order and the exact first implementation task.
4. If stopping, list the concrete blocker and the rejected implementation path.

Required conclusion for proceeding:

- bot loop remains serial;
- worker boundary is plain-data only;
- MMAP read/write lease is feasible;
- `MoveToLOS` dry-run can be implemented without consuming worker output;
- resolved filter flags and worker output stage are explicitly defined;
- no generic value precompute or action intent batching is included.

Stop if:

- any previous static document is missing;
- any previous static document has `Decision: STOP`.

## Static Analysis Inputs

Use source searches and compiler-assisted analysis where available:

- `rg` for call sites and forbidden APIs;
- generated `compile_commands.json` if the local build configuration supports it;
- `clang-tidy` or equivalent AST tools when available;
- CMake TSan support for later runtime proof, not as a substitute for this static phase.

Do not rely on staging canaries to answer source-architecture questions.
