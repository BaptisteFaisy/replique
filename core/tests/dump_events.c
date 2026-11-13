/* dump_events.c — pilote `kev-dump FICHIER` : lignes canoniques « ev|… »
 * (CONTRACT.md §4.1, §4.7). Sert au différentiel C↔Python. */
#include "driver_io.h"
#include "replique/report.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
  replique_sched_config cfg;
  driver_trace trace;
  char *buffer;
  size_t len = 0u;
  unsigned truncated = 0u;
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
  buffer = (char *)malloc(trace.cap * 128u + 128u);
  if (!buffer) {
    driver_close(&trace);
    return DRIVER_EXIT_IO;
  }
  len = replique_report_events(trace.events, trace.n, buffer,
                               trace.cap * 128u + 128u, &truncated);
  if (!truncated) {
    driver_emit(buffer, len);
  } else {
    fprintf(stderr, "replique: tampon de sortie trop petit\n");
  }
  free(buffer);
  driver_close(&trace);
  return truncated ? DRIVER_EXIT_IO : DRIVER_EXIT_OK;
}
