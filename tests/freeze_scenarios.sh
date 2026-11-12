#!/usr/bin/env bash
# freeze_scenarios.sh — gèle les attentes de chaque fixture dans scenarios/<nom>.json
# (schéma SPEC §6). Les valeurs numériques proviennent d'une exécution RÉELLE des
# DEUX implémentations : si le C et Python divergent, rien n'est écrit (exit 1).
set -euo pipefail
source "$(dirname "$0")/lib.sh"

exiger_outillage
[ -n "$(fixtures)" ] || die "aucune fixture : lance 'make fixtures'"

mkdir -p scenarios
for f in $(fixtures); do
  n="$(basename "$f" .rkv1)"
  ./build/analyze "$f" > "$TMP/$n.c.vd" 2>/dev/null || true
  python_cli analyze "$f" > "$TMP/$n.py.vd" 2>/dev/null || true
  cmp -s "$TMP/$n.c.vd" "$TMP/$n.py.vd" \
    || die "$n : le C et Python divergent — attentes NON gelées (diff ci-dessous)
$(diff -u "$TMP/$n.c.vd" "$TMP/$n.py.vd" | sed -n '1,10p')"
  ./build/replay-dump "$f" --hash > "$TMP/$n.c.hash" 2>/dev/null || echo "hash=0" > "$TMP/$n.c.hash"
  python_cli replay "$f" --hash > "$TMP/$n.py.hash" 2>/dev/null || echo "hash=0" > "$TMP/$n.py.hash"
  cmp -s "$TMP/$n.c.hash" "$TMP/$n.py.hash" \
    || die "$n : hachages C/Python différents — attentes NON gelées"
  TRACE="$f" VERDICTS="$TMP/$n.c.vd" HASH="$(cat "$TMP/$n.c.hash")" \
    OUT="scenarios/$n.json" python3 tests/freeze_lib.py
  echo "scenarios/$n.json gelé"
done
resume "fixtures gelées"
