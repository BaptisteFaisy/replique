"""kev2rkv1.py — adaptateur `.kev` QNX 8.0 (binaire) → conteneur `RKV1`.

Tranche l'essai E1 du cahier des charges : le `.kev` est lu **directement**
(format documenté par `sys/trace.h`), sans passer par le texte de `traceprinter`.
La correspondance est un **sous-ensemble assumé** du modèle `RKV1`
(CONTRACT.md §2) : cycle de vie process/thread, états de thread, message
passing, changements de priorité.

Ce que la trace donne réellement (vérifié contre l'oracle `traceprinter` sur
10 989 événements d'état de thread) :

  * un événement d'état de thread porte `(pid, tid)` **et** `(priorité,
    politique)` — la seconde moitié de la combinaison ;
  * un événement de message porte `(rcvid, pid)` pour `SND`/`REC`, et
    `(tid, pid)` pour `REPLY`/`ERROR` ; le `pid` est celui du **détenteur du
    canal** (le serveur), pas celui de l'expéditeur ;
  * la trace n'expose **ni** identifiant de mutex, **ni** valeur de sémaphore,
    **ni** numéro de séquence : ces champs restent à 0.

Usage :
    python3 -m replique.kev2rkv1 trace.kev -o trace.rkv1
    python3 -m replique.kev2rkv1 trace.kev            # résumé à l'écran
"""
from __future__ import annotations

import argparse
import sys
from typing import Dict, List, Tuple

from replique import kev as rkv
from replique import kev_reader as kr
from replique import model as m

# états QNX -> (subcode RKV1, raison de blocage RKV1)
_STATE_MAP = {
    0: (0, 0),   # DEAD
    1: (1, 0),   # RUNNING
    2: (2, 0),   # READY
    3: (4, 0),   # STOPPED
    4: (3, 2), 5: (3, 2), 7: (3, 2), 8: (3, 2),   # SEND / RECEIVE / MQ_*
    6: (3, 6),                                     # REPLY -> REPLYWAIT
    9: (3, 0),                                     # WAITPAGE
    10: (3, 0), 11: (3, 0), 18: (3, 0), 22: (3, 0),  # SIGSUSPEND/W/WAITCTX/PIPE
    12: (3, 4),                                    # NANOSLEEP -> TIMER
    13: (3, 1), 14: (3, 1), 19: (3, 1), 20: (3, 1), 21: (3, 1), 26: (3, 1),  # MUTEX/CONDVAR/RWLOCK/BARRIER/MUON
    15: (3, 7),                                    # JOIN
    16: (3, 5),                                    # INTR -> INTERRUPT
    17: (3, 3),                                    # SEM -> SEMAPHORE
    30: (3, 2),                                    # SEND_NOTIFY -> MSGSEND
}
# politiques QNX (sched.h) -> politique du contrat (§2.5)
_POLICY_MAP = {1: 0, 2: 1, 4: 2}        # SCHED_FIFO/RR/SPORADIC
_COMM_MAP = {0: m.EV_MSG_SEND, 2: m.EV_MSG_RECEIVE, 10: m.EV_MSG_REPLY,
             11: m.EV_MSG_ERROR}


def _event(ts, pid, tid, cls, sub, a=0, b=0, c=0):
    return m.Event(ts, pid & 0xFFFFFFFF, tid & 0xFFFFFFFF, cls, sub & 0xFFFF,
                   a & 0xFFFFFFFF, b & 0xFFFFFFFF, c & 0xFFFFFFFF)


def _prio_policy(e):
    """(priorité, politique) porté par un événement d'état de thread."""
    if len(e.words) >= 4:
        return e.words[2], e.words[3]
    return None, None


def _first_priorities(events) -> Dict[Tuple[int, int], Tuple[int, int]]:
    """Priorité/politique vues à la PREMIÈRE activité de chaque thread."""
    table: Dict[Tuple[int, int], Tuple[int, int]] = {}
    for e in events:
        if e.cls != kr.CLS_PROC_THREAD or e.code not in _STATE_MAP:
            continue
        prio, policy = _prio_policy(e)
        if prio is None:
            continue
        key = (e.pid, e.tid)
        if key not in table:
            table[key] = (prio, policy)
    return table


def _map_thread(e, out, first_prio, seen_prio):
    pid, tid = e.pid, e.tid
    if e.code == kr.TH_CREATE:
        prio, policy = first_prio.get((pid, tid), (None, None))
        sub = _POLICY_MAP.get(policy, 0) if policy is not None else 0
        out.append(_event(e.ts, pid, tid, m.EV_THREAD_CREATE, sub,
                          prio if prio is not None else 0))
    elif e.code == kr.TH_DESTROY:
        out.append(_event(e.ts, pid, tid, m.EV_THREAD_DESTROY, 0, 0))
    elif e.code in _STATE_MAP:
        sub, reason = _STATE_MAP[e.code]
        out.append(_event(e.ts, pid, tid, m.EV_THREAD_STATE, sub, reason))
        prio, _policy = _prio_policy(e)
        if prio is not None:
            key = (pid, tid)
            previous = seen_prio.get(key)
            if previous is not None and previous != prio:
                out.append(_event(e.ts, pid, tid, m.EV_PRIO_CHANGE, 0,
                                  prio, previous, 0))
            seen_prio[key] = prio
    elif e.code in (kr.P_CREATE, kr.P_CREATE_NAME):
        ppid = e.words[0] if e.words else 0
        p = e.words[1] if len(e.words) > 1 else 0
        out.append(_event(e.ts, p, 0, m.EV_PROCESS_CREATE, 0, ppid))
    elif e.code in (kr.P_DESTROY, kr.P_DESTROY_NAME):
        p = e.words[1] if len(e.words) > 1 else 0
        out.append(_event(e.ts, p, 0, m.EV_PROCESS_DESTROY, 0, 0))
    # TH_NAME : informationnel, hors RKV1 — ignoré


def _map_comm(e, out):
    cls = _COMM_MAP.get(e.code)
    if cls is None:
        return
    w0 = e.words[0] if e.words else 0
    w1 = e.words[1] if len(e.words) > 1 else 0
    if e.code in (0, 2):
        # w0 = rcvid (canal), w1 = pid du détenteur du canal.
        # MSG_SEND : ce détenteur EST le destinataire -> a = w1.
        # MSG_RECEIVE : le contrat veut le pid SOURCE, que la trace n'expose
        # pas -> a = 0 (le pid du canal reste dans la colonne pid).
        out.append(_event(e.ts, w1, 0, cls, 0, w1 if e.code == 0 else 0))
    else:
        # REPLY / ERROR : w0 = tid du répondant, w1 = pid du répondant.
        out.append(_event(e.ts, w1, w0, cls, 0, 0))


def _map_system(e, out):
    if e.cls == kr.CLS_INT:
        out.append(_event(e.ts, e.pid, e.tid, m.EV_INTERRUPT, 0, 0))
    elif e.code == 0x12:  # _NTO_TRACE_SYS_TIMER
        out.append(_event(e.ts, e.pid, e.tid, m.EV_TIMER, 0, 0))


def convert(blob: bytes) -> bytes:
    """Décode un `.kev` et rend un contenu `RKV1` (32+32*n octets)."""
    hdr, events = kr.logical_events(blob)
    first_prio = _first_priorities(events)
    seen_prio: Dict[Tuple[int, int], int] = {}
    out: List[m.Event] = []
    for e in events:
        if e.cls == kr.CLS_PROC_THREAD:
            _map_thread(e, out, first_prio, seen_prio)
        elif e.cls == kr.CLS_COMM:
            _map_comm(e, out)
        elif e.cls in (kr.CLS_SYSTEM, kr.CLS_INT):
            _map_system(e, out)
    out.sort(key=lambda x: x.ts)
    return rkv.write(out, flags=0, time_unit=hdr.cycles_per_sec or m.TIME_UNIT_NS)


def summary(blob: bytes) -> str:
    hdr, events = kr.logical_events(blob)
    rkv_blob = convert(blob)
    h2, mapped = rkv.parse(rkv_blob)
    return ("kev : %d événements logiques, cycles/s=%d, CPUs=%d\n"
            "rkv1: %d événements mappés (%d octets)\n"
            % (len(events), hdr.cycles_per_sec, hdr.cpu_num, h2.n_events, len(rkv_blob)))


def main(argv=None):
    ap = argparse.ArgumentParser(description=".kev QNX 8.0 -> RKV1")
    ap.add_argument("input", help="fichier .kev d'entrée")
    ap.add_argument("-o", "--output", help="fichier .rkv1 de sortie")
    args = ap.parse_args(argv)
    blob = open(args.input, "rb").read()
    if args.output:
        data = convert(blob)
        with open(args.output, "wb") as fh:
            fh.write(data)
        sys.stderr.write(summary(blob))
    else:
        sys.stdout.write(summary(blob))
    return 0


if __name__ == "__main__":
    sys.exit(main())