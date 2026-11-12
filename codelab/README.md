# Codelab — « Figer, expliquer et rejouer un crash QNX »

Quatre chapitres pour comprendre un bogue de concurrence QNX de bout en bout (exigence C8) : de la trace noyau qui s'efface au rejeu déterministe sur votre PC, en passant par le graphe d'attente et le coupable désigné.

## Les chapitres

| # | Chapitre | Ce qu'on fait | QNX requis ? |
|---|---|---|---|
| 1 | [01-figer.md](01-figer.md) | Comprendre le tampon circulaire qui s'efface ; Déclic fige la trace au crash et archive tout | oui (SDP + QEMU) — **conditionné aux essais E2/E3, non tranchés** |
| 2 | [02-expliquer.md](02-expliquer.md) | Ouvrir la trace, lire les événements, obtenir le graphe d'attente, la chaîne de blocage et le coupable | non |
| 3 | [03-rejouer.md](03-rejouer.md) | Rejeu déterministe (hachage stable sur 5 rejeux) et mode « et si » | non |
| 4 | [04-aller-plus-loin.md](04-aller-plus-loin.md) | Contribuer, étendre le modèle, limites assumées | non |

Les chapitres 2 et 3 tournent **entièrement sur PC**, sans QNX, sans cible et sans licence (exigence S8) : ils utilisent les fixtures engendrées par le dépôt. Le chapitre 1 décrit la procédure cible ; il est lisible sans matériel, mais son exécution exige le SDP QNX 8.0 et QEMU.

## Prérequis (chapitres 2–4)

- Linux (ou macOS) avec `gcc` (C11), `make`, `python3` ≥ 3.10 — **stdlib seule, aucune dépendance tierce** ;
- le dépôt cloné, à sa racine : toutes les commandes supposent `PWD` = racine du dépôt ;
- environ 5 minutes : `make test` construit le cœur C, engendre les fixtures et vérifie le différentiel C↔Python.

```sh
make test
```

Si `make test` passe, vous avez tout ce qu'il faut pour les chapitres 2 à 4.

## Conventions utilisées dans les chapitres

- Les commandes sont données **exactement** comme dans le `Makefile` et `CONTRACT.md` §4.7 ; la CLI Python s'invoque avec `PYTHONPATH=host python3 -m replique.cli …` depuis la racine du dépôt ;
- les sorties montrées sont au **format canonique gelé** (lignes `ev|`, `vd|`, `dec|` — CONTRACT.md §4). Là où une valeur dépend de l'exécution (hachage, horodatage exact des fixtures), l'exemple le signale explicitement ;
- les priorités QNX sont toujours dans l'ordre gelé : **0 = plus haute**, 255 = plus basse ;
- chaque chapitre se termine par « ce qui peut mal tourner » — consultez-le avant de suspecter un bogue.

## Honnêteté

Ce codelab distingue toujours ce qui est **démontrable aujourd'hui sur PC** (parseur RKV1, graphe d'attente, rejeu déterministe, différentiel C↔Python — chapitres 2–3) de ce qui est **conditionné aux essais E1–E5** du cahier des charges (figeage réel sur cible, format `.kev` brut, fidélité au vrai noyau — chapitre 1, et §3 de `docs/SCHED-MODEL.md`). Rien n'y est présenté comme acquis qui ne le soit : les affirmations non vérifiées sont marquées comme telles (cahier §13).

Bonne exploration — et souvenez-vous : un interblocage « une fois sur mille » n'a plus besoin de chance, seulement d'une archive.
