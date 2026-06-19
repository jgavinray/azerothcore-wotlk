# MMAP Safety Decision

Decision: use a narrow MMAP read/write lease before any worker Detour reads are implemented.

## Reason

Worker path compute needs to read `dtNavMesh`. In this codebase, shared `dtNavMesh` state is not
immutable during runtime, and there is no existing active MMAP synchronization protecting it:

- `Map::LoadMMap` calls `MMapMgr::loadMap`.
- `MMapMgr::loadMap` calls `dtNavMesh::addTile`.
- `Map::UnloadGrid` calls `MMapMgr::unloadMap`.
- `MMapMgr::unloadMap` calls `dtNavMesh::removeTile`.
- playerbot travel/stuck code can also call `MMapMgr::loadMap`.

Also, `MMapMgr::GetNavMeshQuery` is explicitly not thread-safe and lazily mutates the per-instance
query map. Worker jobs must never use it.

`Map::MMapLock` exists as a declaration and getter, but current source review shows no acquisition
sites. Treat this as zero existing synchronization. The MMAP lease introduces the protection; it
does not extend a partially protected system.

## Approved Safety Shape

Add a narrow lease inside `MMapMgr`.

### Lease Owner

The lease owner is `MMapMgr` because `MMapMgr` owns `loadedMMaps`, `MMapData::navMesh`,
`MMapData::loadedTileRefs`, and `MMapData::navMeshQueries`.

The implementation must place the required protection at the MMAP manager boundary or expose an
equivalent manager-owned lease object that cannot be skipped by worker path code. Do not rely on
`Map::MMapLock` unless the implementation also proves every relevant reader and writer acquires it;
current source does not.

### Exclusive/Write Coverage

The exclusive/write lease must cover the full critical section of every operation that can mutate or
destroy shared navmesh state:

- `MMapMgr::loadMapData`;
- `MMapMgr::loadMap`;
- `MMapMgr::unloadMap(map,x,y)`;
- `MMapMgr::unloadMap(map)`;
- `MMapData` destruction or any path that frees `dtNavMesh`, removes tiles, or frees shared queries.

`MMapMgr::unloadMapInstance` classification:

- source review shows it frees a shared `dtNavMeshQuery` and erases from `navMeshQueries`;
- it does not mutate `dtNavMesh` tile content;
- because worker path code must never touch shared queries, it is not part of worker `dtNavMesh`
  read safety;
- still audit it for shared-query safety, but do not treat it as required write-lease coverage for
  private worker query reads unless future source changes make shared query state visible to workers.

### Shared/Read Coverage

The shared/read lease must cover the full worker Detour read operation:

- retrieval of the `dtNavMesh` pointer;
- private `dtNavMeshQuery` allocation and initialization;
- Detour path query execution;
- copying path output into worker-owned result storage.

The read lease must be released before map-thread validation, movement decisions, logging that
touches live objects, script callbacks, packets, DB work, or any playerbot action code.

### Read API Shape

The worker-approved read API must execute the Detour query inside a manager-held read lease scope.

Approved shapes:

- a manager method that accepts a plain-data request and returns copied path output while holding the
  read lease internally;
- a manager-owned RAII read-lease object whose lifetime encloses navmesh pointer retrieval, private
  query initialization, Detour execution, and output copying.

Forbidden shapes:

- returning raw `dtNavMesh*` or `dtNavMesh const*` to worker code after the lease scope ends;
- returning a raw pointer plus an instruction for callers to remember the lock;
- storing a raw navmesh pointer in `AsyncPathCompute`, `BotPathCompute`, `PathComputeJob`, or any
  worker-visible result/state object.

### Forbidden Worker API

Worker path code must not call:

- `MMapMgr::loadMapData`;
- `MMapMgr::loadMap`;
- `MMapMgr::GetNavMesh`;
- `MMapMgr::GetNavMeshQuery`;
- any method returning a shared `dtNavMeshQuery`;
- any method that lazily creates or mutates shared query state;
- any method that can perform file I/O, register maps, load tiles, or abort on map-id validation;
- any map/playerbot/game-object API while holding the read lease.

The worker path helper may only run while holding a read lease. It must create a private
`dtNavMeshQuery` and initialize it against the leased navmesh. The lease must be released before
returning to map-thread validation or movement code.

## Required Proof Before Worker Path Code

Before adding `BotPathCompute`, `PathComputeJob`, or `AsyncPathCompute`, the executor must update
`static-mmap-thread-safety.md` with:

- every `loadMapData` call path;
- every `loadMap` call site;
- every `unloadMap` call site;
- every `GetNavMesh` call site;
- every `GetNavMeshQuery` call site;
- a statement that worker code cannot call `loadMapData`, `loadMap`, `GetNavMesh`, or
  `GetNavMeshQuery` directly;
- a `Decision: GO` line.

If the document cannot say `Decision: GO`, Phase 2 must stop.

## Rejected Alternatives

- Use `GetNavMeshQuery`: rejected because it returns a shared non-thread-safe query and mutates
  `navMeshQueries`.
- Assume tiles are immutable after load: rejected because tiles can load and unload at runtime.
- Let canary/TSan catch races without synchronization: rejected because data preservation is a core
  requirement.
- Clone the entire navmesh per worker: rejected for this low-invasive pass due to complexity and
  memory/ownership risk.

## Implementation Scope For Later

The later implementation task may touch only:

- `src/common/Collision/Management/MMapMgr.h`;
- `src/common/Collision/Management/MMapMgr.cpp`;
- tests or guards explicitly named by that task.

Any broader map, movement, or playerbot refactor means this decision is invalid and Phase 2 must
stop.
