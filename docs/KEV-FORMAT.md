# docs/KEV-FORMAT.md — trace noyau QNX, conteneur RKV1 et méthode d'oracle

Ce document sépare strictement trois choses :

1. la trace noyau QNX **telle que documentée publiquement** (§1) — avec le statut de vérification de chaque affirmation ;
2. le conteneur **RKV1** du projet (§2) — reprise expliquée de `CONTRACT.md` §1, qui lui est **gelé et vérifiable** ;
3. la **méthode d'oracle** contre `traceprinter` (§3).

## 1. La trace noyau QNX (documentée publiquement)

### 1.1 Ce que dit la documentation QNX 8.0

| Élément | Rôle | Statut pour le projet |
|---|---|---|
| `tracelogger` | démarre la capture des événements noyau dans un **tampon circulaire** ; à l'arrêt, le tampon est écrit dans un fichier `.kev` | Documenté publiquement. Le **figeage propre au moment d'un crash** (arrêter `tracelogger` assez vite pour conserver l'historique avant-crash) est **NON VÉRIFIÉ** — essai E2 |
| Fichier `.kev` | représentation sur disque du tampon d'événements noyau : événements d'ordonnancement, message passing, interruptions, timers, cycle de vie process/thread | Existence et rôle **documentés** ; le **format binaire exact** (en-têtes, tailles d'enregistrements, encodage des champs) est **NON VÉRIFIÉ** — essai E1. Il n'est pas consommé directement par le cœur (voir §2) |
| `traceprinter` | décode un `.kev` en texte lisible | **VÉRIFIÉ dans l'inventaire lot B** (existe, décode, n'établit aucune causalité) ; à confirmer sur une trace réelle. Sert d'**oracle** au projet (§3) |
| System Profiler (Momentics) | visualise la trace (frise, threads) | Documenté ; mesure et visualise, n'explique pas la causalité, ne rejoue pas (cahier §2.1) |
| Événements disponibles | états de threads, `MsgSend`/`MsgReceive`/`MsgReply`, interruptions, timers, décisions d'ordonnancement, création/destruction process/thread | Documentés (cahier §2.1). Leur **encodage binaire** dans le `.kev` : NON VÉRIFIÉ (E1). Leur **observabilité temps réel** pour déclencher Déclic : NON VÉRIFIÉ (E3) |

### 1.2 Conséquence — essai E1 **tranché**

Le format binaire `.kev` QNX 8.0 est **documenté** dans `sys/trace.h` du SDP
(`traceevent_t`, `traceheader`, masques de bits `_NTO_TRACE_GETCPU`/`CLASS`/`EVENT`,
combinaison `CB`/`CC`/`CE`) et **décodé directement** par
`host/replique/kev_reader.py` ; `host/replique/kev2rkv1.py` le convertit en
`RKV1` (et `target/declic/kev_rkv1.c` fait la même chose **sur la cible**).
Le décodage est **validé par différentiel** contre l'oracle `traceprinter` :
mêmes comptes par type d'événement **et mêmes champs** (`pid`, `tid`,
priorité, politique) sur **10 989** événements d'état de thread de la trace
« wide » — 0 écart (voir §3).

#### Ce que la trace donne réellement

| Événement `.kev` | Contenu décodé | Projection RKV1 |
|---|---|---|
| état de thread (2 records combinés) | `(pid, tid)` puis `(priorité, politique)` | `THREAD_STATE` (état + raison de blocage) ; la priorité alimente `THREAD_CREATE.a` et les changements → `PRIO_CHANGE` |
| création de thread | `(pid, tid)` | `THREAD_CREATE` (politique + priorité **observées à la première activité**) |
| `SND_MESSAGE` / `REC_MESSAGE` | `(rcvid, pid du détenteur du canal)` | `MSG_SEND` (`a` = pid destinataire) / `MSG_RECEIVE` (le pid **source** n'est pas exposé : `a` = 0) |
| `REPLY_MESSAGE` / `MSG_ERROR` | `(tid, pid)` du répondant | `MSG_REPLY` / `MSG_ERROR` (tid et pid du répondant dans les colonnes génériques) |

#### Pourquoi l'identifiant de mutex n'est pas dans la trace (mesuré, 13/11/2026)

En QNX 8, un mutex `pthread` **contendu** ne bloque pas le thread sur un
objet noyau nommé : le client envoie un `MsgSend` au **serveur de
synchronisation**, qui détient le mutex. L'oracle officiel le montre sans
ambiguïté sur la trace « wide » (`docs/preuves/trace-qnx800-wide.kev`) :

```
KER_CALL  :MSG_SENDV/11   coid:0x00000001
COMM      :SND_MESSAGE    rcvid:0x00010006 pid:3
THREAD    :THREPLY        pid:180232 tid:2 priority:60 policy:1
```

Le `MsgSend` est tracé avec **l'en-tête** du message (`0x102`, `0x27`, `0x0`),
pas avec son **corps** — c'est là que vit l'identifiant de l'objet de
synchronisation, et le noyau ne le recopie pas. Aucun `SYNC_MUTEX_LOCK`
n'apparaît dans la capture ; les deux seuls événements de ce nom sont des
**sorties** d'appel interrompues par le tueur du processus (`errno 39`,
`EL3HLT`).

Conséquence, mesurée et assumée : **le graphe d'attente n'est pas
constructible à partir d'une trace QNX 8.0** pour un interblocage de mutex
`pthread`. Le projet le dit plutôt que de le maquiller. Ce que la trace
expose en revanche : le **canal** (`rcvid`) de chaque envoi, le pid de son
détenteur, et — côté noyau — le `tid` et le `pid` du serveur qui reçoit le
message (`KER_EXIT :MSG_RECEIVEV`). De quoi reconstruire la causalité des
messages (trou R5), et un graphe d'attente **sur les canaux** : c'est la suite
naturelle.

Les champs **non exposés** par la trace restent à 0 : identifiant de mutex,
valeur de sémaphore, numéro de séquence, tid du serveur d'un canal, pid source
d'une réception. La trace dit *ce qui se passe*, pas *pourquoi* (c'est le
postulat du projet, cf. `docs/GAP.md`) — mais elle donne désormais **les
priorités réelles**, y compris les **héritages** observés (un thread de
serveur qui passe de 10 à 24 puis revient à 10 : c'est l'héritage de priorité
rendu, le phénomène que l'exigence C5 cherche à détecter).

## 2. Le conteneur RKV1 (CONTRACT.md §1 — gelé)

### 2.1 Pourquoi un conteneur propre

- **Documenté** : la spécification tient dans `CONTRACT.md` §1 et dans ce fichier ; aucune zone d'ombre binaire.
- **Auto-descriptif** : magic, version, unité de temps, nombre d'événements, drapeaux.
- **Vérifiable** : CRC-32 sur le blob d'enregistrements ; toute incohérence → `REPLIQUE_PARSE_ERROR` avec cause gelée, jamais de reprise approximative.
- **Testable sans QNX** : les fixtures sont engendrées sur PC (`host/replique/kevgen.py`, `make fixtures`), donc toute la chaîne (parse → graphe → rejeu) est démontrable sans cible ni licence — exigence S8.

### 2.2 En-tête (32 octets, petit-boutien)

| Décalage | Taille | Champ | Valeur |
|---|---|---|---|
| 0 | 4 | `magic` | ASCII `"RKV1"` (sans octet nul) |
| 4 | 4 | `version` | `1` |
| 8 | 4 | `flags` | bit 0 `TRUNCATED` (tampon circulaire écrasé), bit 1 `SYNTHETIC` (générée sur PC), bits 2–31 réservés = 0 |
| 12 | 4 | `n_events` | nombre d'enregistrements qui suivent |
| 16 | 4 | `time_unit` | ticks par seconde (fixtures : `1000000000` = ns) |
| 20 | 4 | `crc32` | CRC-32/ISO-HDLC (polynôme réfléchi `0xEDB88320`, init et xorout `0xFFFFFFFF`) sur **le blob d'enregistrements seul** (octets 32..fin) |
| 24 | 8 | `reserved` | zéros |

### 2.3 Enregistrement d'événement (32 octets, petit-boutien, aligné)

| Décalage | Taille | Champ |
|---|---|---|
| 0 | 8 | `ts` — horodatage en ticks, **monotone non décroissant** |
| 8 | 4 | `pid` |
| 12 | 4 | `tid` |
| 16 | 2 | `class` — `enum replique_event_class` (CONTRACT.md §2.1) |
| 18 | 2 | `subcode` — signification dépendante de la classe (§2.2) |
| 20 | 4 | `a` |
| 24 | 4 | `b` |
| 28 | 4 | `c` |

Taille totale d'un fichier valide : `32 + 32 * n_events`. Toute autre taille → `REPLIQUE_PARSE_ERROR`. **Lecture interdite par cast de structure** : le parseur lit champ par champ (`memcpy` ou accès octet à octet) pour rester indépendant de l'alignement et du boutisme de l'hôte.

Erreurs de parseur (toutes → `REPLIQUE_PARSE_ERROR`, `a` = décalage en octets, chaînes gelées) : `short-header`, `bad-magic`, `bad-version`, `bad-length`, `bad-crc`, `bad-class`, `ts-not-monotonic`, `bad-flags`. Les fixtures dégradées qui les exercent sont décrites dans `scenarios/SPEC.md` §5.

Le jeu de classes RKV1 (18 classes, CONTRACT.md §2.1) couvre les besoins du graphe d'attente et du rejeu : états de thread, cycle de vie process/thread, message passing (`MSG_SEND`/`MSG_RECEIVE`/`MSG_REPLY`/`MSG_ERROR`), synchronisation (mutex, sémaphores), timer, interruption, décision d'ordonnancement observée (`SCHED`), changement de priorité dont héritage (`PRIO_CHANGE`).

### 2.4 L'adaptateur kev→rkv1 (quand E1 sera tranché)

Le point d'insertion est **Déclic, côté cible** (`target/declic/`), pas le cœur :

```
tracelogger (tampon circulaire) → [figeage au crash — E2/E3]
    → .kev brut → ADAPTATEUR kev→rkv1 (target/declic/, à écrire après E1)
    → .rkv1 → archive Déclic (manifeste, pidin, slog2, core dump, SHA-256)
    → cœur portable (parse RKV1 → analyse → rejeu), inchangé
```

Propriétés de ce découpage : le cœur, ses tests, le contrat RKV1 et les attentes gelées des scénarios **ne changent pas** quand l'adaptateur arrive ; seul un convertisseur amont s'ajoute, validé par l'oracle `traceprinter` (§3). **Statut : essai E1 tranché — adaptateur écrit** (`host/replique/kev_reader.py` + `kev2rkv1.py`), sur le binaire direct (pas le texte de l'oracle). À l'avenir, la même logique pourra être portée côté cible (Déclic) pour convertir dans l'archive.

## 3. Méthode d'oracle

Le décodage est validé **par différentiel contre `traceprinter`** (exigence S4, méthode établie sur Tamis contre `secpolcompile`) :

1. Sur une machine avec SDP : capturer une trace QNX réelle (`tracelogger`), la décoder avec `traceprinter` → texte de référence ;
2. convertir la même trace en `.rkv1` (adaptateur §2.4) ;
3. déposer le texte oracle à côté de la fixture : `fixtures/<nom>.tp.txt` ;
4. `make oracle` (`tests/differential_kev_oracle.py` + `tests/differential_traceprinter.sh`) compare le décodage au texte oracle. Le script Python décode le `.kev` **binaire** et confronte, par type d'événement, ses comptes à ceux de `traceprinter` (régénéré si le SDP est présent, sinon via le `.tp.txt` voisin).

**Sans SDP** (cas de la machine de développement aujourd'hui) : le différentiel tourne sur les fixtures `.rkv1` pré-enregistrées et sur le différentiel C↔Python (`tests/differential_c_python.sh`) — la chaîne de preuve n'est jamais bloquée par la licence, mais la comparaison au noyau réel reste **conditionnée à E1/E2** et le dit.

### Récapitulatif VÉRIFIÉ / NON VÉRIFIÉ

| Affirmation | Statut |
|---|---|
| Conteneur RKV1 : en-tête, enregistrements, erreurs | **GELÉ et vérifiable** (CONTRACT.md §1, CRC-32, fixtures dégradées SPEC §5) |
| Sorties canoniques `ev|`/`vd|`/`dec|` | **GELÉ** (CONTRACT.md §4), formateurs implémentés (`core/src/report.c`, `host/replique/report.py`) |
| Existence de `tracelogger`/`traceprinter`/`.kev`, tampon circulaire | **Documenté publiquement** ; comportement au crash à revérifier page à page (cahier §13) |
| Format binaire exact du `.kev` QNX 8.0 | **VÉRIFIÉ** — documenté par `sys/trace.h`, décodé par `host/replique/kev_reader.py` (E1 tranché) |
| Figeage propre avec historique avant-crash | **NON VÉRIFIÉ** (E2 non tranché) |
| Terminaison observable en temps réel | **NON VÉRIFIÉ** (E3 non tranché) |
| Décodage identique à `traceprinter` sur trace réelle | **VÉRIFIÉ** — comptes par type identiques sur `docs/preuves/trace-qnx800.kev` (`tests/differential_kev_oracle.py`, cible `make oracle`) |
| Capture du **processus fautif** dans la trace | **VÉRIFIÉ** : le mode « wide » (`tracelogger -w`, après le boot, dans un invité piloté par la console série) le contient (`docs/preuves/trace-qnx800-wide.kev`), et le décodeur binaire le lit (`make oracle` sur la trace wide) |
