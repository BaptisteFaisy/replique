/* replay.c — rejeu déterministe (CONTRACT.md §5, §6). Jumeau de
 * host/replique/replay.py : une passe, aucune horloge, aucun aléa.
 * P1 : mêmes événements + même configuration → mêmes décisions, même hachage. */
#include "replique/replay.h"
#include "replique/kev.h"
#include "replique/report.h"
#include "replique/sched.h"
#include "replique/verdict.h"
#include <string.h>

#define LINE_BUF 160

static int is_overridden(const replique_sched_config *cfg, uint32_t tid)
{
  uint32_t i;

  if (!cfg) {
    return 0;
  }
  for (i = 0u; i < cfg->n_overrides && i < REPLIQUE_MAX_OVERRIDES; ++i) {
    if (cfg->overrides[i].tid == tid) {
      return 1;
    }
  }
  return 0;
}

/* Raison gelée de la décision (CONTRACT.md §2.6, ordre de priorité). */
static uint32_t decide_reason(int first, const replique_event *ev,
                              uint32_t prev_current, uint32_t best,
                              int subject_blocked)
{
  int killed = (ev->cls == REPLIQUE_EV_THREAD_DESTROY ||
                ev->cls == REPLIQUE_EV_PROCESS_DESTROY);
  int blocked_current = (ev->cls == REPLIQUE_EV_THREAD_STATE &&
                         ev->tid == prev_current &&
                         (ev->subcode == REPLIQUE_TH_BLOCKED ||
                          ev->subcode == REPLIQUE_TH_DEAD));

  if (first) {
    return REPLIQUE_SR_INITIAL;
  }
  if (killed && prev_current && best != prev_current) {
    return REPLIQUE_SR_TERMINATE;
  }
  if (blocked_current) {
    return REPLIQUE_SR_BLOCK;
  }
  if (ev->cls == REPLIQUE_EV_TIMER && ev->tid == prev_current) {
    return REPLIQUE_SR_TIMER;
  }
  if (best == ev->tid && subject_blocked) {
    return REPLIQUE_SR_UNBLOCK;
  }
  return REPLIQUE_SR_PREEMPT;
}

uint64_t replique_replay_hash(const replique_decision *dec, size_t n)
{
  uint64_t hash = replique_fnv1a64_begin();
  size_t i;

  for (i = 0u; i < n; ++i) {
    char line[LINE_BUF];
    unsigned truncated = 0u;
    size_t len = replique_report_decision(dec ? &dec[i] : NULL, line,
                                          sizeof line, &truncated);
    if (truncated) {
      break;                        /* ligne impossible : contrat violé */
    }
    hash = replique_fnv1a64_update(hash, line, len);
  }
  return hash;
}

static void reset_result(replique_replay_result *res)
{
  res->status = REPLIQUE_STATUS_OK;
  res->n_decisions = 0u;
  res->hash = 0u;
  res->diverged = 0u;
  res->diverge_ts = 0u;
  res->diverge_tid = 0u;
  res->expected_reason = 0u;
  res->found_reason = 0u;
  res->incomplete = 0u;
  res->incomplete_detail[0] = '\0';
}

/* Une décision d'ordonnancement : raison gelée, puis « et si » prioritaire. */
static int emit_decision(replique_replay_result *res, const replique_sched *st,
                         const replique_event *ev, uint32_t prev_current,
                         uint32_t best, int subject_blocked)
{
  uint32_t reason = decide_reason(res->n_decisions == 0u, ev, prev_current,
                                  best, subject_blocked);

  if (is_overridden(&st->cfg, best)) {
    reason = REPLIQUE_SR_WHATIF;
  }
  if (res->n_decisions >= res->cap) {
    res->status = REPLIQUE_ERR_OVERFLOW;
    return 0;
  }
  if (res->decisions) {
    res->decisions[res->n_decisions].ts = ev->ts;
    res->decisions[res->n_decisions].tid = best;
    res->decisions[res->n_decisions].reason = reason;
  }
  ++res->n_decisions;
  return 1;
}

int replique_replay(const replique_event *ev, size_t n,
                    const replique_sched_config *cfg,
                    replique_replay_result *res)
{
  replique_sched state;
  size_t i;

  if (!res || (n && !ev)) {
    return REPLIQUE_ERR_ARG;
  }
  replique_sched_init(&state, cfg);
  reset_result(res);
  for (i = 0u; i < n && res->status == REPLIQUE_STATUS_OK; ++i) {
    const replique_thread *subject = replique_sched_find(&state, ev[i].tid);
    int subject_blocked = (subject && subject->state == REPLIQUE_TH_BLOCKED);
    uint32_t prev_current = state.running_tid;
    uint32_t best;

    replique_sched_apply(&state, &ev[i]);
    best = replique_sched_pick(&state, ev[i].ts);
    if (!best || best == state.running_tid) {
      continue;
    }
    if (!emit_decision(res, &state, &ev[i], prev_current, best,
                       subject_blocked)) {
      break;
    }
    replique_sched_elect(&state, best, ev[i].ts);
  }
  res->hash = replique_replay_hash(res->decisions, res->n_decisions);
  res->incomplete = state.incomplete;
  memcpy(res->incomplete_detail, state.incomplete_detail,
         sizeof res->incomplete_detail);
  return res->status;
}

static void diverged(replique_verdict *out, uint64_t ts, uint32_t tid,
                     uint32_t expected, uint32_t found)
{
  replique_verdict_set(out, REPLIQUE_V_REPLAY_DIVERGED, ts, 0u, tid, expected,
                       found, 0u, "replay-diverged");
}

int replique_replay_check_fidelity(const replique_event *ev, size_t n,
                                   const replique_decision *dec, size_t n_dec,
                                   replique_verdict *out)
{
  size_t observed = 0u;
  size_t index = 0u;
  size_t i;

  if (!ev || !dec || !out) {
    return REPLIQUE_ERR_ARG;
  }
  for (i = 0u; i < n; ++i) {
    if (ev[i].cls == REPLIQUE_EV_SCHED) {
      ++observed;
    }
  }
  if (!observed) {
    return 0;                       /* rien à comparer : aucun verdict */
  }
  for (i = 0u; i < n; ++i) {
    if (ev[i].cls != REPLIQUE_EV_SCHED) {
      continue;
    }
    if (index >= n_dec) {
      diverged(out, ev[i].ts, ev[i].a, ev[i].subcode, 0u);
      return 1;
    }
    if (dec[index].ts != ev[i].ts || dec[index].tid != ev[i].a) {
      diverged(out, ev[i].ts, ev[i].a, ev[i].subcode, dec[index].reason);
      return 1;
    }
    if (dec[index].reason != ev[i].subcode) {
      diverged(out, ev[i].ts, ev[i].a, ev[i].subcode, dec[index].reason);
      return 1;
    }
    ++index;
  }
  if (n_dec != observed) {
    diverged(out, dec[observed].ts, dec[observed].tid, 0u,
             dec[observed].reason);
    return 1;
  }
  return 0;
}
