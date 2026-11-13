"""kev2rkv1.py — adaptateur `.kev` QNX 8.0 (binaire) → conteneur `RKV1`.

Tranche l'essai E1 du cahier des charges : le `.kev` est lu **directement**
(format documenté par `sys/trace.h`, cf. `kev_reader.py`), sans passer par le
texte de `traceprinter`. La correspondance est un **sous-ensemble** assumé du
modèle `RKV1` (CONTRACT.md §2) : cycle de vie process/thread, états de thread,
message passing. Les champs non exposés par la trace (id de mutex, valeur de
sémaphore, numéro de séquence) restent à 0 — la trace dit *ce qui se passe*,
pas *pourquoi* (c'est le postulat du projet).

Usage :
    python3 -m replique.kev2rkv1 trace.kev -o trace.rkv1
    python3 -m replique.kev2rkv1 trace.kev            # résumé à l'écran
"""
from __future__ import annotations

import argparse
import sys
from typing import List

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
_COMM_MAP = {0: m.EV_MSG_SEND, 2: m.EV_MSG_RECEIVE, 10: m.EV_MSG_REPLY,
             11: m.EV_MSG_ERROR}
_CTL_TIME = kr.CTL_TIME


def _event(ts, pid, tid, cls, sub, a=0, b=0, c=0):
    return m.Event(ts, pid & 0xFFFFFFFF, tid & 0xFFFFFFFF, cls, sub & 0xFFFF,
                   a & 0xFFFFFFFF, b & 0xFFFFFFFF, c & 0xFFFFFFFF)


def _map_thread(e, out):
    pid, tid = e.pid, e.tid
    if e.code == kr.TH_CREATE:
        out.append(_event(e.ts, pid, tid, m.EV_THREAD_CREATE, 0, 0))
    elif e.code == kr.TH_DESTROY:
        out.append(_event(e.ts, pid, tid, m.EV_THREAD_DESTROY, 0, 0))
    elif e.code in _STATE_MAP:
        sub, reason = _STATE_MAP[e.code]
        out.append(_event(e.ts, pid, tid, m.EV_THREAD_STATE, sub, reason))
    elif e.code in (kr.P_CREATE, kr.P_CREATE_NAME):
        ppid = e.words[0] if e.words else 0
        p = e.words[1] if len(e.words) > 1 else 0
        out.append(_event(e.ts, p, 0, m.EV_PROCESS_CREATE, 0, ppid))
    elif e.code in (kr.P_DESTROY, kr.P_DESTROY_NAME):
        ppid = e.words[0] if e.words else 0
        p = e.words[1] if len(e.words) > 1 else 0
        out.append(_event(e.ts, p, 0, m.EV_PROCESS_DESTROY, 0, 0))
    # TH_NAME : informationnel, hors RKV1 — ignoré


def _map_comm(e, out):
    cls = _COMM_MAP.get(e.code)
    if cls is None:
        return
    tid = e.words[0] if e.words else 0  # RCVID/tid côté émetteur (partiel)
    out.append(_event(e.ts, 0, tid, cls, 0, 0))


def _map_system(e, out):
    if e.cls == kr.CLS_INT:
        out.append(_event(e.ts, e.pid, e.tid, m.EV_INTERRUPT, 0, 0))
    elif e.code == 0x12:  # _NTO_TRACE_SYS_TIMER
        out.append(_event(e.ts, e.pid, e.tid, m.EV_TIMER, 0, 0))


def convert(blob: bytes) -> bytes:
    """Décode un `.kev` et rend un contenu `RKV1` (32+32*n octets)."""
    hdr, events = kr.logical_events(blob)
    out: List[m.Event] = []
    for e in events:
        if e.cls == kr.CLS_PROC_THREAD:
            _map_thread(e, out)
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