# Chapitre 1 — Figer : le tampon circulaire qui s'efface, et Déclic

## Objectif

Comprendre pourquoi l'historique d'un crash QNX disparaît, et comment **Déclic** (démon cible) fige la trace noyau à l'instant de la terminaison et produit une archive corrélée : trace + `pidin` + `slog2` + core dump, horodatée, avec empreinte SHA-256 et manifeste (exigences S2, S3).

> **Statut honnête.** Ce chapitre décrit la procédure cible (SDP QNX 8.0 + QEMU). Trois affirmations qu'elle suppose sont **non tranchées** (cahier des charges §10, §13) : le format binaire `.kev` (E1), le figeage propre de `tracelogger` avec historique avant-crash (E2), l'observabilité temps réel de la terminaison (E3). Le code cible (`target/declic/`) est en cours d'assemblage ; la **logique portable** de Déclic, elle, est testable sur PC dès maintenant (étape 1). Rien dans ce chapitre ne doit être présenté comme acquis avant le résultat des essais.

## Prérequis

- **Sur PC (toujours)** : le dépôt, `gcc`, `make`, `python3` ≥ 3.10.
- **Pour la partie cible** : SDP QNX 8.0 installé (`qcc` dans le `PATH`), QEMU, une licence QNX (gratuite en usage non commercial, QNX Everywhere). Sans cela, lisez ce chapitre — les chapitres [2](02-expliquer.md) et [3](03-rejouer.md) se font entièrement sur PC.

## Étape 1 — La logique portable de Déclic, sur PC (sans QNX)

Déclic est découpé en deux : la logique **portable** (décision de déclenchement, construction de l'archive, manifeste, empreinte) dans `target/declic/*_portable.c`, et la **colle QNX** (surveillance de terminaison, figeage, collecte) dans `target/declic/*_qnx.c` — voir `docs/DESIGN.md` §7 et la décision I3 de `docs/DECISIONS.md`.

1. Construisez et exécutez le test de la logique portable :

   ```sh
   make build
   ./build/declic-test
   ```

2. **Vous devez observer** : le test passe (code de retour 0) — il est inclus dans `make unit`, donc dans `make test`.

## Étape 2 — Le problème : le tampon circulaire

1. Sur la cible (ou l'invité QEMU), `tracelogger` capture les événements noyau dans un **tampon circulaire** : quand il est plein, les événements les plus anciens sont écrasés.
2. Au crash d'un processus, le système continue de tourner — et la trace qui explique le bogue (l'historique *avant* le crash) est **en train d'être écrasée** par les événements postérieurs. C'est le trou R1 (`docs/GAP.md`).
3. **Vous devez observer** ce phénomène vous-même : démarrez `tracelogger`, générez de l'activité (les générateurs de bogues des scénarios SPEC existent pour ça), attendez, puis arrêtez la capture et constatez que le début de la fenêtre a disparu. *Le comportement exact de figeage à l'arrêt est l'objet de l'essai E2 — non tranché à ce jour.*

## Étape 3 — Construire Déclic avec le SDP

Depuis la racine du dépôt, sur une machine avec le SDP :

```sh
make target-qnx
```

Cette cible exécute `make -C target/declic -f Makefile.qnx` avec `qcc`. Sans `qcc` dans le `PATH`, elle affiche un message explicatif et ne fait rien — `make test`, lui, n'exige jamais le SDP (S8).

- **Vous devez observer** : la compilation des fichiers `*_qnx.c` (colle QNX) et la production du binaire Déclic.
- L'image invitée QEMU (Déclic + générateurs de bogues) est construite par le buildfile `mkifs` dans `image/` (voir `docs/DESIGN.md` §7).

## Étape 4 — Déclencher Déclic sur l'invité QEMU

1. Démarrez l'invité QNX avec l'image construite à l'étape 3.
2. Vérifiez que Déclic tourne (processus distinct — le microkernel isole les processus : le crash de la victime n'empêche pas Déclic de figer la trace).
3. Provoquez la terminaison d'un processus cible (l'exigence S7 du cahier : « tuer un processus sur l'invité QNX »).
4. **Vous devez observer** : Déclic détecte la terminaison, fige le tampon de trace et produit **une archive horodatée unique et reproductible** contenant trace, `pidin`, `slog2`, core dump (`dumper`), manifeste et empreinte SHA-256.
5. Le déclencheur exact (HAM, événement noyau `PROCESS_DESTROY`, crochet `dumper`) sera **choisi après l'essai E3** : la colle QNX est conçue pour plusieurs déclencheurs, le plus précoce gagne (cahier §12).

## Étape 5 — Ramener l'archive sur PC

1. Copiez l'archive depuis l'invité vers l'hôte.
2. La trace qu'elle contient est destinée au cœur portable au format **RKV1** (`CONTRACT.md` §1, `docs/KEV-FORMAT.md`) : l'adaptateur `kev → rkv1` dans Déclic convertit la trace QNX — **non écrit à ce jour, conditionné à E1**.
3. Une fois convertie, l'analyse se poursuit sans QNX : chapitre [2 — expliquer](02-expliquer.md).

## Ce qui peut mal tourner

| Symptôme | Cause probable | Remède |
|---|---|---|
| `make target-qnx` ne compile rien | `qcc` absent du `PATH` | installer le SDP QNX 8.0 ; sur PC sans SDP, seule l'étape 1 fonctionne (c'est voulu, S8) |
| `./build/declic-test` introuvable | `make build` non exécuté ou échoué | relancer `make build` et lire la première erreur de compilation |
| Archive produite sans historique avant-crash | tampon écrasé avant figeage — exactement le risque E2 | sur-dimensionner le tampon, déclencher sur l'événement le plus précoce (E3) ; consigner le résultat dans l'essai |
| Terminaison non détectée par Déclic | déclencheur choisi non observable en temps réel — risque E3 | essayer les autres déclencheurs (HAM / événement noyau / crochet `dumper`) |

**Avant de graver un résultat** : les essais E1–E3 alimentent la table de statut du cahier des charges §13 — toute correction d'affirmation y est consignée, jamais de mise à jour silencieuse.
