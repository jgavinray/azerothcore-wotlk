#!/usr/bin/env bash
#
# check_task.sh — mechanical guardrail for the playerbot compute-offload spec.
# Enforces that a task touches only allow-listed files and that job files do not contain obvious
# live-object, singleton, or mutation access. Run BEFORE every commit:
#
#     tools/check_task.sh <task-id>
#
# Exit 0 = safe to commit. Non-zero = STOP; the offending paths/tokens are printed.
#
# This is orchestration harness, not part of the server build. Edit only the ALLOWLIST table below
# when a task legitimately adds a new allow-listed file.
set -euo pipefail

TASK="${1:-}"
if [[ -z "$TASK" ]]; then echo "usage: check_task.sh <task-id>"; exit 2; fi

# --- Per-task allow-list of path globs (extended-regex, matched against changed paths) ----------
# Keep in sync with each task's "Files you may edit/create". One row per task id.
case "$TASK" in
  -1.1) ALLOW='docs/playerbot-parallelization/static-callgraph\.md' ;;
  -1.2) ALLOW='docs/playerbot-parallelization/static-forbidden-boundaries\.md' ;;
  -1.3) ALLOW='docs/playerbot-parallelization/static-mmap-thread-safety\.md' ;;
  -1.4) ALLOW='docs/playerbot-parallelization/static-path-equivalence\.md' ;;
  -1.5) ALLOW='docs/playerbot-parallelization/static-implementation-decision\.md' ;;
  0.1) ALLOW='modules/mod-playerbots/src/PlayerbotAIConfig\.(h|cpp)|modules/mod-playerbots/conf/playerbots\.conf\.dist' ;;
  0.2) ALLOW='modules/mod-playerbots/src/BotPerfTap\.(h|cpp)|modules/mod-playerbots/CMakeLists\.txt' ;;
  0.3) ALLOW='modules/mod-playerbots/src/Playerbots\.cpp' ;;
  1.2) ALLOW='modules/mod-playerbots/src/BotComputePool\.(h|cpp)|modules/mod-playerbots/CMakeLists\.txt' ;;
  1.3) ALLOW='src/test/playerbots/BotComputePoolTest\.cpp' ;;
  1.4) ALLOW='modules/mod-playerbots/src/Playerbots\.cpp' ;;
  2.0A) ALLOW='docs/playerbot-parallelization/source-anchors\.md|docs/playerbot-parallelization/mmap-safety-decision\.md' ;;
  2.0B) ALLOW='docs/playerbot-parallelization/mmap-safety-decision\.md' ;;
  2.0C) ALLOW='src/common/Collision/Management/MMapMgr\.(h|cpp)' ;;
  2.0D) ALLOW='docs/playerbot-parallelization/mmap-safety-decision\.md|docs/playerbot-parallelization/static-mmap-thread-safety\.md|docs/playerbot-parallelization/static-implementation-decision\.md' ;;
  2.0E) ALLOW='docs/playerbot-parallelization/verification-notes\.md' ;;
  2.1) ALLOW='docs/playerbot-parallelization/path-callsite-inventory\.md' ;;
  2.2) ALLOW='modules/mod-playerbots/src/AsyncPathCompute\.h' ;;
  2.3) ALLOW='modules/mod-playerbots/src/BotPathCompute\.(h|cpp)|src/test/playerbots/BotPathComputeTest\.cpp|modules/mod-playerbots/CMakeLists\.txt' ;;
  2.4) ALLOW='modules/mod-playerbots/src/PathComputeJob\.h' ;;
  2.5) ALLOW='modules/mod-playerbots/src/strategy/actions/MovementActions\.(cpp|h)|modules/mod-playerbots/src/AsyncPathCompute\.h' ;;
  2.6) ALLOW='modules/mod-playerbots/src/strategy/actions/MovementActions\.(cpp|h)' ;;
  2.7) ALLOW='modules/mod-playerbots/src/strategy/actions/MovementActions\.(cpp|h)|modules/mod-playerbots/src/AsyncPathCompute\.h' ;;
  *) echo "FAIL: unknown task id '$TASK' (add its allow-list row to check_task.sh)"; exit 2 ;;
esac

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

# --- Check 1: every changed file is allow-listed ------------------------------------------------
mapfile -t CHANGED < <(git diff --name-only HEAD; git ls-files --others --exclude-standard)
bad=0
for f in "${CHANGED[@]}"; do
  [[ -z "$f" ]] && continue
  if ! [[ "$f" =~ ^($ALLOW)$ ]]; then
    echo "FAIL rule 2: '$f' is not in the allow-list for task $TASK"; bad=1
  fi
done

# --- Check 2: job files are pure ----------------------------------------------------------------
FORBIDDEN='(Player|Unit|Creature|GameObject|WorldObject|Map|MotionMaster|Spell)[ ]*\*|\bObjectAccessor\b|->(Set|Add|Remove|Cast|Move|Update|Damage|Kill|Apply|Send|Teleport|Summon|Interrupt)|\bs[A-Z][A-Za-z0-9]*\b|\b(GetMap|Visit|LoadGrid|GetGrid|loadMapData|loadMap|GetNavMesh|GetNavMeshQuery)\b'
while IFS= read -r jf; do
  [[ -z "$jf" ]] && continue
  if grep -nE "$FORBIDDEN" "$jf" >/dev/null 2>&1; then
    echo "FAIL rule 5: forbidden token in job file '$jf':"
    grep -nE "$FORBIDDEN" "$jf" | sed 's/^/    /'
    bad=1
  fi
done < <(printf '%s\n' "${CHANGED[@]}" | grep -E 'Job\.(h|cpp)$' || true)

if [[ "$bad" -ne 0 ]]; then echo "check_task.sh: BLOCKED — do not commit."; exit 1; fi
echo "check_task.sh: OK for task $TASK"; exit 0
