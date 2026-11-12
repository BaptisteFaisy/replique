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

int driver_load(const char *path, replique_event *events, size_t cap, size_t *n,
                replique_header *header)
{
  size_t len = 0u;
  uint8_t *buf;
  unsigned cause = 0u;
  size_t offset = 0u;
  int status;

  if (!path) {
    fprintf(stderr, "replique: fichier manquant en argument\n");
    return DRIVER_EXIT_IO;
  }
  buf = driver_slurp(path, &len);
  if (!buf) {
    fprintf(stderr, "replique: %s illisible\n", path);
    return DRIVER_EXIT_IO;
  }
  status = replique_kev_parse_diag(buf, len, events, cap, n, &cause, &offset);
  if (header) {
    if (replique_kev_header(buf, len, header) != REPLIQUE_STATUS_OK) {
      memset(header, 0, sizeof *header);
    }
  }
  free(buf);
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
