# Playerbot Compute Offload Spec

The current implementation spec lives under:

`docs/playerbot-parallelization/`

Start with:

1. `docs/playerbot-parallelization/START-HERE.md`
2. `docs/playerbot-parallelization/execution-contract.md`
3. `docs/playerbot-parallelization/README.md`
4. current phase file

Important:

- Do not use the old async `PathGenerator` owner-free plan.
- Do not worker-read MMAP/Detour data until the MMAP read-safety gate is resolved.
- Do not begin implementation until the Phase -1 static-analysis documents say `Decision: PROCEED`.
- Do not add a map-start generic value prepass.
- Do not move playerbot action execution off the map thread.
- All implementation must preserve current game-loop ordering and atomic map-thread mutations.
