# Static MMAP Thread-Safety Analysis

Decision: STOP for worker Detour reads until an MMAP read/write lease exists.

## Observed Source Facts

`MMapMgr::GetNavMesh`:

- returns a pointer to shared `dtNavMesh`;
- does not synchronize access.
- must not be called directly by worker code.

`MMapMgr::GetNavMeshQuery`:

- returns a shared per-instance query;
- header notes the returned query is not thread-safe;
- lazily allocates `dtNavMeshQuery`;
- mutates `navMeshQueries`.

`MMapMgr::loadMap`:

- calls `loadMapData`;
- loads tile data;
- calls `dtNavMesh::addTile`;
- mutates `loadedTileRefs`;
- mutates loaded tile counters.

`MMapMgr::loadMapData`:

- may perform file I/O;
- may insert into `loadedMMaps` only while the environment is still thread-safe;
- may abort on an unregistered map id after `InitializeThreadUnsafe` marks the environment
  thread-unsafe;
- must not be called directly by worker code.

`MMapMgr::unloadMap(map,x,y)`:

- calls `dtNavMesh::removeTile`;
- mutates `loadedTileRefs`;
- mutates loaded tile counters.

`MMapMgr::unloadMap(map)`:

- removes all tiles for the map;
- deletes `MMapData`;
- invalidates shared navmesh data.

`MMapMgr::unloadMapInstance`:

- frees a shared `dtNavMeshQuery`;
- erases from `navMeshQueries`;
- does not mutate `dtNavMesh` tile content;
- is not part of private worker-query read safety because workers never use shared queries.

Runtime call sites:

- `Map::LoadMMap` can call `MMapMgr::loadMap`;
- `Map::UnloadGrid` can call `MMapMgr::unloadMap`;
- playerbot travel/stuck code can call `MMapMgr::loadMap`;
- command/debug tooling can call navmesh accessors.

Existing lock state:

- `Map::MMapLock` is declared and exposed by getter;
- current source review found no acquisition sites;
- treat current MMAP access as zero active synchronization.

## Risk

A private Detour query object protects query-local state only. It does not protect shared
`dtNavMesh` tile arrays from concurrent add/remove operations.

## Required Safety Decision

Before worker Detour reads:

- add a narrow read/write lease in `MMapMgr`;
- worker Detour reads hold a read lease;
- all tile add/remove and map unload paths hold a write lease;
- worker jobs never call `loadMapData`, `loadMap`, raw `GetNavMesh`, or `GetNavMeshQuery`;
- worker jobs initialize private query objects under the read lease;
- the worker-approved read API does not let raw `dtNavMesh*` or `dtNavMesh const*` escape the
  manager-owned lease scope.

## Architectural Decision

Phase 2 path compute cannot proceed until the MMAP lease is implemented and statically reviewed.
Without that lease, path compute remains serial.
