#!/usr/bin/env bash
# integration_check.sh — operator's one-command gate per phase.
# Usage: integration_check.sh <log-path> <phase>
# Exit 0 = phase promotes. Non-zero = blocked; the log line that failed is printed.
set -euo pipefail

LOG="${1:?usage: integration_check.sh <log> <phase>}"
PHASE="${2:?usage: integration_check.sh <log> <phase>}"

fail=0

verify=$(grep -c 'PARALLEL_VERIFY_FAIL' "$LOG" || true)
if [[ "$verify" -ne 0 ]]; then
  echo "FAIL: $verify PARALLEL_VERIFY_FAIL lines in $LOG"
  echo "First 5:"; grep 'PARALLEL_VERIFY_FAIL' "$LOG" | head -5
  fail=1
fi

tsan=$(grep -c 'WARNING: ThreadSanitizer' "$LOG" || true)
if [[ "$tsan" -ne 0 ]]; then
  echo "FAIL: $tsan ThreadSanitizer warnings in $LOG"
  echo "First 3:"; grep 'WARNING: ThreadSanitizer' "$LOG" | head -3
  fail=1
fi

case "$PHASE" in
  0)
    if ! grep -q 'playerbots-perf map=' "$LOG"; then
      echo "FAIL: phase 0 needs at least one 'playerbots-perf map=' line"
      fail=1
    fi
    ;;
  1)
    if ! grep -q 'BotComputePool' "$LOG"; then
      echo "FAIL: phase 1 needs a diagnostic line confirming BotComputePool lifecycle"
      fail=1
    fi
    ;;
  2)
    if ! grep -q 'AsyncPathCompute=1' "$LOG"; then
      echo "FAIL: phase 2 needs the startup line confirming AsyncPathCompute=1"
      fail=1
    fi
    if ! grep -q 'ParallelVerify=1' "$LOG"; then
      echo "FAIL: phase 2 needs the startup line confirming ParallelVerify=1"
      fail=1
    fi
    ;;
  *)
    echo "FAIL: unknown phase '$PHASE'"; exit 2 ;;
esac

if [[ "$fail" -ne 0 ]]; then exit 1; fi
echo "integration_check: OK for phase $PHASE"
