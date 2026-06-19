# Phase 1: Playerbot Compute Pool

Goal: add a reusable worker pool for playerbot compute jobs without changing gameplay.

The pool exists only to run bounded plain-data jobs. It must not know about playerbot decisions,
world objects, maps, spells, movement, DB, packets, or sessions.

## Editable Scope

Only these files may be touched in this phase:

- `modules/mod-playerbots/src/BotComputePool.h`;
- `modules/mod-playerbots/src/BotComputePool.cpp`, only if a separate implementation file is
  needed by local style;
- `src/test/playerbots/BotComputePoolTest.cpp`;
- `modules/mod-playerbots/src/Playerbots.cpp`, only for pool lifecycle wiring;
- `modules/mod-playerbots/CMakeLists.txt`, only if the module does not auto-discover new sources.

Do not edit actions, values, movement behavior, `PathGenerator`, or map update logic in this phase.

## Task 1.1: Inspect Existing Thread Pool Pattern

Read the existing map updater implementation and summarize its lifecycle before writing code.

Task items:

1. Identify how workers are started.
2. Identify how jobs are queued.
3. Identify how pending jobs are tracked.
4. Identify how wait and shutdown work.
5. Record the summary in the task notes or commit message.

No source files may be changed in this task.

Stop if:

- the existing worker pattern is not understood.

## Task 1.2: Add Compute Pool Type

Create a playerbot compute pool that accepts abstract jobs and runs them on worker threads.

Allowed files:

- `modules/mod-playerbots/src/BotComputePool.h`;
- `modules/mod-playerbots/src/BotComputePool.cpp`, only if needed;
- `modules/mod-playerbots/CMakeLists.txt`, only if needed.

Requirements:

- no game or playerbot gameplay headers beyond the existing queue utility if avoidable;
- no `Player*`, `Unit*`, `WorldObject*`, `Map*`, `MotionMaster`, `Spell`, DB, packet, session, or
  script concepts;
- no implicit startup in the constructor;
- explicit activate;
- explicit wait;
- explicit deactivate;
- safe shutdown with zero jobs;
- pending-job accounting.

Task items:

1. Add the pool declaration.
2. Add the pool implementation, or make it header-only if that matches test constraints.
3. Expose a module-style singleton.
4. Build with no gameplay references.

Stop if:

- the pool needs to include live game-object types;
- shutdown semantics are unclear.

## Task 1.3: Add Compute Pool Unit Tests

Test the pool without game objects.

Allowed file:

- `src/test/playerbots/BotComputePoolTest.cpp`.

Required test scenarios:

- submitting many jobs increments a plain atomic counter exactly once per job;
- activating and deactivating with zero jobs does not hang;
- waiting after submitted jobs completes only after all jobs run;
- jobs submitted while workers are active are not lost or double-run.

Task items:

1. Add unit test file.
2. Run tests.
3. Fix only pool/test issues.

Stop if:

- tests require booting the game server;
- tests require DB or map state.

## Task 1.4: Wire Pool Lifecycle

Start the pool only when future compute features are enabled.

Allowed file:

- `modules/mod-playerbots/src/Playerbots.cpp`.

Requirements:

- activate when `AsyncPathCompute` is enabled;
- use `BotComputeThreads`;
- deactivate during module/world shutdown;
- with all switches off, no worker threads start;
- startup/shutdown must not change gameplay behavior.

Task items:

1. Add lifecycle start in the existing playerbot world/module initialization path.
2. Add lifecycle stop in the matching shutdown path.
3. Add minimal diagnostic logging only if already consistent with module style.
4. Build and test.

Stop if:

- there is no safe shutdown hook;
- lifecycle wiring requires unrelated core changes.

## Phase Exit Criteria

- compute pool exists;
- unit tests pass;
- pool starts only when enabled;
- pool stops cleanly;
- no gameplay code submits jobs yet;
- switch-off behavior is unchanged.
