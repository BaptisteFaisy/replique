"""model.py — jumeau Python exact de core/include/replique/types.h.

Aucune logique ici : seulement les structures gelées par CONTRACT.md et les
constantes numériques du contrat. Les valeurs sont celles des enums C.
"""
from __future__ import annotations

from dataclasses import dataclass, field

MAGIC = b"RKV1"
VERSION = 1
HEADER_SIZE = 32
RECORD_SIZE = 32
TIME_UNIT_NS = 1_000_000_000
FLAG_TRUNCATED = 1
FLAG_SYNTHETIC = 2
FLAG_RESERVED_MASK = 0xFFFFFFFC
DETAIL_MAX = 64
MAX_CHAIN = 32
PRIO_LEVELS = 256

# enum replique_status (C)
STATUS_OK = 0
ERR_ARG = -1
ERR_TRUNCATED = -2
ERR_MAGIC = -3
ERR_VERSION = -4
ERR_CRC = -5
ERR_ORDER = -6
ERR_CLASS = -7
ERR_OVERFLOW = -8
ERR_FLAGS = -9
ERR_NOMEM = -10

# enum replique_event_class (CONTRACT.md §2.1)
EV_NONE, EV_THREAD_STATE, EV_PROCESS_CREATE, EV_PROCESS_DESTROY = 0, 1, 2, 3
EV_THREAD_CREATE, EV_THREAD_DESTROY = 4, 5
EV_MSG_SEND, EV_MSG_RECEIVE, EV_MSG_REPLY, EV_MSG_ERROR = 6, 7, 8, 9
EV_MUTEX_LOCK, EV_MUTEX_UNLOCK, EV_SEM_WAIT, EV_SEM_POST = 10, 11, 12, 13
EV_TIMER, EV_INTERRUPT, EV_SCHED, EV_PRIO_CHANGE = 14, 15, 16, 17
EV_CLASS_MAX = 17

# enum replique_thread_state (§2.3)
TH_DEAD, TH_RUNNING, TH_READY, TH_BLOCKED, TH_STOPPED = 0, 1, 2, 3, 4

# enum replique_block_reason (§2.4)
BR_UNKNOWN, BR_MUTEX, BR_MSGSEND, BR_SEMAPHORE = 0, 1, 2, 3
BR_TIMER, BR_INTERRUPT, BR_REPLYWAIT, BR_JOIN = 4, 5, 6, 7

# enum replique_sched_policy (§2.5)
SCHED_FIFO, SCHED_RR, SCHED_SPORADIC = 0, 1, 2

# enum replique_sched_reason (§2.6)
SR_INITIAL, SR_PREEMPT, SR_BLOCK, SR_UNBLOCK = 0, 1, 2, 3
SR_YIELD, SR_TIMER, SR_TERMINATE, SR_WHATIF = 4, 5, 6, 7

# enum replique_verdict_code (§3.1)
V_OK, V_PARSE_ERROR, V_DEADLOCK_FOUND, V_CULPRIT = 0, 1, 2, 3
V_PRIO_INVERSION, V_REPLAY_DETERMINISTIC, V_REPLAY_DIVERGED = 4, 5, 6
V_TRACE_INCOMPLETE, V_CORRELATION_OK, V_WHATIF = 7, 8, 9

SEV_INFO, SEV_WARNING, SEV_ERROR = 0, 1, 2

# Causes de PARSE_ERROR (§1.4) : index -> détail gelé, identique au C.
CAUSE_SHORT_HEADER, CAUSE_BAD_MAGIC, CAUSE_BAD_VERSION = 1, 2, 3
CAUSE_BAD_LENGTH, CAUSE_BAD_CRC, CAUSE_BAD_CLASS = 4, 5, 6
CAUSE_TS_MONOTONIC, CAUSE_BAD_FLAGS = 7, 8


class ParseError(Exception):
    """Trace illisible : arrête la chaîne (CONTRACT.md §3.2).

    `offset` est le décalage en octets porté par le verdict PARSE_ERROR (champ
    `a`), `cause` l'indice de CAUSE_* dont le nom gelé est donné par
    event.cause_name().
    """

    def __init__(self, cause: int, offset: int) -> None:
        super().__init__(f"cause={cause} offset={offset}")
        self.cause = cause
        self.offset = offset


@dataclass(frozen=True)
class Header:
    version: int = VERSION
    flags: int = 0
    n_events: int = 0
    time_unit: int = TIME_UNIT_NS
    crc32: int = 0


@dataclass(frozen=True)
class Event:
    ts: int
    pid: int
    tid: int
    cls: int
    subcode: int = 0
    a: int = 0
    b: int = 0
    c: int = 0


@dataclass
class Verdict:
    code: int
    sev: int = SEV_INFO
    ts: int = 0
    pid: int = 0
    tid: int = 0
    a: int = 0
    b: int = 0
    c: int = 0
    hash: int = 0
    chain: tuple[int, ...] = ()
    detail: str = ""


@dataclass(frozen=True)
class Decision:
    ts: int
    tid: int
    reason: int


@dataclass(frozen=True)
class Edge:
    from_tid: int
    to_tid: int
    kind: int
    resource: int
    ts: int = 0


@dataclass
class Thread:
    tid: int
    pid: int = 0
    state: int = TH_DEAD
    policy: int = SCHED_FIFO
    base_prio: int = PRIO_LEVELS - 1
    eff_prio: int = PRIO_LEVELS - 1
    block_reason: int = BR_UNKNOWN
    resource: int = 0
    holder: int = 0
    ready_ts: int = 0
    alive: bool = False


@dataclass
class SchedConfig:
    n_cpus: int = 1
    inheritance: int = 1
    rr_quantum: int = 0
    overrides: tuple[tuple[int, int], ...] = ()


@dataclass
class ReplayResult:
    status: int = STATUS_OK
    decisions: list[Decision] = field(default_factory=list)
    hash: int = 0
    diverged: bool = False
    diverge_ts: int = 0
    diverge_tid: int = 0
    expected_reason: int = 0
    found_reason: int = 0
    incomplete: bool = False
    incomplete_detail: str = ""
