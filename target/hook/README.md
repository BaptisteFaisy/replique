# target/hook — les deux déclencheurs alternatifs de Déclic

Déclic a besoin de savoir **le plus tôt possible** qu'un processus vient de
terminer, pour figer le tampon de trace circulaire *avant* qu'il ne soit
écrasé. Ce dossier contient deux déclencheurs alternatifs, plus le repli
« événement noyau » déjà implémenté dans le démon.

| Déclencheur | Fichier | Où s'exécute | Précocité | Instrumentation du programme ? |
|---|---|---|---|---|
| **Crochet `dumper`** | `dumper_hook.c` | dans le processus fautif | **la plus précoce** — à l'instant de la faute | **oui** (le programme installe le crochet) |
| Crochet **HAM** | `ham_hook.c` | dans le processus HAM | intermédiaire — à la *confirmation* de la mort | non (déclaration au HAM) |
| Repli **événement noyau** (`pidin`) | `target/declic/declic_qnx.c` | dans le démon | la plus grossière (période de scrutation) | non |

## Lequel est le plus précoce ? → le crochet `dumper`

Le gestionnaire de signal du crochet `dumper` s'exécute **pendant** la
faute, avant que le processus ne meure : la notification part au moment
exact où l'information est encore fraîche. Le HAM, lui, n'apprend la mort
d'un processus que lorsque le microkernel l'a confirmée — donc plus tard. La
scrutation `pidin` est plus tard encore (latence = période de scrutation).

C'est exactement la préoccupation du cahier des charges (§12, risque E3 :
« Déclic déclenché trop tard ») et la raison pour laquelle le crochet
`dumper` est le **premier** crochet essayé.

**Contrepartie** : le crochet `dumper` n'est disponible que pour les
programmes instrumentés (il faut appeler `declic_hook_install()`), et il ne
couvre que les fautes *interceptables* par un gestionnaire de signal. Le HAM
couvre n'importe quel processus déclaré surveillable, y compris une
terminaison volontaire — d'où l'intérêt d'avoir les deux.

**Honnêteté** : cet ordre est déduit de la documentation publique, **pas
mesuré** sur le SDP (l'essai E3 n'est pas tranché). Il faut le confirmer sur
cible avant de figer le choix.

---

## Essai E3 : comment essayer

### Pré-requis

- SDP 8.0 installé (`source ~/qnx800/qnxsdp-env.sh`), image QEMU buildable
  (voir `image/README.md`) ;
- le démon lancé en écoute du canal :

```sh
declic --out /var/log/declic --all --once
```

### Essai E3-a — crochet `dumper` (le plus précoce)

Dans le programme à instrumenter :

```c
#include "declic.h"
extern int declic_hook_install(void);

int main(void)
{
    declic_hook_install();   /* AVANT toute opération risquée */
    ...
}
```

Puis, dans l'invité QEMU :

```sh
kill -SEGV <pid>          # faute simulée
ls -l /var/log/declic/    # l'archive doit apparaître
```

Vérifier que l'archive est **complète** (trace non vide, `pidin`, `slog2`,
`core`, `manifeste`) et que son horodatage est celui de la faute.

### Essai E3-b — crochet HAM

Dans le superviseur (le processus qui demande la surveillance) :

```c
extern int declic_ham_hook_init(void);
...
declic_ham_hook_init();
```

Puis tuer le processus surveillé et vérifier que l'archive apparaît **après**
la confirmation de la mort (donc plus tard que E3-a) — c'est la mesure qui
tranche l'ordre de précocité.

### Essai E3-c — repli `pidin` (sans rien instrumenter)

```sh
declic --out /var/log/declic --all --once --poll 200
```

Tuer un processus sans crochet : l'archive doit apparaître après au plus
`--poll` millisecondes. C'est la mesure de la latence du repli.

### Ce qu'il faut mesurer (à consigner dans `docs/VALIDATION.md`)

1. **latence** entre la terminaison réelle et l'écriture de l'archive, pour
   chacun des trois déclencheurs ;
2. **couverture** : le crochet `dumper` rate-t-il les terminaisons
   volontaires ? le HAM rate-t-il les fautes non interceptables ?
3. **fiabilité** : l'archive produite est-elle toujours complète (5 sections,
   empreintes conformes) ?

---

## Compilation des crochets

```sh
cd target/hook
qcc -Vgcc_ntox86_64 -std=c11 -O2 -Wall -Wextra -Werror -I../declic \
    -c dumper_hook.c ham_hook.c
```

`declic_hook_notify()` (helper d'envoi de la notification) est défini dans
`dumper_hook.c` et utilisé par `ham_hook.c` : **une seule implémentation**, les
deux fichiers se compilent ensemble.

Les deux fichiers sont entièrement sous `#ifdef __QNX__` : ils ne sont jamais
compilés par `make test` sur PC.

---

## Points non vérifiés (SDP absent)

- `name_open()` / `name_attach()` : ouverture d'un canal nommé entre le
  crochet (dans le processus fautif) et le démon, et usage de `MsgSend()`
  sur le descripteur obtenu ;
- la liste exacte des signaux à traiter et l'effet de l'installation d'un
  gestionnaire sur la production du core dump par `dumper` ;
- l'API réelle du HAM : nom de la bibliothèque, en-tête, signature de
  `ham_attach()` / `ham_detach()`, forme du rappel d'événement, et la
  granularité de la notification (le HAM dit-il *quel* signal a tué ?) ;
- la nécessité (ou non) d'un droit particulier pour ouvrir le canal nommé.
