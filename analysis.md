# Project State Evaluation: AzerothCore WotLK

## Overview

A fork of the AzerothCore WotLK emulator (v12.0.0-dev.1) running on the `testing` branch. ~1,000 C++ source files, ~36K total files, 14,289 commits. The project is in **active development with a recent testing infrastructure push**.

---

## Strengths

**Testing infrastructure is real and growing.** The biggest recent shift: 277 files added in one commit (`236ceb228`) bringing GoogleTest, ~32 test files, 60+ individual tests, CI workflows (TSan + ASan), and a mockable `IWorld` interface. This went from nothing to a working test suite with `unit_tests` executable. Tests cover threading primitives, entity actors, packet lifetime, map boundaries, and async DB operations.

**The new actor-based concurrency architecture is genuinely interesting.** `EntityActor` gives each game entity its own command queue, `EntityRouter` routes commands, `ActorWorkerPool` dispatches to worker threads. This is a meaningful evolution past the old single-threaded world loop — and the tests verify it.

**Modern C++ adoption is solid.** `[[nodiscard]]`, `override`, `final`, `constexpr`, `if constexpr`, `std::string_view`, `std::unique_ptr`, `std::atomic` — the code is on C++17 and uses it consistently. The `sWorld` singleton using `unique_ptr<IWorld>` is a clean abstraction for testing.

**CI is comprehensive.** 17 GitHub Actions workflows covering Linux/macOS/Windows builds, no-PCH/PCH variants, sanitizers, Docker, code style checks, SQL linting, and module builds.

**The script registry system is well-designed.** Static registration via `ScriptRegistry<T>::AddScript()` with hook tracking. Supports creature, item, spell, player, map, battleground, and spell scripts. Plus Lua integration (Eluna).

---

## Concerns

**1. Critical concurrency bugs in global lookups.** `PlayerNameMapHolder` (ObjectAccessor.cpp:86-116) — a `static unordered_map` accessed by Insert/Remove/Find with **zero synchronization**. Classic use-after-free pattern in `FindPlayer()`: `HashMapHolder<Player>::Find()` returns a pointer under shared_lock, then `player->IsInWorld()` dereferences without protection — the player could be removed between the two operations.

**2. No static analysis at the root.** No `.clang-tidy`, no root `.clang-format`, no cppcheck rules configured. Only `mod-playerbots` has a `.clang-format`. For a 245K-line C++ codebase, this is a gap.

**3. The main game loop is still single-threaded.** `World::Update()` drives everything sequentially. MapUpdater gives you cross-map parallelism, database workers handle queries, but the core update loop cannot utilize multiple cores. The new ActorWorkerPool helps with opcode dispatch but the fundamental bottleneck remains.

**4. `i_scriptLock` is a raw `bool` used without synchronization** (Map.h:746). Set to true/false around `ScriptsProcess()` calls. This is a data race — multiple threads can access the same map concurrently.

**5. ProducerConsumerQueue::Cancel() has a known race.** If `Push()` happens concurrently with `Cancel()`, the notification ordering can cause missed wakeups. Minor but real.

**6. Code quality debt.** ~30 FIXME/HACK/TODO markers scattered across critical files (Spell, Unit, Group, LFGMgr). No automated style enforcement. Mixed include guard styles. `using namespace std` in a few headers. `NULL` still present alongside `nullptr`.

**7. Testing is narrow.** All tests are unit-level with mocks. No integration tests, no end-to-end tests, no load tests. The test surface (~32 files) is small relative to the codebase (~1,000 source files). Many game systems have zero test coverage.

**8. Modules are somewhat siloed.** The 4 active modules (`mod-playerbots`, `mod-ah-bot`, `mod-account-mounts`, `mod-account-achievements`) each have their own git submodules, configs, and build systems. Cross-module integration isn't well-defined.

---

## Architecture Assessment

| Dimension | Rating | Notes |
|-----------|--------|-------|
| **Threading** | 6/10 | Actor pool is good. Main loop bottleneck, data races in global state. |
| **Testing** | 5/10 | Real infrastructure now exists. Coverage is narrow and shallow. |
| **Code Quality** | 6/10 | Modern C++ features used well. No static analysis. Legacy debt accumulates. |
| **Build System** | 7/10 | Mature CMake setup. Cross-platform. Well-organized. |
| **Architecture** | 6/10 | Singleton-heavy, but `IWorld` abstraction is clean. Script system is well-designed. |
| **CI/CD** | 8/10 | Comprehensive. Sanitizers, multi-platform, code style checks. |
| **Modularity** | 6/10 | Module system works but cross-module concerns are underdeveloped. |

---

## Top Priorities

1. **Fix the `PlayerNameMapHolder` data race** — it's the highest-impact bug, touches the most critical path (player login/logout).
2. **Add root `.clang-tidy` and `.clang-format`** — automated quality gate before more code accumulates.
3. **Expand test surface** — 32 test files for 245K lines is roughly 1 test per 7.5K lines. Spells, quests, and instances are completely untested.
4. **Resolve `i_scriptLock` and other map-level races** — clean up the remaining concurrency debt before scaling further.
5. **Automate style enforcement in CI** — the cppcheck runs but without a config or quality gate.
