/* declic_watch.c — observation des terminaisons de processus (essai E3).
 * Déclenché par la scrutation de /proc : le HAM et le service de noms ont
 * disparu du SDP 8.0, et `pidin dead` n'existe plus non plus. On ne récupère
 * ici QUE la disparition d'un pid ; le signal de fin est fourni par l'appelant
 * (démo S7, --signal/--status). */
#include "declic_watch.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DECLIC_MAX_SEEN 512u
#define DECLIC_BUF 4096u

static uint32_t g_seen[DECLIC_MAX_SEEN];
static unsigned g_n_seen;

int declic_process_alive(uint32_t pid)
{
  char path[64];

  snprintf(path, sizeof path, "/proc/%u", pid);
  return access(path, F_OK) == 0;
}

static void nap_ms(unsigned ms)
{
  struct timespec ts;

  ts.tv_sec = (time_t)(ms / 1000u);
  ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
  nanosleep(&ts, NULL);
}

/* Liste des pid en scroutant /proc (plus fiable que le format pidin, et sans
 * dépendre d'un shell) : chaque entrée numérique de /proc est un pid. */
unsigned declic_list_pids(uint32_t *out, unsigned cap)
{
  DIR *dir = opendir("/proc");
  struct dirent *entry;
  unsigned n = 0u;

  if (!dir) {
    return 0u;
  }
  while (n < cap && (entry = readdir(dir)) != NULL) {
    char *end = NULL;
    unsigned long value = strtoul(entry->d_name, &end, 10);

    if (end != entry->d_name && *end == '\0' && value > 0u &&
        value < UINT32_MAX) {
      out[n++] = (uint32_t)value;
    }
  }
  closedir(dir);
  return n;
}

/* Attend la disparition d'un pid suivi. */
int declic_wait_for_death(uint32_t pid, unsigned poll_ms)
{
  while (declic_process_alive(pid)) {
    nap_ms(poll_ms);
  }
  return 0;
}

/* Attend la disparition de N'IMPORTE QUEL processus (mode global). */
int declic_wait_for_any_death(uint32_t *pid_out, unsigned poll_ms)
{
  uint32_t pids[DECLIC_MAX_SEEN];
  unsigned n = 0u;
  unsigned i;
  unsigned j;

  for (;;) {
    n = declic_list_pids(pids, DECLIC_MAX_SEEN);
    for (i = 0u; i < g_n_seen; ++i) {
      int still_there = 0;

      for (j = 0u; j < n; ++j) {
        if (g_seen[i] == pids[j]) {
          still_there = 1;
          break;
        }
      }
      if (!still_there) {
        *pid_out = g_seen[i];
        return 0;
      }
    }
    g_n_seen = (n > DECLIC_MAX_SEEN) ? DECLIC_MAX_SEEN : n;
    memcpy(g_seen, pids, g_n_seen * sizeof pids[0]);
    nap_ms(poll_ms);
  }
}

/* Statut et signal de fin. QNX 8.0 n'expose plus la liste des morts
 * (`pidin dead` a disparu : « invalid or ambiguous shorthand ») et le HAM
 * n'existe plus ; la scrutation /proc ne voit qu'une disparition de pid, pas
 * le signal. L'appelant fournit donc --signal/--status (démo S7) ; à défaut,
 * on rend des valeurs neutres (0/0) plutôt que d'inventer. */
void declic_detect_termination(uint32_t pid, uint32_t *status, uint32_t *signal)
{
  (void)pid;
  if (status != NULL) {
    *status = 0u;
  }
  if (signal != NULL) {
    *signal = 0u;
  }
}
