#!/usr/bin/env bash
# value_audit.sh — produce docs/playerbot-value-purity-audit.csv mechanically.
# No judgment: tokens are classified by deterministic grep.
set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
VAL_DIR="$REPO_ROOT/modules/mod-playerbots/src/strategy/values"
OUT="$REPO_ROOT/docs/playerbot-value-purity-audit.csv"

SINGLETONS='\b(sObjectMgr|sSpellMgr|sMapMgr|sWorld|sLog|sConfigMgr|sObjectAccessor|sScriptMgr|sRandomPlayerbotMgr|sPlayerbotAIConfig|sPlayerbotsMgr|sPlayerbotTextMgr|sTravelMgr|sPlayerbotDbStore|sLootMgr|sBattlegroundMgr|sGuildMgr|sCharacterCache|sCreatureTextMgr|sQuestPoolMgr|sGameEventMgr|sAchievementMgr|sChannelMgr|sInstanceSaveMgr|sLFGMgr|sOutdoorPvPMgr|sPoolMgr|sTransportMgr|sWeatherMgr|sWorldState)\b'
MUT='[.>]\s*(Set|Add|Remove|Cast|Move|Update|Damage|Kill|Apply|Send|Yield|Interrupt|Teleport|Summon|Learn)[A-Za-z0-9_]*\s*\('
GRID='(VisitAll|VisitNearby|GetCreatureListWithEntryInGrid|LoadGrid|GetGrid|GetMap\s*\()'

echo 'value_file,calculate_methods,check_interval,mutation_hits,singleton_hits,grid_hits,classification' > "$OUT"

extract_calc() {
  awk '
    /::Calculate\s*\(/ { in_func = 1; depth = 0 }
    in_func {
      print
      for (i = 1; i <= length($0); i++) {
        c = substr($0, i, 1)
        if (c == "{") depth++
        else if (c == "}") { depth--; if (depth == 0 && in_func) { in_func = 0; print "---END---" } }
      }
    }
  ' "$1"
}

extract_interval() {
  grep -oE 'CalculatedValue<[^>]+>\([^)]*\)' "$1" \
    | grep -oE ',\s*[0-9]+\s*\)' \
    | grep -oE '[0-9]+' \
    | sort -n | head -1
}

shopt -s globstar nullglob
for f in "$VAL_DIR"/**/*.cpp "$VAL_DIR"/*.cpp; do
  [[ -f "$f" ]] || continue
  body=$(extract_calc "$f")
  if [[ -z "$body" ]]; then continue; fi
  calc_count=$(grep -c '::Calculate\s*(' "$f" || true)
  interval=$(extract_interval "$f")
  [[ -z "$interval" ]] && interval=1
  mut=$(echo "$body" | grep -cE "$MUT" || true)
  sing=$(echo "$body" | grep -cE "$SINGLETONS" || true)
  grid=$(echo "$body" | grep -cE "$GRID" || true)
  if [[ "$mut" -eq 0 && "$sing" -eq 0 && "$grid" -eq 0 && "$interval" -ge 2 ]]; then
    cls=CANDIDATE
  else
    cls=UNSAFE
  fi
  rel="${f#$REPO_ROOT/}"
  echo "$rel,$calc_count,$interval,$mut,$sing,$grid,$cls" >> "$OUT"
done

echo "value_audit: wrote $OUT"
echo "CANDIDATE: $(grep -c ',CANDIDATE$' "$OUT" || true)"
echo "UNSAFE:    $(grep -c ',UNSAFE$' "$OUT" || true)"
