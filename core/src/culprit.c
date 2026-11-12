/* culprit.c — désignation du coupable d'un interblocage (CONTRACT.md §3.3).
 * Règle gelée : dans le cycle, priorité de BASE la plus basse (plus grand
 * numéro), puis ressource acquise le plus anciennement, puis plus petit tid.
 * Découpage de deadlock.c pour tenir la garde des 500 lignes par fichier. */
#include "replique/deadlock.h"
#include "replique/sched.h"
#include "replique/verdict.h"
#include <string.h>

#define ACQUIS_CAP REPLIQUE_MAX_RESOURCES
#define NO_TS UINT64_MAX

typedef struct replique_acquis {
  uint32_t tid;
  uint32_t resource;
  uint64_t ts;
} replique_acquis;

static const replique_thread *lookup(const replique_thread *threads, size_t n,
                                     uint32_t tid)
{
  size_t i;

  for (i = 0u; i < n; ++i) {
    if (threads[i].tid == tid) {
      return &threads[i];
    }
  }
  return NULL;
}

/* Écrit un entier en décimal (cœur pur : pas de snprintf) ; renvoie sa taille. */
static size_t u32_str(char *out, uint32_t value)
{
  char tmp[12];
  size_t len = 0u;
  size_t i;

  if (!value) {
    out[0] = '0';
    return 1u;
  }
  while (value && len < sizeof tmp) {
    tmp[len++] = (char)('0' + (value % 10u));
    value /= 10u;
  }
  for (i = 0u; i < len; ++i) {
    out[i] = tmp[len - 1u - i];
  }
  return len;
}

static void format_culprit(char *out, size_t cap, uint32_t resource,
                           uint32_t prio)
{
  static const char prefix[] = "holds-resource-";
  static const char middle[] = "-at-prio-";
  size_t used = 0u;

  out[0] = '\0';
  if (cap < sizeof prefix + sizeof middle + 24u) {
    return;
  }
  memcpy(out, prefix, sizeof prefix - 1u);
  used = sizeof prefix - 1u;
  used += u32_str(out + used, resource);
  memcpy(out + used, middle, sizeof middle - 1u);
  used += sizeof middle - 1u;
  used += u32_str(out + used, prio);
  out[used] = '\0';
}

/* ------------------------------------------------------------- coupable */

static size_t acquisitions(const replique_event *ev, size_t n,
                           replique_acquis *out, size_t cap)
{
  size_t count = 0u;
  size_t i;
  size_t j;

  for (i = 0u; i < n; ++i) {
    int known = 0;
    if (ev[i].cls != REPLIQUE_EV_MUTEX_LOCK && ev[i].cls != REPLIQUE_EV_SEM_WAIT) {
      continue;
    }
    if (ev[i].subcode != 0u) {
      continue;
    }
    for (j = 0u; j < count; ++j) {
      known = known || (out[j].tid == ev[i].tid && out[j].resource == ev[i].a);
    }
    if (!known && count < cap) {
      out[count].tid = ev[i].tid;
      out[count].resource = ev[i].a;
      out[count].ts = ev[i].ts;
      ++count;
    }
  }
  return count;
}

static uint64_t acquis_ts(const replique_acquis *list, size_t n, uint32_t tid,
                          uint32_t resource)
{
  size_t i;

  for (i = 0u; i < n; ++i) {
    if (list[i].tid == tid && list[i].resource == resource) {
      return list[i].ts;
    }
  }
  return NO_TS;
}

/* Règle gelée §3.3 : priorité de BASE la plus basse, puis acquisition la plus
 * ancienne, puis plus petit tid. */
static int better_culprit(uint32_t base, uint64_t when, uint32_t tid,
                          uint32_t best_base, uint64_t best_when,
                          uint32_t best_tid)
{
  if (base != best_base) {
    return base > best_base;
  }
  if (when != best_when) {
    return when < best_when;
  }
  return tid < best_tid;
}

static void choose_culprit(const uint32_t *cycle, size_t n_cycle,
                           const replique_edge *edges, size_t n_edges,
                           const replique_thread *threads, size_t n_threads,
                           const replique_acquis *acquis, size_t n_acquis,
                           uint32_t *tid_out, uint32_t *res_out,
                           uint32_t *prio_out)
{
  uint64_t best_when = NO_TS;
  int found = 0;
  size_t i;

  *tid_out = 0u;
  *res_out = 0u;
  *prio_out = 0u;
  for (i = 0u; i < n_cycle; ++i) {
    const replique_thread *thread = lookup(threads, n_threads, cycle[i]);
    uint32_t base = thread ? thread->base_prio : REPLIQUE_PRIO_LEVELS - 1u;
    uint32_t held = 0u;
    uint64_t when;
    size_t j;
    for (j = 0u; j < n_edges; ++j) {
      if (edges[j].to_tid == cycle[i]) {
        held = edges[j].resource;
      }
    }
    when = acquis_ts(acquis, n_acquis, cycle[i], held);
    if (!found || better_culprit(base, when, cycle[i], *prio_out, best_when,
                                 *tid_out)) {
      *tid_out = cycle[i];
      *res_out = held;
      *prio_out = base;
      best_when = when;
      found = 1;
    }
  }
}

size_t replique_culprit_emit(const replique_event *ev, size_t n,
                            const replique_edge *edges, size_t n_edges,
                            const replique_thread *threads, size_t n_threads,
                            const uint32_t *chain, size_t n_chain,
                            replique_verdict *out)
{
  uint32_t tid = 0u;
  uint32_t held = 0u;
  uint32_t base = 0u;
  uint32_t closing = 0u;
  uint64_t last = 0u;
  static replique_acquis acquis[ACQUIS_CAP];
  size_t n_acquis = 0u;
  char detail[REPLIQUE_DETAIL_MAX];
  size_t i;

  for (i = 0u; i < n_edges; ++i) {
    if (edges[i].ts > last) {
      last = edges[i].ts;
      closing = edges[i].resource;
    }
  }
  n_acquis = acquisitions(ev, n, acquis, ACQUIS_CAP);
  choose_culprit(chain, n_chain, edges, n_edges, threads, n_threads, acquis,
                 n_acquis, &tid, &held, &base);
  replique_verdict_set(&out[0], REPLIQUE_V_DEADLOCK_FOUND, last, 0u, 0u, 0u,
                       closing, 0u, "cycle-detected");
  out[0].chain_len = (uint32_t)n_chain;
  memcpy(out[0].chain, chain, n_chain * sizeof chain[0]);
  format_culprit(detail, sizeof detail, held, base);
  replique_verdict_set(&out[1], REPLIQUE_V_CULPRIT, last, 0u, tid, 0u, held, 0u,
                       detail);
  out[1].chain_len = (uint32_t)n_chain;
  memcpy(out[1].chain, chain, n_chain * sizeof chain[0]);
  return 2u;
}

