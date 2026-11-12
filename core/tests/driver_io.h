#ifndef REPLIQUE_DRIVER_IO_H
#define REPLIQUE_DRIVER_IO_H
/* driver_io.h — services partagés des trois pilotes de différentiel
 * (dump_events, dump_replay, dump_pipeline). Ce sont des PILOTES de test :
 * ils ont droit à stdio/stdlib, contrairement au cœur (CONTRACT.md §7). */
#include "replique/types.h"
#include <stddef.h>
#include <stdint.h>

#define DRIVER_EXIT_OK 0
#define DRIVER_EXIT_IO 2
#define DRIVER_EXIT_PARSE 3

uint8_t *driver_slurp(const char *path, size_t *len);
void driver_emit(const char *text, size_t len);
/* Analyse FICHIER [--whatif TID=PRIO]… [--hash] ; renvoie le chemin ou NULL. */
const char *driver_args(int argc, char **argv, replique_sched_config *cfg,
                        int *hash_only);
/* Charge et décode une trace ; gère les codes de sortie gelés (CONTRACT §4.7).
 * Renvoie DRIVER_EXIT_OK et remplit events/n/header, ou un code d'erreur. */
int driver_load(const char *path, replique_event *events, size_t cap,
                size_t *n, replique_header *header);
void driver_parse_error(unsigned cause, size_t offset);
void driver_add_override(replique_sched_config *cfg, const char *spec);
/* Émission canonique + code de retour (0). Utilisés par dump_replay/dump_pipeline. */
int emit_decisions(const replique_replay_result *res);
int emit_verdicts(const replique_verdict *verdicts, size_t n);
#endif
