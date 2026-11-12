# PREUVE.md — ce que le dépôt démontre aujourd'hui (12/11/2026)

Réplique est un projet **en construction** : ce document distingue, sans
enjoliver, ce qui est **prouvé et exécutable aujourd'hui** de ce qui reste
conditionné à une cible QNX ou au SDP.

Les chiffres ci-dessous sont ceux de la chaîne complète exécutée sur la machine
de développement, puis rejouée par la CI sur le runner self-hosted
`r630-replique-01` (R630, Debian 13).

---

## 1. Ce qui est vert, sans QNX, sans cible, sans licence

`make test` enchaîne garde → build → fixtures → tests unitaires → différentiel →
déterminisme. Chaîne complète : **moins d'une minute**.

| Preuve | Exigence | Commande | Résultat observé |
|---|---|---|---|
| Différentiel C↔Python | S5 | `make differential` | **40 comparaisons, 0 écart** (9 fixtures × 4 surfaces + « et si » + fichier absent) |
| Déterminisme du rejeu | P1 | `make determinism` | 5 rejeux → 5 hachages identiques, **C == Python**, sur les 6 fixtures rejouables |
| Fidélité du rejeu | P2 | (inclus dans le différentiel) | **aucun** verdict `REPLAY_DIVERGED` sur les fixtures : le rejeu reproduit les décisions observées dans la trace |
| Diagnostic de l'interblocage | P3 / C3 | `build/analyze fixtures/deadlock.rkv1` | `DEADLOCK_FOUND` chain `101,102`, `b=16`, `ts=1700` + `CULPRIT` `tid=102`, `detail=holds-resource-32-at-prio-60` |
| Détection d'inversion de priorité | C5 | `build/analyze fixtures/prio-inversion.rkv1` | `PRIO_INVERSION` chain `201,203,202`, `a=0` (sans héritage) / `a=1` (avec héritage) |
| Mode « et si » | X4 | `build/replay-dump fixtures/race.rkv1 --whatif 402=20` | verdict `WHATIF` + **hachage différent** du rejeu de base |
| Cœur portable | S8 | `.github/checks/purity_guard.py` | `core/` sans `fopen`, horloge, socket, allocation ni en-tête QNX ; `host/` en stdlib seule |
| Garde de taille | S9 | `.github/checks/size_guard.py` | 49 fichiers, tous < 500 lignes, 113 fonctions C et les fonctions Python < 50 lignes de code |
| Budget CI | S9 | `.github/ci-budget/check_timeouts.py` | chaque job ≤ 5 min ; **run `ci` réel : 52 s** |
| CI sur runner | S9 | `gh run list --repo Tradoooo/replique` | `ci` **vert** (52 s), `ci-budget` **vert** (40 s) |

## 2. Les artefacts gelés

`fixtures/` contient 9 traces `.rkv1` engendrées par `host/replique/kevgen.py`
(tables gelées dans `scenarios/SPEC.md`) : nominal, interblocage, inversion de
priorité (avec et sans héritage), course, plus trois traces dégradées (magic,
CRC, troncature) et une trace à tampon circulaire écrasé.

`scenarios/<nom>.json` porte les **attentes issues d'une exécution réelle** des
deux implémentations (`tests/freeze_scenarios.sh` refuse d'écrire si le C et
Python divergent). Exemple :

```json
{"schema":"replique-scenario/1","name":"deadlock","n_events":20,
 "expected_verdicts":[{"code":"DEADLOCK_FOUND","chain":[101,102],"b":16,"ts":1700},
                      {"code":"CULPRIT","tid":102,"detail":"holds-resource-32-at-prio-60"}],
 "replay":{"deterministic":true,"hash":17104745365914896697}}
```

## 3. Ce qui n'est PAS démontré (et pourquoi)

| Affirmation | Statut | Condition de levée |
|---|---|---|
| Le format binaire `.kev` de QNX 8.0 est documenté | **Non vérifié** (essai E1) | lecture du SDP 8.0 ; en attendant, le cœur consomme le conteneur `RKV1` documenté dans `CONTRACT.md` |
| `tracelogger` se fige proprement, tampon circulaire compris | **Non vérifié** (E2) | SDP + cible ; `target/declic/declic_qnx.c` est écrit mais **jamais compilé** ici (qcc absent) |
| La terminaison d'un processus est observable en temps réel | **Non vérifié** (E3) | idem ; deux déclencheurs (HAM, crochet `dumper`) sont fournis, à valider sur cible |
| Le core dump `dumper` contient les états de threads | **Non vérifié** (E5) | SDP ; la corrélation `CORRELATION_OK` n'est donc pas encore émise |
| Le simulateur est fidèle à l'ordonnanceur QNX réel | **Partiellement** (E4) | P2 est vérifié sur les fixtures, mais le modèle reste un sous-ensemble (voir `docs/SCHED-MODEL.md`) |
| `SCHED_SPORADIC` et partitions adaptatives | **Non modélisés** | le rejeu signale `sporadic-not-modelled` (jamais un rejeu faux) |
| Différentiel contre `traceprinter` | **Impossible ici** | `traceprinter` absent (licence SDP) ; `tests/differential_traceprinter.sh` sort 0 avec un avertissement et jouera dès qu'un SDP est présent |

### Ce qui a été vérifié sur une vraie cible QNX 8.0 (serveur `r630`, 12/11/2026)

Le SDP 8.0 est installé sur `r630` (`~/qnx800`). Ce qui y a été établi, source
QNX à l'appui :

| Fait | Conséquence pour le projet |
|---|---|
| `qcc -V gcc_ntox86_64` compile **Déclic** et les trois générateurs de bogues en `-Werror` | la partie cible n'est pas de la fiction : elle compile contre le vrai SDK |
| **le HAM n'existe plus** dans le SDP 8.0 (ni en-tête `<ham/ham.h>`, ni binaire) | le déclencheur de Déclic est la scrutation de `/proc/<pid>`, plus le signal récupéré par `pidin dead` |
| le service de noms a disparu aussi (pas de `<name.h>`) | pas de crochet « message de terminaison » : d'où `target/declic/declic_watch.c` |
| `tracelogger`, `traceprinter`, `dumper`, `pidin`, `slog2info` sont livrés | les quatre sources de l'archive RDA1 existent sur la cible |
| la chaîne `mkifs` → `mkxfs -t qnx6fsimg` → `diskimage` produit un disque amorçable, et QEMU l'amorce jusqu'à « QNX v1.2d Boot Loader: qnx_sdp.ifs » | l'image de `image/` est constructible ; il reste la main tendue du chargeur à l'IFS (voir `image/README.md`) |
| sans `io-blk.so` et `fs-qnx6.so` dans l'IFS, procnto ne monte rien et le boot s'arrête | ces deux modules sont dans `image/buildfile` |

## 4. Comment reproduire

```sh
make test                                   # toute la chaîne, sans QNX
make differential                            # 40 comparaisons C↔Python
make determinism                             # P1 : 5 rejeux identiques
build/analyze fixtures/deadlock.rkv1        # le coupable en trois lignes
build/replay-dump fixtures/race.rkv1 --whatif 402=20   # le « et si »
tests/freeze_scenarios.sh                   # regèle les attentes depuis l'exécution réelle
```

Le différentiel contre l'oracle du SDP s'active seul quand `traceprinter` est
dans le `PATH` :

```sh
tests/differential_traceprinter.sh          # vert (avertissement) sans SDP, comparatif avec
```

## 5. Outillage GitHub

| Élément | État |
|---|---|
| Dépôt | https://github.com/Tradoooo/replique |
| Runner | `r630-replique-01` (self-hosted, R630, label `replique-worker`) |
| CI | `.github/workflows/ci.yml` — jobs `ci` et `gardes`, ≤ 5 min, `fetch-depth: 1`, `concurrency` |
| Hermes | gate de fraîcheur (`.github/hermes/gate.py`) + résolveur de conflit **InferHub** (`.github/hermes/resolve_conflict.py`) |
| Secrets | `INFERHUB_API_KEY` (voie IA du résolveur) |
| Merge | Mergify seul pilote (`HERMES_LITE_DRY_RUN=1` : le gate évalue, il ne merge jamais) |
| Protection | `main` protégé : PR obligatoire, historique linéaire, checks `ci` + `gardes` + `ci-budget` exigés |
