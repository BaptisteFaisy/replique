/* deadlock.c — Autopsie : états, graphe d'attente, cycle, coupable.
 * CONTRACT.md §3.3 ; jumeau de host/replique/deadlock.py (mêmes règles, même
 * ordre de parcours). Aucune allocation, aucun fichier, aucune horloge.
 * L'inversion de priorité vit dans inversion.c, la désignation du coupable
 * dans culprit.c (même en-tête deadlock.h). */
#include "replique/deadlock.h"
#include "replique/sched.h"
#include "replique/verdict.h"
#include <string.h>

#define NO_TS UINT64_MAX

static const replique_thread *find_thread(const replique_thread *threads,
                                          size_t n, uint32_t tid);
static uint64_t last_block_ts(const replique_event *ev, size_t n, uint32_t tid);
static size_t rotate_cycle(const uint32_t *path, size_t plen, uint32_t entry,
                           uint32_t *chain, size_t cap);
static size_t build_succession(const replique_edge *edges, size_t n,
                               uint32_t *src, uint32_t *dst, size_t cap);
static uint32_t successeur(const uint32_t *src, const uint32_t *dst, size_t ns,
                           uint32_t node);
static size_t walk_chain(const uint32_t *src, const uint32_t *dst, size_t ns,
                         size_t start, uint32_t *path, size_t path_cap,
                         uint32_t *chain, size_t cap);
static size_t dead_holders(const replique_event *ev, size_t n,
                           const replique_thread *threads, size_t n_threads,
                           replique_verdict *out, size_t cap);

int replique_states_rebuild(const replique_event *ev, size_t n,
                            replique_thread *out, size_t cap, size_t *n_out)
{
  replique_sched state;
  size_t i;
  size_t written = 0u;

  if (n_out) {
    *n_out = 0u;
  }
  if (!out || (n && !ev)) {
    return REPLIQUE_ERR_ARG;
  }
  replique_sched_init(&state, NULL);
  for (i = 0u; i < n; ++i) {
    replique_sched_apply(&state, &ev[i]);
  }
  for (i = 0u; i < state.n_threads && written < cap; ++i) {
    out[written++] = state.threads[i];
  }
  if (n_out) {
    *n_out = written;
  }
  return written < state.n_threads ? REPLIQUE_ERR_OVERFLOW : REPLIQUE_STATUS_OK;
}

static const replique_thread *find_thread(const replique_thread *threads,
                                          size_t n, uint32_t tid)
{
  size_t i;

  for (i = 0u; i < n; ++i) {
    if (threads[i].tid == tid) {
      return &threads[i];
    }
  }
  return NULL;
}

static uint64_t last_block_ts(const replique_event *ev, size_t n, uint32_t tid)
{
  uint64_t ts = 0u;
  size_t i;

  for (i = 0u; i < n; ++i) {
    if (ev[i].tid == tid && ev[i].cls == REPLIQUE_EV_THREAD_STATE &&
        ev[i].subcode == REPLIQUE_TH_BLOCKED) {
      ts = ev[i].ts;
    }
  }
  return ts;
}

int replique_waitfor_build(const replique_event *ev, size_t n,
                           replique_edge *out, size_t cap, size_t *n_out)
{
  static replique_thread threads[REPLIQUE_MAX_THREADS];
  size_t count = 0u;
  size_t written = 0u;
  size_t i;

  if (n_out) {
    *n_out = 0u;
  }
  if (!out) {
    return REPLIQUE_ERR_ARG;
  }
  if (replique_states_rebuild(ev, n, threads, REPLIQUE_MAX_THREADS, &count)) {
    return REPLIQUE_ERR_OVERFLOW;
  }
  for (i = 0u; i < count && written < cap; ++i) {
    const replique_thread *holder;
    if (threads[i].state != REPLIQUE_TH_BLOCKED || !threads[i].holder) {
      continue;
    }
    holder = find_thread(threads, count, threads[i].holder);
    if (!holder || !holder->alive) {
      continue;                      /* attente orpheline : aucune arête */
    }
    out[written].from_tid = threads[i].tid;
    out[written].to_tid = threads[i].holder;
    out[written].kind = threads[i].block_reason;
    out[written].resource = threads[i].resource;
    out[written].ts = last_block_ts(ev, n, threads[i].tid);
    ++written;
  }
  if (n_out) {
    *n_out = written;
  }
  return REPLIQUE_STATUS_OK;
}

/* ------------------------------------------------- graphe d'attente / cycle */

static size_t build_succession(const replique_edge *edges, size_t n,
                               uint32_t *src, uint32_t *dst, size_t cap)
{
  size_t ns = 0u;
  size_t i;
  size_t j;

  for (i = 0u; i < n; ++i) {
    int known = 0;
    for (j = 0u; j < ns; ++j) {
      known = known || src[j] == edges[i].from_tid;
    }
    if (!known && ns < cap) {
      src[ns] = edges[i].from_tid;
      dst[ns] = edges[i].to_tid;
      ++ns;
    }
  }
  for (i = 1u; i < ns; ++i) {          /* tri par insertion, déterministe */
    uint32_t ks = src[i];
    uint32_t kd = dst[i];
    j = i;
    while (j > 0u && src[j - 1u] > ks) {
      src[j] = src[j - 1u];
      dst[j] = dst[j - 1u];
      --j;
    }
    src[j] = ks;
    dst[j] = kd;
  }
  return ns;
}

static uint32_t successeur(const uint32_t *src, const uint32_t *dst, size_t ns,
                           uint32_t node)
{
  size_t k;

  for (k = 0u; k < ns; ++k) {
    if (src[k] == node) {
      return dst[k];
    }
  }
  return 0u;
}

static size_t rotate_cycle(const uint32_t *path, size_t plen, uint32_t entry,
                           uint32_t *chain, size_t cap)
{
  uint32_t tmp[REPLIQUE_MAX_CHAIN];
  size_t start = 0u;
  size_t len = 0u;
  size_t min_at = 0u;
  size_t i;

  for (i = 0u; i < plen; ++i) {
    if (path[i] == entry) {
      start = i;
    }
  }
  for (i = start; i < plen && len < cap; ++i) {
    chain[len++] = path[i];
  }
  for (i = 1u; i < len; ++i) {
    if (chain[i] < chain[min_at]) {
      min_at = i;
    }
  }
  if (min_at && len <= REPLIQUE_MAX_CHAIN) {
    memcpy(tmp, chain + min_at, (len - min_at) * sizeof tmp[0]);
    memcpy(tmp + (len - min_at), chain, min_at * sizeof tmp[0]);
    memcpy(chain, tmp, len * sizeof tmp[0]);
  }
  return len;
}

/* Suit la chaîne depuis src[start] ; renvoie la longueur du cycle (0 sinon). */
static size_t walk_chain(const uint32_t *src, const uint32_t *dst, size_t ns,
                         size_t start, uint32_t *path, size_t path_cap,
                         uint32_t *chain, size_t cap)
{
  size_t plen = 0u;
  uint32_t node = src[start];

  while (node && plen < path_cap) {
    size_t k;
    int present = 0;
    for (k = 0u; k < plen; ++k) {
      present = present || path[k] == node;
    }
    if (present) {
      return rotate_cycle(path, plen, node, chain, cap);
    }
    path[plen++] = node;
    node = successeur(src, dst, ns, path[plen - 1u]);
  }
  return 0u;
}

/* Chaque thread bloqué a au plus une arête sortante : suivre la chaîne suffit.
 * Le cycle est renvoyé en commençant par son plus petit tid. */
static size_t find_cycle(const replique_edge *edges, size_t n, uint32_t *chain,
                         size_t cap)
{
  uint32_t src[REPLIQUE_MAX_THREADS];
  uint32_t dst[REPLIQUE_MAX_THREADS];
  uint32_t path[REPLIQUE_MAX_CHAIN];
  size_t ns;
  size_t i;
  size_t len;

  ns = build_succession(edges, n, src, dst, REPLIQUE_MAX_THREADS);
  for (i = 0u; i < ns; ++i) {
    len = walk_chain(src, dst, ns, i, path, REPLIQUE_MAX_CHAIN, chain, cap);
    if (len) {
      return len;
    }
  }
  return 0u;
}

int replique_deadlock_detect(const replique_event *ev, size_t n,
                             replique_verdict *out, size_t cap, size_t *n_out)
{
  static replique_thread threads[REPLIQUE_MAX_THREADS];
  static replique_edge edges[REPLIQUE_MAX_EDGES];
  uint32_t chain[REPLIQUE_MAX_CHAIN];
  size_t n_threads = 0u;
  size_t n_edges = 0u;
  size_t n_chain = 0u;
  size_t written = 0u;

  if (n_out) {
    *n_out = 0u;
  }
  if (!out || !cap) {
    return REPLIQUE_ERR_ARG;
  }
  replique_states_rebuild(ev, n, threads, REPLIQUE_MAX_THREADS, &n_threads);
  replique_waitfor_build(ev, n, edges, REPLIQUE_MAX_EDGES, &n_edges);
  n_chain = find_cycle(edges, n_edges, chain, REPLIQUE_MAX_CHAIN);
  if (n_chain && cap >= 2u) {
    written = replique_culprit_emit(ev, n, edges, n_edges, threads, n_threads,
                                    chain, n_chain, out);
  }
  written += dead_holders(ev, n, threads, n_threads, out + written,
                          cap > written ? cap - written : 0u);
  if (n_out) {
    *n_out = written;
  }
  return REPLIQUE_STATUS_OK;
}

/* ------------------------------------------------------------- utilitaires */

/* Threads ENCORE bloqués en fin de trace sur un détenteur terminé : attente
 * orpheline, jamais un cycle (SPEC §4). */
static size_t dead_holders(const replique_event *ev, size_t n,
                           const replique_thread *threads, size_t n_threads,
                           replique_verdict *out, size_t cap)
{
  size_t orphans = 0u;
  size_t first = 0u;
  size_t i;
  int seen = 0;

  if (!cap) {
    return 0u;
  }
  for (i = 0u; i < n_threads; ++i) {
    const replique_thread *holder;
    if (threads[i].state != REPLIQUE_TH_BLOCKED || !threads[i].holder) {
      continue;
    }
    holder = find_thread(threads, n_threads, threads[i].holder);
    if (holder && holder->alive) {
      continue;
    }
    if (!seen) {
      first = i;
      seen = 1;
    }
    ++orphans;
  }
  if (!orphans) {
    return 0u;
  }
  replique_verdict_set(out, REPLIQUE_V_TRACE_INCOMPLETE,
                       last_block_ts(ev, n, threads[first].tid),
                       threads[first].pid, threads[first].tid,
                       (uint32_t)orphans, threads[first].resource, 0u,
                       "holder-dead");
  return 1u;
}
