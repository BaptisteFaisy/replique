/* report.c — sorties texte canoniques (CONTRACT.md §4). Ces octets sont le
 * format d'échange du différentiel C↔Python : aucune variante locale, aucune
 * horloge, aucune allocation (l'appelant fournit le tampon). */
#include "replique/report.h"
#include "replique/verdict.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CHAIN_BUF (REPLIQUE_MAX_CHAIN * 12u + 2u)

static void appendf(char *out, size_t cap, size_t *used, unsigned *trunc,
                    const char *fmt, ...)
{
  size_t room;
  int n;
  va_list ap;

  if (*trunc) {
    return;
  }
  room = (*used < cap) ? (cap - *used) : 0u;
  va_start(ap, fmt);
  n = vsnprintf(room ? out + *used : NULL, room, fmt, ap);
  va_end(ap);
  if (n < 0) {
    *trunc = 1u;
    return;
  }
  if ((size_t)n >= room) {
    *trunc = 1u;
  }
  *used += (size_t)n;
}

static void chain_text(const replique_verdict *v, char *buf, size_t cap)
{
  size_t used = 0u;
  uint32_t i;

  buf[0] = '\0';
  for (i = 0u; i < v->chain_len && i < REPLIQUE_MAX_CHAIN; ++i) {
    int n = snprintf(buf + used, cap - used, "%s%u", i ? "," : "",
                     v->chain[i]);
    if (n < 0 || (size_t)n >= cap - used) {
      break;
    }
    used += (size_t)n;
  }
}

static int guard_args(const void *src, const char *out, size_t cap,
                      unsigned *truncated)
{
  if (!src || !out || !cap || !truncated) {
    return 0;
  }
  *truncated = 0u;
  return 1;
}

size_t replique_report_event(const replique_event *ev, char *out, size_t cap,
                             unsigned *truncated)
{
  size_t used = 0u;

  if (!guard_args(ev, out, cap, truncated)) {
    return 0u;
  }
  appendf(out, cap, &used, truncated,
          "ev|ts=%llu|pid=%u|tid=%u|class=%s|sub=%u|a=%u|b=%u|c=%u\n",
          (unsigned long long)ev->ts, (unsigned)ev->pid, (unsigned)ev->tid,
          replique_class_name(ev->cls), (unsigned)ev->subcode, (unsigned)ev->a,
          (unsigned)ev->b, (unsigned)ev->c);
  return used;
}

size_t replique_report_verdict(const replique_verdict *v, char *out, size_t cap,
                               unsigned *truncated)
{
  char chain[CHAIN_BUF];
  char detail[REPLIQUE_DETAIL_MAX];
  size_t used = 0u;

  if (!guard_args(v, out, cap, truncated)) {
    return 0u;
  }
  chain_text(v, chain, sizeof chain);
  memcpy(detail, v->detail, sizeof detail);
  detail[sizeof detail - 1u] = '\0';
  replique_verdict_sanitize_detail(detail);
  appendf(out, cap, &used, truncated,
          "vd|code=%s|sev=%s|ts=%llu|pid=%u|tid=%u|a=%u|b=%u|c=%u|hash=%llu"
          "|chain=%s|detail=%s\n",
          replique_verdict_name(v->code), replique_severity_name(v->sev),
          (unsigned long long)v->ts, (unsigned)v->pid, (unsigned)v->tid,
          (unsigned)v->a, (unsigned)v->b, (unsigned)v->c,
          (unsigned long long)v->hash, chain, detail);
  return used;
}

size_t replique_report_decision(const replique_decision *d, char *out,
                                size_t cap, unsigned *truncated)
{
  size_t used = 0u;

  if (!guard_args(d, out, cap, truncated)) {
    return 0u;
  }
  appendf(out, cap, &used, truncated, "dec|ts=%llu|tid=%u|reason=%s\n",
          (unsigned long long)d->ts, (unsigned)d->tid,
          replique_sched_reason_name(d->reason));
  return used;
}

static size_t report_many(size_t n, char *out, size_t cap, unsigned *truncated,
                          size_t (*one)(const void *, char *, size_t,
                                        unsigned *),
                          const void *items, size_t item_size)
{
  size_t used = 0u;
  size_t i;

  if (!items || !out || !cap || !truncated) {
    return 0u;
  }
  *truncated = 0u;
  for (i = 0u; i < n && !*truncated; ++i) {
    unsigned trunc = 0u;
    used += one((const char *)items + i * item_size, out + used, cap - used,
                &trunc);
    if (trunc) {
      *truncated = 1u;
      break;
    }
  }
  return used;
}

static size_t one_event(const void *p, char *o, size_t c, unsigned *t)
{
  return replique_report_event((const replique_event *)p, o, c, t);
}

static size_t one_verdict(const void *p, char *o, size_t c, unsigned *t)
{
  return replique_report_verdict((const replique_verdict *)p, o, c, t);
}

static size_t one_decision(const void *p, char *o, size_t c, unsigned *t)
{
  return replique_report_decision((const replique_decision *)p, o, c, t);
}

size_t replique_report_events(const replique_event *ev, size_t n, char *out,
                              size_t cap, unsigned *truncated)
{
  return report_many(n, out, cap, truncated, one_event, ev,
                     sizeof(replique_event));
}

size_t replique_report_verdicts(const replique_verdict *v, size_t n, char *out,
                                size_t cap, unsigned *truncated)
{
  return report_many(n, out, cap, truncated, one_verdict, v,
                     sizeof(replique_verdict));
}

size_t replique_report_decisions(const replique_decision *d, size_t n, char *out,
                                 size_t cap, unsigned *truncated)
{
  return report_many(n, out, cap, truncated, one_decision, d,
                     sizeof(replique_decision));
}
