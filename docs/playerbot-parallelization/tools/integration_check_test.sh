#!/usr/bin/env bash
# integration_check_test.sh — proves tools/integration_check.sh works correctly.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHECK="$SCRIPT_DIR/integration_check.sh"
FIX="$SCRIPT_DIR/fixtures"
[[ -f "$CHECK" ]] || { echo "cannot find integration_check.sh"; exit 2; }

pass=0; fail=0
run() {
  local desc="$1" exp="$2" log="$3" phase="$4"; set +e
  bash "$CHECK" "$log" "$phase" >/tmp/_ic_out 2>&1; local rc=$?; set -e
  if [[ "$rc" -eq "$exp" ]]; then echo "PASS [$desc] exit=$rc"; pass=$((pass+1))
  else echo "FAIL [$desc] expected=$exp got=$rc"; sed 's/^/    /' /tmp/_ic_out; fail=$((fail+1)); fi
}

run "phase 1 clean log" 0 "$FIX/clean_phase1.log" 1
run "phase 1 verify fail" 1 "$FIX/verify_fail.log" 1
run "phase 1 tsan warning" 1 "$FIX/tsan.log" 1

echo "----"; echo "PASS=$pass FAIL=$fail"
[[ "$fail" -eq 0 ]]
