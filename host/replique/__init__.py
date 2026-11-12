"""Réplique — jumeau Python du coeur C11 (CONTRACT.md).

Stdlib seule, aucune dépendance tierce. Ce paquet est le jumeau de
vérification de core/ : mêmes formats canoniques, mêmes règles, sorties
comparées octet pour octet par tests/differential_c_python.sh.

Importations volontairement absentes ici : chaque module se charge à la
demande (cli.py le fait) pour que `import replique` ne dépende d'aucun
module en cours de construction.
"""

__version__ = "0.1.0"
SCHEMA = "replique/1"
