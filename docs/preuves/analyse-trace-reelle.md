# Preuve — autopsie sur la trace QNX 8.0 réelle

Trace : `docs/preuves/trace-qnx800-wide.kev` (823 Ko, capturée dans l'invité
QNX 8.0.0 avec `tracelogger -w`, programme `bug-deadlock` en cours).

## Ce que la chaîne produit

| Mesure | Valeur |
|---|---|
| événements logiques dans le `.kev` | 21 652 |
| événements RKV1 mappés | 13294 |
| décisions de rejeu | 4 418 |
| hachage FNV-1a du rejeu | `hash=3463212668984122407` |
| états « bloqué » | 2907 |
| changements de priorité | 68 |
| envois de message | 714 |

Verdicts (identiques en C et en Python, octet pour octet) :

```
vd|code=OK|sev=info|ts=0|pid=0|tid=0|a=13294|b=0|c=0|hash=0|chain=|detail=parse-ok
vd|code=REPLAY_DETERMINISTIC|sev=info|ts=0|pid=0|tid=0|a=0|b=0|c=0|hash=3463212668984122407|chain=|detail=decisions-4418
```

## Ce que la trace dit du bug

Les deux threads de travail du processus fautif (pid 180232) sont
**bloqués** :

- tid 2 : priorité de base observée 10, changements 10→60, 3 blocages (raison TIMER, REPLYWAIT)
- tid 3 : priorité de base observée 10, changements 10→50, 50→60, 60→50, 1 blocages (raison REPLYWAIT)

Le thread principal (tid 1) attend la fin des deux `pthread_join`
(35 états bloqués, mêmes raisons).

Ils ne bloquent pas sur un mutex nommé mais sur un **envoi de message au
serveur de synchronisation** — en QNX 8, un mutex `pthread` contentieux passe
par un `MsgSend` au serveur de sync (oracle `traceprinter` : `KER_CALL
:MSG_SENDV` puis `THREAD :THREPLY`).

La trace montre aussi l'**héritage de priorité** : 68
changements de priorité au total, dont 58 sur le thread du
serveur de synchronisation (pid 3, tid 1), qui monte à la priorité 60 — celle
du client bloqué — puis la rend.

## La limite, prouvée (et non masquée)

Le graphe d'attente **ne peut pas** être construit sur cette trace : la trace
n'expose pas l'identifiant de l'objet de synchronisation. Le `MsgSend` vers le
serveur de sync est tracé avec l'en-tête du message (`0x102`, `0x27`, `0x0`),
pas avec son corps — l'identifiant du mutex y vit, et il n'est pas recopié.
Aucun `SYNC_MUTEX_LOCK` n'apparaît dans la capture, et les seuls événements
`SYNC_MUTEX_LOCK` présents (2) sont des sorties de appel interrompues par le
tuerre du processus (`errno 39`).

Conséquence, assumée : **aucun verdict `DEADLOCK_FOUND`** n'est produit sur
cette trace — le projet ne prétend pas le contraire. Le noyau dit *ce qui se
passe*, pas *pourquoi*.

Ce que la trace **expose** en revanche, et qui suffit déjà : le canal
(`rcvid`) de chaque envoi et le pid de son détenteur — de quoi reconstruire
la causalité des messages (trou R5), pas l'identité du mutex.

## Reproduire

```sh
make differential-trace     # C == Python sur la trace réelle (3 comparaisons)
python3 tests/preuve_trace_reelle.py   # régénère les deux artefacts
```
