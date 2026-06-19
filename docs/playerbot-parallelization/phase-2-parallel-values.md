# Legacy Redirect: Parallel Values

Do not use this file for implementation.

Generic parallel value precompute is deferred. The previous map-start prepass idea would change
value read timing relative to session processing and per-player map update order, and it does not
account for shared value context.

Use these files instead:

1. `phase-2-path-compute.md` for the only approved current offload target.
2. `phase-3-deferred-work.md` for the conditions required before any future value parallelism.

Required rule:

- do not add a map-start value prepass;
- do not worker-compute shared values;
- do not worker-read live world, map, group, grid, singleton, or object state for generic values.
