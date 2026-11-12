#!/usr/bin/env bash
# differential_traceprinter.sh — différentiel contre l'ORACLE du SDP (S4).
# Sans QNX sur la machine (cas nominal en CI), traceprinter est absent : le
# script avertit et sort 0 — jamais d'échec pour absence de licence.
set -euo pipefail
source "$(dirname "$0")/lib.sh"

# Normalisation des deux textes avant comparaison : ts, pid, tid, classe.
normaliser() {
  awk '{
    ts=""; pid=""; tid=""; cls="";
    for (i = 1; i <= NF; i++) {
      if ($i ~ /^(ts|timestamp)=/) ts=$i;
      if ($i ~ /^pid=/) pid=$i;
      if ($i ~ /^tid=/) tid=$i;
      if ($i ~ /^class=/) cls=$i;
    }
    if (ts != "") print ts, pid, tid, cls;
  }'
}

if ! command -v traceprinter >/dev/null 2>&1; then
  echo "AVERTISSEMENT : traceprinter absent (SDP QNX 8.0 non installé)."
  echo "Le différentiel contre l'oracle est ignoré — la CI reste verte sans licence."
  oracles=0
  for f in fixtures/*.tp.txt; do [ -e "$f" ] && oracles=$((oracles + 1)); done
  echo "Fixtures accompagnées d'un oracle .tp.txt : $oracles"
  exit 0
fi

exiger_outillage
for oracle in fixtures/*.tp.txt; do
  [ -e "$oracle" ] || continue
  trace="${oracle%.tp.txt}"
  [ -f "$trace" ] || { echo "oracle orphelin : $oracle"; continue; }
  timeout 60 traceprinter "$trace" 2>/dev/null | normaliser > "$TMP/oracle.txt" || true
  ./build/kev-dump "$trace" | normaliser > "$TMP/parseur.txt"
  if cmp -s "$TMP/oracle.txt" "$TMP/parseur.txt"; then
    OK=$((OK + 1)); echo "$(basename "$trace") : identique à traceprinter"
  else
    KO=$((KO + 1)); echo "KO $(basename "$trace") : divergence avec l'oracle"
    diff -u "$TMP/oracle.txt" "$TMP/parseur.txt" | sed -n '1,12p'
  fi
done
resume "comparaisons contre traceprinter"
