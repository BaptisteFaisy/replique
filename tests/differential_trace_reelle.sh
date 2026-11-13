#!/usr/bin/env bash
# differential_trace_reelle.sh — la trace QNX 8.0 RÉELLE (« wide », 823 Ko,
# 21 652 événements) traverse toute la chaîne du projet :
#
#   .kev --(décodeur C)--> RKV1 --(Autopsie C)--> verdicts
#   .kev --(jumeau Py)--> RKV1 --(Autopsie Py)--> verdicts
#
# Les deux chemins doivent produire EXACTEMENT les mêmes octets : c'est la
# preuve que le cœur C n'est pas « vert sur fixtures » seulement mais aussi
# sur une trace de production (13 294 événements RKV1, 4 418 décisions).
# Ne requiert ni QNX ni SDP (gcc + python3 suffisent) : CI ordinaire.
set -euo pipefail
RACINE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cd "$RACINE"

CC="${CC:-gcc}"
command -v "$CC" >/dev/null 2>&1 || { echo "AVERTISSEMENT : $CC absent, test C ignoré."; exit 0; }
command -v python3 >/dev/null 2>&1 || { echo "AVERTISSEMENT : python3 absent, test C ignoré."; exit 0; }

KEV="docs/preuves/trace-qnx800-wide.kev"
[ -f "$KEV" ] || { echo "AVERTISSEMENT : trace réelle absente, test ignoré."; exit 0; }

make -s build >/dev/null
"$CC" -std=c11 -Wall -Wextra -Werror -O2 \
    target/declic/kev_rkv1.c target/declic/kev_rkv1_tool.c -o "$TMP/kevtool"

ok=0; ko=0
verdict() { # verdict <libellé> <fichier1> <fichier2>
  if cmp -s "$2" "$3"; then
    ok=$((ok + 1)); echo "ok   $1"
  else
    ko=$((ko + 1)); echo "KO   $1"; diff -u "$2" "$3" | sed -n '1,10p'
  fi
}

# 1. décodage .kev -> RKV1 : le décodeur C (celui de la cible) == Python
"$TMP/kevtool" "$KEV" "$TMP/c.rkv1"
PYTHONPATH=host python3 -m replique.kev2rkv1 "$KEV" -o "$TMP/py.rkv1" >/dev/null
verdict "décodage .kev -> RKV1 : C == Python" "$TMP/py.rkv1" "$TMP/c.rkv1"

# 2. Autopsie : les verdicts du cœur C == ceux du jumeau Python
build/analyze "$TMP/c.rkv1" >"$TMP/c.vd"
PYTHONPATH=host python3 -m replique.cli analyze "$TMP/py.rkv1" >"$TMP/py.vd"
verdict "Autopsie (verdicts) : C == Python" "$TMP/py.vd" "$TMP/c.vd"

# 3. Rejeu déterministe : le hachage FNV-1a du C == celui du Python
build/replay-dump "$TMP/c.rkv1" --hash >"$TMP/c.hash"
PYTHONPATH=host python3 -m replique.cli replay "$TMP/py.rkv1" --hash >"$TMP/py.hash"
verdict "Rejeu (hachage) : C == Python" "$TMP/py.hash" "$TMP/c.hash"

echo "differentiel trace réelle : OK=$ok KO=$ko"
[ "$ko" -eq 0 ]
