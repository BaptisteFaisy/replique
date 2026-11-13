/* dump_replay.c — pilote `replay-dump FICHIER [--whatif TID=PRIO]… [--hash]` :
 * lignes canoniques « dec|… » ou « hash=<u64> » (CONTRACT.md §4.3, §4.7). */
#include "driver_io.h"
#include "replique/replay.h"
#include "replique/report.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
  replique_sched_config cfg;
  replique_replay_result result;
  driver_trace trace;
  int hash_only = 0;
  const char *path = driver_args(argc, argv, &cfg, &hash_only);
  int status = driver_open(path, &trace);

  if (status != DRIVER_EXIT_OK) {
    return status;
  }
  status = driver_parse(&trace);
  if (status != DRIVER_EXIT_OK) {
    driver_close(&trace);
    return status;
  }
  memset(&result, 0, sizeof result);
  result.decisions = trace.decisions;
  result.cap = trace.dcap;
  replique_replay(trace.events, trace.n, &cfg, &result);
  if (hash_only) {
    printf("hash=%llu\n", (unsigned long long)result.hash);
    driver_close(&trace);
    return DRIVER_EXIT_OK;
  }
  status = emit_decisions(&result);
  driver_close(&trace);
  return status;
}
