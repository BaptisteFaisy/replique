"""kev_reader.py — lecteur du format binaire `.kev` QNX 8.0 (essai E1 tranché).

Le format est **documenté** par `sys/trace.h` du SDP 8.0 :

  fichier  = en-tête ASCII `TRACE_*::valeur...TRACE_HEADER_END::`
             + page système (`TRACE_SYSPAGE_LEN` octets)
             + une suite d'enregistrements de 16 octets (`traceevent_t`) ;
  record   = header (4 o) + data[3] (12 o) ;
  header   = [struct:2][inutilisé][cpu:6 bits 24-29][classe:5 bits 10-14][code:10 bits 0-9].

Les événements « combinés » (noms, états de thread) occupent plusieurs records :
le champ `struct` vaut 0 (simple), 1 (CB début), 2 (CC continuation), 3 (CE fin).
Le champ `data[0]` porte toujours la partie basse de l'horloge ; la partie haute
(msb) vient des événements CONTROL:TIME. On reconstitue donc (msb,lsb) par CPU,
avec détection de débordement des 32 bits bas.
"""
from __future__ import annotations

import re
import struct
from dataclasses import dataclass, field
from typing import Iterator, List

HEADER_END = b"TRACE_HEADER_END::"
RECORD = struct.Struct("<IIII")  # header, data0, data1, data2

# classes internes (trace.h, _TRACE_*_C >> 10)
CLS_EMPTY = 0
CLS_CONTROL = 1
CLS_KERCALL = 2
CLS_INT = 3
CLS_PROC_THREAD = 4
CLS_SYSTEM = 5
CLS_USER = 6
CLS_COMM = 7

# événements de contrôle
CTL_TIME = 1
CTL_BUFFER = 2
CTL_BUFFER_END = 4
CTL_DROPPED = 8

# événements process/thread (trace.h + sys/states.h)
TH_CREATE = 24
TH_DESTROY = 25
TH_NAME = 0x140          # _TRACE_PR_TH_NAME_T (5<<6)
P_CREATE = 0x40
P_DESTROY = 0x80
P_CREATE_NAME = 0xC0
P_DESTROY_NAME = 0x100

# états de thread (sys/states.h) -> nom lisible
STATE_NAMES = {
    0: "DEAD", 1: "RUNNING", 2: "READY", 3: "STOPPED", 4: "SEND",
    5: "RECEIVE", 6: "REPLY", 7: "MQ_SEND", 8: "MQ_RECEIVE", 9: "WAITPAGE",
    10: "SIGSUSPEND", 11: "SIGWAITINFO", 12: "NANOSLEEP", 13: "MUTEX",
    14: "CONDVAR", 15: "JOIN", 16: "INTR", 17: "SEM", 18: "WAITCTX",
    19: "RWLOCK_READ", 20: "RWLOCK_WRITE", 21: "BARRIER", 22: "PIPE",
    24: "CREATE", 25: "DESTROY", 26: "MUON_MUTEX", 27: "TRACEBUFFER",
}


@dataclass
class Header:
    keywords: dict
    syspage_len: int
    cycles_per_sec: int
    cpu_num: int
    start: int  # offset du 1er enregistrement


@dataclass
class Event:
    """Événement logique (un ou plusieurs records combinés)."""
    ts: int
    cpu: int
    cls: int
    code: int
    words: List[int] = field(default_factory=list)  # data1,data2 de chaque record

    @property
    def pid(self):
        return self.words[0] if self.words else 0

    @property
    def tid(self):
        return self.words[1] if len(self.words) > 1 else 0


def read_header(blob: bytes) -> Header:
    i = blob.find(HEADER_END)
    if i < 0:
        raise ValueError("en-tête .kev introuvable (pas de TRACE_HEADER_END::)")
    text = blob[:i].decode("latin-1")
    kw = dict(re.findall(r"(TRACE_[A-Z0-9_]+)::([^\n]*?)(?=TRACE_[A-Z0-9_]+::|$)", text))
    sp = int(kw.get("TRACE_SYSPAGE_LEN", "2464"))
    cps = int(kw.get("TRACE_CYCLES_PER_SEC", "1000000000"))
    cpu = int(kw.get("TRACE_CPU_NUM", "1"))
    start = i + len(HEADER_END) + sp
    return Header(kw, sp, cps, cpu, start)


def _unwrap(msb, prev_ts, lsb):
    """Reconstitue un horodatage 64 bits (msb,lsb) avec détection de débordement."""
    ts = (msb << 32) | lsb
    if prev_ts is not None and ts + 0x80000000 < prev_ts:
        msb += 1
        ts = (msb << 32) | lsb
    return msb, ts


def iter_records(blob: bytes, hdr: Header):
    """Parcourt les records bruts en reconstituant l'horloge par CPU."""
    msb = {}
    prev = {}
    n = (len(blob) - hdr.start) // 16
    for k in range(n):
        header, d0, d1, d2 = RECORD.unpack_from(blob, hdr.start + k * 16)
        cpu = (header & 0x3F000000) >> 24
        cls = (header & (0x1F << 10)) >> 10
        code = header & 0x3FF
        struct_type = (header >> 30) & 0x3
        if cls == CLS_CONTROL and code == CTL_TIME:
            msb[cpu] = d1
            prev[cpu] = (d1 << 32) | d0
            yield d0, cpu, cls, code, struct_type, d1, d2
            continue
        m, ts = _unwrap(msb.get(cpu, 0), prev.get(cpu), d0)
        msb[cpu] = m
        prev[cpu] = ts
        yield ts, cpu, cls, code, struct_type, d1, d2


def logical_events(blob: bytes) -> tuple[Header, List[Event]]:
    """Rend les événements logiques (combine résolu), triés par horodatage."""
    hdr = read_header(blob)
    out: List[Event] = []
    cur = None  # événement combiné en cours
    for ts, cpu, cls, code, st, d1, d2 in iter_records(blob, hdr):
        if cls == CLS_CONTROL:
            continue  # TIME/BUFFER/BUFFER_END/DROPPED : structure, pas d'événement
        if st == 0:  # simple
            out.append(Event(ts, cpu, cls, code, [d1, d2]))
            cur = None
        elif st == 1:  # CB : début d'une combinaison
            cur = Event(ts, cpu, cls, code, [d1, d2])
        elif st == 2 and cur is not None:  # CC : continuation
            cur.words.extend([d1, d2])
        elif st == 3:  # CE : fin
            if cur is not None and cur.cls == cls and cur.code == code:
                cur.words.extend([d1, d2])
                out.append(cur)
            else:
                out.append(Event(ts, cpu, cls, code, [d1, d2]))
            cur = None
        else:
            out.append(Event(ts, cpu, cls, code, [d1, d2]))
    out.sort(key=lambda e: e.ts)
    return hdr, out


def name_of(event: Event) -> str:
    """Reconstitue un nom ASCII depuis les mots d'un événement NAME combiné.

    Convention observée : le 1er record porte (ppid/pid) ou (pid/tid), les
    suivants portent des morceaux de 4 octets ; le dernier est bourré de zéros.
    """
    raw = b""
    for w in event.words[2:]:
        raw += struct.pack("<I", w & 0xFFFFFFFF)
    return raw.split(b"\x00", 1)[0].decode("latin-1", "replace")