/* sched.c — modèle de l'ordonnanceur QNX (CONTRACT.md §5).
 * Jumeau de host/replique/sched.py : mêmes règles, même ordre d'élection,
 * aucune allocation, aucune horloge. Priorités QNX : 0 = plus haute. */
#include "replique/sched.h"
#include <string.h>

void replique_sched_config_default(replique_sched_config *cfg)
{
  if (!cfg) {
    return;
  }
  memset(cfg, 0, sizeof *cfg);
  cfg->n_cpus = 1u;
  cfg->inheritance = 1u;
  cfg->rr_quantum = 0u;
  cfg->n_overrides = 0u;
}

void replique_sched_init(replique_sched *s, const replique_sched_config *cfg)
{
  if (!s) {
    return;
  }
  memset(s, 0, sizeof *s);
  if (cfg) {
    s->cfg = *cfg;
  } else {
    replique_sched_config_default(&s->cfg);
  }
}

replique_thread *replique_sched_find(replique_sched *s, uint32_t tid)
{
  size_t i;

  if (!s || !tid) {
    return NULL;
  }
  for (i = 0u; i < s->n_threads; ++i) {
    if (s->threads[i].tid == tid) {
      return &s->threads[i];
    }
  }
  return NULL;
}

replique_thread *replique_sched_intern(replique_sched *s, uint32_t tid,
                                       uint32_t pid)
{
  replique_thread *thread = replique_sched_find(s, tid);

  if (thread) {
    if (pid && !thread->pid) {
      thread->pid = pid;
    }
    return thread;
  }
  if (!s || s->n_threads >= REPLIQUE_MAX_THREADS) {
    return NULL;
  }
  thread = &s->threads[s->n_threads++];
  memset(thread, 0, sizeof *thread);
  thread->tid = tid;
  thread->pid = pid;
  thread->base_prio = REPLIQUE_PRIO_LEVELS - 1u;
  thread->eff_prio = thread->base_prio;
  return thread;
}

/* Ordre d'élection gelé : (priorité effective, ready_ts, tid) minimal.
 * Le thread RUNNING est candidat : à égalité il garde la main. */
uint32_t replique_sched_pick(replique_sched *s, uint64_t now)
{
  size_t i;
  int found = 0;
  replique_thread chosen;

  if (!s) {
    return 0u;
  }
  memset(&chosen, 0, sizeof chosen);
  for (i = 0u; i < s->n_threads; ++i) {
    const replique_thread *t = &s->threads[i];
    if (!t->alive || (t->state != REPLIQUE_TH_READY &&
                      t->state != REPLIQUE_TH_RUNNING)) {
      continue;
    }
    if (!found || t->eff_prio < chosen.eff_prio ||
        (t->eff_prio == chosen.eff_prio &&
         (t->ready_ts < chosen.ready_ts ||
          (t->ready_ts == chosen.ready_ts && t->tid < chosen.tid)))) {
      chosen = *t;
      found = 1;
    }
  }
  s->now = now;
  return found ? chosen.tid : 0u;
}

void replique_sched_elect(replique_sched *s, uint32_t tid, uint64_t now)
{
  replique_thread *previous;
  replique_thread *chosen;

  if (!s) {
    return;
  }
  previous = replique_sched_find(s, s->running_tid);
  if (previous && previous->tid != tid && previous->alive &&
      previous->state == REPLIQUE_TH_RUNNING) {
    previous->state = REPLIQUE_TH_READY;
    previous->ready_ts = now;
  }
  chosen = replique_sched_find(s, tid);
  if (chosen) {
    chosen->state = REPLIQUE_TH_RUNNING;
    if (!chosen->ready_ts || chosen->ready_ts > now) {
      chosen->ready_ts = now;
    }
  }
  s->running_tid = tid;
  s->now = now;
}

void replique_sched_take(replique_sched *s, uint32_t resource, uint32_t tid)
{
  size_t i;

  if (!s) {
    return;
  }
  for (i = 0u; i < s->n_resources; ++i) {
    if (s->resources[i].id == resource) {
      s->resources[i].owner = tid;
      return;
    }
  }
  if (s->n_resources < REPLIQUE_MAX_RESOURCES) {
    s->resources[s->n_resources].id = resource;
    s->resources[s->n_resources].owner = tid;
    ++s->n_resources;
  }
}

void replique_sched_drop(replique_sched *s, uint32_t resource)
{
  size_t i;

  if (!s) {
    return;
  }
  for (i = 0u; i < s->n_resources; ++i) {
    if (s->resources[i].id == resource) {
      s->resources[i] = s->resources[--s->n_resources];
      return;
    }
  }
}

uint32_t replique_sched_owner(const replique_sched *s, uint32_t resource)
{
  size_t i;

  if (!s) {
    return 0u;
  }
  for (i = 0u; i < s->n_resources; ++i) {
    if (s->resources[i].id == resource) {
      return s->resources[i].owner;
    }
  }
  return 0u;
}

void replique_sched_inherit(replique_sched *s, uint32_t holder, uint32_t waiter)
{
  replique_thread *target;
  replique_thread *source;

  if (!s || !s->cfg.inheritance) {
    return;
  }
  target = replique_sched_find(s, holder);
  source = replique_sched_find(s, waiter);
  if (target && source && source->eff_prio < target->eff_prio) {
    target->eff_prio = source->eff_prio;
  }
}

void replique_sched_release(replique_sched *s, uint32_t holder)
{
  replique_thread *target = replique_sched_find(s, holder);
  uint32_t best;
  size_t i;

  if (!target) {
    return;
  }
  best = target->base_prio;
  for (i = 0u; i < s->n_threads; ++i) {
    const replique_thread *t = &s->threads[i];
    if (t->alive && t->state == REPLIQUE_TH_BLOCKED && t->holder == holder &&
        t->eff_prio < best) {
      best = t->eff_prio;
    }
  }
  target->eff_prio = best;
}

uint32_t replique_sched_override_prio(const replique_sched_config *cfg,
                                      uint32_t tid, uint32_t base)
{
  uint32_t i;

  if (!cfg) {
    return base;
  }
  for (i = 0u; i < cfg->n_overrides && i < REPLIQUE_MAX_OVERRIDES; ++i) {
    if (cfg->overrides[i].tid == tid) {
      return cfg->overrides[i].prio;
    }
  }
  return base;
}

/* ------------------------------------------------------------ application */

static void on_create(replique_sched *s, const replique_event *ev)
{
  replique_thread *thread = replique_sched_intern(s, ev->tid, ev->pid);

  if (!thread) {
    return;
  }
  thread->alive = 1u;
  thread->policy = ev->subcode;
  thread->base_prio = replique_sched_override_prio(&s->cfg, ev->tid, ev->a);
  thread->eff_prio = thread->base_prio;
  if (ev->subcode == REPLIQUE_SCHED_SPORADIC && !s->incomplete) {
    /* Chaîne gelée (CONTRACT.md §5) : copiée sans stdio.h, cœur pur. */
    static const char detail[] = "sporadic-not-modelled";
    s->incomplete = 1u;
    memcpy(s->incomplete_detail, detail, sizeof detail);
  }
}

static void on_destroy(replique_sched *s, const replique_event *ev)
{
  size_t i;

  for (i = 0u; i < s->n_threads; ++i) {
    replique_thread *t = &s->threads[i];
    int victim = (ev->cls == REPLIQUE_EV_THREAD_DESTROY) ? (t->tid == ev->tid)
                                                          : (t->pid == ev->pid);
    if (victim) {
      t->alive = 0u;
      t->state = REPLIQUE_TH_DEAD;
      if (t->tid == s->running_tid) {
        s->running_tid = 0u;
      }
    }
  }
  for (i = 0u; i < s->n_resources;) {
    const replique_thread *owner = replique_sched_find(s, s->resources[i].owner);
    if (!owner || !owner->alive) {
      s->resources[i] = s->resources[--s->n_resources];
      continue;
    }
    ++i;
  }
}

static void on_state(replique_sched *s, const replique_event *ev)
{
  replique_thread *thread = replique_sched_intern(s, ev->tid, ev->pid);

  if (!thread) {
    return;
  }
  thread->alive = (ev->subcode != REPLIQUE_TH_DEAD);
  if (ev->subcode == REPLIQUE_TH_READY) {
    thread->state = REPLIQUE_TH_READY;
    thread->ready_ts = ev->ts;
    thread->block_reason = 0u;
    thread->resource = 0u;
    thread->holder = 0u;
    if (thread->tid == s->running_tid) {
      s->running_tid = 0u;         /* cède sa place (quantum, yield) */
    }
  } else if (ev->subcode == REPLIQUE_TH_BLOCKED) {
    thread->state = REPLIQUE_TH_BLOCKED;
    thread->block_reason = ev->a;
    thread->resource = ev->b;
    thread->holder = ev->c;
    if (thread->tid == s->running_tid) {
      s->running_tid = 0u;
    }
    if (s->cfg.inheritance && s->cfg.n_overrides && ev->a == REPLIQUE_BR_MUTEX) {
      replique_sched_inherit(s, ev->c, ev->tid);
    }
  } else if (ev->subcode == REPLIQUE_TH_RUNNING) {
    thread->state = REPLIQUE_TH_RUNNING;
    s->running_tid = thread->tid;
  } else {
    thread->state = ev->subcode;
    if (thread->tid == s->running_tid) {
      s->running_tid = 0u;
    }
  }
}

static void on_lock(replique_sched *s, const replique_event *ev)
{
  if (ev->subcode == 0u) {
    replique_sched_take(s, ev->a, ev->tid);
  } else if (s->cfg.inheritance && s->cfg.n_overrides) {
    replique_sched_inherit(s, ev->b, ev->tid);
  }
}

static void on_prio(replique_sched *s, const replique_event *ev)
{
  replique_thread *thread = replique_sched_intern(s, ev->tid, ev->pid);

  if (!thread) {
    return;
  }
  thread->eff_prio = ev->a;
  if (ev->subcode == 2u) {
    thread->base_prio = replique_sched_override_prio(&s->cfg, ev->tid, ev->a);
  }
}

/* Fin de quantum RR : le thread courant repasse prêt, derrière les autres. */
static void on_timer(replique_sched *s, const replique_event *ev)
{
  replique_thread *thread = replique_sched_find(s, ev->tid);

  if (!thread || thread->tid != s->running_tid) {
    return;
  }
  if (thread->policy == REPLIQUE_SCHED_RR && (ev->b || s->cfg.rr_quantum)) {
    thread->state = REPLIQUE_TH_READY;
    thread->ready_ts = ev->ts + 1u;
    s->running_tid = 0u;
  }
}

int replique_sched_apply(replique_sched *s, const replique_event *ev)
{
  if (!s || !ev) {
    return REPLIQUE_ERR_ARG;
  }
  s->now = ev->ts;
  if (ev->cls == REPLIQUE_EV_SCHED) {
    return REPLIQUE_STATUS_OK;     /* observation du noyau : état inchangé */
  }
  switch (ev->cls) {
    case REPLIQUE_EV_THREAD_CREATE: on_create(s, ev); break;
    case REPLIQUE_EV_THREAD_DESTROY:
    case REPLIQUE_EV_PROCESS_DESTROY: on_destroy(s, ev); break;
    case REPLIQUE_EV_THREAD_STATE: on_state(s, ev); break;
    case REPLIQUE_EV_MUTEX_LOCK:
    case REPLIQUE_EV_SEM_WAIT: on_lock(s, ev); break;
    case REPLIQUE_EV_MUTEX_UNLOCK:
    case REPLIQUE_EV_SEM_POST: replique_sched_drop(s, ev->a); break;
    case REPLIQUE_EV_PRIO_CHANGE: on_prio(s, ev); break;
    case REPLIQUE_EV_TIMER: on_timer(s, ev); break;
    default: break;
  }
  return REPLIQUE_STATUS_OK;
}
