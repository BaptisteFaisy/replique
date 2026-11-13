"""test_rda1.py — l'archive produite SUR LA CIBLE contient un RKV1 fidèle.

Artefact de référence : docs/preuves/archive-declic-rkv1.rda1 (21 771 octets),
assemblé par Déclic dans l'invité QNX 8.0 du r630. Le test vérifie que :

  * les six sections sont présentes et leurs empreintes sha256 exactes ;
  * la section `rkv1` est OCTET POUR OCTET ce que le décodeur Python produit
    à partir de la section `trace` (le .kev brut) de la même archive.

C'est la preuve que le décodeur C embarqué dans Déclic et le jumeau Python ne
divergent pas sur une trace réelle capturée sur la cible.
"""
from __future__ import annotations

import os
import unittest

from replique import kev2rkv1, rda1

_ARCHIVE = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "preuves",
                        "archive-declic-rkv1.rda1")


@unittest.skipUnless(os.path.exists(_ARCHIVE), "archive de référence absente")
class Rda1Test(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(_ARCHIVE, "rb") as handle:
            cls.sections = rda1.read(handle.read())

    def test_six_sections(self):
        self.assertEqual(
            list(self.sections),
            ["trace", "pidin", "slog2", "core", "manifest", "rkv1"])

    def test_rkv1_fidele(self):
        self.assertEqual(self.sections["rkv1"][:4], b"RKV1")
        self.assertEqual(self.sections["rkv1"],
                         kev2rkv1.convert(self.sections["trace"]))


if __name__ == "__main__":
    unittest.main()