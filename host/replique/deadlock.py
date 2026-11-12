"""deadlock.py — Autopsie : graphe d'attente, cycle, coupable, inversion
(CONTRACT.md §3.3, §3.4), jumeau de core/src/deadlock.c.

Règle de désignation gelée : dans le cycle, le coupable est le thread dont la
priorité de BASE est la plus basse (plus grand numéro), départagé par l'ancienneté
d'acquisition de la ressource puis par le plus petit tid.
"""
from __future__ import annotations

from typing import Dict, List, Optional, Sequence, Tuple

from replique import model as m
from replique import sched as sched_mod


def rebuild_states(events: Sequence[m.Event]) -> List[m.Thread]:
    """Exigence C1 : état final de chaque thread, reconstruit depuis la trace."""
    state = sched_mod.Scheduler()
    for ev in events:
        state.apply(ev)
    return [state.threads[tid] for tid in sorted(state.threads)]


def build_waitfor(events: Sequence[m.Event]) -> List[m.Edge]:
    """Exigence C2 : une arête « T attend la ressource R tenue par U » par
    thread encore bloqué à la fin de la trace, détenteur vivant uniquement."""
    threads = {t.tid: t for t in rebuild_states(events)}
    edges = []
    for tid in sorted(threads):
        thread = threads[tid]
        holder = threads.get(thread.holder)
        if thread.state != m.TH_BLOCKED or not thread.holder:
            continue
        if holder is None or not holder.alive:
            continue
        edges.append(m.Edge(tid, thread.holder, thread.block_reason,
                            thread.resource, _last_block_ts(events, tid)))
    return edges


def _last_block_ts(events: Sequence[m.Event], tid: int) -> int:
    ts = 0
    for ev in events:
        if ev.tid == tid and ev.cls == m.EV_THREAD_STATE and ev.subcode == m.TH_BLOCKED:
            ts = ev.ts
    return ts


def find_cycle(edges: Sequence[m.Edge]) -> List[int]:
    """Chaque thread bloqué a au plus une arête sortante : suivre la chaîne
    suffit. Le cycle est renvoyé en commençant par son plus petit tid."""
    suivant: Dict[int, int] = {}
    for edge in sorted(edges, key=lambda e: e.from_tid):
        suivant.setdefault(edge.from_tid, edge.to_tid)
    for start in sorted(suivant):
        path, node = [], start
        while node in suivant and node not in path:
            path.append(node)
            node = suivant[node]
        if node in path:
            cycle = path[path.index(node):]
            pivot = cycle.index(min(cycle))
            return cycle[pivot:] + cycle[:pivot]
    return []


def _acquisitions(events: Sequence[m.Event]) -> Dict[Tuple[int, int], int]:
    """(tid, ressource) → horodatage d'acquisition (le plus ancien conservé)."""
    acquis: Dict[Tuple[int, int], int] = {}
    for ev in events:
        if ev.cls in (m.EV_MUTEX_LOCK, m.EV_SEM_WAIT) and ev.subcode == 0:
            acquis.setdefault((ev.tid, ev.a), ev.ts)
    return acquis


def _held_resources(cycle: Sequence[int], edges: Sequence[m.Edge]) -> Dict[int, int]:
    """Ressource détenue par chaque membre du cycle (arête pointant vers lui)."""
    held: Dict[int, int] = {}
    for edge in edges:
        if edge.to_tid in cycle and edge.from_tid in cycle:
            held.setdefault(edge.to_tid, edge.resource)
    return held


def _culprit(cycle: Sequence[int], threads: Dict[int, m.Thread],
             edges: Sequence[m.Edge],
             acquis: Dict[Tuple[int, int], int]) -> Tuple[int, int, int]:
    """Renvoie (tid coupable, ressource détenue, priorité de base) — §3.3."""
    held = _held_resources(cycle, edges)
    candidats = []
    for tid in cycle:
        thread = threads.get(tid)
        base = thread.base_prio if thread else m.PRIO_LEVELS - 1
        resource = held.get(tid, 0)
        candidats.append((-base, acquis.get((tid, resource), 1 << 62), tid,
                          resource, base))
    candidats.sort()
    _, _, tid, resource, base = candidats[0]
    return tid, resource, base


def detect(events: Sequence[m.Event]) -> List[m.Verdict]:
    """Exigence C3 : cycle → DEADLOCK_FOUND + CULPRIT ; détenteur mort →
    TRACE_INCOMPLETE « holder-dead » (jamais un cycle, SPEC §4)."""
    threads = {t.tid: t for t in rebuild_states(events)}
    edges = build_waitfor(events)
    cycle = find_cycle(edges)
    verdicts: List[m.Verdict] = []
    if cycle:
        acquis = _acquisitions(events)
        last = max((e.ts for e in edges if e.from_tid in cycle), default=0)
        resource = next((e.resource for e in edges
                         if e.from_tid in cycle and e.ts == last), 0)
        tid, held, base = _culprit(cycle, threads, edges, acquis)
        verdicts.append(m.Verdict(code=m.V_DEADLOCK_FOUND, sev=m.SEV_ERROR,
                                  ts=last, b=resource, chain=tuple(cycle),
                                  detail="cycle-detected"))
        verdicts.append(m.Verdict(code=m.V_CULPRIT, sev=m.SEV_ERROR, ts=last,
                                  tid=tid, b=held, chain=tuple(cycle),
                                  detail=f"holds-resource-{held}-at-prio-{base}"))
    verdicts.extend(_dead_holders(events, threads))
    return verdicts


def _dead_holders(events: Sequence[m.Event],
                  threads: Dict[int, m.Thread]) -> List[m.Verdict]:
    """Threads ENCORE bloqués en fin de trace sur un détenteur terminé.

    Un blocage passé puis levé (MsgReply, libération) n'est pas une attente
    orpheline : seul l'état final compte.
    """
    orphans = [threads[tid] for tid in sorted(threads)
               if threads[tid].state == m.TH_BLOCKED and threads[tid].holder
               and not threads[threads[tid].holder].alive
               and threads[threads[tid].holder] is not None]
    if not orphans:
        return []
    first = orphans[0]
    return [m.Verdict(code=m.V_TRACE_INCOMPLETE, sev=m.SEV_WARNING,
                      ts=_last_block_ts(events, first.tid), pid=first.pid,
                      tid=first.tid, a=len(orphans), b=first.resource,
                      detail="holder-dead")]


def detect_prio_inversion(events: Sequence[m.Event]) -> List[m.Verdict]:
    """Exigence C5 : chaîne [victime, intermédiaire, bloqueur] (CONTRACT §3.4)."""
    threads = {t.tid: t for t in rebuild_states(events)}
    verdicts = []
    for window in _block_windows(events, threads):
        found = _scan_window(events, threads, window)
        if found:
            verdicts.append(found)
    return verdicts


def _block_windows(events: Sequence[m.Event],
                   threads: Dict[int, m.Thread]) -> List[Tuple[int, int, int, int]]:
    """(victime, bloqueur, ressource, début) pour chaque blocage sur ressource."""
    windows = []
    for ev in events:
        if ev.cls != m.EV_THREAD_STATE or ev.subcode != m.TH_BLOCKED:
            continue
        if ev.a not in (m.BR_MUTEX, m.BR_SEMAPHORE) or not ev.c:
            continue
        victim, holder = threads.get(ev.tid), threads.get(ev.c)
        if not victim or not holder:
            continue
        if victim.base_prio >= holder.base_prio:      # QNX : plus petit = prioritaire
            continue
        windows.append((ev.tid, ev.c, ev.b, ev.ts))
    return windows


def _window_end(events: Sequence[m.Event], victim: int, start: int) -> int:
    for ev in events:
        if ev.tid != victim or ev.ts <= start or ev.cls != m.EV_THREAD_STATE:
            continue
        if ev.subcode in (m.TH_READY, m.TH_RUNNING, m.TH_DEAD):
            return ev.ts
    return 1 << 62


def _scan_window(events: Sequence[m.Event], threads: Dict[int, m.Thread],
                 window: Tuple[int, int, int, int]) -> Optional[m.Verdict]:
    victim, holder, resource, start = window
    end = _window_end(events, victim, start)
    prio_v = threads[victim].base_prio
    prio_h = threads[holder].base_prio
    running, ready, inherited = (0, 0), (0, 0), 0
    for ev in events:
        if ev.ts < start or ev.ts > end:
            continue
        if ev.cls == m.EV_PRIO_CHANGE and ev.tid == holder and ev.subcode == 1:
            inherited = 1
        if ev.cls != m.EV_THREAD_STATE or ev.tid in (victim, holder):
            continue
        if ev.subcode not in (m.TH_RUNNING, m.TH_READY):
            continue
        other = threads.get(ev.tid)
        if not other or not (prio_v < other.base_prio < prio_h):
            continue
        slot = running if ev.subcode == m.TH_RUNNING else ready
        if not slot[0]:
            if ev.subcode == m.TH_RUNNING:
                running = (ev.ts, ev.tid)
            else:
                ready = (ev.ts, ev.tid)
    inter_ts, inter_tid = running if running[0] else ready
    if not inter_tid:
        return None
    return m.Verdict(code=m.V_PRIO_INVERSION, sev=m.SEV_WARNING, ts=inter_ts,
                     tid=victim, a=inherited,
                     chain=(victim, inter_tid, holder),
                     detail=f"resource-{resource}")
