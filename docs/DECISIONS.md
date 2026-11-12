# docs/DECISIONS.md — décisions de conception et d'implémentation

Deux parties : les décisions **D1–D11** du cahier des charges (§15), au format décision / justification / conséquence ; puis les **décisions d'implémentation** visibles dans le code. Toute décision nouvelle s'ajoute ici avec sa date — jamais de changement silencieux.

## Décisions du cahier des charges (D1–D11)

### D1 — Fusionner Déclic + Autopsie + Réplique en UN produit cohérent

- **Justification.** Les trois traitent la même donnée (la trace d'un crash) ; séparés, ils se concurrencent et dupliquent le parseur.
- **Conséquence.** Une seule chaîne figer → expliquer → rejouer ; un dépôt unique, un contrat unique (`CONTRACT.md`), un parseur unique partagé par les trois composants.

### D2 — Cœur C11 pur, séparé de Déclic (cible)

- **Justification.** Le cœur est démontrable sans QNX ni cible ; Déclic n'est que la capture sur cible.
- **Conséquence.** `core/` sans horloge, sans fichier, sans socket, sans en-tête QNX (garde `purity_guard.py`) ; `make test` vert sans QNX, sans cible, sans licence (S8) ; le sous-ensemble S = Déclic + parseur.

### D3 — Parseur validé par différentiel contre `traceprinter`

- **Justification.** Ne pas dépendre d'un format binaire sous-documenté ; l'oracle du SDP garantit l'exactitude du décodage (méthode Tamis contre `secpolcompile`).
- **Conséquence.** `make oracle` / `tests/differential_traceprinter.sh` ; fixtures accompagnables d'un oracle `<nom>.tp.txt` ; sans SDP, le différentiel tourne sur fixtures pré-enregistrées (voir `docs/KEV-FORMAT.md`).

### D4 — Jumeau Python indépendant

- **Justification.** Vérification croisée du cœur C, comme sur tout le portefeuille.
- **Conséquence.** `host/replique/` réimplémente le contrat de façon indépendante ; `tests/differential_c_python.sh` exige les mêmes octets des deux côtés ; toute évolution du contrat touche les deux jumeaux dans la même PR.

### D5 — Le rejeu porte sur l'ordonnancement, pas sur l'exécution des instructions

- **Justification.** Reproduire l'interblocage/l'inversion de priorité (le problème de concurrence) sans être un émulateur complet ; périmètre réaliste et utile.
- **Conséquence.** Le simulateur (`core/src/sched.c`, `replay.c`) produit une séquence de décisions d'ordonnancement, pas d'état mémoire ; hors périmètre assumé : reconstruction du code source (cahier §3.2).

### D6 — Déterminisme du rejeu prouvé par hachage (P1)

- **Justification.** C'est la propriété qui fait de Réplique une machine à remonter le temps fiable ; vérifiable automatiquement en CI.
- **Conséquence.** Hachage FNV-1a 64 gelé sur les lignes `dec|` (CONTRACT.md §6) ; `make determinism` rejoue 5 fois et exige 5 hachages identiques.

### D7 — Le sous-ensemble 15/12 = Déclic (figer + archiver)

- **Justification.** Faisable en 4 semaines, démo forte (rien n'est perdu au crash), et c'est la brique dont Autopsie et Réplique dépendent.
- **Conséquence.** Priorité aux essais E2/E3 et au lot L5 ; la soumission concours tient debout même si les niveaux C et X glissent.

### D8 — Complément de Tamis, pas concurrent

- **Justification.** Tamis = analyse statique à froid (avant déploiement) ; Réplique = post-mortem (après crash). Le portefeuille couvre les deux bouts.
- **Conséquence.** Les deux projets peuvent candidater dans la même catégorie sans se cannibaliser ; la soumission le dit explicitement.

### D9 — Déclic réutilisable par Veille/Ambre/Relais

- **Justification.** Le « figeur de trace » est un besoin commun du portefeuille pour la capture de preuve empirique.
- **Conséquence.** L'archive Déclic (manifeste + empreinte SHA-256) est conçue comme un format d'échange stable, pas comme un format privé.

### D10 — Licence MIT

- **Justification.** Cohérent avec le portefeuille.
- **Conséquence.** `LICENSE` MIT, © 2026 Baptiste Faisy ; aucune dépendance tierce (Python stdlib seule, C11 sans bibliothèque externe) pour rester compatible.

### D11 — Codelab inclus

- **Justification.** Bonus Best Codelab/Tutorial ; pédagogie « comprendre un crash QNX » forte.
- **Conséquence.** `codelab/` en 4 chapitres (exigence C8) ; les chapitres 2 et 3 tournent sans QNX — le codelab est faisable par tout lecteur sur PC.

## Décisions d'implémentation

### I1 — Conteneur `RKV1` plutôt que `.kev` brut, tant que E1 n'est pas tranché

- **Contexte.** L'essai E1 (format binaire exact du `.kev` QNX 8.0) est **non tranché** : le SDP n'est pas installé sur la machine de développement. Écrire un parseur contre un format non vérifié produirait du code faux non testable.
- **Décision.** Le cœur portable consomme un conteneur **documenté, auto-descriptif et vérifiable**, `RKV1` (CONTRACT.md §1) : en-tête 32 octets (magic, version, drapeaux, `n_events`, `time_unit`, CRC-32), enregistrements 32 octets alignés, lecture champ par champ (jamais de cast de structure), erreurs gelées (§1.4).
- **Conséquence.** Fixtures engendrées sur PC (`host/replique/kevgen.py`) → toute la chaîne est démontrable sans QNX dès aujourd'hui. L'adaptateur `kev → rkv1` s'insérera dans Déclic (`target/declic/`) quand E1 sera tranché : le cœur, les tests et le contrat ne changeront pas, seul un convertisseur amont s'ajoutera. Détail : `docs/KEV-FORMAT.md`.

### I2 — Pilotes de différentiel dans `core/tests`

- **Contexte.** Le différentiel C↔Python doit comparer des **octets** produits par le cœur C, sans framework de test.
- **Décision.** Trois pilotes C minimaux vivent dans `core/tests/` et sont compilés par le Makefile : `dump_events.c` → `build/kev-dump` (lignes `ev|`), `dump_pipeline.c` → `build/analyze` (lignes `vd|`), `dump_replay.c` → `build/replay-dump` (lignes `dec|`). Ils partagent les codes de retour des commandes Python (0 conforme, 2 fichier illisible, 3 trace corrompue).
- **Conséquence.** `tests/differential_c_python.sh` compare directement la stdout des pilotes C et des commandes Python équivalentes — même surface de commande des deux côtés (CONTRACT.md §4.7). Les pilotes sont hors de la bibliothèque `libreplique.a` : la pureté du cœur reste intacte (ce sont eux qui font les I/O).

### I3 — Logique portable de Déclic séparée de la colle QNX (`*_portable.c` / `*_qnx.c`)

- **Contexte.** Déclic est un démon QNX (HAM, `dumper`, `tracelogger`, `slog2`), mais sa valeur — décision de déclenchement, construction de l'archive corrélée, manifeste, empreinte SHA-256 — est une logique pure.
- **Décision.** Dans `target/declic/`, la logique portable vit dans `*_portable.c` (compilée par le Makefile racine avec `gcc` host), la colle QNX dans `*_qnx.c` (compilée uniquement par `make target-qnx` avec `qcc`, via `target/declic/Makefile.qnx`). Le test `core/tests/declic_test.c` → `build/declic-test` exerce la logique portable sur PC et fait partie de `make unit`.
- **Conséquence.** La partie la plus risquée de Déclic (E2/E3 non tranchés) est isolée dans des fichiers qui ne cassent jamais `make test` ; `declic_test.c` ne suit volontairement pas le motif `test_*.c` car il a besoin des objets de `target/`.

### I4 — Scénarios gelés par `scenarios/SPEC.md`, attentes figées par exécution réelle

- **Contexte.** Les tests de diagnostic (P3) et de déterminisme (P1) ont besoin de résultats attendus stables.
- **Décision.** Les quatre scénarios (`nominal`, `deadlock`, `prio-inversion` + variante `prio-inversion-heritage`, `race`) et les fixtures dégradées sont gelés dans `scenarios/SPEC.md` ; les valeurs numériques des attentes (`n_events`, `hash`) sont remplies depuis une exécution réelle et vérifiées identiques en C et en Python avant gel (`tests/freeze_scenarios.sh`), dans `scenarios/<nom>.json`.
- **Conséquence.** Un `hash` gelé qui ne correspond pas aux deux jumeaux = test rouge, jamais de mise à jour silencieuse ; toute modification de scénario = PR motivée touchant générateur, attentes et `CONTRACT.md` ensemble.
