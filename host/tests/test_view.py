"""test_view.py — tests du rendu SVG (C7) et du rapport Markdown (C6).

Stdlib seule : `xml.etree.ElementTree` vérifie que le SVG est un XML bien
formé, le reste contrôle le contenu affiché (tids, cycle, coupable, empreinte,
hachage de rejeu).
"""
from __future__ import annotations

import unittest
import xml.etree.ElementTree as ET

from replique import kev
from replique import kevgen
from replique import markdown_report
from replique import report
from replique import sched
from replique import verdict
from replique import view


def _deadlock_case():
    """Trace `deadlock` analysée : (en-tête, octets, événements, verdicts, rejeu)."""
    blob = kevgen.blob("deadlock")
    header, events = kev.parse(blob)
    verdicts, result = verdict.pipeline_events(events, cfg=sched.default_config(),
                                               header=header, blob=blob)
    return header, blob, events, verdicts, result


class TestRenderSvg(unittest.TestCase):
    """Le SVG du graphe d'attente doit être un XML bien formé et complet."""

    @classmethod
    def setUpClass(cls) -> None:
        _, _, cls.events, cls.verdicts, cls.result = _deadlock_case()
        cls.svg = view.render_svg(cls.events, cls.verdicts)

    def test_xml_bien_forme(self) -> None:
        root = ET.fromstring(self.svg)
        self.assertTrue(root.tag.endswith("svg"))

    def test_contient_les_tids(self) -> None:
        texte = "".join(ET.fromstring(self.svg).itertext())
        for tid in (101, 102):
            self.assertIn(f"tid {tid}", texte)

    def test_contient_le_cycle(self) -> None:
        texte = "".join(ET.fromstring(self.svg).itertext())
        self.assertIn("Cycle", texte)
        self.assertIn("101 → 102 → 101", texte)

    def test_contient_le_coupable(self) -> None:
        texte = "".join(ET.fromstring(self.svg).itertext())
        self.assertIn("CUPABLE", texte)
        self.assertIn("holds-resource-32-at-prio-60", texte)

    def test_etiquette_des_arêtes(self) -> None:
        texte = "".join(ET.fromstring(self.svg).itertext())
        self.assertIn("res 32", texte)
        self.assertIn("MUTEX", texte)

    def test_echappement_xml(self) -> None:
        """Les caractères '&', '<' et '>' des libellés sont échappés."""
        blob = kevgen.blob("deadlock")
        header, events = kev.parse(blob)
        verdicts, _ = verdict.pipeline_events(events, cfg=sched.default_config(),
                                          header=header, blob=blob)
        for item in verdicts:
            item.detail = "a&b<c>d"
        svg = view.render_svg(events, verdicts)
        ET.fromstring(svg)
        self.assertIn("a&amp;b&lt;c&gt;d", svg)


class TestRenderSvgCasNonBloquant(unittest.TestCase):
    """Un scénario sain produit un SVG sans cycle ni coupable."""

    def test_svg_sans_cycle(self) -> None:
        blob = kevgen.blob("nominal")
        header, events = kev.parse(blob)
        verdicts, _ = verdict.pipeline_events(events, cfg=sched.default_config(),
                                          header=header, blob=blob)
        svg = view.render_svg(events, verdicts)
        ET.fromstring(svg)
        texte = "".join(ET.fromstring(svg).itertext())
        self.assertIn("aucun cycle", texte)


class TestMarkdownReport(unittest.TestCase):
    """Le rapport Markdown doit porter empreinte, chaîne, coupable et rejeu."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.header, cls.blob, cls.events, cls.verdicts, cls.result = \
            _deadlock_case()
        cls.markdown = markdown_report.render(cls.header, cls.blob, cls.events,
                                              cls.verdicts, cls.result)

    def test_empreinte_sha256(self) -> None:
        self.assertIn(report.sha256_hex(self.blob), self.markdown)

    def test_chaine_de_blocage(self) -> None:
        self.assertIn("Chaîne de blocage", self.markdown)
        self.assertIn("101", self.markdown)
        self.assertIn("102", self.markdown)

    def test_coupable(self) -> None:
        self.assertIn("holds-resource-32-at-prio-60", self.markdown)
        self.assertIn("Coupable", self.markdown)

    def test_hachage_de_rejeu(self) -> None:
        self.assertIn(str(self.result.hash), self.markdown)
        self.assertIn(f"{len(self.result.decisions)} décisions", self.markdown)

    def test_horodatage_utc(self) -> None:
        self.assertIn("UTC", self.markdown)

    def test_tableau_des_verdicts(self) -> None:
        self.assertIn("| Code | Sévérité |", self.markdown)
        self.assertIn("DEADLOCK_FOUND", self.markdown)
        self.assertIn("CULPRIT", self.markdown)

    def test_recommandation(self) -> None:
        self.assertIn("## Recommandation", self.markdown)


class TestMarkdownReportSansDeadlock(unittest.TestCase):
    """Scénario sain : pas de coupable, recommandation « aucune anomalie »."""

    def test_rapport_sain(self) -> None:
        blob = kevgen.blob("nominal")
        header, events = kev.parse(blob)
        verdicts, result = verdict.pipeline_events(events,
                                                cfg=sched.default_config(),
                                                header=header, blob=blob)
        markdown = markdown_report.render(header, blob, events, verdicts, result)
        self.assertIn(report.sha256_hex(blob), markdown)
        self.assertIn("Aucun coupable", markdown)
        self.assertIn(str(result.hash), markdown)


if __name__ == "__main__":
    unittest.main()
