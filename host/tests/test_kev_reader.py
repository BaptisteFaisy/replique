"""test_kev_reader.py — décodage du vrai `.kev` QNX 8.0 (essai E1).

Vérifie que le lecteur binaire (`kev_reader`) et l'adaptateur (`kev2rkv1`)
extraient, d'une trace réelle produite sur la cible, exactement ce que
l'oracle `traceprinter` rapporte (comptes par type), et que le `RKV1` produit
est lisible par le cœur portable (déterminisme des hachages).

La trace de référence est `docs/preuves/trace-qnx800.kev` (7 484 octets,
capturée dans l'invité QNX 8.0 et figée par Déclic).
"""
from __future__ import annotations

import collections
import os
import unittest

from replique import kev
from replique import kev2rkv1
from replique import kev_reader as kr
from replique import model as m

_KEV = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "preuves",
                    "trace-qnx800.kev")


def _counts(events):
    c = collections.Counter()
    for e in events:
        c[(e.cls, e.code)] += 1
    return c


@unittest.skipUnless(os.path.exists(_KEV), "trace de référence absente")
class KevReaderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(_KEV, "rb") as fh:
            cls.blob = fh.read()
        cls.hdr, cls.events = kr.logical_events(cls.blob)

    def test_header(self):
        self.assertEqual(self.hdr.cpu_num, 4)
        self.assertGreater(self.hdr.cycles_per_sec, 1_000_000)

    def test_comptes_oracle(self):
        # chiffres vérifiés contre traceprinter (docs/preuves/traceprinter-qnx800.txt)
        c = _counts(self.events)
        self.assertEqual(c[(4, kr.TH_CREATE)], 61)         # THCREATE
        self.assertEqual(c[(4, kr.TH_NAME)], 43)           # PROCTHREAD_NAME
        self.assertEqual(c[(4, kr.P_CREATE_NAME)], 11)     # PROCCREATE_NAME

    def test_ts_monotone(self):
        for a, b in zip(self.events, self.events[1:]):
            self.assertLessEqual(a.ts, b.ts)

    def test_conversion_lisible(self):
        blob = kev2rkv1.convert(self.blob)
        header, events = kev.parse(blob)
        self.assertGreater(len(events), 0)
        self.assertEqual(header.time_unit, self.hdr.cycles_per_sec)
        # rejouer le RKV1 réel ne doit pas lever, et deux conversions sont égales
        self.assertEqual(kev2rkv1.convert(self.blob), blob)

    def test_fixture_commitee(self):
        path = os.path.join(os.path.dirname(__file__), "..", "..", "docs",
                            "preuves", "trace-qnx800.rkv1")
        if not os.path.exists(path):
            self.skipTest("fixture RKV1 absente")
        with open(path, "rb") as handle:
            self.assertEqual(handle.read(), kev2rkv1.convert(self.blob))

    def test_prio_change_derive(self):
        """La trace « wide » contient de vrais changements de priorité."""
        path = os.path.join(os.path.dirname(__file__), "..", "..", "docs",
                            "preuves", "trace-qnx800-wide.kev")
        if not os.path.exists(path):
            self.skipTest("trace wide absente")
        with open(path, "rb") as handle:
            blob = handle.read()
        _, events = kev.parse(kev2rkv1.convert(blob))
        changes = [e for e in events if e.cls == m.EV_PRIO_CHANGE]
        self.assertGreater(len(changes), 0)
        for e in changes:
            self.assertNotEqual(e.a, e.b)      # la priorité a vraiment changé
            self.assertGreater(e.a, 0)


if __name__ == "__main__":
    unittest.main()