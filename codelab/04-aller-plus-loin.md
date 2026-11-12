# Chapitre 4 — Aller plus loin : contribuer, étendre, limites

## Objectif

Donner au lecteur les moyens de passer d'utilisateur à contributeur : les règles du dépôt, les extensions naturelles du modèle, et les limites assumées à connaître avant de s'appuyer sur l'outil.

## Contribuer

### La règle d'or : le contrat d'abord

`CONTRACT.md` est **gelé** (conteneur RKV1, événements, verdicts, sorties canoniques, modèle d'ordonnanceur, déterminisme). Changer le contrat = une PR explicite, motivée, qui met à jour **dans la même PR** :

1. `CONTRACT.md` ;
2. les **deux jumeaux** (C dans `core/`, Python dans `host/replique/`) ;
3. les fixtures et attentes concernées (`scenarios/SPEC.md`, `scenarios/<nom>.json`, via `tests/freeze_scenarios.sh`).

Un `hash` gelé qui ne correspond pas aux deux jumeaux = test rouge, **jamais de mise à jour silencieuse** (SPEC.md §6).

### Les gardes automatiques

Avant de proposer quoi que ce soit, `make test` doit rester vert **sans QNX, sans cible, sans licence** (S8). Il enchaîne : `guard` (taille : fichiers < 500 lignes, fonctions < 50 lignes ; pureté : `core/` sans horloge, sans fichier, sans socket, sans en-tête QNX ; budget CI ≤ 5 min), `build`, `fixtures`, `unit`, `differential` (C↔Python : mêmes octets), `determinism` (5 rejeux, 5 hachages identiques).

### Où toucher selon l'objectif

| Objectif | Fichiers |
|---|---|
| Nouvel événement / nouveau verdict | `CONTRACT.md` §2–3, `core/include/replique/event.h` + `types.h`, `core/src/event.c`, `host/replique/model.py` + `event.py`, fixtures, tests |
| Nouvelle règle de diagnostic | `core/src/deadlock.c` + `host/replique/deadlock.py`, attentes dans `scenarios/`, `docs/DESIGN.md` |
| Nouveau scénario de référence | `scenarios/SPEC.md` (gel), `host/replique/kevgen.py`, `scenarios/<nom>.json` via `tests/freeze_scenarios.sh` |
| Nouvelle sortie hôte (JSON/SVG/Markdown) | `host/replique/report.py`, `view.py`, `markdown_report.py` — les sorties canoniques `ev|`/`vd|`/`dec|` ne changent jamais sans contrat |
| Déclic (cible) | `target/declic/*_portable.c` (testé sur PC via `core/tests/declic_test.c`), `*_qnx.c` (SDP seul), `docs/DESIGN.md` §7 |

## Étendre le modèle d'ordonnanceur

Pistes ouvertes par le cahier des charges (niveau X), dans l'ordre de difficulté croissant :

1. **`SCHED_SPORADIC` (X5)** — aujourd'hui réservé non implémenté : une politique `SPORADIC` dans une trace produit `REPLIQUE_TRACE_INCOMPLETE` avec `detail=sporadic-not-modelled`, jamais un rejeu faux. L'implémenter = budget de capacité + règle de replenishment dans `sched.c`/`sched.py`, en gardant le déterminisme (P1) et les deux jumeaux synchrones.
2. **Partitions adaptatives (X5)** — même exigence : le modèle v1 est mono-partition implicite ; l'extension touche l'élection (`replique_sched_pick` / `Scheduler.pick`).
3. **Multi-CPU** — `replique_sched_config.n_cpus` existe mais la v1 n'a de sens qu'à `n_cpus = 1` (CONTRACT.md §5) ; l'extension redéfinit l'invariant « un seul RUNNING » et la forme des décisions.
4. **Rejeu pas-à-pas interactif (X6)** — la commande `replay --step` existe (avancer d'événement en événement) ; l'enrichir (inspection d'état, points d'arrêt) se fait dans `host/replique/replay.py` (`step_through`) sans toucher au cœur.

Règle pour toute extension : la propriété **P1 (déterminisme) n'est jamais négociable** — pas d'horloge, pas d'aléa, pas d'allocation non bornée côté C ; et chaque mécanisme ajouté doit être couvert par le différentiel C↔Python.

## Limites assumées (à lire avant de s'appuyer sur l'outil)

| Limite | Détail | Où c'est écrit |
|---|---|---|
| Le rejeu porte sur l'**ordonnancement**, pas sur l'exécution des instructions | Réplique reproduit un interblocage/une inversion, ce n'est pas un émulateur ni `rr` : pas d'état mémoire rejoué | décision D5, cahier §3.2 |
| Fidélité au vrai noyau **non vérifiée** (E4) | le modèle est documenté (priorités fixes, FIFO/RR, héritage prise maximale, message passing bloquant) mais l'écart avec QNX 8.0 réel n'a pas encore été mesuré ; les divergences sont nommées par `REPLAY_DIVERGED`, jamais masquées | `docs/SCHED-MODEL.md` §7, cahier §13 |
| Format `.kev` brut **non vérifié** (E1) | le cœur consomme le conteneur RKV1 ; l'adaptateur kev→rkv1 de Déclic reste à écrire et à valider contre l'oracle `traceprinter` | `docs/KEV-FORMAT.md` §2.4 |
| Figeage réel sur cible **non vérifié** (E2, E3) | la procédure du chapitre 1 suppose `tracelogger` figeable proprement et la terminaison observable en temps réel — ce sont des essais, pas des acquis | `codelab/01-figer.md`, cahier §10 |
| Corrélation core dump **non implémentée en v1** | le verdict `CORRELATION_OK` (C4) attend l'essai E5 (contenu réel du vidage `dumper`) | cahier §10, §13 |
| Mono-CPU, 256 threads, capacités fixes | `REPLIQUE_MAX_THREADS 256`, tampons bornés fournis par l'appelant (pas d'allocation dans le cœur) ; traces géantes = fenêtrage autour du crash (cahier §12) | `core/include/replique/sched.h`, `types.h` |

## Pour continuer

- [CONTRACT.md](../CONTRACT.md) — le contrat octet-pour-octet ;
- [docs/DESIGN.md](../docs/DESIGN.md) — architecture et responsabilités par module ;
- [docs/VALIDATION.md](../docs/VALIDATION.md) — ce qui prouve quoi (P1, P2, P3, différentiels, mutants, gardes) ;
- [docs/DECISIONS.md](../docs/DECISIONS.md) — pourquoi ces choix (D1–D11 + implémentation) ;
- [REPLIQUE-cahier-des-charges.md](../REPLIQUE-cahier-des-charges.md) — le projet complet : contexte, exigences S/C/X, planning, risques.

Un dernier mot : si vous capturez une vraie trace QNX et que Déclic + l'adaptateur kev→rkv1 la traitent chez vous, les essais E1–E3 vous intéressent — vos résultats (positifs ou négatifs) valent de l'or : consignez-les, c'est ainsi que le cahier des charges §13 passe de « non vérifié » à « vérifié ».
