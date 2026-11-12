"""kevgen.py — générateur des fixtures de référence (scenarios/SPEC.md).

Chaque scénario est une TABLE d'événements gelée : (ts, pid, tid, classe,
subcode, a, b, c). Les tables sont la source de vérité du différentiel C↔Python
et des attentes de scenarios/*.json ; les modifier = une PR motivée touchant
SPEC.md en même temps.
"""
from __future__ import annotations

import os
from typing import Dict, List, Sequence, Tuple

from replique import event as ev
from replique import kev
from replique import model as m

Row = Tuple[int, int, int, str, int, int, int, int]

# Raccourcis lisibles (référencent les constantes gelées de model.py).
READY, RUNNING, BLOCKED, DEAD = m.TH_READY, m.TH_RUNNING, m.TH_BLOCKED, m.TH_DEAD
MUTEX, MSGSEND, TIMER_BR = m.BR_MUTEX, m.BR_MSGSEND, m.BR_TIMER
FIFO, RR = m.SCHED_FIFO, m.SCHED_RR
INITIAL, PREEMPT, BLOCK_SR, UNBLOCK, TIMER_SR, TERMINATE = (
    m.SR_INITIAL, m.SR_PREEMPT, m.SR_BLOCK, m.SR_UNBLOCK, m.SR_TIMER,
    m.SR_TERMINATE)
INH_APPLIED, INH_RELEASED = 1, 2

# ---------------------------------------------------------------- nominal
# Serveur pid 1 / tid 1 (prio 10) et client pid 2 / tid 2 (prio 20) :
# MsgSend bloquant, réponse, temporisation, préemption, terminaison.
NOMINAL: Tuple[Row, ...] = (
    (1000, 1, 1, "PROCESS_CREATE", 0, 0, 0, 0),
    (1000, 1, 1, "THREAD_CREATE", FIFO, 10, 0, 0),
    (1000, 1, 1, "THREAD_STATE", READY, 0, 0, 0),
    (1000, 1, 1, "SCHED", INITIAL, 1, 0, 0),
    (1000, 1, 1, "THREAD_STATE", RUNNING, 0, 0, 0),
    (2000, 2, 2, "PROCESS_CREATE", 0, 1, 0, 0),
    (2000, 2, 2, "THREAD_CREATE", FIFO, 20, 0, 0),
    (2000, 2, 2, "THREAD_STATE", READY, 0, 0, 0),
    (3000, 2, 2, "MSG_SEND", 0, 1, 1, 1),
    (3000, 2, 2, "THREAD_STATE", BLOCKED, MSGSEND, 1, 1),
    (3100, 1, 1, "MSG_RECEIVE", 0, 2, 2, 1),
    (3200, 1, 1, "MSG_REPLY", 0, 2, 1, 0),
    (3200, 2, 2, "THREAD_STATE", READY, 0, 0, 0),
    (3400, 1, 1, "THREAD_STATE", BLOCKED, TIMER_BR, 1, 0),
    (3400, 2, 2, "SCHED", BLOCK_SR, 2, 0, 0),
    (3400, 2, 2, "THREAD_STATE", RUNNING, 0, 0, 0),
    (4000, 1, 1, "TIMER", 0, 1, 600, 0),
    (4000, 1, 1, "THREAD_STATE", READY, 0, 0, 0),
    (4000, 1, 1, "SCHED", UNBLOCK, 1, 0, 0),
    (4000, 1, 1, "THREAD_STATE", RUNNING, 0, 0, 0),
    (4000, 2, 2, "THREAD_STATE", READY, 0, 0, 0),
    (5000, 1, 1, "PROCESS_DESTROY", 0, 0, 0, 0),
    (5000, 1, 1, "THREAD_STATE", DEAD, 0, 0, 0),
    (5000, 2, 2, "SCHED", TERMINATE, 2, 0, 0),
    (5000, 2, 2, "THREAD_STATE", RUNNING, 0, 0, 0),
)

# ---------------------------------------------------------------- deadlock
# SPEC §2 : 102 (prio 60) prend le mutex 32 puis attend un pulse ; 101 (prio 50)
# prend le mutex 16 puis attend le 32 ; 102 attend le 16 → cycle fermé.
DEADLOCK: Tuple[Row, ...] = (
    (1000, 10, 101, "PROCESS_CREATE", 0, 0, 0, 0),
    (1000, 10, 101, "THREAD_CREATE", FIFO, 50, 0, 0),
    (1000, 10, 102, "THREAD_CREATE", FIFO, 60, 0, 0),
    (1000, 10, 102, "THREAD_STATE", READY, 0, 0, 0),
    (1000, 10, 102, "SCHED", INITIAL, 102, 0, 0),
    (1000, 10, 102, "THREAD_STATE", RUNNING, 0, 0, 0),
    (1200, 10, 102, "MUTEX_LOCK", 0, 32, 102, 0),
    (1300, 10, 102, "THREAD_STATE", BLOCKED, TIMER_BR, 7, 0),
    (1400, 10, 101, "THREAD_STATE", READY, 0, 0, 0),
    (1400, 10, 101, "SCHED", PREEMPT, 101, 0, 0),
    (1400, 10, 101, "THREAD_STATE", RUNNING, 0, 0, 0),
    (1500, 10, 101, "MUTEX_LOCK", 0, 16, 101, 0),
    (1550, 10, 102, "TIMER", 0, 7, 0, 0),
    (1550, 10, 102, "THREAD_STATE", READY, 0, 0, 0),
    (1600, 10, 101, "MUTEX_LOCK", 1, 32, 102, 0),
    (1600, 10, 101, "THREAD_STATE", BLOCKED, MUTEX, 32, 102),
    (1600, 10, 102, "SCHED", BLOCK_SR, 102, 0, 0),
    (1600, 10, 102, "THREAD_STATE", RUNNING, 0, 0, 0),
    (1700, 10, 102, "MUTEX_LOCK", 1, 16, 101, 0),
    (1700, 10, 102, "THREAD_STATE", BLOCKED, MUTEX, 16, 101),
)

# ----------------------------------------------------------- inversion (tête)
# SPEC §3 : 202 (prio 100) détient le mutex 48 ; 201 (prio 10) s'y bloque ;
# 203 (prio 50) vole le CPU. Sans héritage, 203 doit finir pour débloquer 201.
INVERSION_TETE: Tuple[Row, ...] = (
    (1000, 20, 201, "PROCESS_CREATE", 0, 0, 0, 0),
    (1000, 20, 201, "THREAD_CREATE", FIFO, 10, 0, 0),
    (1000, 20, 202, "THREAD_CREATE", FIFO, 100, 0, 0),
    (1000, 20, 203, "THREAD_CREATE", FIFO, 50, 0, 0),
    (1100, 20, 202, "THREAD_STATE", READY, 0, 0, 0),
    (1100, 20, 202, "SCHED", INITIAL, 202, 0, 0),
    (1100, 20, 202, "THREAD_STATE", RUNNING, 0, 0, 0),
    (1200, 20, 202, "MUTEX_LOCK", 0, 48, 202, 0),
    (1300, 20, 202, "THREAD_STATE", BLOCKED, TIMER_BR, 9, 0),
    (1400, 20, 203, "THREAD_STATE", READY, 0, 0, 0),
    (1400, 20, 203, "SCHED", PREEMPT, 203, 0, 0),
    (1400, 20, 203, "THREAD_STATE", RUNNING, 0, 0, 0),
    (1500, 20, 201, "THREAD_STATE", READY, 0, 0, 0),
    (1500, 20, 201, "SCHED", PREEMPT, 201, 0, 0),
    (1500, 20, 201, "THREAD_STATE", RUNNING, 0, 0, 0),
)

INVERSION_BLOCAGE: Tuple[Row, ...] = (
    (1600, 20, 201, "MUTEX_LOCK", 1, 48, 202, 0),
    (1600, 20, 201, "THREAD_STATE", BLOCKED, MUTEX, 48, 202),
    (1600, 20, 203, "SCHED", BLOCK_SR, 203, 0, 0),
    (1600, 20, 203, "THREAD_STATE", RUNNING, 0, 0, 0),
)

# Variante sans héritage : 203 finit, 202 libère enfin le mutex, 201 repart.
INVERSION_SANS: Tuple[Row, ...] = (
    (1700, 20, 202, "TIMER", 0, 9, 0, 0),
    (1700, 20, 202, "THREAD_STATE", READY, 0, 0, 0),
    (2000, 20, 203, "THREAD_DESTROY", 0, 0, 0, 0),
    (2000, 20, 203, "THREAD_STATE", DEAD, 0, 0, 0),
    (2000, 20, 202, "SCHED", TERMINATE, 202, 0, 0),
    (2000, 20, 202, "THREAD_STATE", RUNNING, 0, 0, 0),
    (2100, 20, 202, "MUTEX_UNLOCK", 0, 48, 0, 0),
    (2100, 20, 201, "THREAD_STATE", READY, 0, 0, 0),
    (2100, 20, 201, "SCHED", UNBLOCK, 201, 0, 0),
    (2100, 20, 201, "THREAD_STATE", RUNNING, 0, 0, 0),
    (2100, 20, 202, "THREAD_STATE", READY, 0, 0, 0),
)

# Variante avec héritage : le pulse de 202 échoit AVANT le blocage de 201
# (sinon 202 ne serait pas prêt à hériter), puis le noyau élève 202 à la
# priorité de 201 (PRIO_CHANGE subcode 1) et le rend ensuite (subcode 2).
INVERSION_AVEC_PRE: Tuple[Row, ...] = (
    (1550, 20, 202, "TIMER", 0, 9, 0, 0),
    (1550, 20, 202, "THREAD_STATE", READY, 0, 0, 0),
)

INVERSION_AVEC: Tuple[Row, ...] = (
    (1600, 20, 202, "PRIO_CHANGE", INH_APPLIED, 10, 100, 201),
    (1600, 20, 202, "SCHED", PREEMPT, 202, 0, 0),
    (1600, 20, 202, "THREAD_STATE", RUNNING, 0, 0, 0),
    (1600, 20, 203, "THREAD_STATE", READY, 0, 0, 0),
    (1700, 20, 202, "MUTEX_UNLOCK", 0, 48, 0, 0),
    (1700, 20, 202, "PRIO_CHANGE", INH_RELEASED, 100, 10, 201),
    (1700, 20, 203, "SCHED", PREEMPT, 203, 0, 0),
    (1700, 20, 203, "THREAD_STATE", RUNNING, 0, 0, 0),
    (1700, 20, 202, "THREAD_STATE", READY, 0, 0, 0),
    (1700, 20, 201, "THREAD_STATE", READY, 0, 0, 0),
    (1700, 20, 201, "SCHED", UNBLOCK, 201, 0, 0),
    (1700, 20, 201, "THREAD_STATE", RUNNING, 0, 0, 0),
    (1700, 20, 203, "THREAD_STATE", READY, 0, 0, 0),
)

# ---------------------------------------------------------------------- race
# SPEC §4 : deux threads RR de même priorité ; 402 meurt (SEGV) sans libérer le
# mutex 64 ; 401 attend ensuite un détenteur mort → attente sans fin, pas de cycle.
RACE: Tuple[Row, ...] = (
    (1000, 40, 401, "PROCESS_CREATE", 0, 0, 0, 0),
    (1000, 40, 401, "THREAD_CREATE", RR, 30, 0, 0),
    (1000, 41, 402, "PROCESS_CREATE", 0, 0, 0, 0),
    (1000, 41, 402, "THREAD_CREATE", RR, 30, 0, 0),
    (1100, 40, 401, "THREAD_STATE", READY, 0, 0, 0),
    (1100, 40, 401, "SCHED", INITIAL, 401, 0, 0),
    (1100, 40, 401, "THREAD_STATE", RUNNING, 0, 0, 0),
    (1200, 40, 401, "MUTEX_LOCK", 0, 64, 401, 0),
    (1300, 40, 401, "MUTEX_UNLOCK", 0, 64, 0, 0),
    (1400, 41, 402, "THREAD_STATE", READY, 0, 0, 0),
    (1500, 40, 401, "TIMER", 0, 1, 1000, 0),
    (1500, 40, 401, "THREAD_STATE", READY, 0, 0, 0),
    (1500, 41, 402, "SCHED", TIMER_SR, 402, 0, 0),
    (1500, 41, 402, "THREAD_STATE", RUNNING, 0, 0, 0),
    (1600, 41, 402, "MUTEX_LOCK", 0, 64, 402, 0),
    (1700, 41, 402, "PROCESS_DESTROY", 0, 0, 11, 0),
    (1700, 41, 402, "THREAD_STATE", DEAD, 0, 0, 0),
    (1700, 40, 401, "SCHED", TERMINATE, 401, 0, 0),
    (1700, 40, 401, "THREAD_STATE", RUNNING, 0, 0, 0),
    (1800, 40, 401, "MUTEX_LOCK", 1, 64, 402, 0),
    (1800, 40, 401, "THREAD_STATE", BLOCKED, MUTEX, 64, 402),
)


def _rows(rows: Sequence[Row]) -> List[m.Event]:
    return [m.Event(ts, pid, tid, ev.class_index(name), sub, a, b, c)
            for ts, pid, tid, name, sub, a, b, c in rows]


def _nominal() -> List[m.Event]:
    return _rows(NOMINAL)


def _deadlock() -> List[m.Event]:
    return _rows(DEADLOCK)


def _inversion() -> List[m.Event]:
    return _rows(INVERSION_TETE + INVERSION_BLOCAGE + INVERSION_SANS)


def _inversion_heritage() -> List[m.Event]:
    return _rows(INVERSION_TETE + INVERSION_AVEC_PRE + INVERSION_BLOCAGE
                 + INVERSION_AVEC)


def _race() -> List[m.Event]:
    return _rows(RACE)


def _corrupt_magic() -> bytes:
    return b"RKV0" + kev.write(_nominal())[4:]


def _corrupt_crc() -> bytes:
    blob = bytearray(kev.write(_nominal()))
    blob[m.HEADER_SIZE + 3] ^= 0xFF           # un octet du corps inversé
    return bytes(blob)


def _corrupt_truncated() -> bytes:
    blob = kev.write(_nominal())
    return blob[:len(blob) - m.RECORD_SIZE // 2]


_BUILDERS: Dict[str, object] = {
    "nominal": _nominal,
    "deadlock": _deadlock,
    "prio-inversion": _inversion,
    "prio-inversion-heritage": _inversion_heritage,
    "race": _race,
}
_BYTES_BUILDERS: Dict[str, object] = {
    "corrupt-magic": _corrupt_magic,
    "corrupt-crc": _corrupt_crc,
    "corrupt-truncated": _corrupt_truncated,
}

NAMES: Tuple[str, ...] = (
    "nominal", "deadlock", "prio-inversion", "prio-inversion-heritage", "race",
    "corrupt-magic", "corrupt-crc", "corrupt-truncated", "truncated-buffer",
)


def scenario(name: str) -> List[m.Event]:
    """Événements d'un scénario nominal (les fixtures corrompues sont en octets)."""
    if name not in _BUILDERS:
        raise KeyError(name)
    return _BUILDERS[name]()          # type: ignore[operator]


def blob(name: str) -> bytes:
    """Octets du fichier .rkv1 d'un scénario, corruptions comprises."""
    if name in _BYTES_BUILDERS:
        return _BYTES_BUILDERS[name]()  # type: ignore[operator]
    if name == "truncated-buffer":
        flags = m.FLAG_TRUNCATED | m.FLAG_SYNTHETIC
        return kev.write(scenario("nominal"), flags=flags)
    return kev.write(scenario(name))


def write(name: str, out_dir: str) -> str:
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, name + ".rkv1")
    with open(path, "wb") as handle:
        handle.write(blob(name))
    return path


def write_all(out_dir: str) -> List[str]:
    return [write(name, out_dir) for name in NAMES]
