# docs/REPORT-TEMPLATE.md — rapport « interblocage expliqué en une page » (C6)

Structure gelée par `CONTRACT.md` §4.5 : le rapport Markdown est produit par l'hôte (`host/replique/markdown_report.py`, fonction `render(header, blob, events, verdicts, replay)`). C'est une **sortie dérivée hôte** : elle est horodatée (l'horloge y est permise — elle est hors différentiel C↔Python, contrairement aux sorties canoniques qui n'en contiennent jamais, CONTRACT.md §7).

## Sections obligatoires, dans l'ordre

| # | Section | Contenu | Source des valeurs |
|---|---|---|---|
| 1 | **Titre** | `# Rapport Réplique — <verdict dominant>` (ex. « interblocage ») | verdicts (code dominant après tri §4.4) |
| 2 | **Horodatage** | date et heure de production du rapport (hôte) | horloge hôte, hors canonique |
| 3 | **Empreinte SHA-256** | empreinte du fichier de trace complet, hexadécimal minuscule | `report.sha256_hex(blob)` ; clé `trace.sha256` du JSON §4.6 |
| 4 | **Chaîne de blocage** | le cycle du graphe d'attente, thread par thread : « T attend R tenu par U » | verdict `DEADLOCK_FOUND` (`chain`, `b`) + arêtes `replique_waitfor_build` |
| 5 | **Coupable** | `tid` + raison gelée | verdict `CULPRIT` (`tid`, `detail=holds-resource-<id>-at-prio-<p>`, règle §3.3) |
| 6 | **Inversion éventuelle** | triple [victime, intermédiaire, bloqueur] et héritage appliqué ou non ; absent si aucun verdict | verdict `PRIO_INVERSION` (`chain`, `a`, règle §3.4) |
| 7 | **Hachage de rejeu** | hachage FNV-1a 64 (décimal) de la séquence de décisions + nombre de décisions | `replay.hash`, `replay.n_decisions` (JSON §4.6) ; verdict `REPLAY_DETERMINISTIC` |
| 8 | **Recommandation** | action courte déduite du diagnostic (ordre d'acquisition des verrous, priorité, héritage) | rédigée par l'hôte à partir des verdicts |

Règles : une page (pas de liste d'événements brute — la trace complète reste accessible via `dump`/`--json`) ; aucune valeur inventée — chaque champ vient d'un verdict ou du hachage de rejeu ; si la trace est illisible, le rapport ne se réduit pas à une approximation : `PARSE_ERROR` seul (CONTRACT.md §3.2).

## Gabarit

```markdown
# Rapport Réplique — <verdict dominant>

Produit le <AAAA-MM-JJ HH:MM:SS TZ> par Réplique (hôte).

- Trace : `<chemin/nom.rkv1>`
- Empreinte SHA-256 : <64 hex minuscules>
- Événements : <n_events> — drapeaux : <TRUNCATED/SYNTHETIC/0>

## Chaîne de blocage

<cycle, une ligne par arête : « tid T (prio P) attend <ressource R> tenu par tid U »>

## Coupable

**tid <T>** — <detail gelé du verdict CULPRIT> (<règle §3.3 en une phrase>).

## Inversion de priorité

<triple [V, I, B] + héritage appliqué (a=1) ou non (a=0) ; « aucune détectée » sinon>

## Rejeu

- Hachage FNV-1a 64 : <décimal> — <n> décisions
- Déterminisme : <N rejeux identiques (P1)> ; fidélité : <conforme / divergence nommée>

## Recommandation

<une à trois lignes actionnables>
```

## Exemple rempli — scénario `deadlock` (SPEC.md §2)

Valeurs issues des attentes gelées du scénario : `tid 101` (priorité 50) et `tid 102` (priorité 60), mutex 16 (0x10) et 32 (0x20). **Horodatage, empreinte et hachage illustratifs** — les valeurs réelles viennent de l'archive et du rejeu ; l'empreinte d'une fixture est stable et sera gelée dans `scenarios/deadlock.json`.

```markdown
# Rapport Réplique — interblocage

Produit le 2026-11-12 14:03:11 CET par Réplique (hôte).

- Trace : `fixtures/deadlock.rkv1`
- Empreinte SHA-256 : 9f2c…<64 hex>…ab10
- Événements : 42 — drapeaux : SYNTHETIC

## Chaîne de blocage

Cycle fermé entre deux threads du processus 10 (mutex 16 et 32) :

- tid 101 (prio 50) attend le mutex 32, tenu par tid 102 ;
- tid 102 (prio 60) attend le mutex 16, tenu par tid 101.

Aucun thread RUNNING : l'attente est sans fin (DEADLOCK_FOUND, chain=101,102, ressource 32).

## Coupable

**tid 102** — `holds-resource-16-at-prio-60` : dans le cycle, c'est le thread qui
détient la ressource acquise le plus anciennement avec la priorité effective la
plus basse (60 > 50 numériquement = moins prioritaire) — règle gelée CONTRACT.md §3.3.

## Inversion de priorité

Aucune détectée (attendu : le scénario `deadlock` n'a pas de thread
intermédiaire ; `PRIO_INVERSION` figure dans les verdicts interdits de
`scenarios/deadlock.json`).

## Rejeu

- Hachage FNV-1a 64 : 1234567890123456789 — 6 décisions
- Déterminisme : 5 rejeux, 5 hachages identiques (P1) ; fidélité : conforme
  aux événements SCHED de la trace (P2, aucune divergence).

## Recommandation

Imposer un ordre global d'acquisition des verrous (toujours 16 avant 32, par
exemple) dans le processus 10 ; à défaut, utiliser des acquisitions temporisées
et libérer 16 avant d'attendre 32. Rejouer avec `--whatif` pour vérifier qu'un
changement de priorité ne suffit pas : un interblocage d'ordre d'acquisition ne
se répare pas par les priorités.
```
