/* inversion.c — détection d'inversion de priorité (CONTRACT.md §3.4, exigence
 * C5). Jumeau de host/replique/deadlock.py:detect_prio_inversion : la chaîne
 * est [victime, intermédiaire, bloqueur] et `a` vaut 1 si le noyau a appliqué
 * l'héritage de priorité pendant la fenêtre de blocage. */
#include "replique/deadlock.h"
#include "replique/sched.h"
#include "replique/verdict.h"
#include <string.h>

#define MAX_WINDOWS 32
#define NO_TS UINT64_MAX

typedef struct replique_window {
  uint32_t victim;
  uint32_t holder;
  uint32_t resource;
  uint64_t start;
} replique_window;

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

/* Fenêtres de blocage où la victime est strictement plus prioritaire que le
 * détenteur (ordres QNX : plus petit numéro = plus prioritaire). */
static size_t find_windows(const replique_event *ev, size_t n,
                           const replique_thread *threads, size_t n_threads,
                           replique_window *out, size_t cap)
{
  size_t count = 0u;
  size_t i;

  for (i = 0u; i < n && count < cap; ++i) {
    const replique_thread *victim;
    const replique_thread *holder;
    if (ev[i].cls != REPLIQUE_EV_THREAD_STATE ||
        ev[i].subcode != REPLIQUE_TH_BLOCKED) {
      continue;
    }
    if (ev[i].a != REPLIQUE_BR_MUTEX && ev[i].a != REPLIQUE_BR_SEMAPHORE) {
      continue;
    }
    if (!ev[i].c) {
      continue;
    }
    victim = lookup(threads, n_threads, ev[i].tid);
    holder = lookup(threads, n_threads, ev[i].c);
    if (!victim || !holder || victim->base_prio >= holder->base_prio) {
      continue;
    }
    out[count].victim = ev[i].tid;
    out[count].holder = ev[i].c;
    out[count].resource = ev[i].b;
    out[count].start = ev[i].ts;
    ++count;
  }
  return count;
}

static uint64_t window_end(const replique_event *ev, size_t n, uint32_t victim,
                           uint64_t start)
{
  size_t i;

  for (i = 0u; i < n; ++i) {
    if (ev[i].tid != victim || ev[i].ts <= start ||
        ev[i].cls != REPLIQUE_EV_THREAD_STATE) {
      continue;
    }
    if (ev[i].subcode == REPLIQUE_TH_READY ||
        ev[i].subcode == REPLIQUE_TH_RUNNING ||
        ev[i].subcode == REPLIQUE_TH_DEAD) {
      return ev[i].ts;
    }
  }
  return NO_TS;
}

static void format_resource(char *out, size_t cap, uint32_t resource)
{
  static const char prefix[] = "resource-";
  char digits[12];
  size_t len = 0u;
  size_t i;
  uint32_t value = resource;

  out[0] = '\0';
  if (cap < sizeof prefix + 12u) {
    return;
  }
  if (!value) {
    digits[len++] = '0';
  }
  while (value && len < sizeof digits) {
    digits[len++] = (char)('0' + (value % 10u));
    value /= 10u;
  }
  memcpy(out, prefix, sizeof prefix - 1u);
  for (i = 0u; i < len; ++i) {
    out[sizeof prefix - 1u + i] = digits[len - 1u - i];
  }
  out[sizeof prefix - 1u + len] = '\0';
}

typedef struct replique_window_scan {
  uint64_t run_ts;
  uint32_t run_tid;
  uint64_t ready_ts;
  uint32_t ready_tid;
  uint32_t inherited;
} replique_window_scan;

static int in_window(const replique_event *ev, uint64_t start, uint64_t end)
{
  return ev->ts >= start && ev->ts <= end;
}

/* Un thread de priorité strictement intermédiaire a-t-il tourné (ou été prêt)
 * pendant la fenêtre ? Le premier RUNNING l'emporte, sinon le premier READY. */
static void note_intermediate(const replique_event *ev,
                              const replique_thread *other,
                              const replique_thread *victim,
                              const replique_thread *holder,
                              replique_window_scan *scan)
{
  if (!other || other->base_prio <= victim->base_prio ||
      other->base_prio >= holder->base_prio) {
    return;
  }
  if (ev->subcode == REPLIQUE_TH_RUNNING) {
    if (!scan->run_ts) {
      scan->run_ts = ev->ts;
      scan->run_tid = ev->tid;
    }
  } else if (!scan->ready_ts) {
    scan->ready_ts = ev->ts;
    scan->ready_tid = ev->tid;
  }
}

static void scan_events(const replique_event *ev, size_t n,
                        const replique_thread *threads, size_t n_threads,
                        const replique_window *win, uint64_t end,
                        replique_window_scan *scan)
{
  const replique_thread *victim = lookup(threads, n_threads, win->victim);
  const replique_thread *holder = lookup(threads, n_threads, win->holder);
  size_t i;

  memset(scan, 0, sizeof *scan);
  if (!victim || !holder) {
    return;
  }
  for (i = 0u; i < n; ++i) {
    if (!in_window(&ev[i], win->start, end)) {
      continue;
    }
    if (ev[i].cls == REPLIQUE_EV_PRIO_CHANGE && ev[i].tid == win->holder &&
        ev[i].subcode == 1u) {
      scan->inherited = 1u;
    }
    if (ev[i].cls != REPLIQUE_EV_THREAD_STATE || ev[i].tid == win->victim ||
        ev[i].tid == win->holder) {
      continue;
    }
    if (ev[i].subcode != REPLIQUE_TH_RUNNING &&
        ev[i].subcode != REPLIQUE_TH_READY) {
      continue;
    }
    note_intermediate(&ev[i], lookup(threads, n_threads, ev[i].tid), victim,
                      holder, scan);
  }
}

static int scan_window(const replique_event *ev, size_t n,
                       const replique_thread *threads, size_t n_threads,
                       const replique_window *win, replique_verdict *out)
{
  replique_window_scan scan;
  char detail[REPLIQUE_DETAIL_MAX];

  scan_events(ev, n, threads, n_threads, win,
              window_end(ev, n, win->victim, win->start), &scan);
  if (!scan.run_tid && !scan.ready_tid) {
    return 0;
  }
  format_resource(detail, sizeof detail, win->resource);
  replique_verdict_set(out, REPLIQUE_V_PRIO_INVERSION,
                       scan.run_ts ? scan.run_ts : scan.ready_ts, 0u,
                       win->victim, scan.inherited, 0u, 0u, detail);
  out->chain_len = 3u;
  out->chain[0] = win->victim;
  out->chain[1] = scan.run_tid ? scan.run_tid : scan.ready_tid;
  out->chain[2] = win->holder;
  return 1;
}

int replique_prio_inversion_detect(const replique_event *ev, size_t n,
                                   replique_verdict *out, size_t cap,
                                   size_t *n_out)
{
  static replique_thread threads[REPLIQUE_MAX_THREADS];
  static replique_window windows[MAX_WINDOWS];
  size_t n_threads = 0u;
  size_t n_windows = 0u;
  size_t written = 0u;
  size_t i;

  if (n_out) {
    *n_out = 0u;
  }
  if (!out) {
    return REPLIQUE_ERR_ARG;
  }
  replique_states_rebuild(ev, n, threads, REPLIQUE_MAX_THREADS, &n_threads);
  n_windows = find_windows(ev, n, threads, n_threads, windows, MAX_WINDOWS);
  for (i = 0u; i < n_windows && written < cap; ++i) {
    if (scan_window(ev, n, threads, n_threads, &windows[i], &out[written])) {
      ++written;
    }
  }
  if (n_out) {
    *n_out = written;
  }
  return REPLIQUE_STATUS_OK;
}
