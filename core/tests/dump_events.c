/* dump_events.c — pilote `kev-dump FICHIER` : lignes canoniques « ev|… »
 * (CONTRACT.md §4.1, §4.7). Sert au différentiel C↔Python. */
#include "driver_io.h"
#include "replique/report.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
  static replique_event events[REPLIQUE_MAX_EVENTS];
  replique_sched_config cfg;
  char *buffer;
  size_t n = 0u;
  size_t len = 0u;
  unsigned truncated = 0u;
  int hash_only = 0;
  const char *path = driver_args(argc, argv, &cfg, &hash_only);
  int status = driver_load(path, events, REPLIQUE_MAX_EVENTS, &n, NULL);

  if (status != DRIVER_EXIT_OK) {
    return status;
  }
  buffer = (char *)malloc(n * 128u + 128u);
  if (!buffer) {
    return DRIVER_EXIT_IO;
  }
  len = replique_report_events(events, n, buffer, n * 128u + 128u, &truncated);
  if (!truncated) {
    driver_emit(buffer, len);
  } else {
    fprintf(stderr, "replique: tampon de sortie trop petit\n");
  }
  free(buffer);
  return truncated ? DRIVER_EXIT_IO : DRIVER_EXIT_OK;
}
