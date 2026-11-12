# docs/DESIGN.md — architecture réelle de Réplique

Ce document décrit l'architecture **telle qu'implémentée**, pas une intention : les surfaces gelées sont `CONTRACT.md`, le `Makefile`, les en-têtes `core/include/replique/*.h` et `scenarios/SPEC.md`. Cahier des charges : `REPLIQUE-cahier-des-charges.md` (§4 architecture, §5 contrat, §15 décisions).

## 1. Les trois corps du projet

| Corps | Emplacement | Langage | Rôle |
|---|---|---|---|
| **Cœur portable** | `core/` | C11 **pur** | parseur RKV1, reconstruction d'états, graphe d'attente, détection, modèle d'ordonnanceur, rejeu, sorties canoniques |
| **Jumeau hôte** | `host/` | Python 3.10+, stdlib seule | réimplémentation **indépendante** du cœur + CLI + générateur de fixtures + JSON/SVG/Markdown |
| **Cible** | `target/` | C (QNX 8.0, `qcc`) | Déclic : fige la trace au crash, archive trace + `pidin` + `slog2` + core dump |

Le cœur et le jumeau produisent **les mêmes octets** sur la même trace (sorties canoniques CONTRACT.md §4) : c'est l'objet du différentiel `tests/differential_c_python.sh`. La double implémentation n'est pas de la redondance, c'est un instrument de vérification (décision D4).

## 2. Règle de portabilité

`core/` est C11 pur : **aucun `fopen`, aucune horloge (`time`, `clock_gettime`), aucun socket, aucun en-tête QNX**. L'appelant fournit le tampon de trace et les tampons de sortie ; le cœur ne possède aucune mémoire (pas d'allocation : capacités passées par l'appelant, cf. `replique_replay_result.cap`, `REPLIQUE_MAX_*` dans `types.h`).

Conséquences :

- le même noyau s'exécute depuis un test unitaire sur PC, depuis un pilote de différentiel et depuis l'outil hôte ;
- la garde `.github/checks/purity_guard.py` (cible `make guard`) échoue sur toute inclusion interdite ;
- jamais d'horodatage réel ni de chemin absolu dans les sorties canoniques (CONTRACT.md §7) — le rapport Markdown horodaté est produit par l'hôte, hors différentiel.

La logique **portable** de Déclic (décision de déclenchement, construction de l'archive, manifeste, empreinte) suit la même règle et vit dans `target/declic/*_portable.c`, testée sur PC via `build/declic-test` (`core/tests/declic_test.c`) ; la colle QNX vit dans `target/declic/*_qnx.c` et ne se compile qu'avec le SDP (§7).

## 3. Règle d'oracle

Le décodage de la trace est validé **par différentiel contre `traceprinter`**, utilitaire hôte du SDP QNX : même trace en entrée, mêmes événements décodés en sortie. Méthode établie sur Tamis contre `secpolcompile`.

- Une fixture `fixtures/<nom>.rkv1` peut être accompagnée d'un oracle `fixtures/<nom>.tp.txt`, produit par `traceprinter` sur la trace QNX d'origine.
- `make oracle` (`tests/differential_traceprinter.sh`) compare quand l'oracle est présent ; **en l'absence de SDP, le différentiel tourne sur les fixtures pré-enregistrées** — la chaîne n'est jamais bloquée par la licence.
- Le format binaire `.kev` brut n'est pas vérifié à ce jour (essai E1 non tranché) : le cœur consomme le conteneur `RKV1` (CONTRACT.md §1, `docs/KEV-FORMAT.md`).

## 4. Chaîne de traitement (figée, cahier §5.1 / CONTRACT §3.2)

```
archive Déclic (.rkv1 + pidin + slog2 + core)
  → replique_kev_parse            (conteneur → événements typés)
  → replique_waitfor_build        (événements → arêtes « T attend U »)
  → replique_deadlock_detect      (cycle → DEADLOCK_FOUND + CULPRIT)
  → replique_prio_inversion       (chaîne → PRIO_INVERSION)
  → replique_replay               (rejeu déterministe → REPLAY_DETERMINISTIC / DIVERGED)
  → rapport (texte canonique, Markdown une page, JSON hôte)
```

Aucune étape ne remonte. Une trace illisible **arrête la chaîne** : un seul verdict `REPLIQUE_PARSE_ERROR` (décalage octet + cause gelée §1.4), aucun autre verdict, jamais de reprise approximative. L'orchestration complète tient dans `replique_pipeline` (`verdict.h`) côté C et `pipeline_blob`/`pipeline_events` (`replique/verdict.py`) côté Python.

## 5. Découpage en modules — `core/`

En-têtes dans `core/include/replique/`, implémentations dans `core/src/` (un `.c` par `.h`). Responsabilité de chaque fichier :

| Fichier | Responsabilité |
|---|---|
| `types.h` | structures et enums gelés du contrat : `replique_event`, `replique_verdict`, `replique_decision`, `replique_edge`, `replique_thread`, `replique_sched_config`, `replique_replay_result`, constantes `RKV1` (magic, tailles, drapeaux), codes d'état, causes de parse, fabriques de verdict |
| `event.h` / `event.c` | tables gelées du contrat (§2) : classes d'événements, états de thread, raisons de blocage, politiques, raisons d'ordonnancement + noms canoniques (`replique_class_name`, `replique_state_name`, `replique_block_reason_name`, `replique_sched_reason_name`) — la sortie texte en dépend |
| `kev.h` / `kev.c` | parseur du conteneur RKV1 : `replique_kev_parse` (+ variante `_diag` avec cause/offset), `replique_kev_header`, `replique_kev_write` (générateur C), `replique_crc32` (ISO-HDLC), `replique_fnv1a64*`. Lecture champ par champ, jamais par cast de structure (indépendance alignement/boutisme) |
| `sched.h` / `sched.c` | modèle d'ordonnanceur (CONTRACT §5) : état `replique_sched` (256 threads max, aucune allocation), init, intern, élection `replique_sched_pick` (priorité puis FIFO/RR), application d'un événement `replique_sched_apply`, héritage de priorité `replique_sched_inherit`/`release` (prise maximale), « et si » `replique_sched_override_prio` |
| `replay.h` / `replay.c` | rejeu déterministe : `replique_replay` (purement fonctionnel), `replique_replay_hash` (FNV-1a 64 des lignes `dec|`), `replique_replay_check_fidelity` (P2 : comparaison aux événements `SCHED` de la trace) |
| `deadlock.h` / `deadlock.c` | Autopsie : `replique_states_rebuild` (C1), `replique_waitfor_build` (C2, arêtes), `replique_deadlock_detect` (C3, cycle + coupable, règle §3.3), `replique_prio_inversion_detect` (C5, triple [V, I, B], règle §3.4) |
| `verdict.h` / `verdict.c` | helpers de verdicts (fabrique, sanitisation du détail, tri stable §4.4) + `replique_pipeline` (chaîne complète du §4 ci-dessus) |
| `report.h` / `report.c` | sorties texte canoniques §4 : `replique_report_event(s)`, `replique_report_verdict(s)`, `replique_report_decision(s)` — écrit dans le tampon fourni, signale la troncature |

Tests et pilotes dans `core/tests/` : `test_*.c` (unitaires, aspirés par la règle générique du Makefile) et les pilotes de différentiel `dump_events.c` → `build/kev-dump`, `dump_pipeline.c` → `build/analyze`, `dump_replay.c` → `build/replay-dump`, plus `declic_test.c` → `build/declic-test` (logique portable de Déclic).

## 6. Découpage en modules — `host/replique/`

API Python gelée (CONTRACT §4.8), stdlib seule :

| Module | Responsabilité |
|---|---|
| `model.py` | jumeau exact de `types.h` : constantes, dataclasses `Header`, `Event`, `Verdict`, `Decision`, `Edge`, `Thread`, `SchedConfig`, `ReplayResult`, exception `ParseError(cause, offset)` |
| `event.py` | noms canoniques jumeaux de `event.c` (`class_name`, `state_name`, `block_reason_name`, `sched_reason_name`, `verdict_name`, `severity_name`, `cause_name`, …) |
| `report.py` | sorties canoniques texte (`event_line`, `verdict_line`, `decision_line`, `events_text`, `verdicts_text`, `decisions_text`), JSON gelé §4.5/§4.6 (`event_json`, `verdict_json`, `replay_json`, `trace_json`, `to_json`, `document`), `sha256_hex`, `sanitize_detail` |
| `kev.py` | parseur/écriveur RKV1 jumeau de `kev.c` : `parse`, `write`, `read_file`, `crc32`, `fnv1a64` |
| `kevgen.py` | générateur de fixtures (S6) : `NAMES`, `scenario(name)`, `write`, `write_all` — les quatre scénarios gelés de `scenarios/SPEC.md` |
| `sched.py` | modèle d'ordonnanceur jumeau de `sched.c` : `default_config`, `override_prio`, classe `Scheduler` (`intern`, `find`, `pick`, `apply`, `inherit`, `release`) |
| `replay.py` | rejeu jumeau de `replay.c` : `run`, `hash_decisions`, `check_fidelity` (P2), `step_through` (X6) |
| `deadlock.py` | Autopsie jumeau de `deadlock.c` : `rebuild_states`, `build_waitfor`, `detect`, `detect_prio_inversion`, `find_cycle` |
| `verdict.py` | tri stable §4.4 (`sort_verdicts`), fabrique `make`, pipelines `pipeline_blob`/`pipeline_events` |
| `view.py` | visualisation du graphe d'attente (C7) : `render_svg`, SVG statique sans dépendance externe |
| `markdown_report.py` | rapport « interblocage expliqué en une page » (C6) : `render` — structure dans `docs/REPORT-TEMPLATE.md` |
| `commands.py` | implémentation des commandes gelées (`dump`, `analyze`, `replay`, `gen`, `view`) avec les codes de retour §4.7 (0 conforme, 2 fichier illisible, 3 trace corrompue) |
| `cli.py` | surface argparse : `python3 -m replique.cli …`, options `--json`, `--whatif TID=PRIO` (répétable), `--hash`, `--step`, `--out` |

Aucun module ne lit l'horloge ni n'écrit de chemin absolu dans une sortie canonique.

## 7. Construire Déclic avec le SDP

Déclic est le seul composant qui exige le SDP QNX 8.0. **`make test` ne l'exige jamais** (S8) ; la construction cible est une cible distincte :

```sh
make target-qnx        # exécute : $(MAKE) -C target/declic -f Makefile.qnx
```

- **Compilateur** : `qcc` (le Makefile.qnx reprend les drapeaux du projet : `-std=c11 -O2 -Wall -Wextra -Werror -Wpedantic`). Sans `qcc` dans le `PATH`, `make target-qnx` affiche un message explicatif et ne fait rien — la logique portable, elle, reste testée sur PC (`build/declic-test`).
- **Découpage** : `target/declic/*_portable.c` (décision de déclenchement, archive, manifeste, empreinte SHA-256 — compilé et testé sur PC) ; `target/declic/*_qnx.c` (colle : surveillance de terminaison, figeage du tampon, collecte `pidin`/`slog2`/`dumper` — SDP uniquement) ; `target/hook/` (intégration `dumper`/HAM).
- **Image invitée** : `image/` porte le buildfile `mkifs` de l'image QEMU (Déclic + générateurs de bogues des scénarios SPEC). Si un système de fichiers flash inscriptible est nécessaire pour les archives, il sera monté par un buildfile `mkdfs` dédié — non produit à ce jour.
- **Honnêteté** : le figeage de `tracelogger` (E2) et l'observabilité temps réel de la terminaison (E3) sont **non tranchés** ; la colle QNX est conçue pour plusieurs déclencheurs (HAM, événement noyau `PROCESS_DESTROY`, crochet `dumper`) et le plus précoce sera choisi après essai (cahier §12).

## 8. Construction et CI

- `gcc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic` pour tout le C (cœur, pilotes, Déclic portable).
- Garde de taille : fichiers < 500 lignes, fonctions < 50 lignes (`.github/checks/size_guard.py`).
- Garde de pureté : `.github/checks/purity_guard.py` (interdits du §2).
- Budget CI ≤ 5 min : `.github/ci-budget/check_timeouts.py` + workflows `.github/workflows/` (`timeout-minutes: 5`, `fetch-depth: 1`).
- Cibles du Makefile : `build`, `test`, `unit`, `differential`, `determinism`, `fixtures`, `oracle`, `guard`, `clean`, `target-qnx`, `help`. `make test` = `guard build fixtures unit differential determinism`.
