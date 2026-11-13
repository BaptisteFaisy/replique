/* driver_io.c — implémentation partagée des pilotes de différentiel. */
#include "driver_io.h"
#include "replique/kev.h"
#include "replique/replay.h"
#include "replique/report.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *driver_slurp(const char *path, size_t *len)
{
  FILE *file = fopen(path, "rb");
  long size;
  uint8_t *buf;

  *len = 0u;
  if (!file) {
    return NULL;
  }
  if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
      fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }
  buf = (uint8_t *)malloc((size_t)size ? (size_t)size : 1u);
  if (!buf) {
    fclose(file);
    return NULL;
  }
  if (size && fread(buf, 1u, (size_t)size, file) != (size_t)size) {
    free(buf);
    fclose(file);
    return NULL;
  }
  fclose(file);
  *len = (size_t)size;
  return buf;
}

void driver_emit(const char *text, size_t len)
{
  if (len) {
    fwrite(text, 1u, len, stdout);
  }
  fflush(stdout);
}

const char *driver_args(int argc, char **argv, replique_sched_config *cfg,
                        int *hash_only)
{
  const char *path = NULL;
  int i;

  replique_sched_config_default(cfg);
  *hash_only = 0;
  for (i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--hash")) {
      *hash_only = 1;
    } else if (!strcmp(argv[i], "--whatif") && i + 1 < argc) {
      driver_add_override(cfg, argv[++i]);
    } else if (!strncmp(argv[i], "--whatif=", 9)) {
      driver_add_override(cfg, argv[i] + 9);
    } else if (!path) {
      path = argv[i];
    }
  }
  return path;
}

void driver_add_override(replique_sched_config *cfg, const char *spec)
{
  unsigned long tid = strtoul(spec, NULL, 10);
  const char *sep = strchr(spec, '=');
  unsigned long prio;

  if (!sep || cfg->n_overrides >= REPLIQUE_MAX_OVERRIDES) {
    fprintf(stderr, "replique: --whatif attendu TID=PRIO, reçu %s\n", spec);
    return;
  }
  prio = strtoul(sep + 1, NULL, 10);
  cfg->overrides[cfg->n_overrides].tid = (uint32_t)tid;
  cfg->overrides[cfg->n_overrides].prio = (uint32_t)prio;
  ++cfg->n_overrides;
}

void driver_parse_error(unsigned cause, size_t offset)
{
  fprintf(stderr, "replique: PARSE_ERROR %s offset=%zu\n",
          replique_cause_name(cause), offset);
  fflush(stderr);
}

/* Tampons dimensionnes sur l'en-tete : une trace reelle n'a pas de taille
 * fixe (la trace "wide" de preuve en a 13 294). */
static int driver_alloc(driver_trace *t)
{
  size_t count = replique_kev_event_count(t->blob, t->blob_len);

  t->cap = count ? count : 1u;
  t->dcap = t->cap;
  t->events = (replique_event *)calloc(t->cap, sizeof *t->events);
  t->decisions = (replique_decision *)calloc(t->dcap, sizeof *t->decisions);
  if (!t->events || !t->decisions) {
    return 0;
  }
  return 1;
}

int driver_open(const char *path, driver_trace *t)
{
  memset(t, 0, sizeof *t);
  if (!path) {
    fprintf(stderr, "replique: fichier manquant en argument\n");
    return DRIVER_EXIT_IO;
  }
  t->blob = driver_slurp(path, &t->blob_len);
  if (!t->blob) {
    fprintf(stderr, "replique: %s illisible\n", path);
    return DRIVER_EXIT_IO;
  }
  if (!driver_alloc(t)) {
    fprintf(stderr, "replique: memoire insuffisante\n");
    driver_close(t);
    return DRIVER_EXIT_IO;
  }
  return DRIVER_EXIT_OK;
}

void driver_close(driver_trace *t)
{
  free(t->events);
  free(t->decisions);
  free(t->blob);
  memset(t, 0, sizeof *t);
}

int driver_parse(driver_trace *t)
{
  unsigned cause = 0u;
  size_t offset = 0u;
  int status = replique_kev_parse_diag(t->blob, t->blob_len, t->events, t->cap,
                                        &t->n, &cause, &offset);

  if (status != REPLIQUE_STATUS_OK) {
    driver_parse_error(cause, offset);
    return DRIVER_EXIT_PARSE;
  }
  return DRIVER_EXIT_OK;
}

int emit_decisions(const replique_replay_result *res)
{
  char *buffer;
  size_t cap;
  size_t len;
  unsigned truncated = 0u;

  if (!res) {
    return DRIVER_EXIT_IO;
  }
  cap = res->n_decisions * 96u + 96u;
  buffer = (char *)malloc(cap);
  if (!buffer) {
    return DRIVER_EXIT_IO;
  }
  len = replique_report_decisions(res->decisions, res->n_decisions, buffer, cap,
                                  &truncated);
  if (!truncated) {
    driver_emit(buffer, len);
  }
  free(buffer);
  return truncated ? DRIVER_EXIT_IO : DRIVER_EXIT_OK;
}

int emit_verdicts(const replique_verdict *verdicts, size_t n)
{
  char *buffer;
  size_t cap;
  size_t len;
  unsigned truncated = 0u;

  cap = n * 256u + 256u;
  buffer = (char *)malloc(cap);
  if (!buffer) {
    return DRIVER_EXIT_IO;
  }
  len = replique_report_verdicts(verdicts, n, buffer, cap, &truncated);
  if (!truncated) {
    driver_emit(buffer, len);
  }
  free(buffer);
  return truncated ? DRIVER_EXIT_IO : DRIVER_EXIT_OK;
}
