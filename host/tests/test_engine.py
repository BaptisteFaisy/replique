"""test_engine.py — tests du moteur sur les scénarios gelés (scenarios/SPEC.md).

Stdlib seule (unittest). Les traces sont construites par `kevgen`/`kev` :
aucun fichier n'est lu, les tests sont donc insensibles aux fixtures.

Écarts constatés entre SPEC.md et le moteur pendant l'écriture (le moteur
n'a PAS été modifié) :
  * SPEC §2 attend `b=16` et `detail=holds-resource-32-at-prio-60` pour le
    scénario `deadlock` — le moteur est conforme ;
  * SPEC §1 attend « aucun verdict d'erreur » pour `nominal` — le moteur
    produit bien OK + REPLAY_DETERMINISTIC (tous deux `info`).
"""
from __future__ import annotations

import unittest

from replique import kev
from replique import kevgen
from replique import model as m
from replique import sched
from replique import verdict


def _pipeline(name: str, cfg: m.SchedConfig | None = None):
    """Chaîne complète sur un scénario : (en-tête, événements, verdicts, rejeu)."""
    blob = kevgen.blob(name)
    header, events = kev.parse(blob)
    verdicts, result = verdict.pipeline_events(events, cfg=cfg, header=header,
                                               blob=blob)
    return header, events, verdicts, result


def _of(verdicts, code: int):
    """Premier verdict portant le code donné (None si absent)."""
    for item in verdicts:
        if item.code == code:
            return item
    return None


class TestNominal(unittest.TestCase):
    """SPEC §1 : trace saine, aucun verdict d'erreur, 4 décisions."""

    @classmethod
    def setUpClass(cls) -> None:
        _, _, cls.verdicts, cls.result = _pipeline("nominal")

    def test_aucun_verdict_erreur(self) -> None:
        erreurs = [v for v in self.verdicts if v.sev == m.SEV_ERROR]
        self.assertEqual(erreurs, [], "aucun verdict d'erreur attendu")

    def test_quatre_decisions(self) -> None:
        self.assertEqual(len(self.result.decisions), 4)

    def test_pas_de_replay_diverged(self) -> None:
        self.assertIsNone(_of(self.verdicts, m.V_REPLAY_DIVERGED))

    def test_replay_deterministe(self) -> None:
        det = _of(self.verdicts, m.V_REPLAY_DETERMINISTIC)
        self.assertIsNotNone(det)
        self.assertEqual(det.hash, self.result.hash)


class TestDeadlock(unittest.TestCase):
    """SPEC §2 : cycle 101↔102, coupable 102."""

    @classmethod
    def setUpClass(cls) -> None:
        _, _, cls.verdicts, cls.result = _pipeline("deadlock")

    def test_deadlock_found(self) -> None:
        found = _of(self.verdicts, m.V_DEADLOCK_FOUND)
        self.assertIsNotNone(found)
        self.assertEqual(tuple(found.chain), (101, 102))
        self.assertEqual(found.b, 16)
        self.assertEqual(found.ts, 1700)
        self.assertEqual(found.sev, m.SEV_ERROR)

    def test_culpable(self) -> None:
        culprit = _of(self.verdicts, m.V_CULPRIT)
        self.assertIsNotNone(culprit)
        self.assertEqual(culprit.tid, 102)
        self.assertEqual(culprit.detail, "holds-resource-32-at-prio-60")

    def test_pas_de_prio_inversion(self) -> None:
        self.assertIsNone(_of(self.verdicts, m.V_PRIO_INVERSION))


class TestPrioInversion(unittest.TestCase):
    """SPEC §3 : triple [201, 203, 202], héritage non appliqué (a=0)."""

    @classmethod
    def setUpClass(cls) -> None:
        _, _, cls.verdicts, cls.result = _pipeline("prio-inversion")

    def test_prio_inversion(self) -> None:
        inv = _of(self.verdicts, m.V_PRIO_INVERSION)
        self.assertIsNotNone(inv)
        self.assertEqual(tuple(inv.chain), (201, 203, 202))
        self.assertEqual(inv.a, 0)
        self.assertEqual(inv.ts, 1600)

    def test_pas_de_deadlock(self) -> None:
        self.assertIsNone(_of(self.verdicts, m.V_DEADLOCK_FOUND))
        self.assertIsNone(_of(self.verdicts, m.V_CULPRIT))


class TestPrioInversionHeritage(unittest.TestCase):
    """SPEC §3 variante : héritage appliqué (a=1)."""

    @classmethod
    def setUpClass(cls) -> None:
        _, _, cls.verdicts, cls.result = _pipeline("prio-inversion-heritage")

    def test_prio_inversion_heritage(self) -> None:
        inv = _of(self.verdicts, m.V_PRIO_INVERSION)
        self.assertIsNotNone(inv)
        self.assertEqual(tuple(inv.chain), (201, 203, 202))
        self.assertEqual(inv.a, 1)
        self.assertEqual(inv.ts, 1600)


class TestRace(unittest.TestCase):
    """SPEC §4 : course, détenteur mort — PAS d'interblocage."""

    @classmethod
    def setUpClass(cls) -> None:
        _, _, cls.verdicts, cls.result = _pipeline("race")

    def test_holder_dead(self) -> None:
        incomplete = _of(self.verdicts, m.V_TRACE_INCOMPLETE)
        self.assertIsNotNone(incomplete)
        self.assertEqual(incomplete.detail, "holder-dead")

    def test_pas_de_deadlock(self) -> None:
        self.assertIsNone(_of(self.verdicts, m.V_DEADLOCK_FOUND))
        self.assertIsNone(_of(self.verdicts, m.V_CULPRIT))


class TestRaceWhatIf(unittest.TestCase):
    """SPEC §4 : « et si » --whatif 402=20 → WHATIF et hachage différent."""

    def test_whatif_hash_different(self) -> None:
        _, _, base_verdicts, base_result = _pipeline("race")
        cfg = sched.default_config()
        cfg.overrides = ((402, 20),)
        _, _, whatif_verdicts, whatif_result = _pipeline("race", cfg)
        marker = _of(whatif_verdicts, m.V_WHATIF)
        self.assertIsNotNone(marker)
        self.assertEqual(marker.tid, 402)
        self.assertEqual(marker.a, 20)
        self.assertNotEqual(base_result.hash, whatif_result.hash)
        self.assertNotEqual(marker.hash, base_result.hash)
        self.assertIsNone(_of(base_verdicts, m.V_WHATIF))


class TestTruncatedBuffer(unittest.TestCase):
    """SPEC §5 : tampon circulaire écrasé → TRACE_INCOMPLETE."""

    @classmethod
    def setUpClass(cls) -> None:
        _, _, cls.verdicts, cls.result = _pipeline("truncated-buffer")

    def test_circular_buffer_overwritten(self) -> None:
        incomplete = _of(self.verdicts, m.V_TRACE_INCOMPLETE)
        self.assertIsNotNone(incomplete)
        self.assertEqual(incomplete.detail, "circular-buffer-overwritten")


class TestCorruptedFixtures(unittest.TestCase):
    """SPEC §5 : un PARSE_ERROR seul, avec le bon détail."""

    CASES = (("corrupt-magic", "bad-magic"),
             ("corrupt-crc", "bad-crc"),
             ("corrupt-truncated", "bad-length"))

    def test_parse_error_seul(self) -> None:
        for name, detail in self.CASES:
            with self.subTest(scenario=name):
                blob = kevgen.blob(name)
                verdicts, _ = verdict.pipeline_blob(blob)
                self.assertEqual(len(verdicts), 1)
                self.assertEqual(verdicts[0].code, m.V_PARSE_ERROR)
                self.assertEqual(verdicts[0].detail, detail)
                self.assertEqual(verdicts[0].sev, m.SEV_ERROR)


class TestDeterminism(unittest.TestCase):
    """P1 : N rejeux d'une même trace → N hachages identiques."""

    def test_hachages_identiques(self) -> None:
        hashes = {result.hash for result in
                  (_pipeline("deadlock")[3] for _ in range(3))}
        self.assertEqual(len(hashes), 1)


if __name__ == "__main__":
    unittest.main()
