#ifndef REPLIQUE_REPORT_H
#define REPLIQUE_REPORT_H
/* report.h — sorties texte canoniques (CONTRACT.md §4). Le texte canonique est
 * l'unique format d'échange du différentiel C↔Python : aucune variante. */
#include "types.h"

/* Écrit dans le tampon fourni ; retourne le nombre d'octets nécessaires
 * (peut dépasser cap → sortie tronquée signalée par *truncated = 1). */
size_t replique_report_event(const replique_event *ev, char *out, size_t cap,
                             unsigned *truncated);
size_t replique_report_events(const replique_event *ev, size_t n, char *out,
                              size_t cap, unsigned *truncated);
size_t replique_report_verdict(const replique_verdict *v, char *out, size_t cap,
                               unsigned *truncated);
size_t replique_report_verdicts(const replique_verdict *v, size_t n, char *out,
                                size_t cap, unsigned *truncated);
size_t replique_report_decision(const replique_decision *d, char *out,
                                size_t cap, unsigned *truncated);
size_t replique_report_decisions(const replique_decision *d, size_t n,
                                 char *out, size_t cap, unsigned *truncated);
#endif
