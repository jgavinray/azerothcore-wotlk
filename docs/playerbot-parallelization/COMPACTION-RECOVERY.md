# Compaction Recovery: Playerbot Compute Offload

Use this file when context has been compacted, a new session starts, or the executor is unsure what
the user intended.

## Recovery Rule

Do not rely on chat memory. Read `execution-contract.md` first and treat it as authoritative for:

- required reading order;
- current go/stop status;
- forbidden worker access;
- measurement gate;
- source revalidation.

## User Intent

The user wants the playerbot latency problem solved architecturally without production source
implementation unless explicitly requested. The solution must preserve current game-loop behavior,
keep atomic gameplay actions on the map thread, and use worker threads only for isolated compute.

## Current Architecture In One Paragraph

The playerbot AI loop, trigger processing, action selection, values, action execution, and movement
commits stay serial on the map/game thread. A bounded playerbot-owned compute pool may run
plain-data jobs only. Path offload remains the leading hypothesis, but Phase 0 measurement must
earn that direction before Phase 2 implementation. `MoveToLOS` is the first low-blast-radius
correctness canary; the expected larger latency payoff is later repeated path work such as
`SearchForBestPath`.

## Fresh Session Prompt

```text
You are in /home/azeroth/azerothcore-wotlk.

Task: review or continue the playerbot latency architecture work without implementing production
source code unless explicitly requested.

First read:
- docs/playerbot-parallelization/START-HERE.md
- docs/playerbot-parallelization/execution-contract.md
- docs/playerbot-parallelization/COMPACTION-RECOVERY.md
- docs/playerbot-parallelization/final-architecture-proposal.md
- docs/playerbot-parallelization/source-anchors.md
- docs/playerbot-parallelization/failure-mode-checklist.md
- the current phase file

Then revalidate source anchors with rg and direct source inspection. Do not infer architecture from
memory. Do not use line-number estimates. Do not implement code unless explicitly asked.

If a task requires live game objects, PathGenerator in workers, raw MMAP load/fetch APIs in workers,
or worker Detour reads before MMAP lease safety, stop.
```

