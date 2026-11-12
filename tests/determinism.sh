#!/usr/bin/env bash
# determinism.sh — preuve P1 (CONTRACT.md §6) : rejouer N fois la même trace
# donne N hachages identiques, en C comme en Python, et les deux se valent.
set -euo pipefail
source "$(dirname "$0")/lib.sh"

N=5
exiger_outillage

for f in $(fixtures); do
  n="$(basename "$f" .rkv1)"
  if ! ./build/replay-dump "$f" --hash >/dev/null 2>&1; then
    echo "$n : non rejouable (trace corrompue attendue) — ignorée"
    continue
  fi
  premier_c="" premier_p=""
  for i in $(seq 1 "$N"); do
    hc="$(./build/replay-dump "$f" --hash)"
    hp="$(python_cli replay "$f" --hash)"
    [ -z "$premier_c" ] && premier_c="$hc"
    [ -z "$premier_p" ] && premier_p="$hp"
    [ "$hc" = "$premier_c" ] || die "$n : rejeu C $i diverge ($hc != $premier_c)"
    [ "$hp" = "$premier_p" ] || die "$n : rejeu Python $i diverge ($hp != $premier_p)"
  done
  [ "$premier_c" = "$premier_p" ] || die "$n : C et Python divergent ($premier_c != $premier_p)"
  echo "$n : ${N} rejeux identiques, C == Python, $premier_c"
done

info "déterminisme vérifié"
