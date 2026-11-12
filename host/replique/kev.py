"""kev.py — conteneur RKV1, jumeau exact de core/src/kev.c (CONTRACT.md §1).

Mêmes offsets, mêmes causes d'erreur, mêmes valeurs de référence : le
différentiel C↔Python compare les sorties ligne à ligne.
"""
from __future__ import annotations

import struct
import zlib
from typing import List, Tuple

from replique import model as m

_HEADER = struct.Struct("<4s7I")          # magic, version, flags, n, unit, crc, r0, r1
_RECORD = struct.Struct("<QIIHHIII")      # ts, pid, tid, class, sub, a, b, c
FNV_BASE = 14695981039346656037
FNV_PRIME = 1099511628211
_U64 = (1 << 64) - 1


def crc32(blob: bytes) -> int:
    """CRC-32/ISO-HDLC — identique à zlib.crc32 et à replique_crc32 (C)."""
    return zlib.crc32(blob) & 0xFFFFFFFF


def fnv1a64(data: bytes) -> int:
    """FNV-1a 64 bits (CONTRACT.md §6) — identique à replique_fnv1a64 (C)."""
    h = FNV_BASE
    for byte in data:
        h = ((h ^ byte) * FNV_PRIME) & _U64
    return h


def parse(blob: bytes) -> Tuple[m.Header, List[m.Event]]:
    """Décode un conteneur RKV1 ; lève ParseError(cause, offset) au 1er incident.

    Ordre de validation gelé (CONTRACT.md §1.4) : longueur d'en-tête, magic,
    version, bits réservés, longueur totale, CRC, classe, monotonie des ts.
    """
    if blob is None or len(blob) < m.HEADER_SIZE:
        raise m.ParseError(m.CAUSE_SHORT_HEADER, len(blob or b""))
    magic, version, flags, n_events, time_unit, crc, _r0, _r1 = _HEADER.unpack_from(blob, 0)
    if magic != m.MAGIC:
        raise m.ParseError(m.CAUSE_BAD_MAGIC, 0)
    if version != m.VERSION:
        raise m.ParseError(m.CAUSE_BAD_VERSION, 4)
    if flags & m.FLAG_RESERVED_MASK:
        raise m.ParseError(m.CAUSE_BAD_FLAGS, 8)
    if len(blob) != m.HEADER_SIZE + n_events * m.RECORD_SIZE:
        raise m.ParseError(m.CAUSE_BAD_LENGTH, len(blob))
    if crc32(blob[m.HEADER_SIZE:]) != crc:
        raise m.ParseError(m.CAUSE_BAD_CRC, 20)
    events: List[m.Event] = []
    previous = 0
    for index in range(n_events):
        offset = m.HEADER_SIZE + index * m.RECORD_SIZE
        ts, pid, tid, cls, sub, a, b, c = _RECORD.unpack_from(blob, offset)
        if cls > m.EV_CLASS_MAX:
            raise m.ParseError(m.CAUSE_BAD_CLASS, offset + 16)
        if index and ts < previous:
            raise m.ParseError(m.CAUSE_TS_MONOTONIC, offset)
        previous = ts
        events.append(m.Event(ts, pid, tid, cls, sub, a, b, c))
    return m.Header(version, flags, n_events, time_unit, crc), events


def header_of(blob: bytes) -> m.Header:
    """En-tête seul, validé (magic, version, flags, longueur, CRC)."""
    return parse(blob)[0]


def write(events, flags: int = m.FLAG_SYNTHETIC,
          time_unit: int = m.TIME_UNIT_NS) -> bytes:
    """Sérialise des événements en RKV1 (fixtures et tests)."""
    body = b"".join(
        _RECORD.pack(e.ts & _U64, e.pid, e.tid, e.cls, e.subcode, e.a, e.b, e.c)
        for e in events
    )
    head = _HEADER.pack(m.MAGIC, m.VERSION, flags, len(body) // m.RECORD_SIZE,
                        time_unit or m.TIME_UNIT_NS, crc32(body), 0, 0)
    return head + body


def read_file(path: str) -> Tuple[m.Header, List[m.Event], bytes]:
    """Lit un fichier .rkv1 ; renvoie (en-tête, événements, octets bruts)."""
    with open(path, "rb") as handle:
        blob = handle.read()
    header, events = parse(blob)
    return header, events, blob
