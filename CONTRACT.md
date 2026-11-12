# CONTRACT.md — contrat gelé de Réplique (v1)

> **Ce fichier est le contrat.** Toute implémentation (C dans `core/`, Python dans
> `host/`, Déclic dans `target/`) s'y conforme octet pour octet. Changer ce
> contrat = une PR explicite, motivée, avec mise à jour des deux jumeaux et des
> fixtures dans la même PR. Exigence S1 du cahier des charges.

Statut : **gelé le 2026-11-12**. Sections : 1 conteneur, 2 événements,
3 verdicts, 4 sorties canoniques, 5 modèle d'ordonnanceur, 6 déterminisme,
7 règles de taille et de portabilité.

---

## 1. Conteneur de trace `RKV1`

### 1.1 Pourquoi un conteneur propre (et pas le `.kev` brut)

L'essai **E1** du cahier des charges (format binaire exact du `.kev` QNX 8.0)
n'est **pas tranché** : le SDP n'est pas installé sur la machine de
développement. Le cœur portable consomme donc un conteneur **documenté,
auto-descriptif et vérifiable**, `RKV1`, produit :

- sur PC, par le générateur de fixtures (`host/replique/kevgen.py`) ;
- sur cible, par l'adaptateur `kev → rkv1` de Déclic (`target/declic/`), dont la
  lecture du `.kev` réel reste conditionnée à E1 (voir `docs/KEV-FORMAT.md`).

Le **différentiel contre `traceprinter`** (oracle du SDP, exigence S4) porte sur
le texte décodé : une fixture `.rkv1` peut être accompagnée d'un fichier oracle
`<nom>.tp.txt` produit par `traceprinter` sur la trace QNX d'origine ; quand il
est présent, `make differential-oracle` compare. En l'absence de SDP, le
différentiel tourne sur les fixtures pré-enregistrées (méthode Tamis).

### 1.2 En-tête (32 octets, petit-boutien)

| Décalage | Taille | Champ | Valeur |
|---|---|---|---|
| 0 | 4 | `magic` | ASCII `"RKV1"` (sans octet nul) |
| 4 | 4 | `version` | `1` |
| 8 | 4 | `flags` | bit 0 `TRUNCATED` (tampon circulaire écrasé), bit 1 `SYNTHETIC` (générée sur PC), bits 2–31 réservés = 0 |
| 12 | 4 | `n_events` | nombre d'enregistrements qui suivent |
| 16 | 4 | `time_unit` | ticks par seconde (fixtures : `1000000000` = ns) |
| 20 | 4 | `crc32` | CRC-32/ISO-HDLC (polynôme réfléchi `0xEDB88320`, init et xorout `0xFFFFFFFF`) calculé sur **le blob d'enregistrements seul** (octets 32..fin) |
| 24 | 8 | `reserved` | zéros |

### 1.3 Enregistrement d'événement (32 octets, petit-boutien, aligné)

| Décalage | Taille | Champ |
|---|---|---|
| 0 | 8 | `ts` — horodatage en ticks, **monotone non décroissant** |
| 8 | 4 | `pid` |
| 12 | 4 | `tid` |
| 16 | 2 | `class` — `enum replique_event_class` (§2.1) |
| 18 | 2 | `subcode` — signification dépendante de la classe (§2.2) |
| 20 | 4 | `a` |
| 24 | 4 | `b` |
| 28 | 4 | `c` |

Taille totale d'un fichier valide : `32 + 32 * n_events`. Toute autre taille →
`REPLIQUE_PARSE_ERROR` (§3), jamais de reprise approximative.

**Lecture interdite par cast de structure** : le parseur lit champ par champ
(`memcpy` ou accès octet à octet) pour rester indépendant de l'alignement et du
boutisme de l'hôte.

### 1.4 Erreurs de parseur (toutes → `REPLIQUE_PARSE_ERROR`, `a` = décalage en octets)

| Cause | `detail` (chaîne gelée) |
|---|---|
| tampon plus court que l'en-tête | `short-header` |
| `magic` != `RKV1` | `bad-magic` |
| `version` != 1 | `bad-version` |
| taille != `32 + 32*n_events` | `bad-length` |
| CRC invalide | `bad-crc` |
| `class` hors table | `bad-class` |
| `ts` strictement décroissant | `ts-not-monotonic` |
| bit réservé de `flags` non nul | `bad-flags` |

---

## 2. Événements

### 2.1 Classes (valeurs gelées — ne jamais réordonner)

| # | Nom C | Nom canonique (sorties) | Rôle |
|---|---|---|---|
| 0 | `REPLIQUE_EV_NONE` | `NONE` | réservé |
| 1 | `REPLIQUE_EV_THREAD_STATE` | `THREAD_STATE` | changement d'état de thread |
| 2 | `REPLIQUE_EV_PROCESS_CREATE` | `PROCESS_CREATE` | création de processus |
| 3 | `REPLIQUE_EV_PROCESS_DESTROY` | `PROCESS_DESTROY` | terminaison de processus (le déclencheur de Déclic) |
| 4 | `REPLIQUE_EV_THREAD_CREATE` | `THREAD_CREATE` | création de thread |
| 5 | `REPLIQUE_EV_THREAD_DESTROY` | `THREAD_DESTROY` | destruction de thread |
| 6 | `REPLIQUE_EV_MSG_SEND` | `MSG_SEND` | `MsgSend` (bloquant) |
| 7 | `REPLIQUE_EV_MSG_RECEIVE` | `MSG_RECEIVE` | `MsgReceive` |
| 8 | `REPLIQUE_EV_MSG_REPLY` | `MSG_REPLY` | `MsgReply` |
| 9 | `REPLIQUE_EV_MSG_ERROR` | `MSG_ERROR` | `MsgError` |
| 10 | `REPLIQUE_EV_MUTEX_LOCK` | `MUTEX_LOCK` | verrouillage de mutex |
| 11 | `REPLIQUE_EV_MUTEX_UNLOCK` | `MUTEX_UNLOCK` | libération de mutex |
| 12 | `REPLIQUE_EV_SEM_WAIT` | `SEM_WAIT` | attente de sémaphore |
| 13 | `REPLIQUE_EV_SEM_POST` | `SEM_POST` | libération de sémaphore |
| 14 | `REPLIQUE_EV_TIMER` | `TIMER` | échéance de temporisateur / pulse |
| 15 | `REPLIQUE_EV_INTERRUPT` | `INTERRUPT` | interruption |
| 16 | `REPLIQUE_EV_SCHED` | `SCHED` | décision d'ordonnancement observée |
| 17 | `REPLIQUE_EV_PRIO_CHANGE` | `PRIO_CHANGE` | changement de priorité (dont héritage) |

### 2.2 Champs par classe (`subcode`, `a`, `b`, `c`)

| Classe | `subcode` | `a` | `b` | `c` |
|---|---|---|---|---|
| `THREAD_STATE` | état (§2.3) | raison de blocage (§2.4) | ressource (id mutex/canal/sémaphore) | thread détenteur (0 = inconnu) |
| `PROCESS_CREATE` | 0 | ppid | 0 | 0 |
| `PROCESS_DESTROY` | 0 | statut de sortie | signal (0 = aucun) | 0 |
| `THREAD_CREATE` | politique (§2.5) | priorité de base | 0 | 0 |
| `THREAD_DESTROY` | 0 | statut | 0 | 0 |
| `MSG_SEND` | 0 | pid destinataire | tid serveur (détenteur du canal) | numéro de séquence |
| `MSG_RECEIVE` | 0 | pid source | tid source | numéro de séquence |
| `MSG_REPLY` | 0 | tid destinataire | numéro de séquence | statut |
| `MSG_ERROR` | 0 | tid destinataire | numéro de séquence | `errno` |
| `MUTEX_LOCK` | 0 = acquis, 1 = bloquant | id mutex | tid détenteur actuel | 0 |
| `MUTEX_UNLOCK` | 0 | id mutex | 0 | 0 |
| `SEM_WAIT` | 0 = acquis, 1 = bloquant | id sémaphore | valeur avant l'opération | 0 |
| `SEM_POST` | 0 | id sémaphore | valeur après l'opération | 0 |
| `TIMER` | 0 | id temporisateur | intervalle (ticks) | 0 |
| `INTERRUPT` | 0 | numéro d'IRQ | 0 | 0 |
| `SCHED` | raison (§2.6) | tid élu | 0 | 0 |
| `PRIO_CHANGE` | 0 = normale, 1 = héritage appliqué, 2 = héritage rendu | nouvelle priorité | ancienne priorité | tid à l'origine de l'héritage |

### 2.3 États de thread (`THREAD_STATE.subcode`)

`0 DEAD`, `1 RUNNING`, `2 READY`, `3 BLOCKED`, `4 STOPPED`.

### 2.4 Raisons de blocage (`THREAD_STATE.a`)

`0 UNKNOWN`, `1 MUTEX`, `2 MSGSEND`, `3 SEMAPHORE`, `4 TIMER`, `5 INTERRUPT`,
`6 REPLYWAIT` (en attente d'un `MsgReply`), `7 JOIN`.

### 2.5 Politiques (`THREAD_CREATE.subcode`)

`0 FIFO`, `1 RR`, `2 SPORADIC`. Priorités QNX : **0 = plus haute**, 255 = plus
basse (ordre gelé dans tout le projet : plus le nombre est petit, plus le thread
est prioritaire).

### 2.6 Raisons d'une décision d'ordonnancement (rejeu et `SCHED.subcode`)

`0 INITIAL`, `1 PREEMPT`, `2 BLOCK`, `3 UNBLOCK`, `4 YIELD`, `5 TIMER`,
`6 TERMINATE`, `7 WHATIF`.

---

## 3. Verdicts

### 3.1 Codes (enum `replique_verdict_code`, valeurs gelées)

| # | Code | Sévérité | Porteurs |
|---|---|---|---|
| 0 | `REPLIQUE_OK` | `info` | — |
| 1 | `REPLIQUE_PARSE_ERROR` | `error` | `a` = décalage octet, `detail` = cause (§1.4) |
| 2 | `REPLIQUE_DEADLOCK_FOUND` | `error` | `chain` = cycle de tids, `b` = ressource, `ts` |
| 3 | `REPLIQUE_CULPRIT` | `error` | `tid` = coupable, `detail` = raison |
| 4 | `REPLIQUE_PRIO_INVERSION` | `warning` | `chain` = [victime, intermédiaire, bloqueur], `a` = 1 si héritage appliqué |
| 5 | `REPLIQUE_REPLAY_DETERMINISTIC` | `info` | `hash` = hachage de la séquence (§6) |
| 6 | `REPLIQUE_REPLAY_DIVERGED` | `error` | `ts`, `tid`, `a` = raison attendue, `b` = raison trouvée |
| 7 | `REPLIQUE_TRACE_INCOMPLETE` | `warning` | `a` = événements manquants estimés, `detail` |
| 8 | `REPLIQUE_CORRELATION_OK` | `info` | `detail` = artefacts corrélés |
| 9 | `REPLIQUE_WHATIF` | `info` | `tid`, `a` = nouvelle priorité, `hash` = hachage du rejeu modifié |

Sévérités : `0 info`, `1 warning`, `2 error` — noms canoniques `info`,
`warning`, `error`.

### 3.2 Chaîne de production (figée, aucune étape ne remonte)

```
archive Déclic (.rkv1 + pidin + slog2 + core)
  → replique_kev_parse            (conteneur → événements typés)
  → replique_waitfor_build        (événements → arêtes « T attend U »)
  → replique_deadlock_detect      (cycle → DEADLOCK_FOUND + CULPRIT)
  → replique_prio_inversion       (chaîne → PRIO_INVERSION)
  → replique_replay               (rejeu déterministe → REPLAY_DETERMINISTIC / DIVERGED)
  → rapport (texte canonique, Markdown une page, JSON hôte)
```

Une trace illisible **arrête la chaîne** : `REPLIQUE_PARSE_ERROR` seul, aucun
autre verdict n'est émis.

### 3.3 Désignation du coupable (règle gelée)

Dans un cycle du graphe d'attente, le coupable est le thread du cycle qui, **au
moment du cycle**, détient la ressource acquise le plus anciennement et dont la
priorité de **base** est la plus basse (plus grand numéro — la priorité de base,
pas l'effective : un héritage de priorité atténue l'incident, il ne déplace pas
la responsabilité). À égalité : le plus petit `tid`. Un verdict `REPLIQUE_CULPRIT` accompagne chaque
`REPLIQUE_DEADLOCK_FOUND`, avec `detail` = `holds-resource-<id>-at-prio-<p>`.

### 3.4 Inversion de priorité (règle gelée)

Triple `[V, I, B]` tel que : `V` (haute priorité) est bloqué sur une ressource
détenue par `B` (basse priorité), et un thread `I` de priorité strictement
intermédiaire (`prio(V) < prio(I) < prio(B)` en ordre QNX, c'est-à-dire
`prio(V)` numériquement plus petit) est `RUNNING` ou `READY` pendant le
blocage. `a = 1` si un `PRIO_CHANGE` d'héritage (`subcode = 1`) a élevé `B` au
niveau de `V` avant la fin du blocage, sinon `a = 0`.

---

## 4. Sorties canoniques

Format d'échange **figé**, identique en C et en Python — c'est ce que compare le
test différentiel (`tests/differential_c_python.sh`). Une ligne par objet,
terminée par `\n` (LF), UTF-8, aucune espace de remplissage, toutes les clés
toujours présentes et dans cet ordre.

### 4.1 Ligne d'événement

```
ev|ts=<u64>|pid=<u32>|tid=<u32>|class=<NOM>|sub=<u16>|a=<u32>|b=<u32>|c=<u32>
```

`<NOM>` = nom canonique de §2.1 (majuscules, sans préfixe).

### 4.2 Ligne de verdict

```
vd|code=<NOM>|sev=<info|warning|error>|ts=<u64>|pid=<u32>|tid=<u32>|a=<u32>|b=<u32>|c=<u32>|hash=<u64>|chain=<t1,t2,...>|detail=<texte>
```

`<NOM>` = nom du code sans préfixe `REPLIQUE_` (ex. `DEADLOCK_FOUND`).
`chain` vide → `chain=` (rien après le `=`). `detail` : ASCII, sans `|`, sans
`\n`, sans caractère de contrôle ; vide → `detail=`.

### 4.3 Ligne de décision d'ordonnancement (rejeu)

```
dec|ts=<u64>|tid=<u32>|reason=<NOM>
```

`<NOM>` = raison de §2.6 sans préfixe (`INITIAL`, `PREEMPT`, …).

### 4.4 Ordre de tri (gelé)

- **événements** : ordre du fichier (aucun tri).
- **décisions** : ordre de production du rejeu.
- **verdicts** : tri stable par `(sévérité décroissante, code croissant, ts
  croissant, tid croissant)` ; à égalité complète, ordre de production.
  `PARSE_ERROR` est toujours seul et premier.

### 4.5 Sorties dérivées (hôte uniquement)

- **JSON** (`host/replique/report.py`, `--json`) : objet compact, clés dans
  l'ordre `{"schema":"replique/1","trace":{...},"events":[...],"verdicts":[...],"replay":{...}}` ;
  un verdict JSON porte exactement les clés de §4.2 dans le même ordre
  (`code`, `sev`, `ts`, `pid`, `tid`, `a`, `b`, `c`, `hash`, `chain`, `detail`),
  `chain` en tableau d'entiers.
- **Markdown** « interblocage expliqué en une page » (exigence C6) :
  `docs/REPORT-TEMPLATE.md` — titre, horodatage, empreinte SHA-256 de l'archive,
  chaîne de blocage, coupable, recommandation.
- **HTML** (visualisation du graphe d'attente, C7) : `host/replique/view.py`,
  SVG statique sans dépendance externe.

---

## 5. Modèle d'ordonnanceur (X1)

- Priorités **fixes** de 0 (plus haute) à 255 (plus basse), 256 niveaux.
- `n_cpus = 1` par défaut : à chaque instant, **un seul** thread `RUNNING`
  (le modèle de base ; multi-CPU hors périmètre v1).
- Règle d'élection : parmi les threads `READY`, le plus petit numéro de
  priorité ; à égalité, en politique `FIFO` le thread prêt le plus anciennement,
  en politique `RR` la tête de file tournante (quantum `rr_quantum` ticks,
  `0` = pas de préemption par quantum).
- Un thread de priorité strictement supérieure (numériquement inférieure) qui
  devient `READY` **préempte** immédiatement le thread courant → décision
  `PREEMPT`.
- **Héritage de priorité** (`cfg.inheritance = 1`) : quand un thread `V` se
  bloque sur une ressource détenue par `B` et que `prio(B) > prio(V)`
  numériquement, `B` hérite de `prio(V)` jusqu'à la libération ; la priorité
  rendue est la priorité de base, sauf autre attente en cours (prise maximale).
  **Portée gelée** : en rejeu de fidélité (aucun override), le modèle applique
  les événements `PRIO_CHANGE` présents dans la trace et n'en **synthétise
  aucun** — P2 compare donc le modèle au noyau réel, et une trace sans héritage
  se rejoue sans héritage. Avec au moins un override (mode « et si »,
  `n_overrides > 0`), `inheritance = 1` fait synthétiser l'héritage par le
  modèle : c'est ce qui permet de tester « et si l'héritage était actif ? ».
- **Événements `SCHED`** : ce sont des **observations** du noyau (l'oracle de
  P2). Le modèle ne les applique pas à son état ; il les compare à ses propres
  décisions.
- **Politique `RR`** : modélisée comme `FIFO` + préemption à l'échéance du
  quantum (événement `TIMER`) ; l'ordre des prêts à tourner est
  `(ready_ts, tid)`. Simplification assumée, documentée dans
  `docs/SCHED-MODEL.md`.
- **Message passing** : `MSG_SEND` bloque l'émetteur (raison `MSGSEND`) jusqu'à
  `MSG_RECEIVE` + `MSG_REPLY` du serveur ; `MSG_REPLY` débloque l'émetteur.
- « **Et si** » (X4) : `replique_sched_config.overrides` remplace la priorité de
  base d'un thread avant le rejeu ; chaque modification appliquée produit un
  verdict `REPLIQUE_WHATIF`.

Limites assumées (documentées dans `docs/SCHED-MODEL.md`) : `SCHED_SPORADIC` et
partitions adaptatives (X5) sont modélisés en **réservé non implémenté** en v1 —
une politique `SPORADIC` dans une trace produit `REPLIQUE_TRACE_INCOMPLETE` avec
`detail=sporadic-not-modelled`, jamais un rejeu faux.

---

## 6. Déterminisme (P1) et hachage

Le rejeu est **purement fonctionnel** : mêmes événements + même configuration →
mêmes décisions, dans le même ordre, sans horloge ni aléa.

Hachage gelé — **FNV-1a 64 bits** sur l'octet :

```
hash = 14695981039346656037
pour chaque octet o de la concaténation UTF-8 des lignes « dec|…\n » (§4.3) :
    hash = (hash XOR o) * 1099511628211  mod 2^64
```

Le hachage couvre **toutes** les lignes de décision, chacune terminée par `\n`.
Il est imprimé en décimal (`%llu`) dans les lignes canoniques et en JSON.

`REPLIQUE_REPLAY_DETERMINISTIC` est émis avec ce hachage ; rejouer N fois doit
donner N hachages identiques (test `tests/determinism.sh`, N = 5).

**Fidélité (P2)** : si la trace contient des événements `SCHED`, le rejeu
compare sa propre séquence à la séquence observée (même `ts`, même `tid` élu).
La première divergence produit `REPLIQUE_REPLAY_DIVERGED` nommant l'événement
(`ts`, `tid`, raison attendue/trouvée) ; sans événement `SCHED`, aucun verdict de
fidélité n'est émis. **En mode « et si »** (`n_overrides > 0`) la comparaison de
fidélité est **sautée** : la séquence diverge de la trace par construction, et le
verdict `REPLIQUE_WHATIF` porte le nouveau hachage.

---

## 7. Règles de construction

- **Portabilité** : `core/` est C11 **pur** — aucun `fopen`, aucune horloge
  (`time`, `clock_gettime`), aucun socket, aucun en-tête QNX. Le vérificateur
  `.github/checks/purity_guard.py` échoue sur toute inclusion interdite.
- **Taille** : tout fichier < 500 lignes, toute fonction < 50 lignes
  (`.github/checks/size_guard.py`, vérifié en CI, exigence S9).
- **Compilateur** : `gcc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic`.
- **Python** : 3.10+, **stdlib seule** (aucune dépendance tierce).
- **Déterminisme des sorties** : jamais d'horodatage réel ni de chemin absolu
  dans les sorties canoniques (le rapport Markdown horodaté est produit par
  l'hôte, hors différentiel).
- **Makefile** : `make test` vert sans QNX, sans cible, sans licence (S8).
  Cibles : `build`, `test`, `differential`, `fixtures`, `guard`, `clean`.

### 4.6 Ordres de clés JSON gelés (hôte seul)

- Racine : `schema`, `trace`, `events`, `verdicts`, `replay`.
- `trace` : `flags`, `n_events`, `time_unit`, `crc32`, `sha256` (empreinte du
  fichier complet, hexadécimal minuscule).
- un événement : `ts`, `pid`, `tid`, `class` (**nom** canonique §2.1), `sub`,
  `a`, `b`, `c`.
- un verdict : `code` (**nom** sans préfixe), `sev` (**nom**), `ts`, `pid`,
  `tid`, `a`, `b`, `c`, `hash`, `chain` (tableau d'entiers), `detail`.
- `replay` : `hash`, `n_decisions`, `diverged`, `incomplete`, `decisions`
  (tableau de `{"ts","tid","reason"}`, `reason` = **nom** §2.6).

JSON compact : séparateurs `,` et `:` sans espace, `ensure_ascii=False`,
une ligne terminée par `\n`.

### 4.7 Surfaces de commande gelées

```
python3 -m replique.cli dump    FICHIER [--json]
python3 -m replique.cli analyze FICHIER [--json] [--whatif TID=PRIO]…
python3 -m replique.cli replay  FICHIER [--json] [--hash] [--step] [--whatif TID=PRIO]…
python3 -m replique.cli gen     [SCÉNARIO|--all] [--out RÉP]
python3 -m replique.cli view    FICHIER [--out FICHIER.svg]

build/kev-dump   FICHIER                                  → lignes « ev| »
build/analyze    FICHIER [--whatif TID=PRIO]…             → lignes « vd| »
build/replay-dump FICHIER [--whatif TID=PRIO]… [--hash]   → lignes « dec| » ou « hash=<u64> »
```

Les trois pilotes C et les commandes Python correspondantes produisent **les
mêmes octets** sur la même trace : c'est l'objet de
`tests/differential_c_python.sh`.

Codes de retour des pilotes (C et Python, identiques) : `0` sortie conforme
(y compris un verdict `PARSE_ERROR` imprimé par `analyze`) ; `2` fichier
illisible ; `3` trace corrompue pour `dump`/`replay` (message
`replique: PARSE_ERROR <detail> offset=<n>` sur stderr, stdout vide).

### 4.8 API Python gelée (host/replique/)

Le jumeau Python expose exactement ces noms (le différentiel et `commands.py`
en dépendent). Signature complète dans les docstrings de chaque module.

| Module | API |
|---|---|
| `model` | constantes et dataclasses `Header`, `Event`, `Verdict`, `Decision`, `Edge`, `Thread`, `SchedConfig`, `ReplayResult`, exception `ParseError(cause, offset)` |
| `event` | `class_name`, `state_name`, `block_reason_name`, `sched_reason_name`, `verdict_name`, `severity_name`, `cause_name`, `verdict_severity`, `class_index` |
| `report` | `event_line`, `verdict_line`, `decision_line`, `events_text`, `verdicts_text`, `decisions_text`, `write_text`, `sanitize_detail`, `sha256_hex`, `event_json`, `verdict_json`, `replay_json`, `trace_json`, `to_json`, `document` |
| `kev` | `parse(blob) -> (Header, list[Event])` (lève `ParseError`), `write(events, flags, time_unit) -> bytes`, `read_file(path) -> (Header, list[Event], bytes)`, `crc32(blob) -> int`, `fnv1a64(data: bytes) -> int` |
| `kevgen` | `NAMES: tuple[str, ...]`, `scenario(name) -> list[Event]`, `write(name, out_dir) -> str`, `write_all(out_dir) -> list[str]` |
| `sched` | `default_config() -> SchedConfig`, `override_prio(cfg, tid, base) -> int`, classe `Scheduler(cfg)` avec `threads: dict[int, Thread]`, `running_tid`, `now`, `intern(tid, pid)`, `find(tid)`, `pick(now)`, `apply(ev)`, `inherit(holder, waiter)`, `release(holder)` |
| `replay` | `run(events, cfg) -> ReplayResult`, `hash_decisions(decisions) -> int`, `check_fidelity(events, decisions) -> Verdict \| None`, `step_through(events, cfg, out)` |
| `deadlock` | `rebuild_states(events) -> list[Thread]`, `build_waitfor(events) -> list[Edge]`, `detect(events) -> list[Verdict]`, `detect_prio_inversion(events) -> list[Verdict]`, `find_cycle(edges) -> list[int]` |
| `verdict` | `make(code, **champs) -> Verdict`, `sort_verdicts(list[Verdict]) -> None` (stable, §4.4), `pipeline_blob(blob, cfg) -> (list[Verdict], ReplayResult)`, `pipeline_events(events, cfg, header, blob) -> (list[Verdict], ReplayResult)` |
| `view` | `render_svg(events, verdicts) -> str` |
| `markdown_report` | `render(header, blob, events, verdicts, replay) -> str` |
| `commands` | `dump`, `analyze`, `replay`, `gen`, `view` (codes de retour §4.7) |

Aucun module ne lit l'horloge ni n'écrit de chemin absolu dans une sortie
canonique.
