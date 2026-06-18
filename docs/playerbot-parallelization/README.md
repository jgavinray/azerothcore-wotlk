# Playerbot Load-Spreading — Implementation Spec Suite

This folder is the complete, ordered instruction set for spreading playerbot AI load off the
single-thread map choke point. It is written to be executed end-to-end by a **weak (~14B) coding
model, one small task at a time, with no human in the loop.** Correctness is enforced by automated
gates the model runs and judges itself (unit tests, an in-process shadow-compare that logs a fixed
failure token, and ThreadSanitizer) — not by anyone approving its work.

Read in this order:
1. `README.md` (this file) — global rules, the task-risk/decomposition map, and the regen prompt.
2. `phase-0-measurement.md` — add profiling; change no behavior.
3. `phase-1-async-pathfinding.md` — worker pool + move pathfinding off the map thread.
4. `phase-2-parallel-values.md` — compute read-only AI "values" in parallel.
5. `phase-3-deferred.md` — not started yet.

## START HERE — how to drive this with an LLM

### Step 0 — one-time setup (operator, once)
```
chmod +x docs/playerbot-parallelization/tools/*.sh
bash docs/playerbot-parallelization/tools/check_task_test.sh    # MUST print: PASS=5 FAIL=0
```
If that test does not print `PASS=5 FAIL=0`, STOP — the guardrail is broken; fix it before any task.

### Step 1 — the task list (run strictly in this order, one LLM session each)
Agent (on-box) tasks, and the off-box INTEGRATION runs that gate phase promotion:
```
0.1 → 0.2 → 0.3
   ⇒ INTEGRATION RUN A (operator): deploy instrumented build to staging, warm up ≥20 min,
     capture per-map + PerformanceMonitor output → commit docs/playerbot-perf-baseline.md
1.1 → 1.2 → 1.3 → 1.4 → 1.5a → 1.5b
   ⇒ INTEGRATION RUN B (operator): ParallelVerify + ThreadSanitizer on staging, ramp canary 1→10→100
1.6 (per site, each followed by its own Integration check)
   ⇒ Phase 2 unlocks only once docs/playerbot-perf-baseline.md exists
2.1 → 2.2 → 2.3 (per value) → 2.4
   ⇒ INTEGRATION RUN C (operator): ParallelVerify + ThreadSanitizer on staging
```
Never skip ahead; never run two agent tasks in one session. The ⇒ INTEGRATION rows are NOT agent
tasks — they are the slow, off-box Tier-2 runs from "Testing & self-verification."

### Step 2 — for EACH agent task, start a fresh LLM instance and give it exactly this
- Coding agent with repo access (in this checkout): paste the **Session Bootstrap Prompt**, filling
  in the task id. It opens the files itself.
- Plain chat LLM, no repo access: paste the Bootstrap Prompt, then this README's "GLOBAL RULES" +
  "Execution protocol" + "Testing & self-verification" sections, then the one task's section text.
  Paste nothing from other tasks.

### Session Bootstrap Prompt (copy/paste; replace <TASK-ID>)
```
You are executing ONE task from docs/playerbot-parallelization/ in the azerothcore-wotlk repo.
Your task is <TASK-ID>. The game CANNOT be run in this checkout — you NEVER boot or run the server.

1. Read docs/playerbot-parallelization/README.md sections "GLOBAL RULES (apply to EVERY task)",
   "Execution protocol", and "Testing & self-verification".
2. Read ONLY the section for task <TASK-ID> in its phase file. Do not read or start any other task.
3. Do the task's numbered steps in order. After each step run the ON-BOX gate only:
   (a) the project compiles with -DBUILD_TESTING=1 -DBUILD_APPLICATION_WORLDSERVER=1
   (b) `ctest --output-on-failure` passes
   (c) `bash docs/playerbot-parallelization/tools/check_task.sh <TASK-ID>` exits 0
   - All green: `git add -A && git commit` for just that step.
   - Any red: `git reset --hard HEAD` to discard the step and retry. Max 2 attempts, then STOP and
     report the exact command output.
4. Edit ONLY files in this task's "Files you may edit"; never touch the FORBIDDEN ZONES.
5. Do NOT run the game, ParallelVerify, or ThreadSanitizer — those are off-box Integration runs done
   later by an operator, not by you.
6. When all steps are committed and the on-box gate is green, report the commits made and the ctest
   output. Then STOP — do not proceed to the next task.
```

### Step 3 — between tasks (operator or orchestration script)
After each agent task, confirm its on-box gate was green, then launch the next id in a fresh session.
At each ⇒ INTEGRATION row, an operator runs the Tier-2 staging procedure (see "Testing &
self-verification" → Integration runbook): deploy, warm up ≥20 min, run ParallelVerify + TSan, grep
the tokens. Do not advance past an Integration row until its run is clean. The Phase 0 run produces
`docs/playerbot-perf-baseline.md`, whose existence is what unlocks Phase 2.

## Why this work exists (one paragraph)

Forked AzerothCore "Playerbot" branch + `modules/mod-playerbots`. Bot AI runs on the map-update
thread (`Playerbots.cpp` `OnAfterUpdate` → `PlayerbotAI::UpdateAI`). `MapUpdate.Threads` is already
16, but a continent is one indivisible `Map` work unit on one thread, and 500+ bots concentrate in
one area, so adding threads cannot help that blob. Hosts have 24+ hardware threads — **the limit is
serialization, not CPU.** Goal: parallelize read-only bot decision work (pathfinding, value
calculation) across cores while keeping every world-state mutation on the single owning map thread,
so consistency is preserved. Today latency is masked by slowing bot thinking (`nextAICheckDelay`);
we want to stop doing that and add more bots.

## Task risk and decomposition — the 14B model does EVERY task

You (the 14B model) execute every task in this suite. There is no hand-off to anyone else. "Risk"
below does **not** change who does the task — it only changes **how finely the task is broken down**
and **how strict the check at the end is**.

| Risk | Tasks | What it means for you |
|------|-------|-----------------------|
| Low | 0.1–0.3, 1.1–1.4, 2.2, 2.3 | One clear change copied from an existing pattern, behind an OFF switch. On-box gate = compile + `ctest` + `check_task.sh`. |
| Medium | 2.1 | A deterministic text procedure (search a method body for a fixed list of forbidden words). Follow the rule literally; when in doubt, choose UNSAFE. |
| High | 1.5a (steps 1.5a.1…), 2.4 (steps 2.4.1…) | Split into many tiny steps. **On-box gate (compile + `ctest` + `check_task.sh`) after EVERY numbered step.** The runtime race/parity proof is the off-box Integration run (operator, staging realm) — you don't run it; when it reports back, you read the result and fix on-box. You never have to reason about thread-safety yourself; the gate does. |

**Your only escape hatch is STOP-and-report.** If a step's on-box gate fails — does not compile,
`ctest` fails, or `check_task.sh` is non-zero — undo just that step and report the exact output. If
an off-box Integration run later reports a race or a `PARALLEL_VERIFY_FAIL`, that result is fed back
to you as a new fix step. Never guess a fix, never skip a gate, never continue past a failed gate.

> Honest note: High-risk steps carry real concurrency risk. The protection is the per-step on-box
> gate plus the off-box Integration race/parity runs — not your understanding of the code. Trust the
> gates; if they pass the step is good, if they fail you STOP.

## GLOBAL RULES (apply to EVERY task — re-read each time)

1. Do exactly ONE task, then stop and report. Wait to be told to continue.
2. Edit ONLY the files named in that task's "Files you may edit." Editing anything else = failure;
   undo and report.
3. NEVER edit any of these unless a task names them explicitly:
   - anything under `src/server/game/Entities/`, `src/server/game/Spells/`,
     `src/server/game/Combat/`, `src/server/game/AI/`
   - any `.sql` file or anything under `data/`
   - any file outside `/archive/azerothcore-wotlk`
4. Every new behavior is behind a config switch that defaults OFF. With the switch OFF the program
   must behave exactly as before. If unsure it does, STOP.
5. **Worker-thread contract — two categories, do not confuse them.**
   - **Pure jobs (Phase 1, REQUIRED).** A `BotComputeJob` takes a plain input struct, fills a plain
     output struct, and reads ONLY static data. It must NEVER use a pointer to a Player/Unit/
     Creature/Map/GameObject; NEVER call Set/Add/Remove/Cast/Move/Update/Damage/Kill/Apply/Send;
     NEVER touch a singleton (`sSomething`) or global/static mutable. Pure-job code lives only in
     files ending `Job.h`/`Job.cpp` so the guard script (rule 9) enforces this by grep. If a Phase 1
     job seems to need any of the above, STOP.
   - **Freeze-window read jobs (Phase 2 ONLY, higher risk).** The value pre-pass reads the live bot
     and world through pointers — it CANNOT be pure. It is safe only under a different model: it runs
     in a window where nothing writes, exactly ONE worker touches a given bot, and it is gated by
     ThreadSanitizer + the `ParallelVerify` shadow-compare. Its true hazard is "read" getters that
     lazily mutate SHARED state (caches, grid loads); Task 2.1's audit must reject those. This is the
     project's most dangerous code — see `phase-2-parallel-values.md`. Phase 2 value jobs are NOT
     named `*Job` and are NOT subject to the pure-job grep; they have their own gate.
6. Do not change existing function inputs/outputs unless the task says so.
7. **This is a source checkout; the game CANNOT be run here.** The loaded server lives elsewhere and
   takes 20+ minutes to ramp bots. So your per-step check is the ON-BOX gate only: the project
   **compiles** and the **`unit_tests` (`ctest`) pass**. You do NOT boot or run the game. All
   runtime checks (ParallelVerify, ThreadSanitizer) happen later in the off-box Integration stage —
   see "Testing & self-verification."
8. **Every task ends with an automated gate that YOU run and judge — no human approval.** For an
   agent task the gate is the on-box gate (compile + `ctest` + `check_task.sh`). If it fails, undo
   and report the exact output. The slow runtime gates are owned by the Integration stage, which
   promotes a whole phase — not individual micro-steps.
9. **Run `tools/check_task.sh <task-id>` before every commit.** It mechanically enforces rules 2 and
   5 (the diff touches only allow-listed files; no job file contains a forbidden token). A non-zero
   exit blocks the commit. You do not rely on remembering the rules — the script is the rule.

## Execution protocol (mandatory, mechanical — this is how a small model stays on the rails)

- **One task per session.** The orchestrator gives you the GLOBAL RULES + exactly ONE task section.
  Never load the whole suite at once; large context causes scope-creep and hallucination.
- **One micro-step = one git commit.** After each numbered step: run the on-box gate (compile +
  `ctest` + `check_task.sh`); if green, `git add -A && git commit`; if red, `git reset --hard HEAD`
  (discard the step) and retry. You never run the game.
- **Retry budget = 2.** If a step fails its on-box gate twice, STOP and report the exact output. Do
  NOT edit additional files to "fix forward" — that is the failure mode that corrupts the codebase.
- **Self-review before declaring done.** Re-open your own diff and confirm: only allow-listed files
  changed; every new behavior sits behind an OFF-by-default switch; job files have no forbidden
  tokens. Then run the on-box gate.
- **Runtime correctness is NOT your job per step.** The `ParallelVerify` and ThreadSanitizer checks
  require the loaded server and run in the Integration stage (below), batched per phase. Your job is
  to leave every new path OFF by default so the build is always safe to deploy for that stage.

## Testing & self-verification — TWO tiers (no human approval, but two different runners)

Correctness is proven in two stages because the game cannot run in this checkout and the real server
needs 20+ minutes to ramp bots. Do not conflate them.

### Tier 1 — ON-BOX gate (the agent runs this every micro-step; fast; no game)
- **Compiles** with `-DBUILD_TESTING=1 -DBUILD_APPLICATION_WORLDSERVER=1`.
- **`ctest --output-on-failure` passes.** The repo builds a `unit_tests` executable from
  `src/test/**` (`src/test/CMakeLists.txt`, links `game` + `gtest_main`/`gmock_main`). Drop a `.cpp`
  under `src/test/...` with `TEST(Suite, Case)` cases — auto-collected, runs standalone (no DB, no
  realm). All isolatable new code (the worker pool, plain helpers, the pure path job) MUST ship unit
  tests — including golden-master cases where feasible (record real input→output pairs, replay as
  fast assertions).
- **`tools/check_task.sh <id>` exits 0** (allow-list + pure-job grep).
This is the entire per-step gate. The agent commits on green and never touches the game.

### Tier 2 — INTEGRATION gate (operator/CI on the staging realm; slow; per PHASE, not per step)
The runtime checks below cannot run on-box. They are batched and run once per phase by whoever owns
the loaded server. They GATE PROMOTION between phases; they do not gate individual commits.

- **B. In-process shadow verification.** With `AiPlayerbot.ParallelVerify=1`, the new parallel path
  also computes the old way and compares in-code; any mismatch logs the fixed token
  `PARALLEL_VERIFY_FAIL`. Gate: `grep -c PARALLEL_VERIFY_FAIL <log>` MUST be 0.
- **C. ThreadSanitizer.** A TSan build run under load: `grep -c "WARNING: ThreadSanitizer"` MUST be
  0. Run first with `BotComputeThreads=1` (proves logic without races), then raise threads (proves
  no races) — this separates "logic wrong" from "race."

**Integration runbook (operator/CI, ~30–40 min per run):**
1. Build the phase's branch (normal build for the parity run; a separate TSan build for the race
   run) and deploy to the **staging realm** (NOT production).
2. Set the phase's switches on (`AsyncPathfinding=1` / `ParallelDecide=1`) plus `ParallelVerify=1`,
   and start with the canary low (`AsyncPathfindingPercent=1`).
3. **Wait for full warmup — ≥20 minutes** until the bot population and the hot-area concentration
   are realistic. Measurement before warmup is meaningless.
4. Hold ≥5 minutes at steady state, then `grep` the log for the two tokens above.
5. If both counts are 0, the phase is promoted: ramp the canary (1→10→100), re-confirm, and unlock
   the next phase. If non-zero, the log names the offending value/path — feed that back to the
   relevant task (e.g. demote a value to UNSAFE in Task 2.1) and the agent fixes it on-box.

Honest note: Tier 2 is where the coupling actually gets tested, and it is human/CI-operated and slow
by nature — there is no way around the 20-minute ramp. The agent's job is to keep Tier 1 green and
everything OFF by default so a Tier-2 run is always safe to launch.

## Known engine facts the tasks rely on (verified in code)

- Per-bot AI entry on the map thread: `modules/mod-playerbots/src/Playerbots.cpp:102`
  (`PlayerbotsPlayerScript::OnAfterUpdate` → `botAI->UpdateAI(diff)`).
- Existing thread pool to copy: `src/server/game/Maps/MapUpdater.{h,cpp}` (uses `PCQueue.h`).
- Config singleton + read style: `#define sPlayerbotAIConfig PlayerbotAIConfig::instance()`
  (`PlayerbotAIConfig.h:336`); options read as
  `x = sConfigMgr->GetOption<bool>("AiPlayerbot.Key", default);` (`PlayerbotAIConfig.cpp:63+`).
- Module config load hook: `PlayerbotsWorldScript::OnBeforeWorldInitialized` calls
  `sPlayerbotAIConfig->Initialize()` (`Playerbots.cpp:234`). Scripts registered in
  `AddPlayerbotsScripts()` (`Playerbots.cpp:334`).
- Singleton style for new managers: `#define sName Class::instance()` with a static local instance
  (see `PerformanceMonitor.h` `instance()`).
- Pathfinding entry today: `PathGenerator path(bot); path.CalculatePath(...)`
  (`modules/mod-playerbots/src/strategy/actions/MovementActions.cpp:138`). `PathGenerator` takes a
  `WorldObject const*` and owns a `dtNavMeshQuery` (`src/.../PathGenerator.h:62,150`) — **not**
  thread-safe to share.
- Value caching: `CalculatedValue::Get()` only caches when `checkInterval >= 2`; with the default
  `checkInterval == 1` it recomputes every call (`strategy/Value.h:71-95`). `LazyGet()` returns the
  cached value without recomputing (`Value.h:97-103`). This governs Phase 2.
- Test harness: `src/test/CMakeLists.txt` builds `unit_tests` (GoogleTest/GoogleMock) from
  `src/test/**`, links the `game` library, registers `add_test(NAME unit ...)`. Enabled by
  `BUILD_TESTING` + `BUILD_APPLICATION_WORLDSERVER`. Mocks live in `src/test/mocks/`. Run via
  `ctest --output-on-failure`.

## Regenerating this spec (paste to a strong planning model)

> Forked AzerothCore "Playerbot" branch + `modules/mod-playerbots`. Bot AI runs on the map-update
> thread; `MapUpdate.Threads` is 16; hosts have 24+ threads so CPU is not the bottleneck —
> single-thread serialization is. 2000+ bots push the tick to ~1000ms, masked to ~300ms by slowing
> bot AI cadence (`nextAICheckDelay`), which we want to stop. Bots cluster 500+ into one area of one
> continent, so per-map/spatial sharding cannot help one interacting blob. Hard constraint: keep
> single-writer-per-map; never mutate game state off-thread. Design = decide-in-parallel /
> apply-serially: parallelize read-only bot work (pathfinding, value calculation via the
> `CalculatedValue` seam in `strategy/Value.h`) on a worker pool; apply mutations serially on the
> map thread. Phases: 0 measurement, 1 async pathfinding, 2 parallel read-only value pre-pass, 3
> deferred. All phases config-gated OFF by default. The executor is a weak ~14B model that must do
> EVERY task itself with NO human in the loop, so produce a CODE-FREE spec of the smallest
> deterministic tasks, each naming exact files it may edit, files it must never touch, and a fully
> automated acceptance gate the model runs and judges (GoogleTest `ctest` for isolatable code; an
> in-process `ParallelVerify` shadow-compare that logs a fixed `PARALLEL_VERIFY_FAIL` token for
> coupled code; ThreadSanitizer for races) plus a STOP condition. No step may depend on a person
> approving, picking, or signing off — replace any such gate with a deterministic rule or a test.
