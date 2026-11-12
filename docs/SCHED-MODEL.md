# docs/SCHED-MODEL.md — le modèle d'ordonnanceur de Réplique

Source gelée : `CONTRACT.md` §5 (modèle), §6 (déterminisme, fidélité) ; en-têtes `core/include/replique/sched.h` et `replay.h` ; jumeau Python `host/replique/sched.py` et `replay.py`. Ce document explique le modèle, ses règles exactes, ses **limites assumées** et l'écart attendu avec le vrai noyau QNX.

## 1. Principes

- **Priorités fixes de 0 à 255**, 256 niveaux, **0 = plus haute**, 255 = plus basse. Cet ordre (plus le nombre est petit, plus le thread est prioritaire) est gelé dans tout le projet (CONTRACT.md §2.5).
- **`n_cpus = 1`** par défaut : à chaque instant, **un seul** thread `RUNNING`. Le multi-CPU est hors périmètre v1.
- **Purement fonctionnel** : l'état vit dans une structure fournie par l'appelant (`replique_sched`, 256 threads max, aucune allocation côté C), le temps vient des horodatages de la trace — jamais de l'horloge. Mêmes événements + même configuration → mêmes décisions (P1, `docs/VALIDATION.md`).

## 2. Règle d'élection

Parmi les threads `READY` :

1. le **plus petit numéro de priorité** (priorité effective, héritage inclus) ;
2. à égalité, selon la politique du thread :
   - **FIFO** : le thread prêt le plus anciennement (`ready_ts` le plus ancien), puis le plus petit `tid` ;
   - **RR** : la tête de file tournante ; quantum `rr_quantum` en ticks, `0` = pas de préemption par quantum.

Un thread de priorité strictement supérieure (numériquement inférieure) qui devient `READY` **préempte** immédiatement le thread courant → décision `PREEMPT`.

Raisons de décision (gelées, CONTRACT.md §2.6) : `INITIAL`, `PREEMPT`, `BLOCK`, `UNBLOCK`, `YIELD`, `TIMER`, `TERMINATE`, `WHATIF`. Chaque décision rejouée est émise en ligne canonique `dec|ts=…|tid=…|reason=…` (§4.3).

## 3. Message passing bloquant

Le modèle reproduit la sémantique synchrone de QNX (CONTRACT.md §5) :

- `MSG_SEND` **bloque l'émetteur** (raison de blocage `MSGSEND`) jusqu'à ce que le serveur fasse `MSG_RECEIVE` puis `MSG_REPLY` ;
- `MSG_REPLY` **débloque l'émetteur** ;
- `MSG_ERROR` termine l'échange en erreur (`errno` porté par le champ `c`).

Ces arêtes nourrissent le graphe d'attente : « thread T attend (ressource R) tenu par thread U » (`replique_waitfor_build`), où R peut être un mutex, un sémaphore **ou** un canal de message — le cœur de l'analyse d'interblocage (C2).

## 4. Héritage de priorité (prise maximale)

Quand `cfg.inheritance = 1` (défaut) :

- un thread `V` se bloque sur une ressource détenue par `B` et `prio(B) > prio(V)` numériquement (B moins prioritaire) → **`B` hérite de `prio(V)`** jusqu'à la libération (`replique_sched_inherit`) ;
- à la libération (`replique_sched_release`), la priorité rendue est la **priorité de base**, sauf autre attente en cours : c'est la **prise maximale** — si plusieurs threads attendent encore `B`, il conserve la plus haute priorité (numériquement la plus petite) parmi les attentes restantes ;
- les événements `PRIO_CHANGE` de la trace portent le sous-code `1` = héritage appliqué, `2` = héritage rendu (CONTRACT.md §2.2), ce qui permet à Autopsie de vérifier que l'héritage a réellement fonctionné (C5, règle §3.4 : verdict `PRIO_INVERSION` avec `a=1` si héritage appliqué avant la fin du blocage).

## 5. Mode « et si » (X4)

`replique_sched_config.overrides` (tableau de `{tid, prio}`, max 16 entrées) **remplace la priorité de base** d'un thread avant le rejeu. En CLI : `--whatif TID=PRIO` (répétable) sur `analyze`/`replay`, et sur les pilotes `build/analyze`/`build/replay-dump`. Chaque modification appliquée produit un verdict `REPLIQUE_WHATIF` (info : `tid`, nouvelle priorité `a`, hachage du rejeu modifié). Le scénario `race` (SPEC.md §4) en fait la démonstration gelée : `--whatif 402=20` doit changer le hachage de rejeu.

## 6. Limites assumées (v1)

| Limite | Comportement du modèle |
|---|---|
| **`SCHED_SPORADIC`** | **réservé non implémenté**. Une politique `SPORADIC` dans une trace produit `REPLIQUE_TRACE_INCOMPLETE` avec `detail=sporadic-not-modelled` — **jamais un rejeu faux**. L'exigence X5 du cahier le couvre à 3 mois |
| **Partitions adaptatives** | non modélisées en v1 (X5) ; même philosophie : signalisation `TRACE_INCOMPLETE`, jamais de décision inventée |
| **Multi-CPU** | `n_cpus = 1` seul supporté en v1 (CONTRACT.md §5) |
| **Exécution des instructions** | hors périmètre par décision D5 : le rejeu porte sur l'**ordonnancement**, pas sur le code exécuté |

## 7. Écart attendu avec le vrai noyau QNX (E4 non tranché)

**Honnêteté (cahier §13) : « Modèle d'ordonnanceur reproductible fidèlement : Non vérifié (E4) ».** Ce qui suit est l'écart *attendu*, pas un écart mesuré — la mesure viendra de P2 sur traces réelles :

- **Ce qu'on attend de fidèle** : l'ordre d'élection à priorités fixes, la préemption immédiate, la sémantique bloquante de `MsgSend`/`MsgReceive`/`MsgReply`, l'héritage avec prise maximale — mécaniques documentées du noyau QNX.
- **Sources de divergence probables** : latences noyau réelles (interrupteurs, interruptions non modélisées finement), politique RR exacte (phase du quantum, interactions avec les interruptions), effets des politiques non modélisées (sporadique, partitions), et tout événement de la trace que le modèle ne couvre pas.
- **Comment l'écart est traité** : jamais masqué. Si la trace contient des événements `SCHED`, le rejeu compare sa séquence à la séquence observée (même `ts`, même `tid` élu) ; la **première divergence** produit `REPLIQUE_REPLAY_DIVERGED` nommant l'événement (`ts`, `tid`, raison attendue/trouvée) — P2, CONTRACT.md §6. Sans événement `SCHED`, aucun verdict de fidélité n'est émis.
- **Valeur malgré l'écart** : le cahier (§12, risque E4) l'assume — commencer par un sous-ensemble (priorités fixes + `MsgSend`), mesurer la divergence, documenter les limites ; un rejeu partiellement fidèle reste utile pour reproduire et comprendre un interblocage, et le déterminisme (P1) est garanti quelle que soit la fidélité.

## 8. Où lire le code

| Sujet | C (`core/`) | Python (`host/replique/`) |
|---|---|---|
| État thread, élection, héritage | `sched.h` / `sched.c` (`replique_sched_pick`, `replique_sched_apply`, `replique_sched_inherit`, `replique_sched_release`) | `sched.py` (classe `Scheduler`, `default_config`, `override_prio`) |
| Rejeu, hachage, fidélité | `replay.h` / `replay.c` (`replique_replay`, `replique_replay_hash`, `replique_replay_check_fidelity`) | `replay.py` (`run`, `hash_decisions`, `check_fidelity`, `step_through`) |
| Configuration « et si » | `types.h` (`replique_sched_config`, `replique_prio_override`) | `model.py` (`SchedConfig`) |
