#ifndef REPLIQUE_VERDICT_H
#define REPLIQUE_VERDICT_H
/* verdict.h — helpers de verdicts + pipeline complète (CONTRACT.md §3, §4.4). */
#include "types.h"

/* Tri stable gelé : sévérité décroissante, code croissant, ts croissant,
 * tid croissant ; à égalité, ordre de production conservé. */
void replique_verdict_sort(replique_verdict *v, size_t n);

/* Fabriques (détail tronqué proprement à REPLIQUE_DETAIL_MAX-1). */
void replique_verdict_set(replique_verdict *v, unsigned code, uint64_t ts,
                          uint32_t pid, uint32_t tid, uint32_t a, uint32_t b,
                          uint32_t c, const char *detail);
void replique_verdict_sanitize_detail(char *detail);

/* Chaîne complète (figée, §3.2) : parse → wait-for → deadlock → inversion →
 * rejeu. Une erreur de parse arrête tout : un seul verdict PARSE_ERROR. */
int replique_pipeline(const uint8_t *buf, size_t len,
                      const replique_sched_config *cfg,
                      replique_verdict *verdicts, size_t vcap, size_t *n_verdicts,
                      replique_replay_result *res);
#endif
