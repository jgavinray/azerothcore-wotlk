#!/usr/bin/env bash
# baseline_extract.sh — produce docs/playerbot-perf-baseline.md from a Phase 0 staging log.
# Log format expected (BotPerfTap output): playerbots-perf map=<id> bot_ai_ms=<N> path_ms=<N> value_ms=<N>
set -euo pipefail
LOG="${1:?usage: baseline_extract.sh <log>}"
OUT="${2:-docs/playerbot-perf-baseline.md}"

awk '
  /playerbots-perf map=/ {
    for (i = 1; i <= NF; i++) {
      split($i, kv, "=")
      f[kv[1]] = kv[2]
    }
    map = f["map"] + 0
    ai[map] += f["bot_ai_ms"] + 0
    path[map] += f["path_ms"] + 0
    val[map] += f["value_ms"] + 0
  }
  END {
    hot_map = -1; hot_ai = 0
    for (m in ai) if (ai[m] > hot_ai) { hot_ai = ai[m]; hot_map = m }
    printf "# Playerbot perf baseline (auto-generated)\n\n"
    printf "Hottest map id: %d (bot_ai_ms total: %d)\n\n", hot_map, hot_ai
    printf "| map | bot_ai_ms | path_ms | value_ms | path%% | value%% |\n"
    printf "|-----|-----------|---------|----------|-------|--------|\n"
    for (m in ai) {
      total = ai[m] > 0 ? ai[m] : 1
      printf "| %d | %d | %d | %d | %.1f%% | %.1f%% |\n", m, ai[m], path[m], val[m], path[m]*100/total, val[m]*100/total
    }
  }
' "$LOG" > "$OUT"

echo "baseline_extract: wrote $OUT"
