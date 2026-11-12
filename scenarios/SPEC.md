# scénarios/SPEC.md — scénarios de référence gelés (exigences S6, P3)

Les quatre scénarios ci-dessous sont **gelés**. Le générateur
(`python3 -m replique.cli gen <nom>`) produit la trace `.rkv1` correspondante ;
`scenarios/<nom>.json` porte le résultat attendu ; les tests différentiels et de
diagnostic s'y réfèrent. Toute modification = PR motivée touchant le générateur,
les attentes et `CONTRACT.md` ensemble.

Conventions : priorités QNX (**0 = plus haute**, 255 = plus basse), `time_unit`
= 1 000 000 000 (ns), `flags` = `SYNTHETIC` (2), horodatages en ns. Chaque
scénario commence par `PROCESS_CREATE` + `THREAD_CREATE` pour chaque thread, et
chaque changement d'état s'accompagne de l'événement `SCHED` correspondant
(c'est ce que compare la fidélité P2).

---

## 1. `nominal` — trace saine (aucun verdict d'erreur)

Deux processus : `pid 1 / tid 1` (serveur, priorité 10, FIFO) et `pid 2 / tid 2`
(client, priorité 20, FIFO). Le client fait un `MsgSend` au serveur, le serveur
reçoit, répond, puis se bloque sur une temporisation ; le client tourne ; la
temporisation échoit, le serveur préempte le client, puis le serveur se termine.

Attendu :
- `REPLIQUE_OK` (info) — parse réussi, `n_events` = celui du fichier ;
- aucun `DEADLOCK_FOUND`, aucun `CULPRIT`, aucun `PRIO_INVERSION` ;
- `REPLAY_DETERMINISTIC` avec un `hash` stable (P1) ;
- aucun `REPLAY_DIVERGED` : la séquence rejouée reproduit les `SCHED` de la trace (P2).

Prouve : S4 (parse), X1 (modèle), X2 (déterminisme), X3 (fidélité sur cas sain).

---

## 2. `deadlock` — interblocage à deux threads (le scénario de la démo)

Threads : `pid 10 / tid 101` (priorité de base **50**), `pid 10 / tid 102`
(priorité de base **60**). Ressources : mutex `0x10` (16) et mutex `0x20` (32).

Séquence gelée (l'ordre compte : 102 prend le mutex 32 AVANT que 101 ne
devienne prêt, sinon aucun des deux ne peut détenir une ressource) :
1. `tid 102` (prio 60) est élu en premier, acquiert le mutex 32, puis se bloque
   sur une temporisation ;
2. `tid 101` (prio 50) devient prêt, préempte, acquiert le mutex 16 ;
3. la temporisation de 102 échoit : il redevient prêt (sans préempter 101) ;
4. `tid 101` tente le mutex 32 → `MUTEX_LOCK sub=1 a=32 b=102` puis
   `THREAD_STATE sub=3(BLOCKED) a=1(MUTEX) b=32 c=102` ; 101 bloqué → 102 élu ;
5. `tid 102` tente le mutex 16 → `MUTEX_LOCK sub=1 a=16 b=101` puis
   `THREAD_STATE sub=3(BLOCKED) a=1(MUTEX) b=16 c=101` ;
6. plus aucun thread prêt : aucune décision d'ordonnancement n'est plus
   possible, la trace s'arrête là (`PROCESS_DESTROY` absent — le crash est
   constaté par Déclic).

Attendu :
- `REPLIQUE_DEADLOCK_FOUND` (error), `chain=101,102` (cycle en ordre croissant,
  **premier tid du cycle = plus petit tid**), `b=16` (ressource du **dernier**
  blocage observé, celui qui ferme le cycle), `ts` = horodatage de ce dernier
  événement de blocage ;
- `REPLIQUE_CULPRIT` (error), `tid=102`, `detail=holds-resource-32-at-prio-60`
  — règle §3.3 : dans le cycle, 102 détient la ressource acquise le plus
  anciennement (32) **et** la priorité effective la plus basse (60 > 50
  numériquement = moins prioritaire) ;
- `REPLAY_DETERMINISTIC` (info) — le rejeu reproduit le blocage à l'identique ;
- aucun `PRIO_INVERSION`.

Prouve : C2, C3, P3 (coupable désigné = coupable attendu).

---

## 3. `prio-inversion` — inversion de priorité sans héritage

Threads : `tid 201` (priorité **10**, haute), `tid 202` (priorité **100**,
basse), `tid 203` (priorité **50**, intermédiaire). Ressource : mutex `0x30` (48).

Séquence sémantique :
1. `tid 202` acquiert le mutex 48 ;
2. `tid 201` tente le mutex 48 → bloqué (raison `MUTEX`, détenteur 202) ;
3. `tid 203` devient `READY` puis `RUNNING` (décision `PREEMPT`) **pendant** que
   `tid 201` est bloqué : le thread intermédiaire vole le CPU au thread haute
   priorité ;
4. aucun `PRIO_CHANGE` d'héritage dans la trace (`subcode` 1 jamais émis).

Attendu :
- `REPLIQUE_PRIO_INVERSION` (warning), `chain=201,203,202`
  ([victime, intermédiaire, bloqueur]), `a=0` (héritage **non** appliqué),
  `ts` = premier instant où l'intermédiaire tourne pendant le blocage ;
- aucun `DEADLOCK_FOUND`, aucun `CULPRIT` ;
- `REPLAY_DETERMINISTIC` (info).

Prouve : C5 (détection + vérification de l'héritage).

Variante `prio-inversion-heritage` (même scénario, l'étape 3 bis émet
`PRIO_CHANGE sub=1 a=10 b=100 c=201` élevant `tid 202` à la priorité 10) :
attendu `PRIO_INVERSION` avec `a=1`. Les deux variantes sont générées ; seule la
première est exigée au niveau C minimal.

---

## 4. `race` — course avec terminaison (le cas « une fois sur mille »)

Threads : `pid 40 / tid 401` (priorité 30) et `pid 41 / tid 402` (priorité 30,
**même priorité**, politique `RR` avec quantum 1 000 ns). Ressource partagée :
mutex `0x40` (64).

Séquence sémantique :
1. `tid 401` acquiert le mutex 64, travaille, puis le libère
   (`MUTEX_UNLOCK a=64`) ;
2. `tid 402` devient `READY`, le quantum expire (`TIMER`) → décision `TIMER`,
   `tid 402` tourne ;
3. `tid 402` acquiert le mutex 64 à son tour, puis **se termine brutalement**
   (`PROCESS_DESTROY pid 41` avec `b=11` = signal SEGV) **sans** libérer le mutex ;
4. `tid 401` tente ensuite le mutex 64 → `MUTEX_LOCK sub=1 a=64 b=402` puis
   bloqué sur un détenteur **mort** : attente sans fin, mais **pas de cycle**.

Attendu :
- `REPLIQUE_TRACE_INCOMPLETE` (warning), `detail=holder-dead` (le détenteur de
  la ressource attendue est terminé) — jamais `DEADLOCK_FOUND` : un cycle
  exige un chemin fermant sur lui-même ;
- aucun `CULPRIT` ;
- `REPLAY_DETERMINISTIC` (info), `hash` stable sur 5 rejeux (P1) ;
- le rejeu avec `--whatif 402=20` (passer `tid 402` en priorité plus haute) doit
  produire `REPLIQUE_WHATIF` (info) et un `hash` **différent** du rejeu de base :
  c'est la démonstration du mode « et si » (X4).

Prouve : X2, X4, et la distinction course/interblocage.

---

## 5. Traces dégradées (parseur, exigence S4)

| Fixture | Défaut injecté | Verdict attendu |
|---|---|---|
| `corrupt-magic.rkv1` | `magic` = `RKV0` | `PARSE_ERROR`, `a=0`, `detail=bad-magic` |
| `corrupt-crc.rkv1` | un octet du blob inversé | `PARSE_ERROR`, `detail=bad-crc` |
| `corrupt-truncated.rkv1` | dernier enregistrement coupé de moitié | `PARSE_ERROR`, `detail=bad-length` |
| `truncated-buffer.rkv1` | trace valide avec `flags` bit 0 (`TRUNCATED`) | `TRACE_INCOMPLETE`, `detail=circular-buffer-overwritten` |

Un `PARSE_ERROR` est **toujours seul** (CONTRACT.md §3.2) : aucun autre verdict.

---

## 6. Fichiers

```
fixtures/<nom>.rkv1              trace binaire (engendrée, versionnée)
fixtures/<nom>.tp.txt            oracle traceprinter, si SDP disponible (optionnel)
scenarios/<nom>.json             attentes gelées (voir schéma ci-dessous)
```

Schéma de `scenarios/<nom>.json` (clés dans cet ordre, JSON compact) :

```json
{"schema":"replique-scenario/1","name":"deadlock","fixture":"fixtures/deadlock.rkv1",
 "n_events":0,"flags":2,"time_unit":1000000000,
 "expected_verdicts":[{"code":"DEADLOCK_FOUND","sev":"error","chain":[101,102]}],
 "forbidden_verdicts":["PRIO_INVERSION"],
 "replay":{"deterministic":true,"hash":0,"whatif":{"tid":0,"prio":0,"hash_changes":false}}}
```

Les valeurs numériques (`n_events`, `hash`) sont **remplies depuis une exécution
réelle** et vérifiées identiques en C et en Python avant d'être gelées (script
`tests/freeze_scenarios.sh`). Un `hash` gelé qui ne correspond pas aux deux
jumeaux = test rouge, jamais de mise à jour silencieuse.
