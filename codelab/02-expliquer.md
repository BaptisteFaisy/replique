# Chapitre 2 — Expliquer : graphe d'attente, chaîne de blocage, coupable

## Objectif

Ouvrir une trace, lire les événements décodés, et laisser **Autopsie** reconstruire le graphe d'attente, détecter la chaîne de blocage et **désigner le coupable** (exigences C1–C3, C7) — entièrement sur PC, sans QNX.

## Prérequis

- PC avec `gcc`, `make`, `python3` ≥ 3.10 ; `PWD` = racine du dépôt ;
- `make test` vert (ou au minimum `make build fixtures`).

## Étape 1 — Engendrer les fixtures

Les traces de référence sont **engendrées** (conteneur RKV1, drapeau `SYNTHETIC`) à partir des scénarios gelés de `scenarios/SPEC.md` :

```sh
make fixtures
```

Équivalent explicite : `PYTHONPATH=host python3 -m replique.cli gen --all --out fixtures` (un seul scénario : `… cli gen deadlock --out fixtures`).

- **Vous devez observer** : les fichiers `fixtures/<nom>.rkv1` — `nominal`, `deadlock`, `prio-inversion`, `prio-inversion-heritage`, `race`, plus les traces dégradées (`corrupt-magic.rkv1`, `corrupt-crc.rkv1`, `corrupt-truncated.rkv1`, `truncated-buffer.rkv1`).

## Étape 2 — Lire les événements décodés (`dump`)

```sh
PYTHONPATH=host python3 -m replique.cli dump fixtures/deadlock.rkv1
```

Le pilote C produit les mêmes octets :

```sh
build/kev-dump fixtures/deadlock.rkv1
```

- **Vous devez observer** : une ligne canonique `ev|` par événement (CONTRACT.md §4.1) :

```
ev|ts=1000300000|pid=10|tid=101|class=MUTEX_LOCK|sub=1|a=32|b=102|c=0
ev|ts=1000300100|pid=10|tid=101|class=THREAD_STATE|sub=3|a=1|b=32|c=102
```

  (horodatages illustratifs — les valeurs exactes sont celles du générateur, gelées avec la fixture). Lisez la deuxième ligne : `sub=3` = `BLOCKED` (§2.3), `a=1` = raison `MUTEX` (§2.4), `b=32` = ressource attendue, `c=102` = détenteur. C'est **une arête du graphe d'attente** : « 101 attend le mutex 32 tenu par 102 ».

## Étape 3 — Le verdict : chaîne de blocage et coupable (`analyze`)

```sh
PYTHONPATH=host python3 -m replique.cli analyze fixtures/deadlock.rkv1
```

Équivalent C : `build/analyze fixtures/deadlock.rkv1`.

- **Vous devez observer** (valeurs gelées par SPEC.md §2) :

```
vd|code=DEADLOCK_FOUND|sev=error|ts=…|pid=10|tid=102|a=0|b=32|c=0|hash=0|chain=101,102|detail=
vd|code=CULPRIT|sev=error|ts=…|pid=10|tid=102|a=0|b=0|c=0|hash=0|chain=|detail=holds-resource-16-at-prio-60
vd|code=REPLAY_DETERMINISTIC|sev=info|…
```

  Lecture :
  - `DEADLOCK_FOUND` : cycle `chain=101,102` — 101 attend 102 qui attend 101 ; `b=32` = ressource du dernier blocage observé ;
  - `CULPRIT tid=102` : règle gelée CONTRACT.md §3.3 — dans le cycle, le détenteur de la ressource la plus anciennement acquise avec la priorité effective la plus **basse** (60 > 50 numériquement = moins prioritaire) ; à égalité, le plus petit `tid` ;
  - verdicts triés par sévérité décroissante puis code, ts, tid (CONTRACT.md §4.4).

## Étape 4 — Visualiser le graphe d'attente (`view`, C7)

```sh
PYTHONPATH=host python3 -m replique.cli view fixtures/deadlock.rkv1 --out /tmp/deadlock.svg
```

- **Vous devez observer** : un SVG statique (sans dépendance externe) du graphe « T attend U » ; le cycle 101 ↔ 102 y apparaît. Ouvrez-le dans un navigateur.

## Étape 5 — Les autres diagnostics

| Trace | Commande | Attendu (gelé, SPEC.md) |
|---|---|---|
| `fixtures/prio-inversion.rkv1` | `analyze` | `PRIO_INVERSION` (warning), `chain=201,203,202` = [victime, intermédiaire, bloqueur], `a=0` (héritage **non** appliqué) ; aucun `DEADLOCK_FOUND` |
| `fixtures/prio-inversion-heritage.rkv1` | `analyze` | idem avec `a=1` : l'héritage a élevé le bloqueur 202 à la priorité de la victime |
| `fixtures/race.rkv1` | `analyze` | `TRACE_INCOMPLETE` (warning), `detail=holder-dead` — le détenteur attendu est **terminé** : attente sans fin mais **pas de cycle**, donc jamais `DEADLOCK_FOUND` |
| `fixtures/nominal.rkv1` | `analyze` | `REPLIQUE_OK` + `REPLAY_DETERMINISTIC` ; aucun verdict d'erreur |

## Étape 6 — Ce qu'une trace corrompue produit

```sh
PYTHONPATH=host python3 -m replique.cli analyze fixtures/corrupt-magic.rkv1
```

- **Vous devez observer** : un **seul** verdict `vd|code=PARSE_ERROR|sev=error|…|a=0|…|detail=bad-magic` et un code de retour **0** (`analyze` imprime le verdict). Avec `dump` en revanche :

```sh
PYTHONPATH=host python3 -m replique.cli dump fixtures/corrupt-magic.rkv1
```

  stdout vide, `replique: PARSE_ERROR bad-magic offset=0` sur stderr, code de retour **3** (CONTRACT.md §4.7). Aucune reprise approximative : la chaîne s'arrête (§3.2).

## Ce qui peut mal tourner

| Symptôme | Cause probable | Remède |
|---|---|---|
| `ModuleNotFoundError: No module named 'replique'` | `PYTHONPATH=host` omis | préfixer la commande, ou passer par les cibles `make` |
| `fixtures/deadlock.rkv1` introuvable | fixtures non engendrées | `make fixtures` |
| `build/kev-dump` introuvable | cœur C non compilé | `make build` |
| Code `3` sur une trace saine | fichier tronqué au téléchargement/copie | régénérer : `make fixtures` ; vérifier la taille = `32 + 32 × n_events` |
| Sortie différente des exemples | les `…` (ts, hash) sont des valeurs d'exécution | comparer aux attentes gelées de `scenarios/<nom>.json`, pas à ce chapitre |

Suite : [chapitre 3 — rejouer](03-rejouer.md), où cette même trace devient un rejeu déterministe.
