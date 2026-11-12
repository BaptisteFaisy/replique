#ifndef REPLIQUE_DEADLOCK_H
#define REPLIQUE_DEADLOCK_H
/* deadlock.h — Autopsie : graphe d'attente, chaîne de blocage, coupable,
 * inversion de priorité (CONTRACT.md §3.3, §3.4 ; exigences C2, C3, C5). */
#include "types.h"
#include "sched.h"   /* replique_thread (état reconstruit, exigence C1) */

/* État final des threads reconstruit depuis la trace (exigence C1). */
int replique_states_rebuild(const replique_event *ev, size_t n,
                            replique_thread *out, size_t cap, size_t *n_out);

/* Arêtes « thread T attend la ressource R tenue par U » (C2).
 * Une arête par thread bloqué au dernier état connu. */
int replique_waitfor_build(const replique_event *ev, size_t n,
                           replique_edge *out, size_t cap, size_t *n_out);

/* Désignation du coupable + émission des deux verdicts d'interblocage
 * (implémenté dans culprit.c, appelé par replique_deadlock_detect). */
size_t replique_culprit_emit(const replique_event *ev, size_t n,
                             const replique_edge *edges, size_t n_edges,
                             const replique_thread *threads, size_t n_threads,
                             const uint32_t *chain, size_t n_chain,
                             replique_verdict *out);

/* Cycle du graphe d'attente → REPLIQUE_V_DEADLOCK_FOUND + REPLIQUE_V_CULPRIT
 * (règle de désignation gelée, §3.3). Signature du cahier des charges §5.4. */
int replique_deadlock_detect(const replique_event *ev, size_t n,
                             replique_verdict *out, size_t cap, size_t *n_out);

/* Chaîne d'inversion de priorité [V, I, B] (§3.4). */
int replique_prio_inversion_detect(const replique_event *ev, size_t n,
                                   replique_verdict *out, size_t cap,
                                   size_t *n_out);
#endif
