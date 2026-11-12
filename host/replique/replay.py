"""replay.py — rejeu déterministe (CONTRACT.md §5, §6), jumeau de replay.c.

Propriété P1 : mêmes événements + même configuration → exactement la même
séquence de décisions, vérifiée par hachage FNV-1a 64 des lignes canoniques.
"""
from __future__ import annotations

from typing import Iterable, List, Optional, Sequence

from replique import kev
from replique import model as m
from replique import report
from replique import sched as sched_mod


def _overridden(cfg: m.SchedConfig, tid: int) -> bool:
    return any(target == tid for target, _ in cfg.overrides)


def _reason(decisions: Sequence[m.Decision], ev: m.Event, prev_current: int,
            best: int, subject_blocked: bool) -> int:
    """Raison gelée de la décision (CONTRACT.md §2.6, ordre de priorité)."""
    if not decisions:
        return m.SR_INITIAL
    killed = ev.cls in (m.EV_THREAD_DESTROY, m.EV_PROCESS_DESTROY)
    if killed and prev_current and best != prev_current:
        return m.SR_TERMINATE
    blocked_current = (ev.cls == m.EV_THREAD_STATE and ev.tid == prev_current
                       and ev.subcode in (m.TH_BLOCKED, m.TH_DEAD))
    if blocked_current:
        return m.SR_BLOCK
    if ev.cls == m.EV_TIMER and ev.tid == prev_current:
        return m.SR_TIMER
    if best == ev.tid and subject_blocked:
        return m.SR_UNBLOCK
    return m.SR_PREEMPT


def run(events: Iterable[m.Event], cfg: Optional[m.SchedConfig] = None) -> m.ReplayResult:
    """Rejoue la trace : une passe, aucune horloge, aucun aléa."""
    cfg = cfg or sched_mod.default_config()
    state = sched_mod.Scheduler(cfg)
    decisions: List[m.Decision] = []
    for ev in events:
        prev_current = state.running_tid
        subject = state.find(ev.tid)
        subject_blocked = bool(subject and subject.state == m.TH_BLOCKED)
        state.apply(ev)
        best = state.pick(ev.ts)
        if best == 0 or best == state.running_tid:
            continue
        reason = _reason(decisions, ev, prev_current, best, subject_blocked)
        if _overridden(cfg, best):
            reason = m.SR_WHATIF
        state.elect(best, ev.ts)
        decisions.append(m.Decision(ev.ts, best, reason))
    result = m.ReplayResult(decisions=decisions, hash=hash_decisions(decisions))
    if state.incomplete:
        result.incomplete = True
        result.incomplete_detail = state.incomplete_detail
    return result


def hash_decisions(decisions: Sequence[m.Decision]) -> int:
    """FNV-1a 64 sur les lignes canoniques « dec|… » (CONTRACT.md §6)."""
    blob = "".join(report.decision_line(d) for d in decisions)
    return kev.fnv1a64(blob.encode("utf-8"))


def check_fidelity(events: Sequence[m.Event],
                   decisions: Sequence[m.Decision]) -> Optional[m.Verdict]:
    """P2 : compare le rejeu aux événements SCHED observés dans la trace.

    Sans événement SCHED, aucun verdict (rien à comparer). Première divergence
    → REPLIQUE_V_REPLAY_DIVERGED nommant l'événement fautif.
    """
    observed = [ev for ev in events if ev.cls == m.EV_SCHED]
    if not observed:
        return None
    for index, ev in enumerate(observed):
        if index >= len(decisions):
            return _diverged(ev.ts, ev.a, ev.subcode, 0)
        got = decisions[index]
        if got.ts != ev.ts or got.tid != ev.a:
            return _diverged(ev.ts, ev.a, ev.subcode, got.reason)
        if got.reason != ev.subcode:
            return _diverged(ev.ts, ev.a, ev.subcode, got.reason)
    if len(decisions) != len(observed):
        extra = decisions[len(observed)]
        return _diverged(extra.ts, extra.tid, 0, extra.reason)
    return None


def _diverged(ts: int, expected_tid: int, expected_reason: int,
              found_reason: int) -> m.Verdict:
    return m.Verdict(code=m.V_REPLAY_DIVERGED, sev=m.SEV_ERROR, ts=ts,
                     tid=expected_tid, a=expected_reason, b=found_reason,
                     detail="replay-diverged")


def step_through(events: Sequence[m.Event], cfg: Optional[m.SchedConfig],
                 out, source=None) -> None:
    """Rejeu pas-à-pas interactif (X6) : `source` fournit les lignes d'entrée."""
    cfg = cfg or sched_mod.default_config()
    state = sched_mod.Scheduler(cfg)
    answers = iter(source if source is not None else [])
    for index, ev in enumerate(events):
        state.apply(ev)
        best = state.pick(ev.ts)
        out.write(f"--- [{index}] {report.event_line(ev).rstrip()}\n")
        out.write(f"    élu={best} courant={state.running_tid} "
                  f"prêts={sorted(t.tid for t in state.ready_threads())}\n")
        for tid in sorted(state.threads):
            thread = state.threads[tid]
            out.write(f"    tid={tid} état={thread.state} prio={thread.eff_prio}"
                      f"/{thread.base_prio} res={thread.resource}\n")
        try:
            answer = next(answers)
        except StopIteration:
            return
        if str(answer).strip().lower() in ("q", "quit"):
            return
