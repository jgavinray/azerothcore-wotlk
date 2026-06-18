#!/usr/bin/env bash
#
# check_task.sh — mechanical guardrail for the playerbot-parallelization spec.
# Enforces GLOBAL RULE 2 (diff touches only allow-listed files) and RULE 5 (job files are pure:
# no game-object pointers, no mutating calls, no singletons/globals). Run BEFORE every commit:
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
  0.1) ALLOW='modules/mod-playerbots/src/PlayerbotAIConfig\.(h|cpp)|modules/mod-playerbots/conf/playerbots\.conf\.dist' ;;
  0.2) ALLOW='modules/mod-playerbots/src/BotPerfTap\.(h|cpp)|modules/mod-playerbots/CMakeLists\.txt' ;;
  0.3) ALLOW='modules/mod-playerbots/src/Playerbots\.cpp' ;;
  1.2) ALLOW='modules/mod-playerbots/src/BotComputePool\.h|src/test/playerbots/BotComputePoolTest\.cpp' ;;
  1.3) ALLOW='modules/mod-playerbots/src/Playerbots\.cpp' ;;
  1.4) ALLOW='modules/mod-playerbots/src/AsyncPath\.h' ;;
  1.5a) ALLOW='src/server/game/Movement/MovementGenerators/PathGenerator\.(h|cpp)' ;;
  1.5b) ALLOW='modules/mod-playerbots/src/strategy/actions/MovementActions\.(cpp|h)|modules/mod-playerbots/src/AsyncPath.*|modules/mod-playerbots/src/PathComputeJob\.h' ;;
  2.1) ALLOW='docs/playerbot-value-purity-audit\.md' ;;
  2.2) ALLOW='modules/mod-playerbots/src/strategy/Value\.h' ;;
  2.3) ALLOW='modules/mod-playerbots/src/strategy/values/.*\.(h|cpp)' ;;
  2.4) ALLOW='src/server/game/Scripting/ScriptMgr\.h|src/server/game/Scripting/ScriptDefines/PlayerbotsScript\.cpp|src/server/game/Maps/Map\.cpp|modules/mod-playerbots/src/Playerbots\.cpp|modules/mod-playerbots/src/.*Job\.(h|cpp)' ;;
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
FORBIDDEN='(Player|Unit|Creature|GameObject)[ ]*\*|Map[ ]*\*|->(Set|Add|Remove|Cast|Move|Update|Damage|Kill|Apply|Send)|\bs[A-Z][A-Za-z0-9]*\b'
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
