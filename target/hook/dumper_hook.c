/* dumper_hook.c — premier déclencheur de Déclic : le crochet « dumper ».
 *
 * Rôle : être le PLUS PRÉCOCE possible. Le processus surveillé installe ce
 * crochet ; à l'instant de la faute (SIGSEGV, SIGABRT, SIGILL, SIGFPE,
 * SIGBUS), le gestionnaire de signal envoie IMMÉDIATEMENT une notification
 * au démon Déclic, puis rend la main au comportement par défaut : le
 * processus meurt exactement comme avant et le core dump est produit par
 * `dumper` comme d'habitude.
 *
 * Le crochet ne fige rien lui-même : il PRÉVIENT. Le figeage de la trace et
 * la collecte restent dans le démon (target/declic/declic_qnx.c), qui
 * possède les droits et la durée de vie nécessaires.
 *
 * Utilisation (dans le programme à instrumenter) :
 *     #include "declic.h"
 *     extern int declic_hook_install(void);
 *     ...
 *     declic_hook_install();
 *
 * Compilation : qcc -Vgcc_ntox86_64 -Wall -Wextra -Werror -I<target/declic>
 * Ce fichier n'est JAMAIS compilé par `make test` sur PC (tout est sous
 * `#ifdef __QNX__`).
 */
#include "declic.h"

#ifdef __QNX__

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/neutrino.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <name.h> /* name_open() — à vérifier sur le SDP 8.0 (E3) */

/* Horodatage fourni au démon (ticks) — l'horloge est ici, pas dans la
 * logique portable. */
static uint64_t hook_now_ticks(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    return 0u;
  }
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* Envoie la notification au démon Déclic sur le canal nommé.
 * Shared: cette fonction est aussi utilisée par ham_hook.c (helper commun,
 * une seule implémentation).
 * À VÉRIFIER SUR LE SDP (E3) : name_open() sur un canal créé par name_attach()
 * côté démon, et l'usage de MsgSend() sur le descripteur obtenu. */
int declic_hook_notify(const declic_term_event *ev)
{
  declic_trigger_msg msg;
  int coid;
  int rc;

  if (!ev) {
    return -1;
  }
  coid = name_open(DECLIC_TRIGGER_CHANNEL, O_WRONLY);
  if (coid < 0) {
    return -1;
  }
  memset(&msg, 0, sizeof msg);
  msg.magic = DECLIC_TRIGGER_MAGIC;
  msg.pid = ev->pid;
  msg.status = ev->status;
  msg.signal = ev->signal;
  msg.timestamp = hook_now_ticks();
  rc = MsgSend(coid, &msg, sizeof msg, NULL, 0);
  close(coid);
  return (rc == 0) ? 0 : -1;
}

/* Gestionnaire de faute : prévenir d'abord, mourir ensuite. */
static void declic_fault_handler(int sig)
{
  declic_term_event ev;

  memset(&ev, 0, sizeof ev);
  ev.pid = (uint32_t)getpid();
  ev.status = 0u;
  ev.signal = (uint32_t)sig;
  (void)declic_hook_notify(&ev);

  /* Comportement par défaut : le processus meurt et `dumper` produit le
   * core dump comme si le crochet n'existait pas. */
  signal(sig, SIG_DFL);
  raise(sig);
}

/* Installe le crochet sur les fautes fatales.
 * À VÉRIFIER SUR LE SDP (E3) : liste complète des signaux à traiter et
 * comportement de `dumper` (core dump) quand un gestionnaire est installé. */
int declic_hook_install(void)
{
  static const int signals[] = {SIGSEGV, SIGABRT, SIGILL, SIGFPE, SIGBUS};
  struct sigaction sa;
  size_t i;

  memset(&sa, 0, sizeof sa);
  sa.sa_handler = declic_fault_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  for (i = 0u; i < sizeof signals / sizeof signals[0]; ++i) {
    if (sigaction(signals[i], &sa, NULL) != 0) {
      fprintf(stderr, "declic-hook: sigaction(%d) : %s\n", signals[i],
              strerror(errno));
      return -1;
    }
  }
  return 0;
}

#endif /* __QNX__ */
