#!/usr/bin/env bash
# lib.sh — bibliothèque commune des scripts d'intégration (CONTRACT.md §4.7).
# Une seule implémentation de la comparaison canonique : les scripts la sourcent.
set -euo pipefail

RACINE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PY="python3"
OK=0
KO=0
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cd "$RACINE"

die()  { echo "ERREUR : $*" >&2; exit 1; }
info() { echo "==> $*"; }

# Vérifie les prérequis : binaires C construits, python3, paquet hôte importable.
exiger_outillage() {
  command -v "$PY" >/dev/null 2>&1 || die "python3 introuvable"
  local b
  for b in kev-dump analyze replay-dump; do
    [ -x "build/$b" ] || die "build/$b absent : lance 'make build'"
  done
  PYTHONPATH=host "$PY" -c 'import replique.kev' 2>/dev/null \
    || die "paquet hôte introuvable : vérifie host/replique/"
}

# Comparaison canonique : mêmes octets sur stdout ET même code de retour.
# Usage : comparer "étiquette" -- commande_A... -- commande_B...
comparer() {
  local etiquette="$1"; shift
  [ "$1" = "--" ] && shift
  local -a cmd_a=() cmd_b=()
  while [ "$1" != "--" ]; do cmd_a+=("$1"); shift; done
  shift
  cmd_b=("$@")
  local rc_a=0 rc_b=0
  "${cmd_a[@]}" >"$TMP/a.out" 2>"$TMP/a.err" || rc_a=$?
  "${cmd_b[@]}" >"$TMP/b.out" 2>"$TMP/b.err" || rc_b=$?
  if [ "$rc_a" != "$rc_b" ]; then
    echo "KO $etiquette : codes de retour différents (C=$rc_a, Python=$rc_b)" >&2
    KO=$((KO + 1)); return 1
  fi
  if ! cmp -s "$TMP/a.out" "$TMP/b.out"; then
    echo "KO $etiquette : sorties différentes — première divergence :" >&2
    diff -u "$TMP/a.out" "$TMP/b.out" | sed -n '1,12p' >&2
    KO=$((KO + 1)); return 1
  fi
  OK=$((OK + 1))
}

python_cli() { PYTHONPATH=host "$PY" -m replique.cli "$@"; }

fixtures() { ls fixtures/*.rkv1 2>/dev/null | sort; }

resume() {
  echo "--- ${1:-comparaisons} : OK=$OK KO=$KO"
  [ "$KO" -eq 0 ] || exit 1
}
