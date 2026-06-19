# Phase 0: Measurement Only

Goal: prove where playerbot time is spent before changing behavior.

This phase must not change bot decisions, action order, movement, sessions, packets, DB behavior, or
map update semantics. All profiling is default-off.

Phase 0 is a value gate. The path-offload architecture is a hypothesis until this phase proves that
path or movement-path-adjacent work is a material contributor to playerbot latency.

## Editable Scope

Only these areas may be touched in this phase:

- playerbot config declarations and config loading;
- `modules/mod-playerbots/conf/playerbots.conf.dist`;
- a new playerbot profiling helper;
- `PlayerbotsPlayerScript::OnAfterUpdate`, only to time existing calls when profiling is enabled.

Do not edit core map update logic. Do not edit actions, values, movement, or `PathGenerator`.

## Task 0.1: Add Default-Off Config Keys

Add config fields only. Nothing may read them yet except config loading.

Required keys:

- `AiPlayerbot.PerfDumpEnabled`, default false;
- `AiPlayerbot.BotComputeThreads`, default 4;
- `AiPlayerbot.AsyncPathCompute`, default false;
- `AiPlayerbot.AsyncPathComputePercent`, default 0;
- `AiPlayerbot.ParallelVerify`, default false.

Task items:

1. Add members to `PlayerbotAIConfig`.
2. Load members in the existing config initialization style.
3. Document keys in `playerbots.conf.dist`.
4. Build and verify no behavior uses the new members.

Stop if:

- the config loading pattern is unclear;
- adding a key requires behavior changes.

## Task 0.2: Add Profiling Helper

Add a small helper that records elapsed playerbot AI time by map id and periodically logs totals.

Requirements:

- single shared instance using the module singleton style;
- thread-safe internal counters;
- only logs when `PerfDumpEnabled` is true;
- dumps at a fixed coarse interval, such as 10 seconds;
- clears totals after each dump;
- does not call game-state APIs.

Task items:

1. Create the helper declaration.
2. Create the helper implementation.
3. Confirm module source discovery includes the files, or make the minimal build registration change.
4. Build with helper unused.

Stop if:

- adding the helper requires changing gameplay files;
- source registration is unclear.

## Task 0.3: Time Existing Bot AI Calls

Instrument only the existing `OnAfterUpdate` call path.

Requirements:

- when `PerfDumpEnabled` is false, the function must execute the same calls in the same order as
  before;
- when enabled, take a timestamp before and after the existing `botAI->UpdateAI(diff)` call;
- record elapsed time against `player->GetMapId()`;
- call the helper dump method after existing playerbot update work.

Task items:

1. Add the guarded timing around the existing bot AI update.
2. Add the guarded dump call.
3. Build and test.
4. Self-review that no existing call was moved or changed.

Stop if:

- instrumentation requires moving `UpdateAI` or `PlayerbotMgr::UpdateAI`;
- a switch-off path differs from current behavior.

## Integration Run A: Baseline

This run happens on staging, not in a source-only checkout.

Procedure:

1. Deploy with `AiPlayerbot.PerfDumpEnabled = 1`.
2. Warm the bot population for at least 20 minutes.
3. Hold steady for at least 5 minutes.
4. Capture profiling log output by map id.
5. Capture existing `PerformanceMonitor` totals for trigger, value, action, and total buckets.
6. Record the hot map, bot count shape, dominant bucket, and approximate latency impact in
   `docs/playerbot-perf-baseline.md`.

Exit criteria:

- baseline file exists;
- hot path is identified;
- no gameplay behavior was changed.

## Architecture Gate A: Baseline Decision

After Integration Run A, update `docs/playerbot-perf-baseline.md` with one of these decisions:

- `Decision: PATH_OFFLOAD_PROCEED`
- `Decision: PATH_OFFLOAD_STOP`

Use `PATH_OFFLOAD_PROCEED` only if profiling shows path or movement-path-adjacent work is a
material contributor to playerbot latency.

Use `PATH_OFFLOAD_STOP` if profiling shows the dominant cost is elsewhere. If stopped, do not begin
Phase 2 path work. Update the architecture before implementing a different offload target.

The baseline must also state whether `MoveToLOS`, `ReachCombatTo`, `SearchForBestPath`, travel, or
another measured area appears to dominate. `MoveToLOS` may still be used as a correctness canary, but
it must not be used as proof that the final latency win exists.
