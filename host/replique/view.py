"""view.py — graphe d'attente en SVG statique (exigence C7).

SVG autonome : aucune police distante, aucune image externe, aucun script,
aucune dépendance hors stdlib. Les dimensions sont calculées à partir du
nombre de threads.

Le graphe est reconstruit depuis les événements (`deadlock.rebuild_states`,
`deadlock.build_waitfor`) ; la détection (interblocage, coupable, inversion)
est lue dans les verdicts reçus — elle n'est jamais recalculée ici.
"""
from __future__ import annotations

import math
from typing import Dict, List, Optional, Sequence, Tuple

from replique import deadlock
from replique import event as ev_names
from replique import model as m

NODE_W = 152
NODE_H = 60
MARGIN = 36
TITLE_H = 34
LEGEND_H = 96
MIN_RADIUS = 130.0
RADIUS_PER_THREAD = 44.0

C_BG = "#ffffff"
C_NODE_STROKE = "#46586b"
C_CYCLE_FILL = "#fdecec"
C_CYCLE_STROKE = "#c62828"
C_CULPRIT_STROKE = "#8e1b1b"
C_INVERSION_STROKE = "#e07b00"
C_EDGE = "#6b7c8d"
C_EDGE_CYCLE = "#c62828"
C_TEXT = "#1d2b36"
C_MUTED = "#55636f"

STATE_FILL = {
    m.TH_RUNNING: "#e3f3e4",
    m.TH_READY: "#e6eefb",
    m.TH_BLOCKED: "#fbeaea",
    m.TH_DEAD: "#ececec",
    m.TH_STOPPED: "#f3ecdf",
}

Point = Tuple[float, float]


def _esc(text: str) -> str:
    """Échappe le texte XML (`&`, `<`, `>`, `"`, `'`)."""
    out = str(text)
    for old, new in (("&", "&amp;"), ("<", "&lt;"), (">", "&gt;"),
                     ('"', "&quot;"), ("'", "&apos;")):
        out = out.replace(old, new)
    return out


def _by_code(verdicts: Sequence[m.Verdict], code: int) -> Optional[m.Verdict]:
    for item in verdicts:
        if item.code == code:
            return item
    return None


def _radius(count: int) -> float:
    return max(MIN_RADIUS, RADIUS_PER_THREAD * count)


def _positions(tids: Sequence[int]) -> Dict[int, Point]:
    """Placement circulaire des nœuds (un tour complet, angle constant)."""
    count = len(tids)
    if count == 0:
        return {}
    radius = _radius(count)
    cx = MARGIN + NODE_W / 2 + radius
    cy = MARGIN + TITLE_H + NODE_H / 2 + radius
    places: Dict[int, Point] = {}
    for index, tid in enumerate(tids):
        angle = -math.pi / 2 + 2 * math.pi * index / count
        places[tid] = (cx + radius * math.cos(angle), cy + radius * math.sin(angle))
    return places


def _trim(center: Point, other: Point) -> Point:
    """Découpe un segment au bord du rectangle du nœud (approximation elliptique)."""
    dx, dy = other[0] - center[0], other[1] - center[1]
    rx, ry = NODE_W / 2 + 12, NODE_H / 2 + 12
    scales = [rx / abs(dx)] if dx else []
    scales += [ry / abs(dy)] if dy else []
    scale = min(scales) if scales else 1.0
    return (center[0] + dx * scale, center[1] + dy * scale)


def _curve(start: Point, end: Point) -> Tuple[str, Point]:
    """Arête courbée (Bézier quadratique) : deux arcs opposés ne se recouvrent
    pas, ce qui rend le cycle lisible. Renvoie (chemin, point du milieu)."""
    dx, dy = end[0] - start[0], end[1] - start[1]
    length = math.hypot(dx, dy) or 1.0
    bend = max(18.0, length * 0.16)
    control = ((start[0] + end[0]) / 2 - dy / length * bend,
               (start[1] + end[1]) / 2 + dx / length * bend)
    path = (f"M {start[0]:.1f} {start[1]:.1f} "
            f"Q {control[0]:.1f} {control[1]:.1f} {end[0]:.1f} {end[1]:.1f}")
    middle = (0.25 * start[0] + 0.5 * control[0] + 0.25 * end[0],
              0.25 * start[1] + 0.5 * control[1] + 0.25 * end[1])
    return path, middle


def _node_lines(thread: m.Thread) -> List[str]:
    """Trois lignes : identifiant, état, priorité effective."""
    state = ev_names.state_name(thread.state)
    prio = f"prio eff {thread.eff_prio}"
    if thread.eff_prio != thread.base_prio:
        prio += f" (base {thread.base_prio})"
    return [f"tid {thread.tid}", state, prio]


def _node_svg(thread: m.Thread, place: Point, cycle: Sequence[int],
              culprit: Optional[m.Verdict],
              inversion: Optional[m.Verdict]) -> str:
    """Boîte d'un thread : état, priorité, mise en évidence du cycle/coupable."""
    in_cycle = thread.tid in cycle
    fill = C_CYCLE_FILL if in_cycle else STATE_FILL.get(thread.state, "#eef3f8")
    stroke = C_NODE_STROKE
    width = 2
    if in_cycle:
        stroke, width = C_CYCLE_STROKE, 3
    if culprit and thread.tid == culprit.tid:
        stroke, width = C_CULPRIT_STROKE, 4
    x, y = place[0] - NODE_W / 2, place[1] - NODE_H / 2
    parts = [f'<rect x="{x:.1f}" y="{y:.1f}" width="{NODE_W}" height="{NODE_H}" '
             f'rx="9" ry="9" fill="{fill}" stroke="{stroke}" '
             f'stroke-width="{width}"/>']
    if inversion and thread.tid in tuple(inversion.chain):
        parts.append(f'<rect x="{x - 5:.1f}" y="{y - 5:.1f}" '
                     f'width="{NODE_W + 10}" height="{NODE_H + 10}" rx="12" ry="12" '
                     f'fill="none" stroke="{C_INVERSION_STROKE}" stroke-width="2" '
                     f'stroke-dasharray="6 4"/>')
    for index, line in enumerate(_node_lines(thread)):
        parts.append(_text(x + 12, y + 20 + index * 16, line, 13,
                           C_TEXT if index == 0 else C_MUTED))
    if culprit and thread.tid == culprit.tid:
        parts.append(_text(x + NODE_W - 62, y - 6, "CUPABLE", 11, C_CULPRIT_STROKE))
    return "".join(parts)


def _text(x: float, y: float, content: str, size: int = 13,
          color: str = C_TEXT, weight: str = "normal") -> str:
    return (f'<text x="{x:.1f}" y="{y:.1f}" font-family="sans-serif" '
            f'font-size="{size}" fill="{color}" font-weight="{weight}">'
            f'{_esc(content)}</text>')


def _edge_svg(edge: m.Edge, places: Dict[int, Point],
              cycle: Sequence[int]) -> str:
    """Arête « T attend R tenu par U » : courbe, flèche et étiquette."""
    if edge.from_tid not in places or edge.to_tid not in places:
        return ""
    start = _trim(places[edge.from_tid], places[edge.to_tid])
    end = _trim(places[edge.to_tid], places[edge.from_tid])
    path, middle = _curve(start, end)
    in_cycle = edge.from_tid in cycle and edge.to_tid in cycle
    stroke = C_EDGE_CYCLE if in_cycle else C_EDGE
    width = 3 if in_cycle else 2
    label = (f"res {edge.resource} · {ev_names.block_reason_name(edge.kind)}")
    spot = _offset(middle, start, end, 28)
    parts = [f'<path d="{path}" fill="none" stroke="{stroke}" stroke-width="{width}" '
             f'marker-end="url(#arrow)"/>']
    box = 7 * len(label) + 8
    parts.append(f'<rect x="{spot[0] - box / 2:.1f}" y="{spot[1] - 10:.1f}" '
                 f'width="{box:.1f}" height="16" fill="{C_BG}" fill-opacity="0.9"/>')
    parts.append(_text(spot[0] - box / 2 + 4, spot[1] + 2, label, 11, C_MUTED))
    return "".join(parts)


def _offset(middle: Point, start: Point, end: Point, distance: float) -> Point:
    """Décale l'étiquette d'une arête perpendiculairement à son axe : deux
    arêtes opposées ne superposent pas leurs étiquettes."""
    dx, dy = end[0] - start[0], end[1] - start[1]
    length = math.hypot(dx, dy) or 1.0
    return (middle[0] - dy / length * distance, middle[1] + dx / length * distance)


def _legend_lines(cycle: Sequence[int], culprit: Optional[m.Verdict],
                  inversion: Optional[m.Verdict]) -> List[str]:
    """Légende : cycle, coupable, inversion — texte, pas d'image."""
    lines: List[str] = []
    if cycle:
        chain = " → ".join(str(tid) for tid in cycle)
        lines.append(f"Cycle d'attente : {chain} → {cycle[0]}")
    else:
        lines.append("Cycle d'attente : aucun cycle détecté")
    if culprit:
        lines.append(f"Coupable : tid {culprit.tid} — {culprit.detail}")
    if inversion:
        chain = " → ".join(str(tid) for tid in inversion.chain)
        heritage = "héritage appliqué" if inversion.a else "héritage non appliqué"
        lines.append(f"Inversion de priorité : {chain} ({heritage})")
    return lines


def render_svg(events: Sequence[m.Event],
               verdicts: Sequence[m.Verdict]) -> str:
    """Graphe d'attente statique en SVG (exigence C7).

    Un nœud par thread (tid, état, priorité effective), une arête par attente
    (étiquette : ressource + raison), cycle mis en évidence, coupable annoté,
    inversion de priorité signalée.
    """
    threads = deadlock.rebuild_states(events)
    edges = deadlock.build_waitfor(events)
    deadlock_verdict = _by_code(verdicts, m.V_DEADLOCK_FOUND)
    culprit = _by_code(verdicts, m.V_CULPRIT)
    inversion = _by_code(verdicts, m.V_PRIO_INVERSION)
    cycle = list(deadlock_verdict.chain) if deadlock_verdict else []
    places = _positions([thread.tid for thread in threads])
    radius = _radius(len(threads))
    width = int(2 * radius + 2 * MARGIN + NODE_W)
    height = int(2 * radius + 2 * MARGIN + NODE_H + TITLE_H + LEGEND_H)
    parts = [_header(width, height, len(threads), len(edges))]
    for edge in edges:
        parts.append(_edge_svg(edge, places, cycle))
    for thread in threads:
        parts.append(_node_svg(thread, places[thread.tid], cycle, culprit,
                               inversion))
    parts.append(_legend(cycle, culprit, inversion, width, height))
    parts.append("</svg>")
    return "".join(parts)


def _header(width: int, height: int, threads: int, edges: int) -> str:
    """Ouverture du document SVG + titre + définitions (flèche)."""
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
        f'height="{height}" viewBox="0 0 {width} {height}" role="img" '
        f'aria-label="Graphe d\'attente Réplique">'
        "<title>Graphe d'attente Réplique</title>"
        "<desc>Un nœud par thread, une arête par attente de ressource. "
        "Les threads du cycle sont en rouge, le coupable est annoté.</desc>"
        '<defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" '
        'markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
        f'<path d="M 0 0 L 10 5 L 0 10 z" fill="{C_EDGE}"/></marker></defs>'
        f'<rect x="0" y="0" width="{width}" height="{height}" fill="{C_BG}"/>'
        + _text(MARGIN, 24, "Graphe d'attente — Réplique", 17, C_TEXT, "bold")
        + _text(MARGIN, 38, f"{threads} thread(s) — {edges} attente(s)", 11,
                C_MUTED)
    )


def _legend(cycle: Sequence[int], culprit: Optional[m.Verdict],
            inversion: Optional[m.Verdict], width: int, height: int) -> str:
    """Légende textuelle en bas de page."""
    parts = [f'<line x1="{MARGIN}" y1="{height - LEGEND_H}" '
             f'x2="{width - MARGIN}" y2="{height - LEGEND_H}" '
             f'stroke="#d7dee5" stroke-width="1"/>']
    for index, line in enumerate(_legend_lines(cycle, culprit, inversion)):
        parts.append(_text(MARGIN, height - LEGEND_H + 20 + index * 18, line, 12,
                           C_TEXT))
    return "".join(parts)
