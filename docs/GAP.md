# docs/GAP.md — les cinq trous exploités par Réplique (R1–R5)

Réplique exploite cinq absences d'outillage dans l'écosystème QNX 8.0, identifiées dans le lot B du dossier `qnx-dossier` (« Outillage temps réel, trace et supervision »). Ce document reprend la table §2.2 du cahier des charges et le statut de vérification §13, **sans rien affirmer de plus**.

> **Honnêteté imposée (cahier §13).** R1–R5 sont des affirmations d'**absence** dans la documentation publique QNX 8.0. Comme pour Tamis et Veille, elles doivent être revérifiées page à page dans le SDP installé avant d'être gravées dans la soumission. Le SDP n'est pas installé sur la machine de développement à ce jour.

## Table des trous

| Code | Trou, tel que formulé | Source | Statut de vérification (§13) |
|---|---|---|---|
| **R1** | La trace noyau est un **tampon circulaire écrasé** : au crash, l'historique *avant* le crash disparaît si on ne le fige pas immédiatement. Aucun outil ne fige la trace à la terminaison et n'archive trace + `pidin` + `slog2` + core dump de façon **corrélée et horodatée**. | Lot B (Déclic), doc kernel trace | À revérifier page à page (doc kernel trace) ; comportement attendu d'un tampon circulaire. Le figeage propre de `tracelogger` avec historique avant-crash est **non vérifié** (E2) |
| **R2** | `traceprinter` **décode mais n'explique pas** : aucune causalité, aucun graphe d'attente, aucun coupable désigné pour un interblocage. | Lot B (Autopsie) | Vérifié dans l'inventaire lot B ; à confirmer sur une trace réelle |
| **R3** | **Aucun rejeu déterministe** d'une trace d'ordonnanceur : reproduire un bogue de concurrence exige la **cible physique** et de la chance. | Lot B (Réplique) | À revérifier (absence dans la doc publique). Linux a `rr`/`perf` comme précédents — cités comme contexte, pas comme dépendance |
| **R4** | Les bogues de concurrence (interblocage, inversion de priorité, course) sont **non déterministes** et non reproductibles à la demande. | Conséquence de R3 | Découle de R3 ; même statut |
| **R5** | Pas de **causalité des messages** : qui a envoyé à qui, avec quelle latence, dans quel ordre — à reconstruire à la main depuis la trace brute. | Lot B (Filature) | Même statut que R2 (inventaire lot B, à confirmer sur trace réelle) |

## Réponse du projet, trou par trou

| Trou | Réponse | Où |
|---|---|---|
| R1 | Déclic fige et archive au crash (archive horodatée, empreinte SHA-256, manifeste) | `target/declic/` ; logique portable testée par `build/declic-test` |
| R2 | Autopsie : graphe d'attente, chaîne de blocage, coupable désigné par règle gelée, rapport une page | `core/src/deadlock.c`, `host/replique/deadlock.py`, `docs/REPORT-TEMPLATE.md` |
| R3 | Réplique : rejeu déterministe prouvé par hachage FNV-1a (P1, 5 rejeux identiques) | `core/src/replay.c`, `host/replique/replay.py`, `tests/determinism.sh` |
| R4 | Les scénarios de référence deviennent des fixtures versionnées, rejouables à l'infini sans cible | `scenarios/SPEC.md`, `fixtures/`, `host/replique/kevgen.py` |
| R5 | Les événements `MSG_SEND`/`MSG_RECEIVE`/`MSG_REPLY`/`MSG_ERROR` du contrat portent pid/tid/numéro de séquence : la causalité est reconstructible | CONTRACT.md §2.2, `replique_waitfor_build` |

## Affirmations techniques conditionnées (cahier §13, verbatim)

| Affirmation | Statut |
|---|---|
| La trace noyau est un tampon circulaire écrasé au crash (R1) | À revérifier page à page (doc kernel trace) ; comportement attendu d'un tampon circulaire |
| `traceprinter` décode sans causalité ni coupable (R2) | Vérifié dans l'inventaire lot B ; à confirmer sur une trace réelle |
| Aucun rejeu déterministe d'ordonnanceur dans l'écosystème QNX (R3) | À revérifier (absence dans la doc publique) |
| Format binaire `.kev` accessible au parseur | **Non vérifié** (E1) — `traceprinter` comme oracle de repli |
| `tracelogger` figeable proprement avec historique avant-crash | **Non vérifié** (E2) |
| Terminaison observable en temps réel (HAM/événement/dumper) | **Non vérifié** (E3) |
| Modèle d'ordonnanceur reproductible fidèlement | **Non vérifié** (E4) |
| Core dump `dumper` contient les états de threads | **Non vérifié** (E5) |

Toute correction d'affirmation est consignée dans le cahier des charges §13 et répercutée ici — jamais de mise à jour silencieuse.
