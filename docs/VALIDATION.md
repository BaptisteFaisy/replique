# docs/VALIDATION.md — la méthode de preuve de Réplique

Ce document décrit **ce qui prouve quoi**, avec le nom exact du script ou du test, l'état réel à ce jour et ce qui reste conditionné à un essai E1–E5. Sources : cahier des charges §7 (P1–P3), §8 (tests), `CONTRACT.md` §6 (déterminisme), `Makefile`, `scenarios/SPEC.md`.

> **État au 2026-11-12.** Le dépôt est en cours d'assemblage par lots (cahier §16). `make test` échoue aujourd'hui sur `guard` (scripts `.github/checks/` non livrés) ; les modules C et Python du cœur sont en écriture parallèle. La colonne « vert aujourd'hui » reflète cet état — elle passe à « vert » à la livraison des lots correspondants, sans changement de méthode.

## P1 — Déterminisme du rejeu

**Propriété.** Pour une trace donnée et une configuration d'ordonnanceur donnée, le rejeu produit exactement la même séquence de décisions, vérifiée par hachage. Rejoué N fois → N hachages identiques.

| Élément | Détail |
|---|---|
| Hachage | **FNV-1a 64 bits** sur l'octet, gelé CONTRACT.md §6 : offset `14695981039346656037`, premier `1099511628211`, mod 2⁶⁴, sur la concaténation UTF-8 des lignes `dec|…\n` (§4.3), imprimé en décimal |
| Implémentations | `replique_fnv1a64*` (`core/include/replique/kev.h`), `replique_replay_hash` (`replay.h`) ; `fnv1a64`, `hash_decisions` (`host/replique/kev.py`, `replay.py`) |
| Vérifié par | `make determinism` → `tests/determinism.sh`, **N = 5** rejeux, 5 hachages identiques exigés |
| Verdict porté | `REPLIQUE_REPLAY_DETERMINISTIC` (info) avec le hachage |
| Vert aujourd'hui | **non** (module de rejeu en écriture) — aucun conditionnement E1–E5 : P1 porte sur des fixtures RKV1 engendrées sur PC |

Le rejeu est purement fonctionnel : aucune horloge, aucun aléa, aucune allocation côté C (tampons fournis par l'appelant). Toute valeur gelée dans `scenarios/<nom>.json` (`hash`) est produite par une exécution réelle et vérifiée identique en C et en Python avant gel (`tests/freeze_scenarios.sh`).

## P2 — Fidélité du modèle

**Propriété.** Le rejeu d'une trace reproduit les ordonnancements observés dans cette trace : chaque événement `SCHED` (même `ts`, même `tid` élu) doit correspondre à la décision rejouée.

| Élément | Détail |
|---|---|
| Comparaison | première divergence → `REPLIQUE_REPLAY_DIVERGED` (error) nommant l'événement (`ts`, `tid`, raison attendue `a`, raison trouvée `b`) ; sans événement `SCHED` dans la trace, aucun verdict de fidélité |
| Implémentations | `replique_replay_check_fidelity` (`core/include/replique/replay.h`) ; `check_fidelity` (`host/replique/replay.py`) |
| Vérifié par | scénarios SPEC : chaque changement d'état s'accompagne de l'événement `SCHED` correspondant ; `nominal` attend zéro divergence ; attentes dans `scenarios/<nom>.json` |
| Vert aujourd'hui | **non** (idem P1) |
| Conditionné | P2 **sur fixtures** : aucun essai. P2 **sur traces réelles QEMU** (l'ambition X3 du cahier) : conditionnée à E2 (capture) et E4 (fidélité du modèle contre le vrai noyau — voir `docs/SCHED-MODEL.md`) |

## P3 — Exactitude du diagnostic

**Propriété.** Sur un scénario d'interblocage de référence, Autopsie détecte le cycle et désigne le coupable attendu.

| Élément | Détail |
|---|---|
| Vérifié par | scénario `deadlock` (SPEC §2) : `DEADLOCK_FOUND` avec `chain=101,102`, `b=32` ; `CULPRIT` `tid=102`, `detail=holds-resource-16-at-prio-60` (règle gelée CONTRACT.md §3.3) ; attentes dans `scenarios/deadlock.json` |
| Cas limites | `race` (SPEC §4) : détenteur mort → `TRACE_INCOMPLETE` `detail=holder-dead`, **jamais** `DEADLOCK_FOUND` (pas de cycle) ; `prio-inversion` : triple `[201,203,202]`, `a=0` sans héritage, variante `prio-inversion-heritage` avec `a=1` |
| Implémentations | `replique_deadlock_detect`, `replique_prio_inversion_detect` (`deadlock.h`) ; `detect`, `detect_prio_inversion`, `find_cycle` (`host/replique/deadlock.py`) |
| Vert aujourd'hui | **non** (modules en écriture) — aucun conditionnement E1–E5 sur fixtures |

## Différentiel C↔Python

| Élément | Détail |
|---|---|
| Vérifié par | `make differential` → `tests/differential_c_python.sh` : les pilotes C (`build/kev-dump`, `build/analyze`, `build/replay-dump`) et les commandes Python (`dump`, `analyze`, `replay`) produisent **les mêmes octets** sur la même trace (CONTRACT.md §4.7) |
| Couverture | lignes `ev|`, `vd|` (tri §4.4 inclus), `dec|`, codes de retour (0/2/3), fixtures dégradées (SPEC §5 : `bad-magic`, `bad-crc`, `bad-length`, tampon tronqué) |
| Vert aujourd'hui | **non** — aucun conditionnement E1–E5 |

## Différentiel contre `traceprinter` (oracle)

| Élément | Détail |
|---|---|
| Vérifié par | `make oracle` → `tests/differential_traceprinter.sh` : compare le décodage au fichier oracle `fixtures/<nom>.tp.txt` produit par `traceprinter` (SDP) sur la trace QNX d'origine, quand il est présent |
| Sans SDP | le différentiel tourne sur les fixtures pré-enregistrées (méthode Tamis) ; la chaîne n'est jamais bloquée par la licence |
| Vert aujourd'hui | **non**, et **conditionné à E1** pour sa pleine portée : le parseur consomme le conteneur RKV1, pas le `.kev` brut (non vérifié). La comparaison texte complète RKV1↔traceprinter exige l'adaptateur kev→rkv1 dans Déclic (`docs/KEV-FORMAT.md`) |

## Mutants

| Élément | Détail |
|---|---|
| But | injection de fautes dans `core/` pour vérifier que les tests les attrapent (cahier §8) |
| Statut | **prévu, non outillé à ce jour** : aucun script de mutation n'est référencé par le Makefile gelé. À câbler avec les lots L2/L3 ; un mutant survivant = test rouge |

## Gardes (taille, pureté, budget CI)

| Garde | Vérifié par | Règle |
|---|---|---|
| Taille | `make guard` → `.github/checks/size_guard.py` | fichiers < 500 lignes, fonctions < 50 lignes (cahier §4, exigence S9) |
| Pureté C11 | `make guard` → `.github/checks/purity_guard.py` | `core/` : aucun `fopen`, aucune horloge, aucun socket, aucun en-tête QNX |
| Budget CI | `make guard` → `.github/ci-budget/check_timeouts.py` | verdict CI ≤ 5 min, `timeout-minutes: 5`, `fetch-depth: 1` |
| Vert aujourd'hui | **non** — scripts en cours de livraison (lot CI) ; aucun conditionnement E1–E5 |

## Double preuve modèle + réel (cahier §7.4)

- **Modèle** (portable, PC) : parseur + graphe d'attente + rejeu, prouvés sur fixtures par P1–P3, différentiel C↔Python et différentiel oracle. Aucune licence QNX requise.
- **Réel** (cible QEMU) : Déclic capture de vraies archives ; P2 est alors mesuré contre de vraies traces. **Conditionné à E2 et E3** (figeage, déclenchement) pour la capture, **E4** pour l'interprétation des divergences, **E5** pour la corrélation core dump (C4, non implémentée en v1).

## Récapitulatif des conditionnements

| Preuve | Sur fixtures (PC) | Sur traces réelles |
|---|---|---|
| P1 déterminisme | aucun essai requis | E2 |
| P2 fidélité | aucun essai requis | E2, E4 |
| P3 diagnostic | aucun essai requis | E2, E3 |
| Différentiel C↔Python | aucun essai requis | — |
| Différentiel traceprinter | portée réduite sans SDP | E1 |
| Corrélation core dump (C4) | non implémentée v1 | E5 |


---

## Vérifications sur cible réelle (serveur `r630`, 12/11/2026)

SDP QNX 8.0 installé dans `~/qnx800` sur le serveur `r630` (Debian 13). Le
code cible a été compilé et l'image construite **sur cette machine** :

| Vérification | Commande | Résultat |
|---|---|---|
| Déclic compile (QNX 8.0, `-Werror`) | `qcc -V gcc_ntox86_64 -std=gnu11 -Wall -Wextra -Werror -Itarget/declic -o declic target/declic/declic_qnx.c target/declic/declic_watch.c target/declic/declic_portable.c target/declic/sha256_portable.c` | **OK** (32 Ko) |
| Générateurs de bogues (QNX 8.0) | `qcc -V gcc_ntox86_64 -Wall -Wextra -Werror -o bug-deadlock image/bugs/deadlock.c` | **OK** pour les trois |
| IFS | `mkifs -v -o . image.resolved ifs.bin` | **OK** (3 Mo) |
| Partition de boot | `mkxfs -t qnx6fsimg -o . boot_part.resolved boot.part` | **OK** (256 Mo) |
| Disque amorçable | `diskimage -p -s 163840 -c layout.txt -o replique-disk.img -b $QNX_TARGET/x86_64/boot/sys/ipl-diskpc1-nomsg` | **OK** (268 Mo) |
| Amorçage sous QEMU | `qemu-system-x86_64 -drive file=replique-disk.img,format=raw,if=ide ...` | SeaBIOS → « Booting from Hard Disk… » → « QNX v1.2d Boot Loader: qnx_sdp.ifs » |

**Ce qui reste** : le chargeur rend la main à l'IFS. Le bisect est fait — l'image
*officielle* du SDP, placée dans le même disque, s'arrête au même endroit : le
blocage est dans la ligne de commande QEMU ou dans le partitionnement (le
quickstart officiel a trois partitions, le nôtre une), pas dans l'IFS du projet.
Pistes et détails dans `image/README.md`.

**Essai E3, tranché pour de bon** : le SDP 8.0 n'a plus de HAM ni de service de
noms. Le déclencheur de terminaison de Déclic est donc la scrutation de
`/proc/<pid>`, avec `pidin dead` pour le signal — c'est implémenté et compilé
dans `target/declic/declic_watch.c`.
