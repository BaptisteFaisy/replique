/* declic_qnx.c — colle QNX de Déclic (le démon de la cible).
 *
 * Tout ce fichier est derrière `#ifdef __QNX__` : il n'est JAMAIS compilé par
 * `make test` sur PC. La logique portable (archive RDA1, manifeste, décision
 * de déclenchement) vit dans declic_portable.c et est testée sur PC
 * (core/tests/declic_test.c → build/declic-test).
 *
 * Rôle : surveiller les terminaisons, figer le tampon de trace circulaire,
 * collecter trace + pidin + slog2 + core dump, assembler l'archive RDA1.
 *
 * AVERTISSEMENT D'HONNÊTÉ : le SDP 8.0 n'est pas installé sur la machine de
 * développement. Chaque appel QNX porte un commentaire « à vérifier sur le
 * SDP (E2/E3) » : le mécanisme est écrit, la signature reste à confirmer.
 */
#include "declic.h"
#include "sha256_portable.h"

#ifdef __QNX__

#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/neutrino.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <name.h> /* name_attach() — à vérifier sur le SDP 8.0 (E3) */

extern char **environ;

#define DECLIC_DEFAULT_OUT_DIR "/var/log/declic"
#define DECLIC_DEFAULT_TRACE "trace.kev"
#define DECLIC_MAX_ARTIFACT (256u * 1024u) /* taille max d'un artefact */
#define DECLIC_MAX_SEEN 512u                /* pid mémorisés par le repli */
#define DECLIC_PIDIN_PROG "pidin"
#define DECLIC_SLOG2_PROG "slog2info"
#define DECLIC_TRACELOGGER_PROG "tracelogger"
#define DECLIC_DUMPER_PROG "dumper"

typedef struct declic_config {
  char out_dir[256];
  char trace_path[512];
  declic_policy policy;
  unsigned poll_ms; /* période de scrutation du repli « pidin » */
  int use_hook;     /* écouter le canal des crochets */
  int use_poll;     /* scruter pidin (repli) */
  int once;         /* sortir après le premier événement traité */
} declic_config;

typedef struct declic_blobs {
  unsigned char trace[DECLIC_MAX_ARTIFACT];
  unsigned char pidin[DECLIC_MAX_ARTIFACT];
  unsigned char slog2[DECLIC_MAX_ARTIFACT];
  unsigned char core[DECLIC_MAX_ARTIFACT];
  size_t trace_len, pidin_len, slog2_len, core_len;
} declic_blobs;

static declic_config g_cfg;
static declic_blobs g_blobs;
static pid_t g_tracelogger_pid; /* pid du tracelogger lancé par Déclic */
static uint32_t g_seen[DECLIC_MAX_SEEN];
static unsigned g_n_seen;
/* --- utilitaires --- */
/* Exécute une commande et capture sa sortie (stdout + stderr) dans le
 * tampon fourni par l'appelant. */
static int run_capture(const char *cmd, unsigned char *buf, size_t cap,
                       size_t *len)
{
  FILE *f;
  size_t total = 0u;

  if (!cmd || !buf || !len) {
    return -1;
  }
  *len = 0u;
  f = popen(cmd, "r");
  if (!f) {
    return -1;
  }
  for (;;) {
    size_t n = fread(buf + total, 1u, cap - total, f);

    total += n;
    if (n == 0u || total == cap) {
      break;
    }
  }
  *len = total;
  return pclose(f);
}

/* Horodatage en ticks — FOURNI par cette couche à la logique portable,
 * qui ne consulte jamais d'horloge (declic.h §2). */
static uint64_t now_ticks(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    return 0u;
  }
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* --- (a) figeage du tampon de trace circulaire --- */
/* Démarre tracelogger et note son pid : c'est ce pid que l'on arrête pour
 * que le tampon circulaire soit vidé proprement dans le fichier .kev.
 * À VÉRIFIER SUR LE SDP (E2) : nom du binaire et ligne de commande exacte. */
static int start_tracelogger(void)
{
  pid_t pid = 0;
  char *argv[4];

  argv[0] = (char *)DECLIC_TRACELOGGER_PROG;
  argv[1] = g_cfg.trace_path;
  argv[2] = NULL;
  argv[3] = NULL;
  if (posix_spawn(&pid, DECLIC_TRACELOGGER_PROG, NULL, NULL, argv, environ) != 0) {
    fprintf(stderr, "declic: tracelogger absent (%s)\n", strerror(errno));
    return -1;
  }
  g_tracelogger_pid = pid;
  return 0;
}

/* Fige la trace : tracelogger est arrêté (SIGTERM), on attend sa fin, puis
 * le fichier .kev est complet.
 * À VÉRIFIER SUR LE SDP (E2) : SIGTERM est-il l'arrêt « propre » qui vide
 * le tampon circulaire, ou faut-il une commande dédiée ? */
static int freeze_trace(void)
{
  int status = 0;

  if (g_tracelogger_pid <= 0) {
    return -1;
  }
  if (kill(g_tracelogger_pid, SIGTERM) != 0) {
    fprintf(stderr, "declic: kill(tracelogger) : %s\n", strerror(errno));
    return -1;
  }
  if (waitpid(g_tracelogger_pid, &status, 0) < 0) {
    fprintf(stderr, "declic: waitpid(tracelogger) : %s\n", strerror(errno));
    return -1;
  }
  g_tracelogger_pid = 0;
  return 0;
}

/* Exécute une commande de collecte et capture sa sortie ; si elle ne rend
 * rien, un repli est tenté.
 * À VÉRIFIER SUR LE SDP (E3) : options de pidin, outil de lecture slog2. */
static int collect(const char *cmd, const char *fallback, unsigned char *buf,
                   size_t cap, size_t *len)
{
  int rc = run_capture(cmd, buf, cap, len);

  if ((rc != 0 || *len == 0u) && fallback != NULL) {
    rc = run_capture(fallback, buf, cap, len);
  }
  return (*len > 0u) ? 0 : -1;
}

/* Trace figée + pidin + slog2 + core : tout ce qui explique le crash.
 * À VÉRIFIER SUR LE SDP (E5) : l'emplacement exact des dumps de `dumper`. */
static void collect_all(uint32_t pid)
{
  char trace_cmd[600];
  char dumper_cmd[128];
  char dump_file[128];

  (void)freeze_trace();
  snprintf(trace_cmd, sizeof trace_cmd, "cat %s 2>&1", g_cfg.trace_path);
  (void)run_capture(trace_cmd, g_blobs.trace, sizeof g_blobs.trace,
                     &g_blobs.trace_len);
  (void)collect(DECLIC_PIDIN_PROG " 2>&1",
                DECLIC_PIDIN_PROG " -F \"%a %b %c\" 2>&1", g_blobs.pidin,
                sizeof g_blobs.pidin, &g_blobs.pidin_len);
  (void)collect(DECLIC_SLOG2_PROG " 2>&1", "cat /var/log/slog2/* 2>&1",
                g_blobs.slog2, sizeof g_blobs.slog2, &g_blobs.slog2_len);
  snprintf(dumper_cmd, sizeof dumper_cmd, "%s %u 2>&1", DECLIC_DUMPER_PROG, pid);
  snprintf(dump_file, sizeof dump_file, "cat /var/dumper/core.%u 2>&1", pid);
  (void)collect(dumper_cmd, dump_file, g_blobs.core, sizeof g_blobs.core,
                &g_blobs.core_len);
}

/* Manifeste : l'horodatage et le nom d'archive sont FOURNIS ici. */
static int render_manifest(const declic_term_event *ev, uint64_t ts, char *out,
                            size_t cap, size_t *len)
{
  declic_manifest man;

  memset(&man, 0, sizeof man);
  snprintf(man.archive_name, sizeof man.archive_name, "declic-%u-%llu", ev->pid,
           (unsigned long long)ts);
  man.timestamp = ts;
  man.pid = ev->pid;
  man.status = ev->status;
  man.signal = ev->signal;
  return declic_manifest_render(&man, out, cap, len);
}

static void fill_artifacts(declic_artifacts *art, const char *manifest,
                            size_t manifest_len)
{
  memset(art, 0, sizeof *art);
  art->trace = g_blobs.trace;   art->trace_len = g_blobs.trace_len;
  art->pidin = g_blobs.pidin;   art->pidin_len = g_blobs.pidin_len;
  art->slog2 = g_blobs.slog2;   art->slog2_len = g_blobs.slog2_len;
  art->core = g_blobs.core;     art->core_len = g_blobs.core_len;
  art->manifest = manifest;     art->manifest_len = manifest_len;
}

static int write_file(const char *path, const unsigned char *data, size_t len)
{
  FILE *f = fopen(path, "wb");

  if (!f) {
    fprintf(stderr, "declic: écriture impossible (%s) : %s\n", path,
            strerror(errno));
    return -1;
  }
  if (fwrite(data, 1u, len, f) != len) {
    fclose(f);
    fprintf(stderr, "declic: écriture partielle (%s)\n", path);
    return -1;
  }
  fclose(f);
  return 0;
}

/* Archive complète (trace + pidin + slog2 + core + manifeste) écrite dans
 * le dossier de sortie, avec son empreinte SHA-256 affichée. */
static int write_archive(const declic_term_event *ev, uint64_t ts)
{
  static unsigned char archive[DECLIC_MAX_ARTIFACT * 2u];
  static char manifest[DECLIC_MANIFEST_MAX];
  declic_artifacts art;
  size_t manifest_len = 0u, archive_len = 0u;
  char path[512];
  char hex[SHA256_HEX_SIZE];
  int status;

  collect_all(ev->pid);
  if (render_manifest(ev, ts, manifest, sizeof manifest, &manifest_len) !=
      DECLIC_OK) {
    fprintf(stderr, "declic: manifeste illisible\n");
    return -1;
  }
  fill_artifacts(&art, manifest, manifest_len);
  status = declic_archive_build(&art, archive, sizeof archive, &archive_len);
  if (status != DECLIC_OK) {
    fprintf(stderr, "declic: archive RDA1 refusée (%s)\n",
            declic_status_name(status));
    return -1;
  }
  snprintf(path, sizeof path, "%s/declic-%u-%llu", g_cfg.out_dir, ev->pid,
           (unsigned long long)ts);
  if (write_file(path, archive, archive_len) != 0) {
    return -1;
  }
  sha256_hex(archive, archive_len, hex);
  printf("declic: archive %s (%lu octets) sha256=%s\n", path,
         (unsigned long)archive_len, hex);
  return 0;
}

/* Liste les pid en cours (via `pidin`, sans API QNX exotique).
 * À VÉRIFIER SUR LE SDP (E3) : le sélecteur de format "%a" et la base de
 * numération des pid affichés (d'où strtoul base 0 : décimal ou hexa). */
static unsigned list_pids(uint32_t *out, unsigned cap)
{
  static unsigned char buf[DECLIC_MAX_ARTIFACT];
  size_t len = 0u, i = 0u;
  unsigned n = 0u;

  if (run_capture(DECLIC_PIDIN_PROG " -F \"%a\" 2>&1", buf, sizeof buf, &len) !=
      0) {
    return 0u;
  }
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

static void remember_pids(const uint32_t *pids, unsigned n)
{
  unsigned i;

  g_n_seen = (n > DECLIC_MAX_SEEN) ? DECLIC_MAX_SEEN : n;
  for (i = 0u; i < g_n_seen; ++i) {
    g_seen[i] = pids[i];
  }
}

/* Un pid vu précédemment et absent maintenant est terminé.
 * À VÉRIFIER SUR LE SDP (E3) : la latence de détection (période de
 * scrutation) et la possibilité de connaître le signal de fin. */
static int find_vanished(const uint32_t *pids, unsigned n, uint32_t *pid_out)
{
  unsigned i, j;

  for (i = 0u; i < g_n_seen; ++i) {
    int still_there = 0;

    for (j = 0u; j < n; ++j) {
      if (g_seen[i] == pids[j]) {
        still_there = 1;
        break;
      }
    }
    if (still_there == 0) {
      *pid_out = g_seen[i];
      return 1;
    }
  }
  return 0;
}

/* Reçoit une notification de terminaison sur le canal des crochets.
 * À VÉRIFIER SUR LE SDP (E3) : l'API de canal nommé (name_attach/name_open)
 * et la sémantique exacte de MsgReceive sur un canal de ce type. */
static int receive_hook(int chid, declic_term_event *ev)
{
  declic_trigger_msg msg;

  if (MsgReceive(chid, &msg, sizeof msg, NULL) < 0) {
    return 0;
  }
  if (msg.magic != DECLIC_TRIGGER_MAGIC) {
    return 0;
  }
  ev->pid = msg.pid;
  ev->status = msg.status;
  ev->signal = msg.signal;
  return 1;
}

static int add_u32(uint32_t *list, uint32_t *n, uint32_t cap, const char *arg)
{
  uint32_t v = (uint32_t)strtoul(arg, NULL, 0);

  if (*n >= cap) {
    return -1;
  }
  list[(*n)++] = v;
  return 0;
}

/* Usage : declic [--out DIR] [--trace PATH] [--pid N]… [--signal N]…
 *         [--exclude N]… [--all] [--poll MS] [--no-hook] [--no-poll] [--once] */
static int parse_args(int argc, char **argv)
{
  int i;

  declic_policy_init(&g_cfg.policy);
  snprintf(g_cfg.out_dir, sizeof g_cfg.out_dir, "%s", DECLIC_DEFAULT_OUT_DIR);
  snprintf(g_cfg.trace_path, sizeof g_cfg.trace_path, "%s/%s",
           DECLIC_DEFAULT_OUT_DIR, DECLIC_DEFAULT_TRACE);
  g_cfg.poll_ms = 500u;
  g_cfg.use_hook = 1;
  g_cfg.use_poll = 1;

  for (i = 1; i < argc; ++i) {
    const char *a = argv[i];

    if (strcmp(a, "--out") == 0 && i + 1 < argc) {
      snprintf(g_cfg.out_dir, sizeof g_cfg.out_dir, "%s", argv[++i]);
    } else if (strcmp(a, "--trace") == 0 && i + 1 < argc) {
      snprintf(g_cfg.trace_path, sizeof g_cfg.trace_path, "%s", argv[++i]);
    } else if (strcmp(a, "--pid") == 0 && i + 1 < argc) {
      if (add_u32(g_cfg.policy.watch, &g_cfg.policy.n_watch, DECLIC_MAX_WATCH,
                  argv[++i]) == 0) {
        g_cfg.policy.watch_all = 0u;
      }
    } else if (strcmp(a, "--exclude") == 0 && i + 1 < argc) {
      (void)add_u32(g_cfg.policy.exclusions, &g_cfg.policy.n_exclusions,
                    DECLIC_MAX_WATCH, argv[++i]);
    } else if (strcmp(a, "--signal") == 0 && i + 1 < argc) {
      (void)add_u32(g_cfg.policy.signals, &g_cfg.policy.n_signals,
                    DECLIC_MAX_SIGNALS, argv[++i]);
    } else if (strcmp(a, "--all") == 0) {
      g_cfg.policy.watch_all = 1u;
    } else if (strcmp(a, "--poll") == 0 && i + 1 < argc) {
      g_cfg.poll_ms = (unsigned)strtoul(argv[++i], NULL, 0);
    } else if (strcmp(a, "--no-hook") == 0) {
      g_cfg.use_hook = 0;
    } else if (strcmp(a, "--no-poll") == 0) {
      g_cfg.use_poll = 0;
    } else if (strcmp(a, "--once") == 0) {
      g_cfg.once = 1;
    } else {
      printf("usage : %s [--out DIR] [--trace PATH] [--pid N] [--signal N] "
             "[--exclude N] [--all] [--poll MS] [--no-hook] [--no-poll] "
             "[--once]\n", argv[0]);
      return -1;
    }
  }
  return 0;
}

/* Boucle d'attente : crochet d'abord (le plus précoce), repli pidin ensuite. */
static void run_loop(int chid)
{
  for (;;) {
    fd_set rfds;
    struct timeval tv;
    declic_term_event ev;
    int have = 0;

    memset(&ev, 0, sizeof ev);
    FD_ZERO(&rfds);
    if (g_cfg.use_hook && chid >= 0) {
      FD_SET(chid, &rfds);
    }
    tv.tv_sec = (time_t)(g_cfg.poll_ms / 1000u);
    tv.tv_usec = (suseconds_t)((g_cfg.poll_ms % 1000u) * 1000u);
    if (select(chid + 1, &rfds, NULL, NULL, &tv) > 0 && chid >= 0 &&
        FD_ISSET(chid, &rfds)) {
      have = receive_hook(chid, &ev);
    }
    if (!have && g_cfg.use_poll) {
      static uint32_t pids[DECLIC_MAX_SEEN];
      unsigned n = list_pids(pids, DECLIC_MAX_SEEN);
      uint32_t gone = 0u;

      if (n > 0u && find_vanished(pids, n, &gone) != 0) {
        ev.pid = gone;
        have = 1;
      }
      remember_pids(pids, n);
    }
    if (!have) {
      continue;
    }
    if (declic_should_capture(&ev, &g_cfg.policy) != 1) {
      if (g_cfg.once) {
        break; /* hors politique : en mode --once, on s'arrête quand même */
      }
      continue;
    }
    (void)write_archive(&ev, now_ticks());
    if (g_cfg.once) {
      break;
    }
  }
}

int main(int argc, char **argv)
{
  int chid = -1;

  if (parse_args(argc, argv) != 0) {
    return 2;
  }
  (void)mkdir(g_cfg.out_dir, 0777);
  if (g_cfg.use_hook) {
    /* À VÉRIFIER SUR LE SDP (E3) : création du canal nommé écouté par les
     * crochets (target/hook). */
    chid = name_attach(DECLIC_TRIGGER_CHANNEL, 0);
    if (chid < 0) {
      fprintf(stderr, "declic: canal %s indisponible (%s)\n",
              DECLIC_TRIGGER_CHANNEL, strerror(errno));
      g_cfg.use_hook = 0;
    }
  }
  if (start_tracelogger() != 0) {
    fprintf(stderr, "declic: trace non instrumentée, le figeage sera vide\n");
  }
  run_loop(chid);
  return 0;
}

#endif /* __QNX__ */
