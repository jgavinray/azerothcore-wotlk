# Playerbot Perf Baseline

Decision: PATH_OFFLOAD_STOP

## Run Context

- Date: 2026-06-19
- Capture directory: `var/playerbot-perf-capture/20260619-145843`
- Worldserver binary: `env/dist/bin/worldserver`
- Bot population: 4141/4141 bots logged in, 0 real players online
- Config:
  - `AiPlayerbot.PerfDumpEnabled = 1`
  - `AiPlayerbot.PerfMonEnabled = 1`
  - `AiPlayerbot.ReactDelay = 100`
  - `AiPlayerbot.DynamicReactDelay = 1`
  - `AiPlayerbot.AsyncPathCompute = 0`
  - `AiPlayerbot.AsyncPathComputePercent = 0`
  - `AiPlayerbot.ParallelVerify = 0`

The server was restarted after changing `AiPlayerbot.ReactDelay` so the live staging config was
loaded. The capture was stopped early after the server showed sustained multi-second world update
diffs; continuing the wait would not change the gate outcome.

## World Update Impact

`env/dist/bin/Server.log` reported severe update lag under the 100 ms baseline:

- Update diff: 2640 ms with 0 players online
- Last 500 diffs mean: 2245 ms
- Last 500 diffs median: 2217 ms
- Last 500 diffs p95 / p99 / max: 2761 ms / 3076 ms / 3386 ms

A later sample remained in the same range:

- Update diff: 2061 ms with 0 players online
- Last 500 diffs mean: 2272 ms
- Last 500 diffs median: 2245 ms
- Last 500 diffs p95 / p99 / max: 2877 ms / 3167 ms / 3280 ms

## PlayerbotPerf Window

Raw window: `var/playerbot-perf-capture/20260619-145843/playerbots-manual-window.log`

Aggregated `PlayerbotPerf` by map:

| Map | Samples | Calls | Total AI time | Average call | Max call |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 | 17 | 105575 | 104.578 s | 991 us | 15.510 ms |
| 1 | 17 | 131223 | 146.776 s | 1119 us | 15.426 ms |
| 530 | 17 | 83053 | 91.046 s | 1096 us | 14.308 ms |

Hot map: map 1.

`PlayerbotPerf` measures only `botAI->UpdateAI(diff)` wall-clock time grouped by current map id. It
does not break out trigger, value, action, path, DB, session, packet, or world-update work.

## PerformanceMonitor Totals

Raw pmon window: `var/playerbot-perf-capture/20260619-145843/playerbots-manual-with-pmon.log`

`PerformanceMonitor` totals after reset:

| Bucket | Total time | Percent of `PlayerbotAI::UpdateAIInternal` |
| --- | ---: | ---: |
| Action total | 145.552 s | 42.877% |
| Trigger total | 88.405 s | 26.778% |
| Value total | 30.155 s | 8.883% |
| RndBot total | 0.147 s | 0.043% |
| `PlayerbotAI::UpdateAIInternal` total | 339.460 s | 100.000% |

`PlayerbotAI::UpdateAIInternal` by map:

| Map | Total time | Percent | Average call |
| --- | ---: | ---: | ---: |
| 1 | 145.854 s | 42.966% | 1.902 ms |
| 0 | 103.135 s | 30.382% | 1.692 ms |
| 530 | 90.471 s | 26.651% | 1.878 ms |

Top individual measured costs:

| Metric | Type | Total time | Percent |
| --- | --- | ---: | ---: |
| `choose rpg target` | Action | 52.175 s | 15.370% |
| `new player nearby` | Trigger | 37.891 s | 11.477% |
| `emote` | Action | 34.508 s | 10.166% |
| `move to rpg target` | Action | 21.722 s | 6.399% |
| `enemy player near` | Value | 16.760 s | 4.937% |
| `enemy player near` | Trigger | 16.700 s | 5.058% |
| `grind target` | Value | 8.862 s | 2.611% |
| `greet` | Action | 6.168 s | 1.817% |
| `talk` | Action | 5.566 s | 1.640% |
| `add gathering loot` | Action | 5.409 s | 1.593% |

## Gate A Assessment

The baseline proves that the 100 ms target cadence overloads the map thread with the full bot
population. However, the measured dominant costs are RPG target selection, nearby-player checks, and
social/RPG actions.

Path or movement-path-adjacent work is present but not dominant:

- `move to rpg target`: 21.722 s, 6.399%
- `move random`: 3.498 s, 1.031%
- `reach spell`: 0.706 s, 0.208%
- `reach melee`: 0.336 s, 0.099%
- `move to loot`: 0.016 s, 0.005%

`MoveToLOS`, `ReachCombatTo`, and `SearchForBestPath` did not appear as dominant measured areas in
this baseline. Travel/RPG target selection appears to dominate instead, with `move to rpg target` as
a secondary movement-adjacent contributor.

## Decision Rationale

`PATH_OFFLOAD_PROCEED` requires profiling to show that path or movement-path-adjacent work is a
material contributor to playerbot latency. This run shows severe latency, but the hot path is not
primarily the Phase 2 path-compute target.

Therefore Phase 2 path offload must not proceed from this baseline. The architecture should be
revised around the measured RPG/nearby/social hot path, or a narrower follow-up measurement must
prove that path computation is the correct offload target before reopening Phase 2.
