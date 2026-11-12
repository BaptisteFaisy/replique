#ifndef REPLIQUE_REPLAY_H
#define REPLIQUE_REPLAY_H
/* replay.h — rejeu déterministe de la trace (CONTRACT.md §5, §6, exigences
 * X1–X4). Purement fonctionnel : mêmes événements + même cfg → mêmes décisions. */
#include "types.h"

/* Rejoue ev[0..n) avec cfg. res->cap doit être fourni par l'appelant avec un
 * tampon de décisions (res->decisions est un pointeur à poser avant l'appel). */
int replique_replay(const replique_event *ev, size_t n,
                    const replique_sched_config *cfg,
                    replique_replay_result *res);

/* Hachage FNV-1a 64 de la séquence de décisions (lignes "dec|…" du contrat). */
uint64_t replique_replay_hash(const replique_decision *dec, size_t n);

/* Compare la séquence rejouée aux événements SCHED de la trace (P2).
 * Retour : 0 = fidèle, 1 = divergence décrite dans *out. */
int replique_replay_check_fidelity(const replique_event *ev, size_t n,
                                   const replique_decision *dec, size_t n_dec,
                                   replique_verdict *out);
#endif
