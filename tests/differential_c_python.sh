#!/usr/bin/env bash
# differential_c_python.sh — preuve centrale du projet (exigence S5) :
# le cœur C11 et le jumeau Python produisent LES MÊMES OCTETS sur la même trace,
# pour les trois surfaces canoniques et pour les codes de retour (CONTRACT §4.7).
set -euo pipefail
source "$(dirname "$0")/lib.sh"

exiger_outillage
[ -n "$(fixtures)" ] || die "aucune fixture : lance 'make fixtures'"

for f in $(fixtures); do
  n="$(basename "$f" .rkv1)"
  comparer "$n dump"    -- ./build/kev-dump "$f"    -- python_cli dump "$f"
  comparer "$n analyze" -- ./build/analyze "$f"     -- python_cli analyze "$f"
  comparer "$n replay"  -- ./build/replay-dump "$f" -- python_cli replay "$f"
  comparer "$n hash"    -- ./build/replay-dump "$f" -- python_cli replay "$f"
done

# Mode « et si » (X4) : les overrides doivent traverser les deux implémentations.
comparer "whatif analyze" \
  -- ./build/analyze fixtures/race.rkv1 --whatif 402=20 \
  -- python_cli analyze fixtures/race.rkv1 --whatif 402=20
comparer "whatif replay" \
  -- ./build/replay-dump fixtures/race.rkv1 --whatif 402=20 \
  -- python_cli replay fixtures/race.rkv1 --whatif 402=20
comparer "whatif hash" \
  -- ./build/replay-dump fixtures/race.rkv1 --whatif 402=20 --hash \
  -- python_cli replay fixtures/race.rkv1 --whatif 402=20 --hash

# Fichier absent : code de retour 2 des deux côtés.
comparer "fichier manquant" \
  -- ./build/kev-dump fixtures/inexistant.rkv1 \
  -- python_cli dump fixtures/inexistant.rkv1

info "différentiel C↔Python terminé"
resume "comparaisons C↔Python"
