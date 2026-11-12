# Réplique — machine à remonter le temps pour QNX 8.0

**Réplique** transforme un crash QNX en artefact exploitable, en trois temps :

1. **Déclic** (cible QNX) fige la trace noyau à l'instant du crash et archive tout, corrélé et horodaté : trace + `pidin` + `slog2` + core dump.
2. **Autopsie** (hôte) reconstruit le graphe d'attente, la chaîne de blocage et désigne le coupable en une page.
3. **Réplique** (hôte) rejoue la trace de façon déterministe dans un simulateur d'ordonnanceur, et permet les « et si » (changer une priorité, un quantum).

**La promesse.** Un bogue de concurrence qui n'arrive « qu'une fois sur mille sur la cible » devient un artefact **figé, expliqué et rejouable à volonté sur un PC** — au lieu d'être perdu dans un tampon circulaire écrasé.

Projet candidat au QNX Global Hackathon 2026, catégorie *Developer Tooling & Host Tools*. Le contrat octet-pour-octet est gelé dans [CONTRACT.md](CONTRACT.md) ; le cahier des charges complet est dans [REPLIQUE-cahier-des-charges.md](REPLIQUE-cahier-des-charges.md).

## Arbre du dépôt

```
replique/
├── CONTRACT.md                  # contrat gelé : conteneur RKV1, événements, verdicts, sorties
├── REPLIQUE-cahier-des-charges.md
├── README.md                    # ce fichier
├── Makefile                     # build/test — vert sans QNX, sans cible, sans licence (S8)
├── LICENSE                      # MIT
├── core/                        # C11 PUR : ni horloge, ni fichier, ni socket, ni QNX
│   ├── include/replique/        #   types.h kev.h event.h sched.h replay.h deadlock.h verdict.h report.h
│   ├── src/                     #   kev.c event.c sched.c replay.c deadlock.c verdict.c report.c
│   └── tests/                   #   test_*.c + pilotes différentiels (dump_events.c, dump_pipeline.c,
│                                #   dump_replay.c) + declic_test.c
├── host/                        # Python 3.10+, stdlib seule : jumeau indépendant + CLI + rapports
│   ├── replique/                #   model event report kev kevgen sched replay deadlock verdict
│   │                            #   view markdown_report commands cli
│   └── tests/
├── target/                      # code QNX : Déclic
│   ├── declic/                  #   logique portable (*_portable.c) + colle QNX (*_qnx.c), Makefile.qnx
│   └── hook/                    #   intégration dumper / HAM
├── fixtures/                    # traces .rkv1 de référence (engendrées) + oracles .tp.txt si SDP
├── scenarios/                   # SPEC.md + résultats attendus gelés (<nom>.json)
├── tests/                       # différentiel C↔Python, déterminisme, différentiel traceprinter
├── codelab/                     # « figer, expliquer et rejouer un crash QNX » (4 chapitres)
├── image/                       # buildfile mkifs (image invitée avec Déclic + générateurs de bogues)
└── docs/                        # DESIGN GAP DECISIONS VALIDATION KEV-FORMAT SCHED-MODEL REPORT-TEMPLATE
```

L'arbre ci-dessus est l'architecture réelle : le cœur portable, le jumeau Python, les fixtures, la CI et le codelab sont en place et vérifiés ; la partie **cible QNX** (`target/`, `image/`) est écrite mais ne se compile qu'avec le SDP 8.0 — la logique portable de Déclic, elle, est testée sur PC.

Ce qui est **prouvé aujourd'hui** et ce qui reste conditionné à une cible sont détaillé dans [docs/PREUVE.md](docs/PREUVE.md). Les surfaces gelées (`CONTRACT.md`, `Makefile`, en-têtes C, `scenarios/SPEC.md`) font foi : les changer est une PR explicite.

## Mise en route

Prérequis hôte : `gcc` (C11), `python3` ≥ 3.10, `make`. **Aucun QNX, aucune cible, aucune licence** ne sont nécessaires (exigence S8).

```sh
make test        # garde + build C + fixtures + tests unitaires + différentiel C↔Python + déterminisme
```

Autres cibles (`make help` pour la liste) :

| Commande | Effet |
|---|---|
| `make build` | compile `build/libreplique.a`, les tests unitaires et les trois pilotes différentiels |
| `make fixtures` | engendre les traces `.rkv1` de référence dans `fixtures/` |
| `make unit` | exécute les binaires de test C (dont `build/declic-test`, logique portable de Déclic) |
| `make differential` | différentiel C↔Python (`tests/differential_c_python.sh`) |
| `make determinism` | P1 : 5 rejeux → 5 hachages identiques (`tests/determinism.sh`) |
| `make oracle` | différentiel contre `traceprinter` si un oracle `.tp.txt` est présent (`tests/differential_traceprinter.sh`) |
| `make guard` | taille (< 500 lignes/fichier, < 50 lignes/fonction), pureté C11, budget CI |
| `make target-qnx` | Déclic avec le SDP QNX 8.0 (`qcc`) — jamais requis par `make test` |

## Commandes CLI

Surface gelée (CONTRACT.md §4.7). Les pilotes C et les commandes Python produisent **les mêmes octets** sur la même trace.

```sh
PYTHONPATH=host python3 -m replique.cli dump    FICHIER [--json]
PYTHONPATH=host python3 -m replique.cli analyze FICHIER [--json] [--whatif TID=PRIO]…
PYTHONPATH=host python3 -m replique.cli replay  FICHIER [--json] [--hash] [--step] [--whatif TID=PRIO]…
PYTHONPATH=host python3 -m replique.cli gen     [SCÉNARIO|--all] [--out RÉP]
PYTHONPATH=host python3 -m replique.cli view    FICHIER [--out FICHIER.svg]

build/kev-dump    FICHIER                                # → lignes « ev| »
build/analyze     FICHIER [--whatif TID=PRIO]…           # → lignes « vd| »
build/replay-dump FICHIER [--whatif TID=PRIO]… [--hash]  # → lignes « dec| » ou « hash=<u64> »
```

### Exemple de sortie canonique

Scénario `deadlock` (SPEC.md §2) : `tid 101` (priorité 50) et `tid 102` (priorité 60) se bloquent mutuellement sur les mutex 16 et 32. Format des lignes gelé par CONTRACT.md §4 ; **horodatages et hachage illustratifs** — les valeurs exactes sont gelées avec les fixtures (`scenarios/deadlock.json`, produit par `tests/freeze_scenarios.sh`).

`dump` — lignes d'événements (`ev|`) :

```
ev|ts=1000300000|pid=10|tid=101|class=MUTEX_LOCK|sub=1|a=32|b=102|c=0
ev|ts=1000300100|pid=10|tid=101|class=THREAD_STATE|sub=3|a=1|b=32|c=102
ev|ts=1000400000|pid=10|tid=102|class=MUTEX_LOCK|sub=1|a=16|b=101|c=0
ev|ts=1000400100|pid=10|tid=102|class=THREAD_STATE|sub=3|a=1|b=16|c=101
```

`analyze` — lignes de verdicts (`vd|`, tri CONTRACT.md §4.4) :

```
vd|code=DEADLOCK_FOUND|sev=error|ts=1000400100|pid=10|tid=102|a=0|b=32|c=0|hash=0|chain=101,102|detail=
vd|code=CULPRIT|sev=error|ts=1000400100|pid=10|tid=102|a=0|b=0|c=0|hash=0|chain=|detail=holds-resource-16-at-prio-60
vd|code=REPLAY_DETERMINISTIC|sev=info|ts=0|pid=0|tid=0|a=0|b=0|c=0|hash=1234567890123456789|chain=|detail=
```

`replay` — lignes de décision d'ordonnancement (`dec|`) :

```
dec|ts=1000000000|tid=101|reason=INITIAL
dec|ts=1000300100|tid=101|reason=BLOCK
dec|ts=1000300100|tid=102|reason=UNBLOCK
dec|ts=1000400100|tid=102|reason=BLOCK
```

Lecture : le graphe d'attente contient le cycle `101 → 102 → 101` ; le coupable désigné (règle gelée CONTRACT.md §3.3) est `tid 102`, détenteur de la ressource 16 à la priorité effective la plus basse (60). Le rejeu reproduit le blocage à l'identique, hachage FNV-1a 64 stable.

## Statut honnête des essais E1–E5

Cinq essais conditionnent des affirmations du cahier des charges (§10). **Aucun n'est tranché à ce jour** — le SDP QNX 8.0 n'est pas installé sur la machine de développement :

| Essai | Question | Statut | Conséquence assumée |
|---|---|---|---|
| E1 | Format binaire `.kev` suffisamment documenté pour un parseur portable ? | **non tranché** | le cœur consomme le conteneur `RKV1` (CONTRACT.md §1), l'adaptateur kev→rkv1 viendra dans Déclic |
| E2 | `tracelogger` figeable proprement, historique avant-crash présent ? | **non tranché** | procédure cible du codelab 01 conditionnée |
| E3 | Terminaison d'un processus observable en temps réel (HAM / événement / `dumper`) ? | **non tranché** | déclencheur de Déclic à choisir sur cible |
| E4 | Modèle d'ordonnanceur reproductible fidèlement ? | **non tranché** | fidélité (P2) mesurée, divergences publiées, jamais masquées |
| E5 | Core dump `dumper` : états de threads exploitables ? | **non tranché** | corrélation C4 non implémentée en v1 |

Détail : [docs/GAP.md](docs/GAP.md) et cahier des charges §13. Ce qui est démontrable **aujourd'hui sans QNX** : le parseur RKV1, le graphe d'attente, le rejeu déterministe et le différentiel C↔Python, sur fixtures engendrées.

## Documentation

- [docs/DESIGN.md](docs/DESIGN.md) — architecture, chaîne de traitement, responsabilités par module, build SDP.
- [docs/KEV-FORMAT.md](docs/KEV-FORMAT.md) — trace noyau QNX (public) + conteneur RKV1 + méthode d'oracle.
- [docs/SCHED-MODEL.md](docs/SCHED-MODEL.md) — le modèle d'ordonnanceur et ses limites assumées.
- [docs/VALIDATION.md](docs/VALIDATION.md) — preuves P1/P2/P3, différentiels, mutants, gardes.
- [docs/GAP.md](docs/GAP.md) — les 5 trous R1–R5 et leur statut de vérification.
- [docs/DECISIONS.md](docs/DECISIONS.md) — D1–D11 + décisions d'implémentation.
- [docs/REPORT-TEMPLATE.md](docs/REPORT-TEMPLATE.md) — rapport « interblocage expliqué en une page » (C6).

## Codelab

**[« Figer, expliquer et rejouer un crash QNX »](codelab/README.md)** — 4 chapitres (exigence C8) : [01 figer](codelab/01-figer.md), [02 expliquer](codelab/02-expliquer.md), [03 rejouer](codelab/03-rejouer.md), [04 aller plus loin](codelab/04-aller-plus-loin.md). Les chapitres 2 et 3 tournent entièrement sur PC, sans QNX.

## Licence

MIT — voir [LICENSE](LICENSE). © 2026 Baptiste Faisy.
