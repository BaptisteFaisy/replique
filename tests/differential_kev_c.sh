#!/usr/bin/env bash
# differential_kev_c.sh — le décodeur C (.kev -> RKV1) rend-il EXACTEMENT les
# mêmes octets que le jumeau Python ? Cible S4/E1, ne requiert ni QNX ni SDP
# (gcc + python3 suffisent) : tourne donc en CI ordinaire.
set -euo pipefail
RACINE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cd "$RACINE"

CC="${CC:-gcc}"
command -v "$CC" >/dev/null 2>&1 || { echo "AVERTISSEMENT : $CC absent, test C ignoré."; exit 0; }
command -v python3 >/dev/null 2>&1 || { echo "AVERTISSEMENT : python3 absent, test C ignoré."; exit 0; }

"$CC" -std=c11 -Wall -Wextra -Werror -O2 \
    target/declic/kev_rkv1.c target/declic/kev_rkv1_tool.c -o "$TMP/kevtool"

ok=0; ko=0
for kev in docs/preuves/trace-qnx800.kev docs/preuves/trace-qnx800-wide.kev; do
  [ -f "$kev" ] || continue
  PYTHONPATH=host python3 -m replique.kev2rkv1 "$kev" -o "$TMP/py.rkv1" 2>/dev/null
  "$TMP/kevtool" "$kev" "$TMP/c.rkv1"
  if cmp -s "$TMP/py.rkv1" "$TMP/c.rkv1"; then
    ok=$((ok + 1)); echo "ok   $(basename "$kev") : C == Python ($(wc -c <"$TMP/c.rkv1") octets)"
  else
    ko=$((ko + 1)); echo "KO   $(basename "$kev") : C != Python"
    cmp -l "$TMP/py.rkv1" "$TMP/c.rkv1" | sed -n '1,5p'
  fi
done
echo "differentiel kev C/Python : OK=$ok KO=$ko"
[ "$ko" -eq 0 ]