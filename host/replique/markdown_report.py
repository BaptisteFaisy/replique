"""markdown_report.py — rapport « interblocage expliqué en une page » (C6).

Le rapport est une sortie DÉRIVÉE de l'hôte : c'est la seule sortie qui
contient une horloge réelle (hors différentiel C↔Python, CONTRACT.md §4.5 et
§7). Toute valeur affichée provient d'un verdict, du hachage de rejeu ou de
l'archive — aucune valeur inventée (docs/REPORT-TEMPLATE.md).
"""
from __future__ import annotations

from datetime import datetime, timezone
from typing import Optional, Sequence

from replique import deadlock
from replique import event as ev_names
from replique import model as m
from replique import report

# Drapeaux de l'en-tête (CONTRACT.md §1.2), dans l'ordre des bits.
FLAG_NAMES = ((m.FLAG_TRUNCATED, "TRUNCATED"), (m.FLAG_SYNTHETIC, "SYNTHETIC"))

def _by_code(verdicts: Sequence[m.Verdict], code: int) -> Optional[m.Verdict]:
    for item in verdicts:
        if item.code == code:
            return item
    return None


def _flags_text(flags: int) -> str:
    names = [name for bit, name in FLAG_NAMES if flags & bit]
    return "|".join(names) if names else "0"


def _title(verdicts: Sequence[m.Verdict]) -> str:
    """Verdict dominant → titre du rapport."""
    for code, label in ((m.V_DEADLOCK_FOUND, "interblocage"),
                        (m.V_PARSE_ERROR, "trace illisible"),
                        (m.V_PRIO_INVERSION, "inversion de priorité"),
                        (m.V_REPLAY_DIVERGED, "rejeu divergent"),
                        (m.V_TRACE_INCOMPLETE, "trace incomplète")):
        if _by_code(verdicts, code):
            return f"# Rapport Réplique — {label}"
    return "# Rapport Réplique — analyse"


def _archive_section(header: m.Header, blob: bytes, events: Sequence[m.Event]) -> str:
    """Empreinte de l'archive, nombre d'événements et drapeaux."""
    return (
        f"- Archive : RKV1 v{header.version} — {len(blob)} octets\n"
        f"- Empreinte SHA-256 : `{report.sha256_hex(blob)}`\n"
        f"- Événements : {len(events)} — drapeaux : {_flags_text(header.flags)}"
    )


def _chain_section(verdicts: Sequence[m.Verdict],
                   events: Sequence[m.Event]) -> str:
    """Chaîne de blocage : cycle et arêtes « T attend R tenu par U »."""
    found = _by_code(verdicts, m.V_DEADLOCK_FOUND)
    if not found:
        return "Aucun cycle d'attente détecté dans le graphe."
    threads = {t.tid: t for t in deadlock.rebuild_states(events)}
    lines = ["Cycle fermé : " + " → ".join(str(t) for t in found.chain)
             + f" (ressource du dernier blocage : {found.b}).", ""]
    for edge in deadlock.build_waitfor(events):
        waiter = threads.get(edge.from_tid)
        prio = waiter.base_prio if waiter else "?"
        lines.append(
            f"- tid {edge.from_tid} (prio {prio}) attend la ressource "
            f"{edge.resource} ({ev_names.block_reason_name(edge.kind)}), "
            f"tenue par tid {edge.to_tid} ;")
    return "\n".join(lines)


def _culprit_section(verdicts: Sequence[m.Verdict]) -> str:
    """Coupable désigné par la règle gelée §3.3."""
    culprit = _by_code(verdicts, m.V_CULPRIT)
    if not culprit:
        return "Aucun coupable désigné (pas de cycle dans le graphe d'attente)."
    return (f"**tid {culprit.tid}** — `{culprit.detail}` : dans le cycle, ce thread "
            "détient la ressource acquise le plus anciennement et a la priorité "
            "de base la plus basse (règle gelée CONTRACT.md §3.3).")


def _inversion_section(verdicts: Sequence[m.Verdict]) -> str:
    """Inversion de priorité : triple [victime, intermédiaire, bloqueur]."""
    inversion = _by_code(verdicts, m.V_PRIO_INVERSION)
    if not inversion:
        return "Aucune inversion de priorité détectée."
    victim, middle, blocker = (list(inversion.chain) + [0, 0, 0])[:3]
    heritage = ("héritage appliqué (`a=1`)" if inversion.a
                else "héritage **non** appliqué (`a=0`)")
    return (f"Triple [victime, intermédiaire, bloqueur] = "
            f"[{victim}, {middle}, {blocker}] — {heritage} ; "
            f"ressource en cause : `{inversion.detail}`.")


def _replay_section(replay: Optional[m.ReplayResult],
                    verdicts: Sequence[m.Verdict]) -> str:
    """Hachage de rejeu (FNV-1a 64) et nombre de décisions."""
    if replay is None:
        return "Rejeu non exécuté."
    lines = [f"- Hachage FNV-1a 64 : {replay.hash} — "
             f"{len(replay.decisions)} décisions"]
    lines.append(f"- Fidélité : {'divergence' if replay.diverged else 'conforme'}"
                 " (rejeu comparé aux événements SCHED de la trace)")
    incomplete = _by_code(verdicts, m.V_TRACE_INCOMPLETE)
    if incomplete:
        lines.append(f"- Trace incomplète : `{incomplete.detail}`")
    return "\n".join(lines)


def _recommendation(verdicts: Sequence[m.Verdict]) -> str:
    """Recommandation actionnable, déduite du diagnostic."""
    found = _by_code(verdicts, m.V_DEADLOCK_FOUND)
    culprit = _by_code(verdicts, m.V_CULPRIT)
    inversion = _by_code(verdicts, m.V_PRIO_INVERSION)
    if found and culprit:
        return ("Casser le cycle : le thread "
                f"{culprit.tid} détient la ressource {culprit.b} — lui faire "
                "relâcher le verrou (libération ordonnée) ou lui accorder une "
                "priorité plus élevée pour qu'il termine. En prévention, "
                "imposer un ordre global d'acquisition des verrous et des "
                "acquisitions temporisées avec abandon.")
    if inversion and not inversion.a:
        return ("Activer l'héritage de priorité sur le bloqueur "
                f"(PRIO_CHANGE sub=1) ou abaisser la priorité de l'intermédiaire ; "
                "sinon le thread de haute priorité reste bloqué. Vérifier aussi "
                "que le bloqueur atteint la zone critique sans attente.")
    if inversion:
        return ("L'héritage de priorité est appliqué : le bloqueur a été élevé "
                "au niveau de la victime ; la trace montre que le gain n'a pas "
                "suffi, réduire la section critique du bloqueur.")
    if _by_code(verdicts, m.V_TRACE_INCOMPLETE):
        return ("Trace incomplète : compléter la collecte (tampon circulaire ou "
                "détenteur disparu) avant tout diagnostic définitif.")
    if _by_code(verdicts, m.V_REPLAY_DIVERGED):
        return ("Le rejeu diverge de la trace : comparer la première décision "
                "divergente et vérifier la configuration d'ordonnancement.")
    if _by_code(verdicts, m.V_PARSE_ERROR):
        return ("Trace illisible : corriger la collecte (en-tête, CRC ou "
                "longueur) puis relancer l'analyse.")
    return ("Aucune anomalie bloquante détectée : conserver la trace et le "
            "hachage de rejeu comme référence de comparaison.")


def _verdict_table(verdicts: Sequence[m.Verdict]) -> str:
    """Tableau Markdown des verdicts (§4.2)."""
    lines = ["| Code | Sévérité | ts | tid | a | b | Chaîne | Détail |",
             "|---|---|---|---|---|---|---|---|"]
    for item in verdicts:
        chain = ",".join(str(t) for t in tuple(item.chain)[: m.MAX_CHAIN])
        lines.append(
            f"| {ev_names.verdict_name(item.code)} "
            f"| {ev_names.severity_name(item.sev)} | {item.ts} | {item.tid} "
            f"| {item.a} | {item.b} | {chain or '—'} "
            f"| `{report.sanitize_detail(item.detail) or '—'}` |")
    return "\n".join(lines)


def render(header: m.Header, blob: bytes, events: Sequence[m.Event],
           verdicts: Sequence[m.Verdict],
           replay: Optional[m.ReplayResult] = None) -> str:
    """Rapport Markdown « interblocage expliqué en une page » (exigence C6).

    Ordre des sections : titre, horodatage, archive (empreinte, événements,
    drapeaux), chaîne de blocage, coupable, inversion de priorité, rejeu,
    recommandation, tableau des verdicts.
    """
    stamp = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
    sections = [
        _title(verdicts),
        f"Produit le {stamp} par Réplique (hôte).",
        _archive_section(header, blob, events),
        "## Chaîne de blocage\n\n" + _chain_section(verdicts, events),
        "## Coupable\n\n" + _culprit_section(verdicts),
        "## Inversion de priorité\n\n" + _inversion_section(verdicts),
        "## Rejeu\n\n" + _replay_section(replay, verdicts),
        "## Recommandation\n\n" + _recommendation(verdicts),
        "## Verdicts\n\n" + _verdict_table(verdicts),
    ]
    return "\n\n".join(sections) + "\n"
