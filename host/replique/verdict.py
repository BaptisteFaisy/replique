"""verdict.py — fabriques, tri gelé et chaîne de traitement (CONTRACT.md §3).

Jumeau de core/src/verdict.c + core/src/pipeline.c. La chaîne ne remonte
jamais : un échec de parse produit UN SEUL verdict PARSE_ERROR.
"""
from __future__ import annotations

from typing import List, Optional, Sequence, Tuple

from replique import deadlock
from replique import event as ev
from replique import kev
from replique import model as m
from replique import replay as replay_mod


def make(code: int, **champs) -> m.Verdict:
    """Fabrique un verdict ; la sévérité est déduite du code (table gelée)."""
    return m.Verdict(code=code, sev=ev.verdict_severity(code), **champs)


def sort_verdicts(verdicts: List[m.Verdict]) -> None:
    """Tri stable gelé (CONTRACT.md §4.4) : sévérité décroissante, code
    croissant, ts croissant, tid croissant ; à égalité, ordre de production."""
    verdicts.sort(key=lambda v: (-v.sev, v.code, v.ts, v.tid))


def parse_error(exc: m.ParseError) -> m.Verdict:
    return make(m.V_PARSE_ERROR, a=exc.offset & 0xFFFFFFFF,
                detail=ev.cause_name(exc.cause))


def pipeline_blob(blob: bytes, cfg: Optional[m.SchedConfig] = None
                  ) -> Tuple[List[m.Verdict], m.ReplayResult]:
    """Chaîne complète depuis les octets du conteneur."""
    try:
        header, events = kev.parse(blob)
    except m.ParseError as exc:
        return [parse_error(exc)], m.ReplayResult()
    return pipeline_events(events, cfg=cfg, header=header, blob=blob)


def pipeline_events(events: Sequence[m.Event], cfg: Optional[m.SchedConfig] = None,
                    header: Optional[m.Header] = None, blob: bytes = b""
                    ) -> Tuple[List[m.Verdict], m.ReplayResult]:
    """Chaîne complète depuis des événements déjà décodés (§3.2)."""
    cfg = cfg or m.SchedConfig()
    verdicts: List[m.Verdict] = [make(m.V_OK, a=len(events), detail="parse-ok")]
    flags = header.flags if header else 0
    if flags & m.FLAG_TRUNCATED:
        verdicts.append(make(m.V_TRACE_INCOMPLETE, detail="circular-buffer-overwritten"))
    verdicts.extend(deadlock.detect(events))
    verdicts.extend(deadlock.detect_prio_inversion(events))
    result = replay_mod.run(events, cfg)
    verdicts.append(make(m.V_REPLAY_DETERMINISTIC, hash=result.hash,
                         detail=f"decisions-{len(result.decisions)}"))
    # P2 n'a de sens qu'en rejeu de fidélité : avec un « et si », la séquence
    # est volontairement différente de la trace observée.
    if not cfg.overrides:
        divergence = replay_mod.check_fidelity(events, result.decisions)
        if divergence:
            verdicts.append(divergence)
    if result.incomplete:
        verdicts.append(make(m.V_TRACE_INCOMPLETE, detail=result.incomplete_detail))
    verdicts.extend(_whatif(cfg, result))
    sort_verdicts(verdicts)
    return verdicts, result


def _whatif(cfg: m.SchedConfig, result: m.ReplayResult) -> List[m.Verdict]:
    """Un verdict WHATIF par override appliqué (X4) : paramètre + hachage."""
    return [make(m.V_WHATIF, tid=tid, a=prio, hash=result.hash,
                 detail="whatif-replay") for tid, prio in cfg.overrides]
