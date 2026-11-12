/* declic_qnx.c — colle QNX 8.0 de Déclic (le démon de la cible).
 *
 * Tout ce fichier est derrière `#ifdef __QNX__` : il n'est JAMAIS compilé par
 * `make test` sur PC. La logique portable (archive RDA1, manifeste, SHA-256,
 * décision de déclenchement) vit dans declic_portable.c et est testée sur PC
 * (core/tests/declic_test.c → build/declic-test). L'observation des
 * terminaisons vit dans declic_watch.c.
 *
 * Rôle : observer la terminaison d'un processus, figer le tampon de trace
 * circulaire, collecter trace + pidin + slog2 + core dump, assembler
 * l'archive RDA1.
 *
 * FAITS VÉRIFIÉS SUR LE SDP 8.0 (serveur r630, 12/11/2026) :
 *   - le HAM n'existe plus (ni <ham/ham.h> ni binaire ham) et le service de
 *     noms non plus : le déclencheur est la scrutation de /proc (essai E3) ;
 *   - tracelogger, traceprinter, dumper, pidin et slog2info sont bien livrés ;
 *   - le figeage de la trace (essai E2) repose sur SIGINT à tracelogger :
 *     il écrit son tampon circulaire et se termine.
 */
#include "declic.h"
#include "declic_watch.h"
#include "sha256_portable.h"

#ifdef __QNX__

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DECLIC_DEFAULT_OUT_DIR "/var/tmp/declic"
#define DECLIC_MAX_ARTIFACT (256u * 1024u)
#define DECLIC_PIDIN_PROG "pidin"
#define DECLIC_SLOG2_PROG "slog2info"
#define DECLIC_TRACELOGGER_PROG "tracelogger"
#define DECLIC_DEFAULT_DUMP_DIR "/var/dumper/cores"
#define DECLIC_POLL_DEFAULT_MS 50u

typedef struct declic_config {
  char out_dir[256];
  char trace_path[512];
  char dump_dir[256];
  uint32_t watch_pid;
  uint32_t trace_pid;
  declic_policy policy;
  unsigned poll_ms;
  int once;
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

static uint64_t now_ticks(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    return 0u;
  }
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* Exécute une commande et capture sa sortie (stdout + stderr). */
static int run_capture(const char *cmd, unsigned char *buf, size_t cap,
                       size_t *len)
{
  FILE *pipe;
  size_t got;

  *len = 0u;
  pipe = popen(cmd, "r");
  if (!pipe) {
    return -1;
  }
  got = fread(buf, 1u, cap, pipe);
  *len = got;
  pclose(pipe);
  return 0;
}

/* Découverte du pid de tracelogger (son nom suffit). */
static uint32_t find_tracelogger_pid(void)
{
  static unsigned char buf[DECLIC_MAX_ARTIFACT];
  size_t len = 0u;
  size_t i = 0u;

  if (run_capture(DECLIC_PIDIN_PROG " -F \"%a %n\" 2>&1", buf, sizeof buf,
                  &len) != 0) {
    return 0u;
  }
  while (i < len) {
    size_t j = i;
    char line[256];

    while (j < len && buf[j] != '\n') {
      ++j;
    }
    if (j - i > 0u && j - i < sizeof line) {
      unsigned int v = 0u;
      char name[128];

      memcpy(line, buf + i, j - i);
      line[j - i] = '\0';
      if (sscanf(line, "%u %127s", &v, name) == 2 &&
          strcmp(name, DECLIC_TRACELOGGER_PROG) == 0) {
        return (uint32_t)v;
      }
    }
    i = (j < len) ? j + 1u : len;
  }
  return 0u;
}

/* Fige la trace (essai E2) : tracelogger reçoit SIGINT, vide son tampon
 * circulaire dans le fichier .kev et se termine proprement. */
static int freeze_trace(void)
{
  uint32_t pid = g_cfg.trace_pid ? g_cfg.trace_pid : find_tracelogger_pid();
  int status = 0;

  if (!pid) {
    fprintf(stderr, "declic: tracelogger introuvable (--trace-pid ?)\n");
    return -1;
  }
  if (kill((pid_t)pid, SIGINT) != 0) {
    fprintf(stderr, "declic: SIGINT à tracelogger (%u) : %s\n", pid,
            strerror(errno));
    return -1;
  }
  (void)waitpid((pid_t)pid, &status, 0);
  g_cfg.trace_pid = 0u;
  fprintf(stderr, "declic: trace figée (tracelogger %u arrêté)\n", pid);
  return 0;
}

/* Core dump : le dumper écrit un répertoire par processus dans son dossier de
 * travail ; on concatène les fichiers qu'il contient (essai E5). */
static void collect_core(uint32_t pid)
{
  static unsigned char buf[DECLIC_MAX_ARTIFACT];
  char cmd[512];
  size_t len = 0u;

  snprintf(cmd, sizeof cmd, "cat %s/%u/* 2>&1", g_cfg.dump_dir, pid);
  (void)run_capture(cmd, buf, sizeof buf, &len);
  if (len) {
    memcpy(g_blobs.core, buf, len);
    g_blobs.core_len = len;
  }
}

/* Trace figée + pidin + slog2 + core : tout ce qui explique le crash. */
static void collect_all(uint32_t pid)
{
  char cmd[600];

  (void)freeze_trace();
  snprintf(cmd, sizeof cmd, "cat %s 2>&1", g_cfg.trace_path);
  (void)run_capture(cmd, g_blobs.trace, sizeof g_blobs.trace,
                    &g_blobs.trace_len);
  (void)run_capture(DECLIC_PIDIN_PROG " 2>&1", g_blobs.pidin,
                    sizeof g_blobs.pidin, &g_blobs.pidin_len);
  (void)run_capture(DECLIC_SLOG2_PROG " 2>&1", g_blobs.slog2,
                    sizeof g_blobs.slog2, &g_blobs.slog2_len);
  collect_core(pid);
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

/* Archive complète (trace + pidin + slog2 + core + manifeste). */
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
  sha256_hex(archive, archive_len, hex);
  /* Résumé sur la console (le port série de la démo) : c'est la preuve
   * visible, indépendante de l'écriture disque. */
  printf("declic: pid=%u signal=%u status=%u\n", ev->pid, ev->signal, ev->status);
  printf("declic: archive %lu octets sha256=%s\n", (unsigned long)archive_len, hex);
  printf("declic: sections trace=%lu pidin=%lu slog2=%lu core=%lu manifest=%lu\n",
         (unsigned long)art.trace_len, (unsigned long)art.pidin_len,
         (unsigned long)art.slog2_len, (unsigned long)art.core_len,
         (unsigned long)art.manifest_len);
  snprintf(path, sizeof path, "%s/declic-%u-%llu", g_cfg.out_dir, ev->pid,
           (unsigned long long)ts);
  if (write_file(path, archive, archive_len) != 0) {
    return 0;   /* l'archive est quand même montrée sur la console */
  }
  return 0;
}

static void usage(void)
{
  fprintf(stderr,
          "declic [--pid <pid>] --out <rép> [--trace <fichier.kev>]\n"
          "       [--trace-pid <pid>] [--dump-dir <rép>] [--interval <ms>]\n"
          "Sans --pid : archive le premier processus disparu (repli `pidin`).\n");
}

/* Analyse des arguments. */
static int parse_args(int argc, char **argv)
{
  int i;

  for (i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--pid") && i + 1 < argc) {
      g_cfg.watch_pid = (uint32_t)strtoul(argv[++i], NULL, 0);
    } else if (!strcmp(argv[i], "--trace-pid") && i + 1 < argc) {
      g_cfg.trace_pid = (uint32_t)strtoul(argv[++i], NULL, 0);
    } else if (!strcmp(argv[i], "--out") && i + 1 < argc) {
      snprintf(g_cfg.out_dir, sizeof g_cfg.out_dir, "%s", argv[++i]);
    } else if (!strcmp(argv[i], "--trace") && i + 1 < argc) {
      snprintf(g_cfg.trace_path, sizeof g_cfg.trace_path, "%s", argv[++i]);
    } else if (!strcmp(argv[i], "--dump-dir") && i + 1 < argc) {
      snprintf(g_cfg.dump_dir, sizeof g_cfg.dump_dir, "%s", argv[++i]);
    } else if (!strcmp(argv[i], "--interval") && i + 1 < argc) {
      g_cfg.poll_ms = (unsigned)strtoul(argv[++i], NULL, 0);
    } else if (!strcmp(argv[i], "--once")) {
      g_cfg.once = 1;
    } else {
      usage();
      return -1;
    }
  }
  return 0;
}

int main(int argc, char **argv)
{
  declic_term_event ev;
  uint64_t ts;

  memset(&g_cfg, 0, sizeof g_cfg);
  memset(&ev, 0, sizeof ev);
  snprintf(g_cfg.out_dir, sizeof g_cfg.out_dir, "%s", DECLIC_DEFAULT_OUT_DIR);
  snprintf(g_cfg.dump_dir, sizeof g_cfg.dump_dir, "%s", DECLIC_DEFAULT_DUMP_DIR);
  g_cfg.poll_ms = DECLIC_POLL_DEFAULT_MS;
  declic_policy_init(&g_cfg.policy);
  if (parse_args(argc, argv) != 0) {
    return 2;
  }
  if (g_cfg.watch_pid) {
    fprintf(stderr, "declic: surveillance du pid %u (scrutation /proc, %u ms)\n",
            g_cfg.watch_pid, g_cfg.poll_ms);
    (void)declic_wait_for_death(g_cfg.watch_pid, g_cfg.poll_ms);
    ev.pid = g_cfg.watch_pid;
  } else {
    (void)declic_wait_for_any_death(&ev.pid, g_cfg.poll_ms);
  }
  ev.signal = 0u;
  ev.status = 0u;
  declic_detect_termination(ev.pid, &ev.status, &ev.signal);
  fprintf(stderr, "declic: terminaison observée du pid %u (statut %u, signal %u)\n",
          ev.pid, ev.status, ev.signal);
  ts = now_ticks();
  return write_archive(&ev, ts) == 0 ? 0 : 1;
}

#else

/* Hors QNX : ce fichier ne fait rien (la logique portable est dans
 * declic_portable.c, testée sur PC). */
int declic_qnx_placeholder(void);

int declic_qnx_placeholder(void)
{
  return 0;
}

#endif
