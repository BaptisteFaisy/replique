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
/* Trace chargee : le pilote alloue (autorise dans core/tests) des tampons
 * dimensionnes sur l'en-tete, pour qu'une trace reelle de taille quelconque
 * passe dans le coeur (le coeur, lui, n'alloue jamais). */
typedef struct {
  uint8_t *blob;              /* fichier lu */
  size_t blob_len;
  replique_event *events;     /* tampon d'evenements (cap) */
  size_t cap;
  size_t n;                   /* evenements decodes */
  replique_decision *decisions; /* tampon de decisions du rejeu (dcap) */
  size_t dcap;
} driver_trace;

/* Charge une trace et alloue les tampons. DRIVER_EXIT_OK si le fichier est
 * lisible : une trace malformee n'est pas une erreur de pilote, c'est le
 * coeur qui rend le verdict PARSE_ERROR. */
int driver_open(const char *path, driver_trace *t);
/* Decode la trace dans t->events. DRIVER_EXIT_OK ou DRIVER_EXIT_PARSE
 * (le diagnostic figé est imprime sur stderr). */
int driver_parse(driver_trace *t);
void driver_close(driver_trace *t);
void driver_parse_error(unsigned cause, size_t offset);
void driver_add_override(replique_sched_config *cfg, const char *spec);
/* Émission canonique + code de retour (0). Utilisés par dump_replay/dump_pipeline. */
int emit_decisions(const replique_replay_result *res);
int emit_verdicts(const replique_verdict *verdicts, size_t n);
#endif
