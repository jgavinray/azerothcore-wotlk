# Phase 0 — Measurement only (no behavior change)

Goal: prove where the time goes and get a baseline, before changing any game behavior. Re-read the
GLOBAL RULES in `README.md` before each task.

---

## Task 0.1 — Add five config switches  · Low risk

**Goal:** create six new settings, all OFF/default, not used anywhere yet.

**Files you may edit:**
- `modules/mod-playerbots/src/PlayerbotAIConfig.h`
- `modules/mod-playerbots/src/PlayerbotAIConfig.cpp`
- `modules/mod-playerbots/conf/playerbots.conf.dist`

**Do:**
1. In `PlayerbotAIConfig.cpp`, find the block where many settings are read in a row (around line
   63, lines that look like `name = sConfigMgr->GetOption<...>("AiPlayerbot.Something", default);`).
   Add four lines in that exact style, reading into four new members:
   - `AiPlayerbot.PerfDumpEnabled` → bool member `perfDumpEnabled`, default `false`
   - `AiPlayerbot.AsyncPathfinding` → bool member `asyncPathfinding`, default `false`
   - `AiPlayerbot.BotComputeThreads` → int32 member `botComputeThreads`, default `4`
   - `AiPlayerbot.ParallelDecide` → bool member `parallelDecide`, default `false`
   - `AiPlayerbot.ParallelVerify` → bool member `parallelVerify`, default `false` (turns on the
     in-process shadow-compare self-checks described in README section "Testing & self-verification")
   - `AiPlayerbot.AsyncPathfindingPercent` → int32 member `asyncPathfindingPercent`, default `0`
     (canary: the percentage of bots whose pathfinding uses the async path; 0 = none even if
     `AsyncPathfinding` is on; ramp 1 → 10 → 100 to limit blast radius)
2. In `PlayerbotAIConfig.h`, declare those four members next to the other members of the same type
   (bools with bools, the int with the ints).
3. In `playerbots.conf.dist`, add the four keys with their default values and a one-line comment
   each, matching the formatting of nearby entries.

**Done when (on-box gate):** compiles; `ctest` passes; `check_task.sh 0.1` exits 0; the six keys
appear in the config file; with no config edits the members hold their defaults. **Nothing reads
these members yet.** (No game run; defaults are confirmed by reading the code, not by booting.)

**STOP if:** you cannot find the existing settings-reading block to copy.

---

## Task 0.2 — Create an empty per-map timing helper  · Low risk

**Goal:** add a new, unused helper that accumulates elapsed time per map id and periodically logs it.

**Files you may create:**
- `modules/mod-playerbots/src/BotPerfTap.h`
- `modules/mod-playerbots/src/BotPerfTap.cpp`

**Do:** Create a class `BotPerfTap` providing:
- one method that records "N nanoseconds occurred on map id M" — it adds N to a running total stored
  in a table keyed by map id, protected by a single mutex;
- one method "maybe dump" that, only when `sPlayerbotAIConfig->perfDumpEnabled` is true AND at least
  10 seconds have passed since the last dump, writes one info-level log line per map id (logger name
  `"playerbots"`) showing that map id and its accumulated milliseconds, then clears all totals and
  resets the 10-second timer.
- Expose one shared instance using the module's singleton style (a `#define sBotPerfTap
  BotPerfTap::instance()` with a static-local `instance()`, exactly like `PerformanceMonitor`'s
  `instance()`).

If the module globs its sources automatically (check `modules/mod-playerbots/CMakeLists.txt` — most
AzerothCore modules glob `src/**`), do NOT edit CMake. Only if it does not glob may you add the new
files there.

**Done when (on-box gate):** compiles; `ctest` passes; `check_task.sh 0.2` exits 0; nothing calls it
yet. (No game run.)

**STOP if:** the module does not glob and you are unsure how to register the new files in CMake.

---

## Task 0.3 — Time each bot's AI update and feed the helper  · Low risk

**Goal:** measure per-map bot-AI cost, but only when the switch is on; zero change when off.

**Files you may edit:** `modules/mod-playerbots/src/Playerbots.cpp` — only inside the function
`OnAfterUpdate` (begins at line 102).

**Do:**
1. Guard all new work with `if (sPlayerbotAIConfig->perfDumpEnabled)`. When the switch is off the
   function must run exactly as it does today.
2. When on: take a steady-clock timestamp immediately before the existing `botAI->UpdateAI(diff)`
   call and another immediately after; compute the elapsed nanoseconds; call the `BotPerfTap` record
   method with `player->GetMapId()` and that nanosecond count.
3. Once, near the end of the function, call the `BotPerfTap` "maybe dump" method.

Do not move or alter the existing `UpdateAI`/`playerbotMgr->UpdateAI` calls.

**Done when (on-box gate):** compiles; `ctest` passes; `check_task.sh 0.3` exits 0; the new code is
fully inside `if (sPlayerbotAIConfig->perfDumpEnabled)` so the switch-off path is unchanged. The
actual log output ("per-map totals every ~10s under load") is observed later in INTEGRATION RUN A on
the staging realm — not on-box.

---

## Phase 0 exit — INTEGRATION RUN A (operator, off-box; the agent does NOT do this)

This is a Tier-2 run on the staging realm, not an agent task. The game cannot run in the checkout,
and the real server takes 20+ minutes to ramp bots, so this is owned by whoever runs the loaded
server:
1. Build the branch containing tasks 0.1–0.3 and deploy it to the **staging realm** with
   `AiPlayerbot.PerfDumpEnabled = 1`.
2. Let bots ramp to full population with 500+ concentrated in one area — **wait ≥20 minutes**;
   numbers taken before warmup are meaningless.
3. Hold ≥5 minutes at steady state, then collect the per-map timing log lines **plus** the module's
   `PerformanceMonitor::PrintStats` output (buckets `PERF_MON_TRIGGER / VALUE / ACTION / TOTAL`).
4. Commit `docs/playerbot-perf-baseline.md` recording: which map id dominates, and the share of
   bot-tick time in pathfinding vs. value calculation vs. other.

**Phase 2 cannot begin until `docs/playerbot-perf-baseline.md` exists.** Note (see Phase 2): this
baseline only *prioritizes* which values to parallelize first — it does NOT decide correctness. The
value safety audit (Task 2.1) is static and runs without it; the baseline just orders the work so
the highest-cost values come first.
