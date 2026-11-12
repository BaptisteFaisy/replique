#!/usr/bin/env bash
# run_all.sh — enchaîne toute la chaîne de preuve, avec un chrono par étape.
# Utilisé par la CI en secours de `make test` (même contenu, timings visibles).
set -euo pipefail
source "$(dirname "$0")/lib.sh"

etape() {
  local nom="$1"; shift
  local debut fin
  debut=$(date +%s)
  echo "==> $nom"
  if "$@"; then
    fin=$(date +%s)
    echo "--- $nom : OK ($((fin - debut)) s)"
  else
    fin=$(date +%s)
    echo "--- $nom : ÉCHEC ($((fin - debut)) s)" >&2
    exit 1
  fi
}

etape "gardes" make guard
etape "build" make build
etape "fixtures" make fixtures
etape "tests unitaires" make unit
etape "différentiel C↔Python" make differential
etape "déterminisme" make determinism

echo "== TOUT EST VERT =="
