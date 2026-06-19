# Phase 3: Deferred Work

This phase documents work that must not be implemented during the current low-invasive pass.

Only revisit these topics after Phase 0 through Phase 2 are complete, staged, verified, and measured.

## Deferred: Generic Parallel Value Compute

Do not add a map-start value prepass in this pass.

Reason:

- current values are computed during the existing per-player map update order;
- computing values at the start of `Map::Update` changes read timing;
- `AiObjectContext` includes shared value context as well as per-bot context;
- many values read live world, group, map, grid, singleton, or object state;
- some values mutate their own caches or change tracking;
- a broad whitelist cannot be assumed safe.

Future work may consider one specific value at a time only if:

- profiling proves that value dominates;
- it is not shared;
- it can be computed from copied input or proven stable read-only state;
- it is cacheable;
- it has verification comparing old and new results;
- ThreadSanitizer is clean.

## Deferred: Action Intent Batching

Do not split actions into worker-side intents in this pass.

Reason:

- `Engine::DoNextAction` interleaves trigger processing, values, multipliers, prerequisites,
  action execution, continuers, alternatives, and queue mutation;
- action execution has broad side effects through spells, movement, packets, groups, inventory,
  quests, auras, threat, and DB-facing logic;
- changing this boundary is not low-invasive;
- preserving exact behavior would require a separate design and much deeper test coverage.

Future work may consider action intents only if:

- path compute no longer provides enough improvement;
- measurement proves serial action apply is the remaining bottleneck;
- a new spec defines intent schema, ordering, conflict rules, failure semantics, and verification;
- the map thread remains the only mutation authority.

## Deferred: Core Game-Object Thread Safety

Do not attempt to make core game objects broadly thread-safe.

Reason:

- it is far beyond the requested lowest-invasive change;
- it risks changing locking behavior and latency across the whole server;
- it would not be limited to playerbots;
- correctness would be difficult to prove.

## Deferred: Full `PathGenerator` Owner-Free Refactor

Do not implement a broad owner-free `PathGenerator` refactor in this pass.

Reason:

- current `PathGenerator` behavior depends on `_source` in deeper methods;
- replacing all source-dependent behavior requires policy decisions for live map reads, Z
  normalization, liquid checks, movement capabilities, water/flying/falling state, and diagnostics;
- this can silently change path results.

The approved Phase 2 approach is narrower: copied input, Detour-only worker compute, map-thread
validation and commit.

## Exit Criteria For Starting Any Deferred Work

Before any deferred topic becomes eligible:

- Phase 2 is clean at 100% canary;
- no verification mismatches remain;
- ThreadSanitizer is clean;
- measurement shows a remaining bottleneck that the deferred work directly targets;
- a new written spec is created with tasks, file scopes, gates, and rollback rules.
