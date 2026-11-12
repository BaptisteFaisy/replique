# Chapitre 3 — Rejouer : déterminisme, hachage et mode « et si »

## Objectif

Transformer la trace en **rejeu déterministe** : le simulateur d'ordonnanceur reproduit la séquence des décisions, le hachage FNV-1a 64 le prouve (P1), et le mode « et si » modifie une priorité pour tester une hypothèse (X2, X4). Sur PC, sans QNX.

## Prérequis

- [Chapitre 2](02-expliquer.md) fait : `make build fixtures` fonctionnel, `PWD` = racine du dépôt.
- Lecture utile : `docs/SCHED-MODEL.md` (règles d'élection, héritage, limites).

## Étape 1 — Le rejeu, décision par décision

```sh
PYTHONPATH=host python3 -m replique.cli replay fixtures/deadlock.rkv1
```

Équivalent C (mêmes octets) : `build/replay-dump fixtures/deadlock.rkv1`.

- **Vous devez observer** : une ligne canonique `dec|` par décision d'ordonnancement (CONTRACT.md §4.3), dans l'ordre de production :

```
dec|ts=1000000000|tid=101|reason=INITIAL
dec|ts=1000300100|tid=101|reason=BLOCK
dec|ts=1000300100|tid=102|reason=UNBLOCK
dec|ts=1000400100|tid=102|reason=BLOCK
```

  (horodatages illustratifs — valeurs exactes gelées avec la fixture). Raisons possibles (§2.6) : `INITIAL`, `PREEMPT`, `BLOCK`, `UNBLOCK`, `YIELD`, `TIMER`, `TERMINATE`, `WHATIF`. Sur `deadlock`, le rejeu se termine quand plus aucun thread n'est `RUNNING` : l'interblocage est **reproduit à l'identique**, sans cible.

## Étape 2 — Le hachage (P1)

```sh
PYTHONPATH=host python3 -m replique.cli replay fixtures/deadlock.rkv1 --hash
```

Équivalent C : `build/replay-dump fixtures/deadlock.rkv1 --hash`.

- **Vous devez observer** : une seule ligne `hash=<u64>` en décimal. Ce nombre est le FNV-1a 64 bits de la concaténation UTF-8 des lignes `dec|…\n` (CONTRACT.md §6) — il **résume toute la séquence d'ordonnancement**.

## Étape 3 — Prouver le déterminisme : 5 rejeux

```sh
make determinism
```

(`tests/determinism.sh`, N = 5.) Vous pouvez aussi le faire à la main :

```sh
for i in 1 2 3 4 5; do
  PYTHONPATH=host python3 -m replique.cli replay fixtures/race.rkv1 --hash
done
```

- **Vous devez observer** : 5 lignes `hash=…` **identiques**. Le rejeu est purement fonctionnel — aucune horloge, aucun aléa : c'est ce qui rend la « machine à remonter le temps » fiable, contrairement au bogue sur la cible. Le verdict `REPLAY_DETERMINISTIC` d'`analyze` porte ce même hachage.

## Étape 4 — Le mode « et si » (X4)

Scénario `race` (SPEC.md §4) : `tid 402` meurt (SEGV) sans libérer le mutex 64 ; `tid 401` attend ensuite un détenteur mort. Et si `402` avait été plus prioritaire ?

```sh
PYTHONPATH=host python3 -m replique.cli replay fixtures/race.rkv1 --whatif 402=20 --hash
PYTHONPATH=host python3 -m replique.cli replay fixtures/race.rkv1 --hash
```

- **Vous devez observer** : deux hachages **différents** (attendu gelé de SPEC §4). L'option `--whatif TID=PRIO` remplace la priorité de base du thread avant le rejeu ; elle est répétable (plusieurs threads). Via `analyze`, chaque modification appliquée produit en plus un verdict :

```sh
PYTHONPATH=host python3 -m replique.cli analyze fixtures/race.rkv1 --whatif 402=20
```

  → ligne `vd|code=WHATIF|sev=info|…|tid=402|a=20|…|hash=<hachage du rejeu modifié>|…`. Équivalent C : `build/analyze fixtures/race.rkv1 --whatif 402=20` et `build/replay-dump fixtures/race.rkv1 --whatif 402=20 --hash`.

- **Interprétation honnête** : un hachage différent prouve que la séquence d'ordonnancement change ; il ne prouve pas à lui seul que « le bogue disparaît ». Dans `race`, l'attente sur détenteur mort n'est pas un interblocage de priorités — le « et si » illustre la mécanique, et c'est la lecture des verdicts (`TRACE_INCOMPLETE detail=holder-dead` toujours présent) qui tranche.

## Étape 5 — Rejeu pas-à-pas (X6)

```sh
PYTHONPATH=host python3 -m replique.cli replay fixtures/deadlock.rkv1 --step
```

- **Vous devez observer** : le rejeu avance d'événement en événement au lieu de tout produire d'un coup (`step_through`, CONTRACT.md §4.8) — utile pour comprendre *quand* le cycle se ferme.

## Étape 6 — Fidélité (P2) : le rejeu face à la trace

Les scénarios SPEC accompagnent chaque changement d'état de l'événement `SCHED` **observé**. Le rejeu compare sa propre séquence à ces événements (même `ts`, même `tid` élu) :

- conforme → aucun verdict de divergence (`nominal`, `deadlock`, `race` l'exigent dans `scenarios/<nom>.json`) ;
- divergence → `vd|code=REPLAY_DIVERGED|sev=error|ts=…|tid=…|a=<raison attendue>|b=<raison trouvée>|…` nommant le premier événement fautif.

Sur fixtures, cela vérifie la cohérence du modèle ; la fidélité au **vrai noyau QNX** est l'essai **E4, non tranché** — voir `docs/SCHED-MODEL.md` §7 pour l'écart attendu, jamais masqué.

## Ce qui peut mal tourner

| Symptôme | Cause probable | Remède |
|---|---|---|
| `--whatif` refuse : `priorité hors bornes 0..255 pour tid …` | priorité ≥ 256 ou négative | rester dans 0–255 (0 = plus haute) |
| Hachage différent entre deux machines | l'une des sorties n'est pas canonique (locale, fin de ligne) | vérifier que les deux utilisent les commandes du dépôt ; le hachage porte sur les octets `dec|…\n` |
| Hachage ≠ valeur citée dans un doc | les exemples de ce codelab sont **illustratifs** | la valeur gelée fait foi : `scenarios/<nom>.json` (rempli par `tests/freeze_scenarios.sh`) |
| Code de retour `3`, stdout vide | trace corrompue pour `replay` | message `replique: PARSE_ERROR <detail> offset=<n>` sur stderr ; régénérer la fixture |
| `REPLAY_DIVERGED` sur une fixture saine | fixture régénérée avec un générateur modifié sans regeler les attentes | relancer `tests/freeze_scenarios.sh` **seulement** si le changement de scénario est une PR motivée (SPEC.md) |

Suite : [chapitre 4 — aller plus loin](04-aller-plus-loin.md).
