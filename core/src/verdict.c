/* verdict.c — fabriques et tri des verdicts (CONTRACT.md §3, §4.4).
 * La chaîne complète vit dans pipeline.c ; les noms dans event.c. */
#include "replique/verdict.h"
#include <string.h>

void replique_verdict_sanitize_detail(char *detail)
{
  char *p;

  if (!detail) {
    return;
  }
  for (p = detail; *p; ++p) {
    unsigned char ch = (unsigned char)*p;
    if (ch == '|' || ch < 0x20 || ch > 0x7E) {
      *p = '_';
    }
  }
}

void replique_verdict_set(replique_verdict *v, unsigned code, uint64_t ts,
                          uint32_t pid, uint32_t tid, uint32_t a, uint32_t b,
                          uint32_t c, const char *detail)
{
  if (!v) {
    return;
  }
  memset(v, 0, sizeof *v);
  v->code = code;
  v->sev = replique_verdict_severity(code);
  v->ts = ts;
  v->pid = pid;
  v->tid = tid;
  v->a = a;
  v->b = b;
  v->c = c;
  if (detail) {
    size_t n = strlen(detail);
    if (n >= sizeof v->detail) {
      n = sizeof v->detail - 1u;
    }
    memcpy(v->detail, detail, n);
    v->detail[n] = '\0';
    replique_verdict_sanitize_detail(v->detail);
  }
}

/* Tri gelé (CONTRACT.md §4.4) : sévérité décroissante, code croissant,
 * ts croissant, tid croissant ; à égalité complète, l'ordre de production est
 * conservé (tri par insertion = stable, n <= REPLIQUE_MAX_VERDICTS). */
static int verdict_before(const replique_verdict *x, const replique_verdict *y)
{
  if (x->sev != y->sev) {
    return x->sev > y->sev;
  }
  if (x->code != y->code) {
    return x->code < y->code;
  }
  if (x->ts != y->ts) {
    return x->ts < y->ts;
  }
  return x->tid < y->tid;
}

void replique_verdict_sort(replique_verdict *v, size_t n)
{
  size_t i;

  if (!v || n < 2u) {
    return;
  }
  for (i = 1u; i < n; ++i) {
    replique_verdict key = v[i];
    size_t j = i;
    while (j > 0u && verdict_before(&key, &v[j - 1u])) {
      v[j] = v[j - 1u];
      --j;
    }
    v[j] = key;
  }
}
