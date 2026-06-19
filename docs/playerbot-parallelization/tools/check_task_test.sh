#!/usr/bin/env bash
#
# check_task_test.sh — proves tools/check_task.sh actually blocks the right things.
# Self-contained: builds a throwaway git repo, exercises good/bad diffs, asserts exit codes.
# Run from anywhere:  bash tools/check_task_test.sh   (exit 0 = all assertions pass)
#
# This exists so the guard is never an unvalidated dependency. CI should run it; the executor
# model does NOT need to validate the guard — this test already does.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GUARD="$SCRIPT_DIR/check_task.sh"
[[ -f "$GUARD" ]] || { echo "cannot find check_task.sh next to this test"; exit 2; }

T="$(mktemp -d)"
trap 'cd /; rm -rf "$T"' EXIT   # cd out before deleting, so cleanup never errors

cd "$T"
git init -q && git config user.email t@t && git config user.name t
mkdir -p modules/mod-playerbots/src/strategy/actions src/server/game/World docs/playerbot-parallelization
cp "$GUARD" check_task.sh && chmod +x check_task.sh
echo "// config" > modules/mod-playerbots/src/PlayerbotAIConfig.cpp
echo "// world"  > src/server/game/World/World.cpp
echo "# anchors" > docs/playerbot-parallelization/source-anchors.md
git add -A && git commit -qm baseline

pass=0; fail=0
run() { # desc expected_exit taskid
  local desc="$1" exp="$2" task="$3"; set +e
  local out
  out="$(mktemp)"
  ./check_task.sh "$task" >"$out" 2>&1; local rc=$?; set -e
  if [[ "$rc" -eq "$exp" ]]; then echo "PASS [$desc] exit=$rc"; pass=$((pass+1))
  else echo "FAIL [$desc] expected=$exp got=$rc"; sed 's/^/    /' "$out"; fail=$((fail+1)); fi
  rm -f "$out"
}

echo "x=1;" >> modules/mod-playerbots/src/PlayerbotAIConfig.cpp
run "0.1 allowed edit" 0 0.1
git checkout -q -- . ; git clean -fdq

echo "y=2;" >> src/server/game/World/World.cpp
run "0.1 forbidden file blocked" 1 0.1
git checkout -q -- . ; git clean -fdq

printf 'struct X { Unit* u; };\n' > modules/mod-playerbots/src/PathComputeJob.h
run "2.4 dirty job blocked" 1 2.4
git clean -fdq

printf 'struct PathRequest { unsigned mapId; float sx, sy, sz; };\n' > modules/mod-playerbots/src/PathComputeJob.h
run "2.4 clean job allowed" 0 2.4
git clean -fdq

printf 'struct X { void run(){ GetNavMesh(); loadMap(1,2,3); } };\n' > modules/mod-playerbots/src/PathComputeJob.h
run "2.4 raw mmap access blocked" 1 2.4
git clean -fdq

echo "revalidated" >> docs/playerbot-parallelization/source-anchors.md
run "2.0A source anchor doc allowed" 0 2.0A
git checkout -q -- . ; git clean -fdq

run "unknown task id" 2 9.9

echo "----"; echo "PASS=$pass FAIL=$fail"
[[ "$fail" -eq 0 ]]
