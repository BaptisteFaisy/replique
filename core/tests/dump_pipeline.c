/* dump_pipeline.c — pilote `analyze FICHIER [--whatif TID=PRIO]…` : lignes
 * canoniques « vd|… » (CONTRACT.md §4.2, §4.7). Une trace corrompue imprime le
 * verdict PARSE_ERROR sur stdout et rend 0 : c'est une donnée, pas un échec. */
#include "driver_io.h"
#include "replique/report.h"
#include "replique/verdict.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
  static replique_verdict verdicts[REPLIQUE_MAX_VERDICTS];
  static replique_decision decisions[REPLIQUE_MAX_DECISIONS];
  replique_sched_config cfg;
  replique_replay_result result;
  size_t n_verdicts = 0u;
  int hash_only = 0;
  const char *path = driver_args(argc, argv, &cfg, &hash_only);
  size_t blob_len = 0u;
  uint8_t *blob = driver_slurp(path ? path : "", &blob_len);

  if (!path || !blob) {
    fprintf(stderr, "replique: %s illisible\n", path ? path : "(aucun fichier)");
    free(blob);
    return DRIVER_EXIT_IO;
  }
  memset(&result, 0, sizeof result);
  result.decisions = decisions;
  result.cap = REPLIQUE_MAX_DECISIONS;
  replique_pipeline(blob, blob_len, &cfg, verdicts, REPLIQUE_MAX_VERDICTS,
                    &n_verdicts, &result);
  free(blob);
  return emit_verdicts(verdicts, n_verdicts);
}
