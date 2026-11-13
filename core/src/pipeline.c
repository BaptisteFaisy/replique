/* pipeline.c — chaîne de traitement figée (CONTRACT.md §3.2). Aucune étape ne
 * remonte : une trace illisible produit UN SEUL verdict PARSE_ERROR.
 * Jumeau de host/replique/verdict.py:pipeline_*. */
#include "replique/verdict.h"
#include "replique/deadlock.h"
#include "replique/kev.h"
#include "replique/replay.h"
#include <string.h>

static void finish_replay(const replique_event *ev, size_t n,
                          const replique_sched_config *cfg,
                          replique_verdict *out, size_t cap, size_t *count,
                          replique_replay_result *res);
static void append_count(char *detail, size_t cap, size_t value);
static replique_decision *decisions_buffer(void);

static void add(replique_verdict *out, size_t cap, size_t *n,
                const replique_verdict *v)
{
  if (*n < cap) {
    out[*n] = *v;
  }
  ++*n;
}

static size_t emit_all(const replique_event *ev, size_t n,
                       const replique_sched_config *cfg, uint32_t flags,
                       replique_verdict *out, size_t cap,
                       replique_replay_result *res)
{
  replique_verdict verdict;
  size_t count = 0u;
  size_t i = 0u;

  replique_verdict_set(&verdict, REPLIQUE_V_OK, 0u, 0u, 0u, (uint32_t)n, 0u, 0u,
                       "parse-ok");
  add(out, cap, &count, &verdict);
  if (flags & REPLIQUE_FLAG_TRUNCATED) {
    replique_verdict_set(&verdict, REPLIQUE_V_TRACE_INCOMPLETE, 0u, 0u, 0u, 0u,
                         0u, 0u, "circular-buffer-overwritten");
    add(out, cap, &count, &verdict);
  }
  replique_deadlock_detect(ev, n, out + (count < cap ? count : cap),
                           cap > count ? cap - count : 0u, &i);
  count += i;
  replique_prio_inversion_detect(ev, n, out + (count < cap ? count : cap),
                                 cap > count ? cap - count : 0u, &i);
  count += i;
  finish_replay(ev, n, cfg, out, cap, &count, res);
  if (count > cap) {
    count = cap;
  }
  replique_verdict_sort(out, count);
  return count;
}

/* Rejeu, déterminisme (P1), fidélité (P2) et verdicts « et si » (X4). */
static void finish_replay(const replique_event *ev, size_t n,
                          const replique_sched_config *cfg,
                          replique_verdict *out, size_t cap, size_t *count,
                          replique_replay_result *res)
{
  replique_verdict verdict;
  uint32_t i;

  replique_replay(ev, n, cfg, res);
  replique_verdict_set(&verdict, REPLIQUE_V_REPLAY_DETERMINISTIC, 0u, 0u, 0u,
                       0u, 0u, 0u, "decisions-");
  verdict.hash = res->hash;
  append_count(verdict.detail, sizeof verdict.detail, res->n_decisions);
  add(out, cap, count, &verdict);
  if (!cfg || !cfg->n_overrides) {
    if (replique_replay_check_fidelity(ev, n, res->decisions, res->n_decisions,
                                       &verdict)) {
      add(out, cap, count, &verdict);
    }
  }
  if (res->incomplete) {
    replique_verdict_set(&verdict, REPLIQUE_V_TRACE_INCOMPLETE, 0u, 0u, 0u, 0u,
                         0u, 0u, res->incomplete_detail);
    add(out, cap, count, &verdict);
  }
  for (i = 0u; cfg && i < cfg->n_overrides && i < REPLIQUE_MAX_OVERRIDES; ++i) {
    replique_verdict_set(&verdict, REPLIQUE_V_WHATIF, 0u, 0u,
                         cfg->overrides[i].tid, cfg->overrides[i].prio, 0u, 0u,
                         "whatif-replay");
    verdict.hash = res->hash;
    add(out, cap, count, &verdict);
  }
}

/* "decisions-<n>" collé dans le detail (cœur pur : pas de snprintf). */
static void append_count(char *detail, size_t cap, size_t value)
{
  static const char prefix[] = "decisions-";
  char digits[24];
  size_t len = 0u;
  size_t used = sizeof prefix - 1u;
  size_t i;

  if (cap < used + 24u) {
    return;
  }
  if (!value) {
    digits[len++] = '0';
  }
  while (value && len < sizeof digits) {
    digits[len++] = (char)('0' + (value % 10u));
    value /= 10u;
  }
  for (i = 0u; i < len; ++i) {
    detail[used + i] = digits[len - 1u - i];
  }
  detail[used + len] = '\0';
}

/* Tampon de décisions du rejeu : statique, le cœur n'alloue jamais. */
static replique_decision *decisions_buffer(void)
{
  static replique_decision decisions[REPLIQUE_MAX_DECISIONS];

  return decisions;
}

/* Tampon d'evenements de repli : statique, borne par REPLIQUE_MAX_EVENTS.
 * Les traces reelles sont chargees avec un tampon dimensionne par l'appelant
 * (replique_kev_event_count) : ce repli ne sert qu'aux appels sans tampon. */
static replique_event *events_buffer(void)
{
  static replique_event events[REPLIQUE_MAX_EVENTS];

  return events;
}

int replique_pipeline(const uint8_t *buf, size_t len,
                      const replique_sched_config *cfg,
                      replique_verdict *verdicts, size_t vcap,
                      size_t *n_verdicts, replique_replay_result *res,
                      replique_event *events, size_t ecap)
{
  replique_header header;
  replique_sched_config local;
  replique_decision *decisions;
  size_t dcap;
  size_t n_events = 0u;
  unsigned cause = 0u;
  size_t offset = 0u;
  int status;

  if (n_verdicts) {
    *n_verdicts = 0u;
  }
  if (!verdicts || !vcap || !res) {
    return REPLIQUE_ERR_ARG;
  }
  decisions = res->decisions;
  dcap = res->cap;
  memset(res, 0, sizeof *res);
  res->decisions = (decisions && dcap) ? decisions : decisions_buffer();
  res->cap = (decisions && dcap) ? dcap : REPLIQUE_MAX_DECISIONS;
  if (!events) {
    events = events_buffer();
    ecap = REPLIQUE_MAX_EVENTS;
  }
  status = replique_kev_parse_diag(buf, len, events, ecap, &n_events, &cause,
                                   &offset);
  if (status != REPLIQUE_STATUS_OK) {
    replique_verdict_set(verdicts, REPLIQUE_V_PARSE_ERROR, 0u, 0u, 0u,
                         (uint32_t)offset, 0u, 0u, replique_cause_name(cause));
    *n_verdicts = 1u;
    memset(res, 0, sizeof *res);
    return status;
  }
  if (replique_kev_header(buf, len, &header) != REPLIQUE_STATUS_OK) {
    header.flags = 0u;
  }
  if (!cfg) {
    replique_sched_config_default(&local);
    cfg = &local;
  }
  *n_verdicts = emit_all(events, n_events, cfg, header.flags, verdicts, vcap,
                         res);
  return REPLIQUE_STATUS_OK;
}
