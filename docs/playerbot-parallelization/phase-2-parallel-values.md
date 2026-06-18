# Phase 2 — Compute expensive read-only "values" in parallel

> **LOCKED until** the Phase 0 profile exists and names the most expensive values. This phase makes
> the only permitted core map edit and is the highest-risk work. Re-read GLOBAL RULES + the
> worker-thread contract. You (the 14B) do every task; high-risk ones are split and gated.

## Background the tasks depend on (verified in code)

The bot AI is a trigger → value → action engine. A "value" is a computed quantity (nearest enemy,
attacker list, flee target…) living in `modules/mod-playerbots/src/strategy/values/`, derived from
`CalculatedValue<T>` (`strategy/Value.h:58`). Each `PlayerbotAI` owns its own value objects, so a
value's cache is **per-bot**, not shared — that is what makes parallel pre-compute safe.

**Caching nuance (critical):** `CalculatedValue::Get()` only stores its result when `checkInterval
>= 2`; with the default `checkInterval == 1` it recomputes on EVERY call (`Value.h:71-95`).
`LazyGet()` returns the cached value without recomputing (`Value.h:97-103`). So the pre-pass only
saves work if the value is cacheable (`checkInterval >= 2`) — otherwise the serial pass recomputes
and we gained nothing. Whitelist only cacheable values.

---

## Task 2.1 — Value purity audit (a deterministic text procedure)  · Medium risk

This is NOT a judgment call. Follow the rule literally. Output a table; change no source code.

**Files you may create:** `docs/playerbot-value-purity-audit.md`.

Correctness does NOT depend on the Phase 0 baseline — this audit is static. Use the baseline only to
ORDER the work (audit the highest-cost values first); if the baseline lists specific values, start
with those, otherwise audit every value under `strategy/values/`.

**Procedure for each value file under `strategy/values/`:**
1. Open the value's `Calculate()` method.
2. Read its constructor and record `checkInterval` (the number passed to the `CalculatedValue` base;
   if none, it is 1).
3. Search the text of `Calculate()` for any of these FORBIDDEN substrings (case-sensitive, as method
   names): `Cast`, `Set`, `Add`, `Remove`, `Move`, `Update`, `Damage`, `Kill`, `Apply`, `Send`,
   `Yield`, `Interrupt`, `Teleport`, `Summon`, `Learn`. Also flag any assignment (`=`) to something
   that is not a local variable declared inside the method.
4. List every function `Calculate()` calls. For each call that is NOT one of: a getter starting with
   `Get`/`Is`/`Has`/`Find`/`Count`/`Nearest`, a math/STL call, or another value's `Get()/Calculate()`
   — treat the value as UNSAFE (you cannot verify that callee here).
4b. **Shared-state hazard (the real danger).** Even an innocent-looking getter can lazily mutate
   SHARED state on first read (spell/template caches, grid/cell loading, last-access stamps), which
   races across worker threads. So ALSO mark UNSAFE if `Calculate()` (or a value it calls) touches
   any shared singleton/manager or grid access — i.e. any token matching `s[A-Z]\w+` (e.g.
   `sSpellMgr`, `sObjectMgr`, `sMapMgr`), or `GetMap(`, `LoadGrid`, `GetGrid`, `VisitAll`,
   `VisitNearby`, `GetCreatureListWithEntryInGrid`, or any `Visit`. When unsure whether a getter is
   read-only at the engine level, mark UNSAFE.
5. Classify:
   - **CANDIDATE** = no forbidden substring, no non-local assignment, every called function is in the
     allowed getter set, AND `checkInterval >= 2`.
   - **UNSAFE** = anything else, or any doubt.
6. Add a row to the table: value name | CANDIDATE/UNSAFE | checkInterval | the first reason it is
   UNSAFE (or "clean").

**Check (no human):** the table covers every flagged value and every UNSAFE row names a concrete
reason. The APPROVED set is determined by the deterministic rule itself — **every row classified
CANDIDATE is automatically approved** for Task 2.3. You do not wait for anyone to pick; the rule in
step 5 IS the picker. The `ParallelVerify` shadow-compare in Task 2.4 is the runtime backstop that
catches any value the text rule wrongly cleared.

> When in doubt in step 5, choose UNSAFE. The whitelist can grow later; a wrong CANDIDATE is caught
> by `ParallelVerify` (it logs `PARALLEL_VERIFY_FAIL`), which fails the Task 2.4 gate.

---

## Task 2.2 — Add an opt-in marker to the value base (off for all)  · Low risk

**Files you may edit:** `modules/mod-playerbots/src/strategy/Value.h`.

**Do:** Add to the `UntypedValue` base class a virtual yes/no method named `IsParallelSafe` that
returns false by default. Override it nowhere in this task.

**Check:** build; behavior unchanged (all false).

---

## Task 2.3 — Mark ONE approved value parallel-safe  · Low risk (repeat per value)

**Files you may edit:** only the one APPROVED value's `.h` (and `.cpp` if the override body goes
there).

**Do:** Override `IsParallelSafe` to return true in that single class. Nothing else. Repeat as a
separate task per approved value — **one value per task.**

**Check:** build. Behavior still unchanged (nothing reads `IsParallelSafe` until 2.4 ships and
`ParallelDecide` is on).

---

## Task 2.4 — Core: parallel pre-pass hook  · **High risk → go ONE step at a time**

> You DO this task, split into tiny steps. **Run the on-box gate (compile + `ctest` +
> `check_task.sh`) after EVERY numbered step; never run the game.** Core must NOT include module
> headers — connect core to the module through a new script hook, exactly like the existing
> `OnPlayerbot*` hooks.

**Goal:** at the very start of a map's update, when `ParallelDecide` is on, pre-compute each bot's
parallel-safe values across the worker pool so the existing serial AI pass finds them cached.

**Steps:**

- **2.4.1 (inventory, no code):** Read how an existing playerbot hook is wired end-to-end: its
  declaration in `src/server/game/Scripting/ScriptMgr.h`, its dispatcher in
  `src/server/game/Scripting/ScriptDefines/PlayerbotsScript.cpp`, and its module implementation in
  `modules/mod-playerbots/src/Playerbots.cpp` (e.g. `OnPlayerbotUpdate`). In your reply, list the 3
  edit points and the exact pattern each follows. Change no code.
- **2.4.2 (declare core hook):** Mirror that pattern to add a new hook `OnPlayerbotPrepareMapTick(Map*
  map)`: declare it in `ScriptMgr.h` and add its dispatcher in `PlayerbotsScript.cpp`, following the
  existing hook's exact style. Do not call it yet. **Files:** `ScriptMgr.h`,
  `ScriptDefines/PlayerbotsScript.cpp`. **On-box gate (compile + `ctest` + `check_task.sh`; never run the game).**
- **2.4.3 (empty module impl):** In `modules/mod-playerbots/src/Playerbots.cpp`, implement
  `OnPlayerbotPrepareMapTick(Map* map)` in the `PlayerbotsScript` class as an EMPTY body that returns
  immediately. **On-box gate (compile + `ctest` + `check_task.sh`; never run the game).** (Now the hook exists end-to-end but does nothing.)
- **2.4.4 (call from core):** In `src/server/game/Maps/Map.cpp`, add a single call to the new hook as
  the **first line of real work** in `Map::Update`, passing `this`. Move or change no existing line.
  **File:** `Map.cpp` (only this one added line). **On-box gate (compile + `ctest` + `check_task.sh`; never run the game).** Behavior must be unchanged (impl
  is still empty).
- **2.4.5 (gather bots, still no parallelism):** Fill the module impl: when
  `sPlayerbotAIConfig->parallelDecide` is true, build a list of the bots on THIS map only (use the
  existing way the module enumerates bots on a map; if none exists, use the map's player list and
  keep only those with a bot AI). For now, just count them and return — do not compute anything.
  When `parallelDecide` is false, return immediately. **On-box gate (compile + `ctest` + `check_task.sh`; never run the game).**
- **2.4.6 (serial pre-compute first):** For each gathered bot, on the SAME thread (no pool yet), call
  `Get()` on each of that bot's values whose `IsParallelSafe()` is true. **On-box gate (compile + `ctest` + `check_task.sh`; never run the game).** Doing
  the pre-compute serially first (no pool) means any behavior change here would be pure logic, not a
  race — its runtime parity is confirmed in Integration Run C, not on-box.
- **2.4.7 (move to the pool — single worker first):** Replace the serial loop with: submit one job
  per bot to `sBotComputePool` that calls `Get()` on that bot's `IsParallelSafe()` values, then
  `sBotComputePool->wait()` before returning. **One job handles exactly one bot — never split a
  bot's values across jobs and never let two jobs touch the same bot** (this exclusivity is what
  makes the per-bot cache writes race-free). Per the README protocol, first run with
  `BotComputeThreads=1` (proves logic, no races) before raising threads. **On-box gate (compile + `ctest` + `check_task.sh`; never run the game).**
- **2.4.8 (shadow-verify):** Add a block that runs only when `sPlayerbotAIConfig->parallelVerify` is
  true: immediately AFTER the pool finishes (still before the serial pass), for each parallel-safe
  value of each bot, recompute it once on the map thread and compare to the value the pool just
  cached. On any difference log the exact fixed token `PARALLEL_VERIFY_FAIL` with the bot guid and
  value name. This changes no behavior; it only logs. (This is what catches a value the Task 2.1 text
  rule wrongly marked CANDIDATE.)

**Why this is safe:** the hook runs before any mutation in the tick, so reads see a stable world;
each job writes only into its own bot's value cache; the serial pass that follows is unchanged and
now reads cached values. Other maps update on other threads without sharing entities — the same
assumption the existing per-map threading already relies on.

**Gate for Task 2.4 — split across the two tiers:**

ON-BOX (the agent runs and judges this before declaring the task done):
1. **Unit:** `ctest --output-on-failure` → all PASS (no regressions).
2. **Guard + off-default:** `check_task.sh 2.4` exits 0 and everything new is OFF by default
   (`ParallelDecide=0`); with the switch off the build is unchanged and safe to deploy.

INTEGRATION RUN C (operator, off-box, staging realm, ≥20-min warmup — NOT the agent):
3. **Parity:** deploy, set `ParallelDecide=1 ParallelVerify=1 BotComputeThreads=1`, warm up ≥20 min,
   hold ≥5 min, then `grep -c PARALLEL_VERIFY_FAIL <log>` → MUST be `0`. A non-zero count names
   exactly which value is impure — demote it to UNSAFE in Task 2.1, remove its `IsParallelSafe`
   override (Task 2.3) on-box, and re-run. Then raise `BotComputeThreads` and repeat.
4. **Races:** deploy a ThreadSanitizer build with `ParallelDecide=1`, warm up, then
   `grep -c "WARNING: ThreadSanitizer" <output>` → MUST be `0`.

**Done when:** the on-box gate is green; Integration Run C reports both counts 0; and with more
`BotComputeThreads` the hot map's serial AI time keeps dropping.

---

## Phase 2 done when
Approved values compute in parallel when `ParallelDecide=1`; ThreadSanitizer clean under load;
behavior matches the switch-off case; the hot map's serial AI time measurably drops and scales with
worker count.
