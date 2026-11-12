/* ham_hook.c — deuxième déclencheur de Déclic : le crochet « HAM »
 * (High Availability Manager).
 *
 * Rôle : être le plus ROBUSTE possible. Le HAM surveille les processus
 * « surveillables » ; quand l'un d'eux meurt, la bibliothèque appelle notre
 * rappel, qui prévient le démon Déclic. Contrairement au crochet `dumper`
 * (dans le processus fautif), le HAM ne demande AUCUNE instrumentation du
 * programme surveillé : il suffit de déclarer le processus comme
 * surveillable par le HAM.
 *
 * Ordre de précocité (à confirmer par l'essai E3, §12 du cahier des
 * charges) : le crochet `dumper` parle AU MOMENT de la faute ; le HAM parle
 * quand la mort du processus est *confirmée* (donc plus tard) ; la scrutation
 * `pidin` du démon est la plus grossière mais la plus universelle.
 *
 * AVERTISSEMENT : le point d'entrée exact de la bibliothèque HAM du SDP 8.0
 * n'est PAS vérifié (le SDP n'est pas installé sur la machine de
 * développement). Les prototypes ci-dessous suivent la documentation
 * publique ; ils seront ajustés après l'essai E3.
 *
 * Compilation : qcc -Vgcc_ntox86_64 -Wall -Wextra -Werror -I<target/declic>
 * dumper_hook.c ham_hook.c
 * declic_hook_notify() est défini dans dumper_hook.c (helper commun).
 */
#include "declic.h"

#ifdef __QNX__

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/* Helper commun : envoi de la notification au démon (dumper_hook.c). */
extern int declic_hook_notify(const declic_term_event *ev);

/* À VÉRIFIER SUR LE SDP (E3) : nom de la bibliothèque (libham ?), en-tête
 * (<ham/...> ?) et signatures exactes de ham_attach() / ham_detach() et du
 * type du rappel d'événement. */
typedef void (*declic_ham_callback)(unsigned pid, int reason);

static declic_ham_callback g_ham_callback;

/* Rappel invoqué par le HAM quand un processus surveillé s'arrête.
 * On traduit l'événement en notification Déclic (pid + statut). */
static void declic_ham_event(unsigned pid, int reason)
{
  declic_term_event ev;

  memset(&ev, 0, sizeof ev);
  ev.pid = (uint32_t)pid;
  ev.status = (reason < 0) ? 0u : (uint32_t)reason;
  ev.signal = 0u; /* le HAM ne dit pas quel signal a tué : à vérifier */
  (void)declic_hook_notify(&ev);
}

/* Branche le rappel sur le HAM.
 * À VÉRIFIER SUR LE SDP (E3) : l'API d'enregistrement d'un callback de
 * surjection de processus et son identifiant de famille. */
extern int ham_attach(void *options, declic_ham_callback callback, int flags);

/* Détache le rappel. */
extern int ham_detach(void);

/* Initialise le crochet HAM : à appeler une fois au démarrage du
 * superviseur. */
int declic_ham_hook_init(void)
{
  g_ham_callback = declic_ham_event;
  if (ham_attach(NULL, g_ham_callback, 0) != 0) {
    fprintf(stderr, "declic-hook: ham_attach : %s\n", strerror(errno));
    return -1;
  }
  return 0;
}

/* Libère le crochet HAM. */
int declic_ham_hook_fini(void)
{
  (void)ham_detach();
  g_ham_callback = NULL;
  return 0;
}

#endif /* __QNX__ */
