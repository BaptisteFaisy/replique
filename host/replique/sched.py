"""sched.py — modèle de l'ordonnanceur QNX (CONTRACT.md §5), jumeau de sched.c.

Priorités QNX : 0 = plus haute. Élection : plus petite priorité effective parmi
les prêts, puis `ready_ts` le plus ancien, puis plus petit tid. Aucune horloge,
aucune allocation dynamique côté C : l'état vit dans l'objet `Scheduler`.
"""
from __future__ import annotations

from typing import Dict, Optional

from replique import model as m


def default_config() -> m.SchedConfig:
    """Configuration par défaut : 1 CPU, héritage actif, pas de quantum forcé."""
    return m.SchedConfig(n_cpus=1, inheritance=1, rr_quantum=0, overrides=())


def override_prio(cfg: m.SchedConfig, tid: int, base: int) -> int:
    """Priorité de base réellement utilisée (mode « et si », X4)."""
    for target, prio in cfg.overrides:
        if target == tid:
            return prio
    return base


class Scheduler:
    """État de l'ordonnanceur : threads, élu courant, ressources détenues."""

    def __init__(self, cfg: Optional[m.SchedConfig] = None) -> None:
        self.cfg = cfg or default_config()
        self.threads: Dict[int, m.Thread] = {}
        self.resources: Dict[int, int] = {}
        self.running_tid = 0
        self.now = 0
        self.incomplete = False
        self.incomplete_detail = ""

    # ------------------------------------------------------------ threads
    def find(self, tid: int) -> Optional[m.Thread]:
        return self.threads.get(tid)

    def intern(self, tid: int, pid: int = 0) -> m.Thread:
        thread = self.threads.get(tid)
        if thread is None:
            thread = m.Thread(tid=tid, pid=pid)
            self.threads[tid] = thread
        elif pid and not thread.pid:
            thread.pid = pid
        return thread

    def ready_threads(self):
        return [t for t in self.threads.values()
                if t.alive and t.state in (m.TH_READY, m.TH_RUNNING)]

    def pick(self, now: int) -> int:
        """Élit le meilleur candidat ; 0 si aucun. L'élu courant garde la main
        à égalité (son `ready_ts` est le plus ancien)."""
        best = None
        for thread in self.ready_threads():
            key = (thread.eff_prio, thread.ready_ts, thread.tid)
            if best is None or key < best[0]:
                best = (key, thread)
        if best is None:
            return 0
        self.now = now
        return best[1].tid

    def elect(self, tid: int, ts: int) -> None:
        """Élit `tid` : rétrograde l'élu précédent, marque le nouvel élu RUNNING."""
        previous = self.find(self.running_tid)
        if previous and previous.tid != tid and previous.state == m.TH_RUNNING \
                and previous.alive:
            previous.state = m.TH_READY
            previous.ready_ts = ts
        chosen = self.find(tid)
        if chosen:
            chosen.state = m.TH_RUNNING
            chosen.ready_ts = min(chosen.ready_ts or ts, ts)
        self.running_tid = tid
        self.now = ts

    # --------------------------------------------------------- ressources
    def take(self, resource: int, tid: int) -> None:
        self.resources[resource] = tid

    def drop(self, resource: int) -> None:
        self.resources.pop(resource, None)

    def inherit(self, holder: int, waiter: int) -> None:
        """Héritage de priorité : prise maximale (CONTRACT.md §5)."""
        target, source = self.find(holder), self.find(waiter)
        if not self.cfg.inheritance or not target or not source:
            return
        if source.eff_prio < target.eff_prio:
            target.eff_prio = source.eff_prio

    def release(self, holder: int) -> None:
        """Rend la priorité de base, sauf attente encore en cours (prise max)."""
        target = self.find(holder)
        if not target:
            return
        best = target.base_prio
        for thread in self.threads.values():
            if (thread.alive and thread.state == m.TH_BLOCKED
                    and thread.holder == holder and thread.eff_prio < best):
                best = thread.eff_prio
        target.eff_prio = best

    # ---------------------------------------------------------- événements
    def apply(self, ev: m.Event) -> None:
        """Applique un événement de trace à l'état (sans émettre de décision).

        Les événements SCHED sont des observations : ils ne modifient pas
        l'état (CONTRACT.md §5).
        """
        self.now = ev.ts
        if ev.cls == m.EV_SCHED:
            return
        if ev.cls == m.EV_THREAD_CREATE:
            self._on_create(ev)
        elif ev.cls in (m.EV_THREAD_DESTROY, m.EV_PROCESS_DESTROY):
            self._on_destroy(ev)
        elif ev.cls == m.EV_THREAD_STATE:
            self._on_state(ev)
        elif ev.cls in (m.EV_MUTEX_LOCK, m.EV_SEM_WAIT):
            self._on_lock(ev)
        elif ev.cls in (m.EV_MUTEX_UNLOCK, m.EV_SEM_POST):
            self.drop(ev.a)
        elif ev.cls == m.EV_PRIO_CHANGE:
            self._on_prio(ev)
        elif ev.cls == m.EV_TIMER:
            self._on_timer(ev)

    def _on_create(self, ev: m.Event) -> None:
        thread = self.intern(ev.tid, ev.pid)
        thread.alive = True
        thread.policy = ev.subcode
        thread.base_prio = override_prio(self.cfg, ev.tid, ev.a)
        thread.eff_prio = thread.base_prio
        if ev.subcode == m.SCHED_SPORADIC and not self.incomplete:
            self.incomplete = True
            self.incomplete_detail = "sporadic-not-modelled"

    def _on_destroy(self, ev: m.Event) -> None:
        victims = ([ev.tid] if ev.cls == m.EV_THREAD_DESTROY
                   else [t.tid for t in self.threads.values() if t.pid == ev.pid])
        for tid in victims:
            thread = self.find(tid)
            if thread:
                thread.alive = False
                thread.state = m.TH_DEAD
        for resource, owner in list(self.resources.items()):
            if owner in victims:
                self.drop(resource)

    def _on_state(self, ev: m.Event) -> None:
        thread = self.intern(ev.tid, ev.pid)
        thread.alive = ev.subcode != m.TH_DEAD
        if ev.subcode == m.TH_READY:
            thread.state = m.TH_READY
            thread.ready_ts = ev.ts
            thread.block_reason, thread.resource, thread.holder = 0, 0, 0
            if thread.tid == self.running_tid:
                self.running_tid = 0          # cède sa place (quantum, yield)
        elif ev.subcode == m.TH_BLOCKED:
            thread.state = m.TH_BLOCKED
            thread.block_reason, thread.resource, thread.holder = ev.a, ev.b, ev.c
            if thread.tid == self.running_tid:
                self.running_tid = 0
            if self.cfg.inheritance and self.cfg.overrides and ev.a == m.BR_MUTEX:
                self.inherit(ev.c, ev.tid)
        elif ev.subcode == m.TH_DEAD:
            thread.state = m.TH_DEAD
            if thread.tid == self.running_tid:
                self.running_tid = 0
        elif ev.subcode == m.TH_RUNNING:
            thread.state = m.TH_RUNNING
            self.running_tid = thread.tid
        else:
            thread.state = ev.subcode

    def _on_lock(self, ev: m.Event) -> None:
        if ev.subcode == 0:
            self.take(ev.a, ev.tid)
        elif self.cfg.inheritance and self.cfg.overrides:
            self.inherit(ev.b, ev.tid)        # tentative bloquante (mode « et si »)

    def _on_prio(self, ev: m.Event) -> None:
        thread = self.intern(ev.tid, ev.pid)
        thread.eff_prio = ev.a
        if ev.subcode == 2:
            thread.base_prio = override_prio(self.cfg, ev.tid, ev.a)

    def _on_timer(self, ev: m.Event) -> None:
        thread = self.find(ev.tid)
        if not thread or thread.tid != self.running_tid:
            return
        if thread.policy == m.SCHED_RR and (ev.b or self.cfg.rr_quantum):
            thread.state = m.TH_READY         # rotation : fin de quantum
            thread.ready_ts = ev.ts + 1       # passe derrière les autres prêts
            self.running_tid = 0
