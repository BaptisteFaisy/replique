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
  static replique_event events[REPLIQUE_MAX_EVENTS];
  static replique_decision decisions[REPLIQUE_MAX_DECISIONS];
  replique_sched_config cfg;
  replique_replay_result result;
  size_t n = 0u;
  int hash_only = 0;
  const char *path = driver_args(argc, argv, &cfg, &hash_only);
  int status = driver_load(path, events, REPLIQUE_MAX_EVENTS, &n, NULL);

  if (status != DRIVER_EXIT_OK) {
    return status;
  }
  memset(&result, 0, sizeof result);
  result.decisions = decisions;
  result.cap = REPLIQUE_MAX_DECISIONS;
  replique_replay(events, n, &cfg, &result);
  if (hash_only) {
    printf("hash=%llu\n", (unsigned long long)result.hash);
    return DRIVER_EXIT_OK;
  }
  return emit_decisions(&result);
}
