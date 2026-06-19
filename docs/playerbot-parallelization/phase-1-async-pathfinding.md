# Legacy Redirect: Async Pathfinding

Do not use this file for implementation.

The earlier async-pathfinding plan was superseded because it assumed `PathGenerator` could be made
worker-safe by adding an owner-free constructor. Code review showed that the existing implementation
still depends on `_source` through deeper methods.

Use these files instead:

1. `phase-1-compute-pool.md` for the worker pool.
2. `phase-2-path-compute.md` for the constrained Detour-only path compute boundary.

Required rule:

- worker path jobs must not call live `PathGenerator` behavior that dereferences `_source`;
- live map checks and movement commits must stay on the map thread.
