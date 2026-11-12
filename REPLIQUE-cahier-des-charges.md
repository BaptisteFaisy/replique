# Réplique — machine à remonter le temps pour QNX 8.0

**Nom de travail : « Réplique »** (rejouer à l'identique ce qui s'est passé sur la cible, sur un PC, autant de fois que nécessaire).
Version 0.1 — 12 novembre 2026 — projet candidat au QNX Global Hackathon, catégorie **Developer Tooling & Host Tools**.
Classement interne : **3ᵉ du reclassement à 3 mois** (v4 du dossier `qnx-dossier`), fusion de trois idées à forte originalité : **Déclic** (o4), **Réplique** (o5), **Autopsie** (o5) — deux notes d'originalité 5 sur trois.
Calibre : **3 mois** (13 semaines), avec un **sous-ensemble livrable au 15/12/2026** (Déclic seul) pour le concours.
Visé explicitement : **Best of Developer Tooling**, bonus **Best use of QNX-specific Technologies** (kernel trace, message passing, microkernel, dumper) et **Best Codelab/Tutorial**.

> Note de nommage : « Réplique » est un nom commun français, aucune marque de tiers. Les noms des trois composants (Déclic, Autopsie, Réplique) sont génériques ; aucun nom de produit de debug commercial n'apparaît dans les supports de soumission.

---

## 0. Lecture rapide

**Ce que c'est.** Une **machine à remonter le temps** pour QNX 8.0, en trois composants :
1. **Déclic** (cible) — un démon qui **fige la trace noyau au moment exact d'un crash** et archive tout, corrélé et horodaté : tampon de trace circulaire (l'historique *avant* le crash), `pidin`, `slog2`, vidage d'incident (`dumper`).
2. **Autopsie** (hôte) — ouvre l'archive, **reconstitue la chaîne de blocage** (qui attend qui : mutex, `MsgSend` bloquant, sémaphore), croise trace + core dump + `pidin`, et **désigne le coupable** en une page.
3. **Réplique** (hôte) — un **simulateur d'ordonnanceur** qui **rejoue la trace de façon déterministe sur PC** : reproduire un bogue de concurrence **sans la cible**, et tester des « et si » (changer une priorité, un délai, un ordre de message).

**La promesse, en une phrase.** Un bogue de concurrence qui n'arrive « qu'une fois sur mille sur la cible » devient un artefact **figé, expliqué, et rejouable à volonté sur un PC** — au lieu d'être perdu dans un tampon circulaire écrasé.

**Trois idées qui portent le projet.**

1. **Le tampon de trace est une preuve qui s'efface.** `tracelogger` écrit dans un tampon **circulaire** : au crash, l'historique qui explique le bogue est en train d'être écrasé par les événements postérieurs. **Déclic** fige ce tampon à l'instant de la terminaison et l'archive avant qu'il ne soit perdu. C'est la brique de base — et le sous-ensemble livrable au 15/12.
2. **Décoder ne suffit pas ; il faut expliquer et rejouer.** `traceprinter` décode la trace brute en texte lisible, mais n'établit **aucune causalité** et ne désigne **aucun coupable**. **Autopsie** reconstruit le graphe d'attente et la chaîne de blocage ; **Réplique** va plus loin : un **modèle exécutable de l'ordonnanceur QNX** qui rejoue la trace de façon déterministe. Rien d'équivalent n'existe dans l'écosystème QNX.
3. **Le cœur est portable et démontrable sans cible.** Le parseur de trace, le modèle d'ordonnanceur et le moteur de rejeu sont en **C11 pur** (ni horloge, ni fichier, ni QNX) : ils se testent sur PC à partir de traces `.kev` enregistrées une fois sur QEMU. On capture des traces de référence (interblocage, inversion de priorité, course), puis on les rejoue et on les analyse indéfiniment **sans QNX, sans cible, sans licence** — la méthode « cœur portable + jumeau » déjà éprouvée sur Ambre, Passerelle, Tamis et Veille.

**Trois niveaux d'ambition.** **S** (socle : Déclic + parseur de trace, livrable 15/12), **C** (complet : Autopsie — chaîne de blocage + corrélation + coupable), **X** (extrême : Réplique — simulateur d'ordonnanceur à rejeu déterministe + « et si »).

**À valider avant d'écrire la moindre ligne de produit** (section 10) : cinq essais, dont E1 — le format de trace noyau est-il suffisamment documenté pour écrire un parseur portable ? — et E3 — la terminaison d'un processus est-elle observable en temps réel pour déclencher Déclic ?

**Statut des affirmations.** Section 13 : le format binaire exact de la trace et le contenu du vidage d'incident doivent être relus page à page dans le SDP et vérifiés contre `traceprinter` (oracle) avant d'être gelés. Ce sont les premiers travaux, pas des hypothèses de plus.

---

## 1. Contexte et cas d'usage

### 1.1 Qui a ce problème

| Acteur | Besoin | Ce qu'il fait aujourd'hui |
|---|---|---|
| Développeur QNX confronté à un crash intermittent | Comprendre un plantage qui ne se reproduit pas à la demande | Relire des traces brutes `traceprinter` de plusieurs Mo, à la main, sans causalité |
| Intégrateur système certifié (ISO 26262 / IEC 62304 / EN 50128) | Prouver la cause racine d'une défaillance pour le dossier d'homologation | Argumentaire prose, captures d'écran, relecture manuelle de traces |
| Ingénieur sûreté de fonctionnement | Reconstituer la chaîne de blocage d'un interblocage | `pidin` figé + intuition ; pas de graphe d'attente automatique |
| Équipe CI/qualité | Capturer automatiquement le contexte complet d'un crash en test | Rien : le tampon circulaire est écrasé avant qu'on regarde |
| Formateur / étudiant QNX | Montrer le message passing et l'ordonnancement en action | Trace brute illisible ; pas de rejeu pas-à-pas |

### 1.2 Pourquoi c'est un vrai marché

QNX équipe des systèmes où un crash rare mais réel a des conséquences graves (automobile, médical, ferroviaire, industriel). Comprendre la **cause racine** d'un bogue de concurrence est aujourd'hui un travail d'orfèvre sur des traces brutes, sans outil de causalité ni de rejeu. Les écosystèmes Linux ont des outils de post-mortem et de rejeu (perf, rr pour le rejeu déterministe) ; **QNX n'a rien d'équivalent**. Réplique comble ce vide : figer (Déclic), expliquer (Autopsie), rejouer (Réplique). C'est aussi un **complément direct de Tamis** : Tamis analyse la politique de sécurité *à froid, avant déploiement* ; Réplique fait le post-mortem *après le crash*.

### 1.3 Pourquoi maintenant

- QNX 8.0 est gratuit en usage non commercial (QNX Everywhere, licence en ~1 h).
- Le kernel trace (`tracelogger`/`traceprinter`), `dumper`, `pidin`, `slog2` et le HAM existent et sont documentés : les **sources de données** sont là ; ce qui manque, c'est la couche **causalité + rejeu**.
- QEMU permet de capturer des traces de référence reproductibles (interblocage, inversion de priorité) sans matériel.
- Les projets frères (Veille, Ambre) capturent déjà des traces comme preuve empirique : Réplique fournit l'outil qui les **exploite** à fond, et Déclic peut devenir leur « figeur de trace » commun.

### 1.4 Ce que Réplique n'est pas

Ni un profileur de performance (cf. « Flamme »/« Bilan », lot B), ni un vérificateur d'échéances (cf. « Échéancier », lot B), ni un détecteur d'inversion de priorité en ligne (cf. « Renverse »), ni un analyseur statique de politique (Tamis), ni un débogueur source pas-à-pas (`gdb`/`nto-gdb` existent). Réplique est l'outil **post-mortem corrélé + rejeu déterministe** qui manque.

---

## 2. Ancrage documentaire

### 2.1 Ce que QNX fournit déjà (à ne pas réinventer)

| Élément | Rôle pour Réplique |
|---|---|
| **`tracelogger`** | Capture les événements noyau dans un tampon circulaire (fichier `.kev`) — la source de la trace |
| **`traceprinter`** | Décode la trace `.kev` en texte lisible — l'**oracle** du parseur (différentiel, comme `secpolcompile` pour Tamis) |
| **Kernel trace** | Événements : états de threads, `MsgSend`/`MsgReceive`/`MsgReply`, interruptions, timers, décisions d'ordonnancement, création/destruction process/thread |
| **`dumper`** | Vidage d'incident (core dump) à la terminaison d'un processus — l'état mémoire et registre au crash |
| **`pidin`** | Instantané de l'état des processus/threads (priorités, états, blocages) |
| **`slog2`** | Journal système structuré — le contexte applicatif autour du crash |
| **HAM** (High Availability Manager) | Surveille les processus, détecte les terminaisons — un déclencheur possible pour Déclic |
| **System Profiler** (Momentics) | Visualise la trace (frise, threads) — mais **mesure et visualise, n'explique pas la causalité ni ne rejoue** |
| **Ordonnanceur QNX** | Priorités fixes, héritage de priorité, `SCHED_SPORADIC`, partitions adaptatives — le modèle à reproduire dans le simulateur |

### 2.2 Les trous exploités (lot B, Gap 4 « post-mortem/rejeu » — à sourcer page à page, section 13)

| Code | Trou, tel que formulé | Source |
|---|---|---|
| **R1** | La trace noyau est un **tampon circulaire écrasé** : au crash, l'historique *avant* le crash disparaît si on ne le fige pas immédiatement. Aucun outil ne fige la trace à la terminaison et n'archive trace + `pidin` + `slog2` + core dump de façon **corrélée et horodatée**. | Lot B (Déclic), doc kernel trace |
| **R2** | `traceprinter` **décode mais n'explique pas** : aucune causalité, aucun graphe d'attente, aucun coupable désigné pour un interblocage. | Lot B (Autopsie) |
| **R3** | **Aucun rejeu déterministe** d'une trace d'ordonnanceur : reproduire un bogue de concurrence exige la **cible physique** et de la chance. | Lot B (Réplique) |
| **R4** | Les bogues de concurrence (interblocage, inversion de priorité, course) sont **non déterministes** et non reproductibles à la demande. | Conséquence de R3 |
| **R5** | Pas de **causalité des messages** : qui a envoyé à qui, avec quelle latence, dans quel ordre — à reconstruire à la main depuis la trace brute. | Lot B (Filature) |

> Honnêteté : R1–R5 sont des affirmations d'**absence** d'outillage dans la documentation publique QNX 8.0. Comme pour Tamis et Veille, elles doivent être revérifiées page à page dans le SDP installé avant d'être gravées dans la soumission. Section 13.

---

## 3. Périmètre

### 3.1 Dans le périmètre

- **Déclic** (`target/`, QNX) : démon qui surveille la terminaison de processus (HAM et/ou événement noyau et/ou crochet `dumper`), fige le tampon de trace, capture `pidin` + `slog2` + core dump, produit une **archive horodatée unique et reproductible** (empreinte SHA-256).
- **Parseur de trace** (`core/`, C11 pur) : lit le format `.kev`, produit une séquence d'événements typés et horodatés ; validé **par différentiel contre `traceprinter`** (oracle), comme Tamis contre `secpolcompile`.
- **Autopsie** (`core/` + `host/`) : reconstruit l'état des threads à l'instant du crash, bâtit le **graphe d'attente** (mutex, `MsgSend` bloquant, sémaphores), détecte les **chaînes de blocage**, désigne le **coupable** ; corrèle avec le core dump et `pidin`.
- **Réplique** (`core/`) : **simulateur d'ordonnanceur** — modèle des threads, priorités, états, héritage de priorité, message passing ; **rejeu déterministe** de la trace ; mode « et si » (modifier une priorité, un délai, un ordre).
- **Jumeau Python** (`host/`) : modèle indépendant + différentiel C↔Python.
- **Codelab** : « figer, expliquer et rejouer un crash QNX ».

### 3.2 Hors périmètre (assumé)

Profilage de performance / flame graph (Flamme) ; vérification d'échéances déclarées (Échéancier) ; détection d'inversion de priorité en ligne (Renverse) ; debug source pas-à-pas (`gdb`) ; analyse statique de politique (Tamis) ; reconstruction du code source (le rejeu porte sur l'**ordonnancement**, pas sur l'exécution des instructions).

---

## 4. Architecture

```
replique/
├── CONTRACT.md                  # contrat gelé : format .kev accepté, événements, verdicts, formats
├── REPLIQUE-cahier-des-charges.md
├── README.md
├── Makefile
├── LICENSE                      # MIT
├── core/                        # C11 PUR : ni horloge, ni fichier, ni socket, ni QNX
│   ├── include/replique/        #   kev.h event.h sched.h replay.h deadlock.h verdict.h report.h
│   ├── src/                     #   kev.c event.c sched.c replay.c deadlock.c verdict.c report.c
│   └── tests/                   #   test_kev.c test_sched.c test_replay.c test_deadlock.c
├── host/                        # Python stdlib : jumeau indépendant + CLI + visualiseur + rapports
│   ├── replique/                #   kev.py sched.py replay.py deadlock.py cli.py report.py view.py
│   └── tests/
├── target/                      # code QNX : Déclic (démon de figeage au crash)
│   ├── declic/                  #   surveillance terminaison, figeage trace, archive corrélée
│   └── hook/                    #   intégration dumper / HAM
├── fixtures/                    # traces .kev de référence (capturées sur QEMU) : nominales, interblocage, inversion, course
├── scenarios/                   # bogues de concurrence de référence + résultats attendus (coupable, chaîne)
├── tests/                       # différentiel C↔Python, différentiel contre traceprinter, rejeu déterministe
├── codelab/                     # « figer, expliquer et rejouer un crash QNX »
├── image/                       # buildfile mkifs (image invitée avec Déclic + générateurs de bogues)
└── docs/                        # DESIGN.md GAP.md DECISIONS.md VALIDATION.md KEV-FORMAT.md SCHED-MODEL.md
```

**Règle de portabilité.** `core/` ne lit aucun fichier et ne consulte aucune horloge : l'appelant fournit le tampon `.kev` (ou la séquence d'événements) et les tampons de sortie. Le même noyau s'exécute depuis un test unitaire sur PC, depuis le jumeau Python, et depuis l'outil hôte. La double implémentation (C dans `core/`, Python dans `host/`) est un **jumeau de vérification** indépendant.

**Règle d'oracle.** Le parseur de trace est validé **par différentiel contre `traceprinter`** (utilitaire hôte du SDP) : même trace en entrée, mêmes événements décodés en sortie. C'est exactement la méthode de Tamis contre `secpolcompile`. Sans SDP, le différentiel tourne sur des fixtures `.kev` pré-enregistrées.

**Règle de taille.** Tout fichier < 500 lignes, toute fonction < 50 lignes, vérifié en CI.

---

## 5. Contrat

### 5.1 Chaîne de traitement (figée)

```
archive Déclic (.kev + pidin + slog2 + core) → parser trace → événements typés → reconstruire états threads → graphe d'attente → chaîne de blocage / coupable → rejeu déterministe → verdicts + rapport
```

Aucune étape ne remonte. Toute trace illisible arrête la chaîne et produit un verdict `REPLIQUE_PARSE_ERROR` avec la position (octet/événement) et la cause — jamais de reprise approximative sur une trace partiellement comprise.

### 5.2 Modèle d'événements (ce que le parseur produit)

Chaque événement de trace devient un record typé : `{ timestamp, thread_id, process_id, class, subcode, données }`. Classes retenues pour le socle : changement d'état de thread (running/ready/blocked/…), message passing (`MsgSend`/`MsgReceive`/`MsgReply`/`MsgError`), timer/pulse, interruption, création/destruction process/thread, (niveau C) événements d'ordonnancement et d'héritage de priorité.

### 5.3 Verdicts (enum `replique_verdict_code`)

| # | Code | Sévérité | Porteur |
|---|---|---|---|
| 0 | `REPLIQUE_OK` | info | — |
| 1 | `REPLIQUE_PARSE_ERROR` | error | offset, cause |
| 2 | `REPLIQUE_DEADLOCK_FOUND` | error | cycle (liste de threads), ressource |
| 3 | `REPLIQUE_CULPRIT` | error | thread_id, raison |
| 4 | `REPLIQUE_PRIO_INVERSION` | warning | chaîne, héritage appliqué? |
| 5 | `REPLIQUE_REPLAY_DETERMINISTIC` | info | hachage de la séquence d'ordonnancement |
| 6 | `REPLIQUE_REPLAY_DIVERGED` | error | événement de divergence, attendu/trouvé |
| 7 | `REPLIQUE_TRACE_INCOMPLETE` | warning | tampon écrasé, événements manquants |
| 8 | `REPLIQUE_CORRELATION_OK` | info | trace ↔ core dump ↔ pidin cohérents |
| 9 | `REPLIQUE_WHATIF` | info | paramètre modifié, résultat du rejeu |

### 5.4 API C (en-têtes sous `core/include/replique/`)

```c
/* kev.h — parseur de trace (validé par différentiel contre traceprinter) */
int replique_kev_parse(const uint8_t *buf, size_t len,
                       replique_event *out, size_t cap, size_t *n_out);

/* deadlock.h — graphe d'attente et chaîne de blocage */
int replique_deadlock_detect(const replique_event *ev, size_t n,
                             replique_verdict *out, size_t cap, size_t *n_out);

/* replay.h — simulateur d'ordonnanceur, rejeu déterministe */
int replique_replay(const replique_event *ev, size_t n,
                    const replique_sched_config *cfg,   /* priorités, « et si » */
                    replique_replay_result *res);       /* séquence + hachage */
```

### 5.5 Formats de sortie

Identiques en structure à Veille/Tamis/Toile (texte une ligne par verdict, JSON compact à ordre de clés figé, Markdown d'audit horodaté et empreinté). Le contrat octet-pour-octet est précisé dans `CONTRACT.md` (étape S1).

---

## 6. Exigences (S / C / X)

### 6.1 Socle (S) — le sous-ensemble livrable au 15/12 : **Déclic**

| ID | Exigence | Niveau |
|---|---|---|
| S1 | `CONTRACT.md` gelé : format `.kev` accepté, événements, verdicts, formats | S |
| S2 | **Déclic** (cible QNX) : surveillance de terminaison (HAM et/ou événement noyau), figeage du tampon de trace | S |
| S3 | Archive corrélée horodatée : trace `.kev` + `pidin` + `slog2` + core dump (`dumper`), empreinte SHA-256, manifeste | S |
| S4 | Parseur de trace `.kev` en C11 pur, validé **par différentiel contre `traceprinter`** sur fixtures | S |
| S5 | Jumeau Python indépendant + différentiel C↔Python vert | S |
| S6 | Fixtures : traces de référence capturées sur QEMU (nominal + 3 bogues de concurrence) | S |
| S7 | Démo : tuer un processus sur l'invité QNX → archive complète produite automatiquement, lisible sur PC | S |
| S8 | `make test` vert **sans QNX, sans cible, sans licence** (parseur + jumeau + fixtures) | S |
| S9 | CI : build C `-Werror`, tests, différentiel, garde taille, budget ≤ 5 min | S |

### 6.2 Complet (C) — **Autopsie**

| ID | Exigence | Niveau |
|---|---|---|
| C1 | Reconstruction de l'état de tous les threads à l'instant du crash (depuis la trace) | C |
| C2 | Graphe d'attente : arêtes « thread T attend la ressource R tenue par thread U » (mutex, `MsgSend` bloquant, sémaphores) | C |
| C3 | Détection de **chaîne de blocage** (cycle dans le graphe) + désignation du **coupable** | C |
| C4 | Corrélation trace ↔ core dump (`dumper`) ↔ `pidin` : cohérence des états, verdict `CORRELATION_OK` | C |
| C5 | Détection d'**inversion de priorité** : chaîne + vérification que l'héritage a fonctionné | C |
| C6 | Rapport « interblocage expliqué en une page » : chaîne, coupable, horodatage, recommandation | C |
| C7 | Visualisation du graphe d'attente (sortie texte/HTML côté hôte) | C |
| C8 | Codelab 4 chapitres : « figer, expliquer et rejouer un crash QNX » | C |

### 6.3 Extrême (X) — **Réplique** (le simulateur, à 3 mois)

| ID | Exigence | Niveau |
|---|---|---|
| X1 | Modèle de l'ordonnanceur QNX : priorités fixes, états de thread, message passing bloquant, héritage de priorité | X |
| X2 | **Rejeu déterministe** : la même trace rejouée N fois donne exactement la même séquence d'ordonnancement (hachage) | X |
| X3 | **Fidélité** : le rejeu d'une trace QEMU reproduit les ordonnancements/latences observés (comparaison `traceprinter`/System Profiler) | X |
| X4 | Mode **« et si »** : modifier une priorité, un délai, un ordre de message, et rejouer — le bogue disparaît-il ? | X |
| X5 | `SCHED_SPORADIC` et partitions adaptatives dans le modèle | X |
| X6 | Rejeu pas-à-pas interactif (CLI) : avancer d'événement en événement, inspecter l'état | X |

---

## 7. Méthode et preuve

### 7.1 Déterminisme du rejeu (la propriété clé)

> **P1 (déterminisme).** Pour une trace `.kev` donnée et une configuration d'ordonnanceur donnée, le rejeu produit **exactement** la même séquence de décisions d'ordonnancement, vérifiée par hachage cryptographique de la sortie. Rejoué N fois → N hachages identiques.

C'est ce qui fait de Réplique une « machine à remonter le temps » fiable : le rejeu est reproductible, contrairement au bogue sur la cible.

### 7.2 Fidélité du modèle

> **P2 (fidélité).** Le rejeu d'une trace capturée sur QEMU reproduit les ordonnancements et latences observés dans cette trace (l'événement « thread T passe running à t » du rejeu correspond à l'événement de la trace). Toute divergence est un verdict `REPLIQUE_REPLAY_DIVERGED` nommant l'événement fautif.

### 7.3 Exactitude de la chaîne de blocage

> **P3 (diagnostic).** Sur un scénario d'interblocage de référence (créé délibérément), Autopsie détecte le cycle et désigne le coupable attendu. Validé sur fixtures avec résultat connu.

### 7.4 Double preuve (modèle + réel), comme Veille et Ambre

- **Modèle** (portable, PC) : parseur + simulateur + détection, prouvés sur fixtures par P1–P3 et différentiel C↔Python + différentiel contre `traceprinter`.
- **Réel** (cible QEMU) : Déclic capture de vraies archives ; le parseur et Autopsie les traitent ; le rejeu est comparé à la trace réelle (P2).

### 7.5 Apport spécifique de QNX

| Mécanisme QNX | Rôle dans Réplique |
|---|---|
| Kernel trace (`tracelogger`) | La source : événements noyau horodatés (message passing, ordonnancement) |
| `traceprinter` | L'oracle du parseur (différentiel) |
| `dumper` | Le core dump corrélé à la trace |
| HAM | Déclencheur de Déclic (surveillance de terminaison) |
| Message passing (`MsgSend`/`MsgReceive`/`MsgReply`) | Le cœur du graphe d'attente : qui bloque qui |
| Héritage de priorité du noyau | À reproduire fidèlement dans le simulateur (X1) ; à vérifier dans Autopsie (C5) |
| Microkernel | Chaque processus est isolé → le crash de l'un n'empêche pas Déclic (processus séparé) de figer la trace |

---

## 8. Tests

| Test | But |
|---|---|
| Différentiel C↔Python | Même sortie sur même trace |
| Différentiel contre `traceprinter` | Le parseur décode comme l'oracle du SDP |
| Fixtures de traces | Nominal, interblocage, inversion de priorité, course — résultats attendus gelés |
| Déterminisme du rejeu (P1) | N rejeux → même hachage |
| Fidélité (P2) | Rejeu == événements de la trace |
| Diagnostic (P3) | Coupable désigné == coupable attendu du scénario |
| Mutants | Injection de fautes dans `core/` pour vérifier la couverture des tests |
| Déclic bout-en-bout (S7) | Tuer un processus sur QEMU → archive complète et lisible |
| Garde taille | CI : fichiers < 500 lignes, fonctions < 50 lignes |

---

## 9. Démo et vidéo (< 3 min)

1. **(0:00–0:30)** Le problème : sur l'invité QNX, un processus plante à cause d'un interblocage. Le tampon de trace tourne déjà — l'historique qui explique le bogue est en train d'être écrasé.
2. **(0:30–1:10)** **Déclic** : au crash, le démon fige la trace et produit une archive horodatée (trace + `pidin` + `slog2` + core dump, empreinte SHA-256). Rien n'est perdu.
3. **(1:10–1:50)** **Autopsie** (sur PC, sans QNX) : ouverture de l'archive, reconstruction du graphe d'attente, la **chaîne de blocage** s'affiche, le **coupable** est désigné. « Interblocage expliqué en une page. »
4. **(1:50–2:30)** **Réplique** : le **rejeu déterministe** de la trace sur PC reproduit l'interblocage à l'identique (hachage stable). Mode « et si » : on augmente la priorité du thread coupable, on rejoue — le bogue disparaît.
5. **(2:30–3:00)** Le cœur portable : `make test` vert sans QNX, différentiel C↔Python et différentiel contre `traceprinter`. Codelab.

Visuel : terminal hôte (archive, graphe d'attente, rejeu) + fenêtre QEMU (le crash). Pas de musique sous copyright, pas de marque tierce.

---

## 10. À valider avant d'écrire la moindre ligne de produit

| Essai | Question | Bloquant |
|---|---|---|
| **E1** | Le format binaire de la trace noyau (`.kev`) est-il suffisamment documenté pour écrire un **parseur portable** ? Sinon, `traceprinter` peut-il servir d'oracle exclusif (différentiel) ? | oui (S4) |
| **E2** | `tracelogger` peut-il être **arrêté proprement** pour figer le tampon circulaire, et le tampon contient-il bien l'historique **avant** le crash (pas seulement après) ? | oui (S2) |
| **E3** | La **terminaison** d'un processus est-elle observable en temps réel (HAM, événement noyau, ou crochet `dumper`) pour déclencher Déclic à l'instant du crash ? | oui (S2) |
| **E4** | Le modèle de l'ordonnanceur QNX (priorités fixes, héritage, `MsgSend` bloquant) est-il **reproductible fidèlement** dans un simulateur ? (commencer par un sous-ensemble, mesurer la divergence) | oui pour X (Réplique) |
| **E5** | Le vidage d'incident (`dumper`) contient-il les **états de threads** (registres, piles, blocages) nécessaires pour corréler avec la trace ? | oui pour C4 |

**E1, E2, E3 sont les trois essais critiques du socle S (Déclic).** S'ils passent, le sous-ensemble 15/12 est réalisable. E4 ne bloque que le niveau X (Réplique) ; E5 ne bloque que la corrélation core dump (C4).

---

## 11. Planning (13 semaines, 12 nov → ~11 fév, avec jalon 15/12)

| Semaines | Livrable |
|---|---|
| S1 (12–18 nov) | E1, E2, E3 tranchés. `CONTRACT.md` gelé. Parseur `.kev` C11 + différentiel contre `traceprinter` sur fixtures. |
| S2 (19–25 nov) | **Déclic** (cible) : surveillance terminaison, figeage trace, archive corrélée horodatée. |
| S3 (26 nov–2 déc) | Jumeau Python + différentiel C↔Python. Fixtures de bogues de concurrence capturées sur QEMU. |
| S4 (3–9 déc) | Démo bout-en-bout Déclic : tuer un processus → archive complète lisible sur PC. CI verte. |
| **Jalon 15/12** | **Sous-ensemble S livré et soumis** : Déclic + parseur + différentiel, démontrable sans cible pour l'analyse. |
| S5–S7 (17 déc–6 janv) | **Autopsie** : reconstruction d'états, graphe d'attente, chaîne de blocage, coupable (C1–C3). |
| S8–S9 (7–20 janv) | Corrélation core dump/`pidin` (C4, E5), inversion de priorité (C5), rapport « une page » (C6), visualisation (C7). |
| S10–S11 (21 janv–3 fév) | **Réplique** : modèle d'ordonnanceur (X1), rejeu déterministe (X2), fidélité (X3, E4). |
| S12 (4–10 fév) | Mode « et si » (X4), rejeu pas-à-pas (X6), `SCHED_SPORADIC`/partitions (X5). |
| S13 (avant ~11 fév) | Codelab complet, docs (DESIGN, GAP, DECISIONS, VALIDATION, KEV-FORMAT, SCHED-MODEL), polissage, marge. |

---

## 12. Risques

| Risque | Impact | Mitigation |
|---|---|---|
| E1 : format `.kev` sous-documenté | Parseur difficile | Utiliser `traceprinter` comme **oracle exclusif** (différentiel), comme Tamis avec `secpolcompile` ; ne pas dépendre du format binaire brut si nécessaire |
| E2 : tampon écrasé avant figeage | Historique avant-crash perdu | Tampon sur-dimensionné, déclenchement de Déclic sur l'événement le plus précoce possible (E3), double tampon |
| E3 : terminaison non observable en temps réel | Déclic déclenché trop tard | Plusieurs déclencheurs : HAM, événement noyau, crochet `dumper` ; choisir le plus précoce |
| E4 : simulateur d'ordonnanceur infidèle | Rejeu qui diverge du réel | Commencer par un sous-ensemble (priorités fixes + `MsgSend`), mesurer la divergence (P2), documenter les limites ; le rejeu reste utile même partiellement fidèle |
| Taille des traces (millions d'événements) | Parsing/rejeu lents | Fenêtrage autour du crash, index, streaming |
| Concurrence avec Tamis (Dev Tooling) | Deux projets même catégorie | Distincts : Tamis = analyse **statique à froid** de politiques de sécurité ; Réplique = **post-mortem + rejeu** d'un crash. Complémentaires, pas concurrents. |
| Chevauchement avec Échéancier/Renverse/Filature (lot B) | Redondance | Réplique **fusionne** Déclic+Autopsie+Réplique et absorbe la causalité (R5) ; Échéancier (échéances déclarées) et Renverse (inversion en ligne) restent des outils distincts plus légers |
| 3 mois > deadline concours | Soumission partielle | Le sous-ensemble S (Déclic) est livrable au 15/12 ; Autopsie/Réplique après |

---

## 13. Statut des affirmations

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

Comme Tamis et Veille : toute correction d'affirmation est consignée ici.

---

## 14. Références

- Lot B du dossier `qnx-dossier` (« Outillage temps réel, trace et supervision ») : Déclic (gap 4), Autopsie (gap 4), Réplique (gap 4), Filature (gap 4), Renverse (gap 3), Échéancier (gap 3).
- QNX SDP 8.0 : Kernel Trace (`tracelogger`, `traceprinter`), System Profiler, `dumper`, `pidin`, `slog2`, High Availability Manager (HAM), ordonnanceur (priorités fixes, héritage, `SCHED_SPORADIC`, partitions adaptatives).
- Méthode du différentiel contre un oracle du SDP : établie sur **Tamis** (`secpolcompile`).
- Méthode de la double preuve modèle + réel : établie sur **Veille** (exploration exhaustive + kernel trace) et **Ambre** (exploration des états de coupure).
- Linux `rr` (rejeu déterministe) et `perf` : précédents dans l'écosystème Linux, sans équivalent QNX — à citer comme contexte, pas comme dépendance.
- QNX Global Hackathon 2026 : https://qnx.devpost.com/ — règles, catégories, bonus.

---

## 15. Décisions de conception

| # | Décision | Justification |
|---|---|---|
| D1 | Fusionner Déclic + Autopsie + Réplique en UN produit cohérent | Les trois traitent la même donnée (la trace d'un crash) ; séparés, ils se concurrencent et dupliquent le parseur. Fusionnés : figer → expliquer → rejouer, une seule chaîne. |
| D2 | Cœur C11 pur (parseur + modèle + rejeu), séparé de Déclic (cible) | Le cœur est démontrable sans QNX/cible ; Déclic n'est que la capture sur cible. Sous-ensemble S = Déclic + parseur. |
| D3 | Parseur validé par différentiel contre `traceprinter` | Ne pas dépendre d'un format binaire sous-documenté ; l'oracle du SDP garantit l'exactitude du décodage (méthode Tamis). |
| D4 | Jumeau Python indépendant | Vérification croisée du cœur C, comme sur tout le portefeuille. |
| D5 | Le rejeu porte sur l'**ordonnancement**, pas sur l'exécution des instructions | Reproduire l'interblocage/l'inversion de priorité (le problème de concurrence) sans être un émulateur complet. Périmètre réaliste et utile. |
| D6 | Déterminisme du rejeu prouvé par hachage (P1) | C'est la propriété qui rend l'outil fiable ; vérifiable automatiquement en CI. |
| D7 | Le sous-ensemble 15/12 = Déclic (figer + archiver) | Faisable en 4 semaines, démo forte (rien n'est perdu au crash), et c'est la brique dont Autopsie et Réplique dépendent. |
| D8 | Complément de Tamis, pas concurrent | Tamis = à froid (avant déploiement) ; Réplique = post-mortem (après crash). Le portefeuille couvre les deux bouts. |
| D9 | Déclic réutilisable par Veille/Ambre/Relais | Le « figeur de trace » est un besoin commun ; Déclic peut devenir la brique partagée de capture de preuve empirique du portefeuille. |
| D10 | MIT, cohérent avec le portefeuille | — |
| D11 | Codelab inclus | Bonus Best Codelab/Tutorial ; pédagogie « comprendre un crash QNX » forte. |

---

## 16. Lots délégables (pour orchestrateur)

| Lot | Contenu | Dépend de |
|---|---|---|
| L1 | `CONTRACT.md` + format d'événements | E1 |
| L2 | Parseur `.kev` C11 + différentiel contre `traceprinter` | L1, E1 |
| L3 | Jumeau Python + différentiel C↔Python | L2 |
| L4 | Fixtures : captures de traces sur QEMU (nominal + 3 bogues) | E2, E3 |
| L5 | **Déclic** (cible) : surveillance terminaison + figeage + archive corrélée | E2, E3 |
| L6 | Graphe d'attente + chaîne de blocage + coupable (Autopsie) | L2 |
| L7 | Corrélation core dump/`pidin` | L5, E5 |
| L8 | Inversion de priorité (détection + héritage vérifié) | L2, L6 |
| L9 | Rapport « une page » + visualisation graphe | L6 |
| L10 | Modèle d'ordonnanceur + rejeu déterministe (Réplique) | L2, E4 |
| L11 | Mode « et si » + rejeu pas-à-pas | L10 |
| L12 | Codelab + docs | L1–L9 |

---

*Fin du cahier des charges — Réplique v0.1.*
