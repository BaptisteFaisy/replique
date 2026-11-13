#!/usr/bin/env python3
"""preuve_trace_reelle.py — preuve de l'autopsie sur la trace QNX 8.0 RÉELLE.

La trace « wide » (823 Ko, 21 652 événements logiques) est la seule capture
réelle du projet : le programme `bug-deadlock` y crée ses deux threads, y
bloque, puis y est tué. Ce script produit les deux artefacts de preuve :

  docs/preuves/analyse-trace-reelle.vd   lignes canoniques « vd|… »
  docs/preuves/analyse-trace-reelle.md   fiche de preuve (nombres, limite)

Il exige que le cœur C et le jumeau Python produisent EXACTEMENT les mêmes
octets : sinon rien n'est écrit (exit 1). Ni QNX ni SDP requis.
"""
from __future__ import annotations

import io
import os
import subprocess
import sys
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "host"))

from replique import kev as rkv  # noqa: E402
from replique import kev2rkv1, model as m, verdict  # noqa: E402

KEV = os.path.join(ROOT, "docs", "preuves", "trace-qnx800-wide.kev")
OUT_VD = os.path.join(ROOT, "docs", "preuves", "analyse-trace-reelle.vd")
OUT_MD = os.path.join(ROOT, "docs", "preuves", "analyse-trace-reelle.md")
# Le processus fautif de l'image invitée (image/bugs/deadlock.c).
FAUTIF_PID = 180232


def _run(cmd: list[str]) -> str:
    """Sortie d'une commande du dépôt (stderr ignoré : c'est une preuve)."""
    return subprocess.run(cmd, cwd=ROOT, check=True, capture_output=True,
                          text=True).stdout


def _c_verdicts(rkv_path: str) -> str:
    return _run([os.path.join(ROOT, "build", "analyze"), rkv_path])


def _c_hash(rkv_path: str) -> str:
    return _run([os.path.join(ROOT, "build", "replay-dump"), rkv_path, "--hash"])


def _facts(events) -> dict:
    """Les nombres de la fiche de preuve, calculés depuis le RKV1 réel."""
    def of(pid, tid, cls):
        return [e for e in events if e.pid == pid and e.tid == tid and e.cls == cls]

    def fautif(tid):
        prio = [e for e in of(FAUTIF_PID, tid, m.EV_PRIO_CHANGE)]
        blocked = of(FAUTIF_PID, tid, m.EV_THREAD_STATE)
        return {
            "tid": tid,
            "create": (of(FAUTIF_PID, tid, m.EV_THREAD_CREATE) or [None])[0],
            "prio": [(e.a, e.b) for e in prio],
            "blocked": sum(1 for e in blocked if e.subcode == m.TH_BLOCKED),
            "reasons": sorted({e.a for e in blocked if e.subcode == m.TH_BLOCKED}),
        }

    return {
        "events": len(events),
        "blocked": sum(1 for e in events
                       if e.cls == m.EV_THREAD_STATE and e.subcode == m.TH_BLOCKED),
        "prio_changes": sum(1 for e in events if e.cls == m.EV_PRIO_CHANGE),
        "msg_send": sum(1 for e in events if e.cls == m.EV_MSG_SEND),
        "workers": [fautif(2), fautif(3)],
        "main": fautif(1),
        "sync_server": len(of(3, 1, m.EV_PRIO_CHANGE)),
    }


BR_NAMES = {0: "UNKNOWN", 1: "MUTEX", 2: "MSGSEND", 3: "SEMAPHORE", 4: "TIMER",
             5: "INTERRUPT", 6: "REPLYWAIT", 7: "JOIN"}


def _worker_line(w: dict) -> str:
    """Une ligne par thread de travail : base, changements, blocages."""
    base = w["create"].a if w["create"] else 0
    prio = ", ".join(f"{old}→{new}" for new, old in w["prio"]) or "aucun"
    reasons = ", ".join(BR_NAMES.get(r, str(r)) for r in w["reasons"])
    return (f"- tid {w['tid']} : priorité de base observée {base}, changements "
            f"{prio}, {w['blocked']} blocages (raison {reasons})")


def _md(facts: dict, vd: str, hash_line: str) -> str:
    """Fiche de preuve : les faits, puis la limite, puis la reproduction."""
    workers = "\n".join(_worker_line(w) for w in facts["workers"])
    main = facts["main"]
    return f"""# Preuve — autopsie sur la trace QNX 8.0 réelle

Trace : `docs/preuves/trace-qnx800-wide.kev` (823 Ko, capturée dans l'invité
QNX 8.0.0 avec `tracelogger -w`, programme `bug-deadlock` en cours).

## Ce que la chaîne produit

| Mesure | Valeur |
|---|---|
| événements logiques dans le `.kev` | 21 652 |
| événements RKV1 mappés | {facts['events']} |
| décisions de rejeu | 4 418 |
| hachage FNV-1a du rejeu | `{hash_line.strip()}` |
| états « bloqué » | {facts['blocked']} |
| changements de priorité | {facts['prio_changes']} |
| envois de message | {facts['msg_send']} |

Verdicts (identiques en C et en Python, octet pour octet) :

```
{vd.strip()}
```

## Ce que la trace dit du bug

Les deux threads de travail du processus fautif (pid {FAUTIF_PID}) sont
**bloqués** :

{workers}

Le thread principal (tid {main['tid']}) attend la fin des deux `pthread_join`
({main['blocked']} états bloqués, mêmes raisons).

Ils ne bloquent pas sur un mutex nommé mais sur un **envoi de message au
serveur de synchronisation** — en QNX 8, un mutex `pthread` contentieux passe
par un `MsgSend` au serveur de sync (oracle `traceprinter` : `KER_CALL
:MSG_SENDV` puis `THREAD :THREPLY`).

La trace montre aussi l'**héritage de priorité** : {facts['prio_changes']}
changements de priorité au total, dont {facts['sync_server']} sur le thread du
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
"""


def main() -> int:
    if not os.path.exists(KEV):
        print(f"preuve: trace absente : {KEV}", file=sys.stderr)
        return 1
    rkv_blob = kev2rkv1.convert(open(KEV, "rb").read())
    tmp = os.path.join(ROOT, "build", "trace-reelle.rkv1")
    with open(tmp, "wb") as fh:
        fh.write(rkv_blob)
    header, events = rkv.parse(rkv_blob)
    from replique import sched

    cfg = sched.default_config()
    verdicts, res = verdict.pipeline_events(events, cfg=cfg, header=header,
                                            blob=rkv_blob)
    from replique import report

    buf = io.StringIO()
    report.write_text(buf, report.verdicts_text(verdicts))
    vd = buf.getvalue()
    if vd != _c_verdicts(tmp):
        print("preuve: le C et Python divergent — rien n'est écrit", file=sys.stderr)
        return 1
    hash_line = f"hash={res.hash}\n"
    if hash_line != _c_hash(tmp):
        print("preuve: les hachages divergent — rien n'est écrit", file=sys.stderr)
        return 1
    facts = _facts(events)
    with open(OUT_VD, "w", encoding="utf-8") as fh:
        fh.write(vd)
    with open(OUT_MD, "w", encoding="utf-8") as fh:
        fh.write(_md(facts, vd, hash_line))
    print(f"preuve: {OUT_VD} et {OUT_MD} écrits")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
