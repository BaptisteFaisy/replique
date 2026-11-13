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
  replique_sched_config cfg;
  replique_replay_result result;
  driver_trace trace;
  size_t n_verdicts = 0u;
  int hash_only = 0;
  const char *path = driver_args(argc, argv, &cfg, &hash_only);
  int status = driver_open(path, &trace);

  if (status != DRIVER_EXIT_OK) {
    return status;
  }
  memset(&result, 0, sizeof result);
  result.decisions = trace.decisions;
  result.cap = trace.dcap;
  replique_pipeline(trace.blob, trace.blob_len, &cfg, verdicts,
                    REPLIQUE_MAX_VERDICTS, &n_verdicts, &result, trace.events,
                    trace.cap);
  driver_close(&trace);
  return emit_verdicts(verdicts, n_verdicts);
}
