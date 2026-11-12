/* declic_watch.c — observation des terminaisons de processus (essai E3).
 * Déclenché par la scrutation de /proc et par `pidin dead` (le HAM et le
 * service de noms ayant disparu du SDP 8.0). */
#include "declic_watch.h"

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

/* Exécute `pidin -F "%a"` et rend la liste des pid. */
unsigned declic_list_pids(uint32_t *out, unsigned cap)
{
  static char buf[DECLIC_BUF];
  size_t len = 0u;
  size_t i = 0u;
  unsigned n = 0u;
  FILE *pipe;

  pipe = popen("pidin -F \"%a\" 2>/dev/null", "r");
  if (!pipe) {
    return 0u;
  }
  len = fread(buf, 1u, sizeof buf - 1u, pipe);
  pclose(pipe);
  while (i < len && n < cap) {
    size_t j = i;
    char line[64];

    while (j < len && buf[j] != '\n') {
      ++j;
    }
    if (j - i > 0u && j - i < sizeof line) {
      unsigned int v = 0u;

      memcpy(line, buf + i, j - i);
      line[j - i] = '\0';
      if (sscanf(line, "%i", &v) == 1 && v > 0u) {
        out[n++] = (uint32_t)v;
      }
    }
    i = (j < len) ? j + 1u : len;
  }
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

/* Numéro de signal QNX à partir de son nom (`pidin dead`). */
static int signal_number(const char *name)
{
  static const struct {
    const char *name;
    int number;
  } table[] = {
    {"SIGINT", 2}, {"SIGILL", 4}, {"SIGTRAP", 5}, {"SIGABRT", 6}, {"SIGFPE", 8},
    {"SIGKILL", 9}, {"SIGBUS", 10}, {"SIGSEGV", 11}, {"SIGDUMP", 13},
    {"SIGALRM", 14}, {"SIGTERM", 15}, {"SIGPROF", 27}
  };
  size_t i;

  for (i = 0u; i < sizeof table / sizeof table[0]; ++i) {
    if (!strcmp(name, table[i].name)) {
      return table[i].number;
    }
  }
  return 0;
}

/* Statut et signal de fin, lus dans `pidin dead`. */
void declic_detect_termination(uint32_t pid, uint32_t *status, uint32_t *signal)
{
  static char buf[DECLIC_BUF];
  size_t len = 0u;
  size_t i = 0u;
  FILE *pipe;

  pipe = popen("pidin dead 2>/dev/null", "r");
  if (!pipe) {
    return;
  }
  len = fread(buf, 1u, sizeof buf - 1u, pipe);
  pclose(pipe);
  while (i < len) {
    size_t j = i;
    char line[256];
    unsigned int v = 0u;
    char name[128];
    char state[32];
    unsigned int code = 0u;
    char sig[64];

    while (j < len && buf[j] != '\n') {
      ++j;
    }
    if (j - i > 0u && j - i < sizeof line) {
      memcpy(line, buf + i, j - i);
      line[j - i] = '\0';
      if (sscanf(line, "%u %127s %31s %x %63s", &v, name, state, &code, sig) >= 4 &&
          v == pid) {
        *status = code;
        *signal = (uint32_t)signal_number(sig);
        return;
      }
    }
    i = (j < len) ? j + 1u : len;
  }
}
