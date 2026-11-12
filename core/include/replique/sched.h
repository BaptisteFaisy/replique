#ifndef REPLIQUE_SCHED_H
#define REPLIQUE_SCHED_H
/* sched.h — modèle de l'ordonnanceur QNX (CONTRACT.md §5). C11 pur :
 * l'état vit dans une structure fournie par l'appelant, jamais d'allocation. */
#include "types.h"

#define REPLIQUE_MAX_THREADS 256
#define REPLIQUE_MAX_RESOURCES 128

typedef struct replique_thread {
  uint32_t tid;
  uint32_t pid;
  uint32_t state;        /* enum replique_thread_state */
  uint32_t policy;       /* enum replique_sched_policy */
  uint32_t base_prio;    /* 0 = plus haute */
  uint32_t eff_prio;     /* priorité effective (héritage inclus) */
  uint32_t block_reason; /* enum replique_block_reason */
  uint32_t resource;     /* ressource attendue */
  uint32_t holder;       /* détenteur de la ressource (0 = inconnu) */
  uint64_t ready_ts;     /* instant du dernier passage READY (FIFO) */
  uint32_t alive;
} replique_thread;

typedef struct replique_resource {
  uint32_t id;            /* id mutex / canal / sémaphore */
  uint32_t owner;         /* tid détenteur, 0 = libre */
} replique_resource;

typedef struct replique_sched {
  replique_thread threads[REPLIQUE_MAX_THREADS];
  size_t n_threads;
  replique_resource resources[REPLIQUE_MAX_RESOURCES];
  size_t n_resources;
  uint32_t running_tid;   /* 0 = aucun */
  uint64_t now;
  uint32_t incomplete;    /* 1 = modèle non couvert (ex. SPORADIC) */
  char incomplete_detail[REPLIQUE_DETAIL_MAX];
  replique_sched_config cfg;
} replique_sched;

void replique_sched_init(replique_sched *s, const replique_sched_config *cfg);
replique_thread *replique_sched_find(replique_sched *s, uint32_t tid);
replique_thread *replique_sched_intern(replique_sched *s, uint32_t tid,
                                       uint32_t pid);
/* Élit le thread READY le plus prioritaire ; 0 si aucun. À priorité égale :
 * FIFO = ready_ts le plus ancien puis petit tid ; RR = file tournante. */
uint32_t replique_sched_pick(replique_sched *s, uint64_t now);
/* Élit `tid` : rétrograde l'élu précédent en READY, marque le nouvel élu. */
void replique_sched_elect(replique_sched *s, uint32_t tid, uint64_t now);
/* Propriété des ressources (mutex, sémaphores). */
void replique_sched_take(replique_sched *s, uint32_t resource, uint32_t tid);
void replique_sched_drop(replique_sched *s, uint32_t resource);
uint32_t replique_sched_owner(const replique_sched *s, uint32_t resource);
/* Applique un événement de trace à l'état (sans produire de décision). */
int replique_sched_apply(replique_sched *s, const replique_event *ev);
/* Héritage de priorité : applique/rend selon cfg.inheritance. */
void replique_sched_inherit(replique_sched *s, uint32_t holder, uint32_t waiter);
void replique_sched_release(replique_sched *s, uint32_t holder);
uint32_t replique_sched_override_prio(const replique_sched_config *cfg,
                                      uint32_t tid, uint32_t base);
#endif
