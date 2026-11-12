#ifndef REPLIQUE_KEV_H
#define REPLIQUE_KEV_H
/* kev.h — parseur du conteneur RKV1 (CONTRACT.md §1). Validé par différentiel
 * contre traceprinter (oracle SDP) et contre le jumeau Python. */
#include "types.h"

/* Décode buf[0..len) dans out[0..cap). *n_out = nombre d'événements écrits.
 * Retour : REPLIQUE_STATUS_OK ou un code négatif ; les pointeurs cause et
 * offset précisent alors le diagnostic (cause gelée, décalage en octets).
 * Aucune reprise approximative : au premier incident, la chaîne s'arrête. */
int replique_kev_parse(const uint8_t *buf, size_t len,
                       replique_event *out, size_t cap, size_t *n_out);

/* Variante avec diagnostic fin (utilisée par les verdicts PARSE_ERROR). */
int replique_kev_parse_diag(const uint8_t *buf, size_t len,
                            replique_event *out, size_t cap, size_t *n_out,
                            unsigned *cause, size_t *offset);

/* En-tête seul : valide magic/version/flags/longueur/CRC. */
int replique_kev_header(const uint8_t *buf, size_t len, replique_header *hdr);

/* Écrit un conteneur RKV1 (générateur de fixtures C + tests). */
int replique_kev_write(const replique_event *ev, size_t n, uint32_t flags,
                       uint32_t time_unit, uint8_t *out, size_t cap,
                       size_t *n_out);

uint32_t replique_crc32(const uint8_t *data, size_t len);
uint64_t replique_fnv1a64(const char *data, size_t len);
uint64_t replique_fnv1a64_begin(void);
uint64_t replique_fnv1a64_update(uint64_t hash, const char *data, size_t len);
#endif
