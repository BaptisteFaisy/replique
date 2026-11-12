"""report.py — sorties canoniques, jumeau exact de core/src/report.c.

Le texte (§4 de CONTRACT.md) est le format d'échange du différentiel C↔Python :
mêmes clés, même ordre, LF, aucune espace de remplissage. Le JSON (§4.5) est
produit par l'hôte seul ; ses ordres de clés sont gelés ici.
"""
from __future__ import annotations

import hashlib
import json
from typing import Iterable, Sequence

from replique import event as ev_names
from replique import model as m

_U64 = (1 << 64) - 1


def sanitize_detail(text: str) -> str:
    """Même règle que le C : octet à octet sur l'UTF-8, tronqué à 63 octets."""
    raw = text.encode("utf-8")[: m.DETAIL_MAX - 1]
    return "".join(
        "_" if (b == 0x7C or b < 0x20 or b > 0x7E) else chr(b) for b in raw
    )


def event_line(e: m.Event) -> str:
    return (
        f"ev|ts={e.ts & _U64}|pid={e.pid}|tid={e.tid}"
        f"|class={ev_names.class_name(e.cls)}|sub={e.subcode}"
        f"|a={e.a}|b={e.b}|c={e.c}\n"
    )


def verdict_line(v: m.Verdict) -> str:
    chain = ",".join(str(t) for t in tuple(v.chain)[: m.MAX_CHAIN])
    return (
        f"vd|code={ev_names.verdict_name(v.code)}"
        f"|sev={ev_names.severity_name(v.sev)}"
        f"|ts={v.ts & _U64}|pid={v.pid}|tid={v.tid}"
        f"|a={v.a}|b={v.b}|c={v.c}|hash={v.hash & _U64}"
        f"|chain={chain}|detail={sanitize_detail(v.detail)}\n"
    )


def decision_line(d: m.Decision) -> str:
    return (
        f"dec|ts={d.ts & _U64}|tid={d.tid}"
        f"|reason={ev_names.sched_reason_name(d.reason)}\n"
    )


def events_text(events: Iterable[m.Event]) -> str:
    return "".join(event_line(e) for e in events)


def verdicts_text(verdicts: Iterable[m.Verdict]) -> str:
    return "".join(verdict_line(v) for v in verdicts)


def decisions_text(decisions: Iterable[m.Decision]) -> str:
    return "".join(decision_line(d) for d in decisions)


def write_text(stream, text: str) -> None:
    stream.write(text)
    stream.flush()


def sha256_hex(blob: bytes) -> str:
    return hashlib.sha256(blob).hexdigest()


def event_json(e: m.Event) -> dict:
    return {
        "ts": e.ts & _U64, "pid": e.pid, "tid": e.tid,
        "class": ev_names.class_name(e.cls), "sub": e.subcode,
        "a": e.a, "b": e.b, "c": e.c,
    }


def verdict_json(v: m.Verdict) -> dict:
    return {
        "code": ev_names.verdict_name(v.code),
        "sev": ev_names.severity_name(v.sev),
        "ts": v.ts & _U64, "pid": v.pid, "tid": v.tid,
        "a": v.a, "b": v.b, "c": v.c, "hash": v.hash & _U64,
        "chain": [int(t) for t in tuple(v.chain)[: m.MAX_CHAIN]],
        "detail": sanitize_detail(v.detail),
    }


def replay_json(res: m.ReplayResult) -> dict:
    return {
        "hash": res.hash & _U64,
        "n_decisions": len(res.decisions),
        "diverged": bool(res.diverged),
        "incomplete": bool(res.incomplete),
        "decisions": [
            {"ts": d.ts & _U64, "tid": d.tid,
             "reason": ev_names.sched_reason_name(d.reason)}
            for d in res.decisions
        ],
    }


def trace_json(header: m.Header, blob: bytes) -> dict:
    return {
        "flags": header.flags, "n_events": header.n_events,
        "time_unit": header.time_unit, "crc32": header.crc32,
        "sha256": sha256_hex(blob),
    }


def to_json(obj: dict) -> str:
    """JSON compact, ordre de clés = ordre d'insertion (gelé, §4.5)."""
    return json.dumps(obj, separators=(",", ":"), ensure_ascii=False)


def document(header: m.Header, blob: bytes, events: Sequence[m.Event],
             verdicts: Sequence[m.Verdict],
             replay: m.ReplayResult | None = None) -> str:
    doc = {
        "schema": "replique/1",
        "trace": trace_json(header, blob),
        "events": [event_json(e) for e in events],
        "verdicts": [verdict_json(v) for v in verdicts],
        "replay": replay_json(replay) if replay else {"hash": 0,
                                                      "n_decisions": 0,
                                                      "diverged": False,
                                                      "incomplete": False,
                                                      "decisions": []},
    }
    return to_json(doc) + "\n"
