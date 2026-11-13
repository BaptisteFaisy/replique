"""rda1.py — lecteur du conteneur d'archive Déclic `RDA1` (CONTRACT.md §3.4).

Une archive est un en-tête de 32 octets, puis une table de `n_sections` entrées
de 80 octets (nom 32, décalage 8, longueur 8, sha256 32), puis les blobs. Chaque
section est vérifiée par empreinte : toute divergence est une erreur, jamais une
reprise approximative.

Usage :
    python3 -m replique.rda1 archive.rda1              # liste les sections
    python3 -m replique.rda1 archive.rda1 rkv1 -o t.rkv1
"""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys

MAGIC = b"RDA1"
MAGICF = struct.Struct("<4s")           # magic, 4 octets ASCII
FIELDS = struct.Struct("<5I")           # version/n_sections/table_off/entry_size/pad
TOTAL = struct.Struct("<Q")             # total, à l'offset 24
ENTRY = struct.Struct("<32sQQ32s")      # nom, offset, longueur, sha256
ENTRY_SIZE = 80


class ArchiveError(Exception):
    """Archive invalide ou section incohérente."""


def read(blob: bytes) -> dict:
    """Rend {nom: octets} après vérification de chaque empreinte sha256."""
    if len(blob) < 32:
        raise ArchiveError("en-tête trop court")
    if MAGICF.unpack_from(blob, 0)[0] != MAGIC:
        raise ArchiveError("mauvais magic")
    _version, n_sections, table_off, entry_size, _pad = FIELDS.unpack_from(blob, 4)
    if entry_size != ENTRY_SIZE:
        raise ArchiveError("taille d'entrée inattendue")
    total, = TOTAL.unpack_from(blob, 24)
    if len(blob) != total:
        raise ArchiveError("longueur totale incohérente")
    out = {}
    for i in range(n_sections):
        base = table_off + i * entry_size
        name, offset, length, digest = ENTRY.unpack_from(blob, base)
        data = blob[offset:offset + length]
        if len(data) != length:
            raise ArchiveError("blob tronqué")
        if hashlib.sha256(data).digest() != digest:
            raise ArchiveError("empreinte invalide : %s" % name.decode())
        out[name.split(b"\x00", 1)[0].decode("ascii")] = data
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description="lit une archive RDA1")
    ap.add_argument("archive")
    ap.add_argument("section", nargs="?", help="nom de section à extraire")
    ap.add_argument("-o", "--output", help="fichier de sortie")
    args = ap.parse_args(argv)
    sections = read(open(args.archive, "rb").read())
    if not args.section:
        for name, data in sections.items():
            print("%-9s %d octets" % (name, len(data)))
        return 0
    if args.section not in sections:
        sys.stderr.write("section absente : %s\n" % args.section)
        return 2
    data = sections[args.section]
    if args.output:
        with open(args.output, "wb") as fh:
            fh.write(data)
    else:
        sys.stdout.buffer.write(data)
    return 0


if __name__ == "__main__":
    sys.exit(main())