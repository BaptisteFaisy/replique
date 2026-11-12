"""event.py — tables de noms gelées, jumeau exact de core/src/event.c."""
from __future__ import annotations

from replique import model as m

CLASS_NAMES = (
    "NONE", "THREAD_STATE", "PROCESS_CREATE", "PROCESS_DESTROY", "THREAD_CREATE",
    "THREAD_DESTROY", "MSG_SEND", "MSG_RECEIVE", "MSG_REPLY", "MSG_ERROR",
    "MUTEX_LOCK", "MUTEX_UNLOCK", "SEM_WAIT", "SEM_POST", "TIMER", "INTERRUPT",
    "SCHED", "PRIO_CHANGE",
)
STATE_NAMES = ("DEAD", "RUNNING", "READY", "BLOCKED", "STOPPED")
BLOCK_REASON_NAMES = (
    "UNKNOWN", "MUTEX", "MSGSEND", "SEMAPHORE", "TIMER", "INTERRUPT",
    "REPLYWAIT", "JOIN",
)
SCHED_REASON_NAMES = (
    "INITIAL", "PREEMPT", "BLOCK", "UNBLOCK", "YIELD", "TIMER", "TERMINATE",
    "WHATIF",
)
VERDICT_NAMES = (
    "OK", "PARSE_ERROR", "DEADLOCK_FOUND", "CULPRIT", "PRIO_INVERSION",
    "REPLAY_DETERMINISTIC", "REPLAY_DIVERGED", "TRACE_INCOMPLETE",
    "CORRELATION_OK", "WHATIF",
)
SEVERITY_NAMES = ("info", "warning", "error")
VERDICT_SEVERITY = (
    m.SEV_INFO, m.SEV_ERROR, m.SEV_ERROR, m.SEV_ERROR, m.SEV_WARNING,
    m.SEV_INFO, m.SEV_ERROR, m.SEV_WARNING, m.SEV_INFO, m.SEV_INFO,
)
CAUSE_NAMES = (
    "", "short-header", "bad-magic", "bad-version", "bad-length", "bad-crc",
    "bad-class", "ts-not-monotonic", "bad-flags",
)
UNKNOWN = "UNKNOWN"


def _name(table: tuple[str, ...], index: int) -> str:
    return table[index] if 0 <= index < len(table) else UNKNOWN


def class_name(cls: int) -> str:
    return _name(CLASS_NAMES, cls)


def state_name(state: int) -> str:
    return _name(STATE_NAMES, state)


def block_reason_name(reason: int) -> str:
    return _name(BLOCK_REASON_NAMES, reason)


def sched_reason_name(reason: int) -> str:
    return _name(SCHED_REASON_NAMES, reason)


def verdict_name(code: int) -> str:
    return _name(VERDICT_NAMES, code)


def severity_name(sev: int) -> str:
    return _name(SEVERITY_NAMES, sev)


def cause_name(cause: int) -> str:
    return _name(CAUSE_NAMES, cause)


def verdict_severity(code: int) -> int:
    return VERDICT_SEVERITY[code] if 0 <= code < len(VERDICT_SEVERITY) else m.SEV_INFO


def class_index(name: str) -> int:
    """Nom canonique -> valeur d'enum (utilisé par le générateur de fixtures)."""
    return CLASS_NAMES.index(name) if name in CLASS_NAMES else -1
