/* kev_rkv1.c — décodeur .kev QNX 8.0 -> RKV1. Jumeau exact de host/replique
 * (kev_reader.py + kev2rkv1.py). Sortie octet pour octet identique au Python. */
#include "kev_rkv1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXCPU 64
#define MAXW 32

/* classes internes QNX (trace.h) */
enum { C_CONTROL = 1, C_INT = 3, C_PROC_THREAD = 4, C_SYSTEM = 5, C_COMM = 7 };
/* codes RKV1 (CONTRACT.md §2) */
enum { EV_THREAD_STATE = 1, EV_PROCESS_CREATE = 2, EV_PROCESS_DESTROY = 3,
       EV_THREAD_CREATE = 4, EV_THREAD_DESTROY = 5, EV_MSG_SEND = 6,
       EV_MSG_RECEIVE = 7, EV_MSG_REPLY = 8, EV_MSG_ERROR = 9,
       EV_TIMER = 14, EV_INTERRUPT = 15 };
/* codes QNX process/thread */
enum { TH_CREATE = 24, TH_DESTROY = 25, TH_NAME = 0x140,
       P_CREATE = 0x40, P_DESTROY = 0x80, P_CREATE_NAME = 0xC0,
       P_DESTROY_NAME = 0x100 };

typedef struct {
    unsigned long long ts;
    unsigned int cpu;
    unsigned short cls, code, nw;
    unsigned int w[MAXW];
    unsigned int idx;
} ev_t;

typedef struct {
    ev_t *v;
    size_t n, cap;
} evbuf_t;

static unsigned int rd32(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static void wr32(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}
static void wr64(unsigned char *p, unsigned long long v) {
    wr32(p, (unsigned int)v); wr32(p + 4, (unsigned int)(v >> 32));
}

static unsigned int crc32_iso(const unsigned char *d, size_t n) {
    unsigned int c = 0xFFFFFFFFu;
    size_t i; int k;
    for (i = 0; i < n; i++) {
        c ^= d[i];
        for (k = 0; k < 8; k++)
            c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return c ^ 0xFFFFFFFFu;
}

static long find_sub(const unsigned char *h, size_t n, const char *needle) {
    size_t m = strlen(needle), i;
    if (m > n) return -1;
    for (i = 0; i + m <= n; i++)
        if (memcmp(h + i, needle, m) == 0) return (long)i;
    return -1;
}

/* Entier décimal qui suit "KEY::" dans l'en-tête ASCII, ou -1. */
static long kw_int(const unsigned char *h, size_t n, const char *key) {
    char pat[64];
    long at, v = 0;
    int got = 0;
    snprintf(pat, sizeof pat, "%s::", key);
    at = find_sub(h, n, pat);
    if (at < 0) return -1;
    at += (long)strlen(pat);
    while (at < (long)n && h[at] >= '0' && h[at] <= '9') {
        v = v * 10 + (h[at] - '0'); got = 1; at++;
    }
    return got ? v : -1;
}

static int push_ev(evbuf_t *b, const ev_t *e) {
    if (b->n == b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 256;
        ev_t *nv = realloc(b->v, nc * sizeof(ev_t));
        if (!nv) return -1;
        b->v = nv; b->cap = nc;
    }
    b->v[b->n++] = *e;
    return 0;
}

static int cmp_ev(const void *a, const void *b) {
    const ev_t *x = a, *y = b;
    if (x->ts < y->ts) return -1;
    if (x->ts > y->ts) return 1;
    return (x->idx < y->idx) ? -1 : (x->idx > y->idx);
}

/* Traite un record (combine CB/CC/CE) et l'ajoute à `out` au besoin. */
static int emit_record(evbuf_t *out, ev_t *cur, int *have_cur, unsigned int *idx,
                       unsigned int cpu, unsigned int cls, unsigned int code,
                       unsigned int st, unsigned int d1, unsigned int d2,
                       unsigned long long ts) {
    ev_t e;
    if (st == 0 || (st == 2 && !*have_cur)) {
        memset(&e, 0, sizeof e);
        e.ts = ts; e.cpu = cpu; e.cls = (unsigned short)cls;
        e.code = (unsigned short)code; e.nw = 2; e.w[0] = d1; e.w[1] = d2;
        e.idx = (*idx)++;
        *have_cur = 0;
        return push_ev(out, &e);
    }
    if (st == 1) {
        memset(cur, 0, sizeof *cur);
        cur->ts = ts; cur->cpu = cpu; cur->cls = (unsigned short)cls;
        cur->code = (unsigned short)code; cur->nw = 2; cur->w[0] = d1; cur->w[1] = d2;
        *have_cur = 1;
        return 0;
    }
    if (st == 2) {
        if (cur->nw + 2 <= MAXW) { cur->w[cur->nw] = d1; cur->w[cur->nw + 1] = d2; cur->nw += 2; }
        return 0;
    }
    /* st == 3 (CE) */
    if (*have_cur && cur->cls == cls && cur->code == code) {
        if (cur->nw + 2 <= MAXW) { cur->w[cur->nw] = d1; cur->w[cur->nw + 1] = d2; cur->nw += 2; }
        cur->idx = (*idx)++;
        *have_cur = 0;
        return push_ev(out, cur);
    }
    memset(&e, 0, sizeof e);
    e.ts = ts; e.cpu = cpu; e.cls = (unsigned short)cls;
    e.code = (unsigned short)code; e.nw = 2; e.w[0] = d1; e.w[1] = d2;
    e.idx = (*idx)++;
    *have_cur = 0;
    return push_ev(out, &e);
}

/* Découpe le flux de records en événements logiques, horloge 64 bits par CPU. */
static int parse_logical(const unsigned char *blob, size_t len, size_t start,
                         evbuf_t *out) {
    unsigned int msb[MAXCPU] = {0}, idx = 0;
    unsigned long long prev[MAXCPU];
    unsigned char has[MAXCPU] = {0};
    size_t n = len >= start ? (len - start) / 16 : 0, k;
    ev_t cur;
    int have_cur = 0, rc = 0;
    memset(prev, 0, sizeof prev);
    for (k = 0; k < n && rc == 0; k++) {
        const unsigned char *p = blob + start + k * 16;
        unsigned int h = rd32(p), d0 = rd32(p + 4), d1 = rd32(p + 8), d2 = rd32(p + 12);
        unsigned int cpu = (h >> 24) & 0x3F, cls = (h >> 10) & 0x1F;
        unsigned int code = h & 0x3FF, st = (h >> 30) & 3;
        unsigned long long ts;
        if (cls == C_CONTROL && code == 1) {        /* CONTROL:TIME (msb) */
            msb[cpu] = d1; prev[cpu] = ((unsigned long long)d1 << 32) | d0;
            has[cpu] = 1; continue;
        }
        ts = ((unsigned long long)msb[cpu] << 32) | d0;
        if (has[cpu] && ts + 0x80000000ull < prev[cpu]) {
            msb[cpu]++; ts = ((unsigned long long)msb[cpu] << 32) | d0;
        }
        prev[cpu] = ts; has[cpu] = 1;
        if (cls == C_CONTROL) continue;
        rc = emit_record(out, &cur, &have_cur, &idx, cpu, cls, code, st, d1, d2, ts);
    }
    return rc;
}

/* état QNX -> (subcode RKV1, raison de blocage RKV1) ; -1 si non mappé. */
static int state_map(unsigned int code, unsigned int *sub, unsigned int *reason) {
    unsigned int s = 3, r = 0;
    switch (code) {
    case 0: s = 0; break; case 1: s = 1; break; case 2: s = 2; break;
    case 3: s = 4; break;
    case 4: case 5: case 7: case 8: r = 2; break;
    case 6: r = 6; break; case 12: r = 4; break;
    case 13: case 14: case 19: case 20: case 21: case 26: r = 1; break;
    case 15: r = 7; break; case 16: r = 5; break; case 17: r = 3; break;
    case 9: case 10: case 11: case 18: case 22: r = 0; break;
    case 30: r = 2; break;
    default: return -1;
    }
    *sub = s; *reason = r;
    return 0;
}

/* Écrit un record RKV1 (32 o) ; rend 1 si l'événement est mappé, 0 sinon. */
static int map_event(const ev_t *e, unsigned char *rec) {
    unsigned int pid = 0, tid = 0, cls = 0, sub = 0, a = 0, b = 0, c = 0;
    unsigned int w0 = e->nw > 0 ? e->w[0] : 0, w1 = e->nw > 1 ? e->w[1] : 0;
    if (e->cls == C_PROC_THREAD) {
        if (e->code == TH_CREATE) { cls = EV_THREAD_CREATE; pid = w0; tid = w1; }
        else if (e->code == TH_DESTROY) { cls = EV_THREAD_DESTROY; pid = w0; tid = w1; }
        else if (state_map(e->code, &sub, &a) == 0) {
            cls = EV_THREAD_STATE; pid = w0; tid = w1;
        } else if (e->code == P_CREATE || e->code == P_CREATE_NAME) {
            cls = EV_PROCESS_CREATE; pid = w1; a = w0;
        } else if (e->code == P_DESTROY || e->code == P_DESTROY_NAME) {
            cls = EV_PROCESS_DESTROY; pid = w1;
        } else {
            return 0; /* noms, non mappé */
        }
    } else if (e->cls == C_COMM) {
        if (e->code == 0) cls = EV_MSG_SEND;
        else if (e->code == 2) cls = EV_MSG_RECEIVE;
        else if (e->code == 10) cls = EV_MSG_REPLY;
        else if (e->code == 11) cls = EV_MSG_ERROR;
        else return 0;
        tid = w0;
    } else if (e->cls == C_INT) {
        cls = EV_INTERRUPT; pid = w0; tid = w1;
    } else if (e->cls == C_SYSTEM && e->code == 0x12) {
        cls = EV_TIMER; pid = w0; tid = w1;
    } else {
        return 0;
    }
    wr64(rec, e->ts); wr32(rec + 8, pid); wr32(rec + 12, tid);
    rec[16] = (unsigned char)cls; rec[17] = (unsigned char)(cls >> 8);
    rec[18] = (unsigned char)sub; rec[19] = (unsigned char)(sub >> 8);
    wr32(rec + 20, a); wr32(rec + 24, b); wr32(rec + 28, c);
    return 1;
}

int kev_rkv1_convert(const unsigned char *kev, size_t len,
                     unsigned char **out, size_t *outlen) {
    long hend = find_sub(kev, len, "TRACE_HEADER_END::");
    size_t start, nmap = 0, cap, i, body_off = 32;
    long sp, cps;
    evbuf_t evs = {0};
    unsigned char *buf, *body;
    if (hend < 0) return -1;
    sp = kw_int(kev, (size_t)hend, "TRACE_SYSPAGE_LEN"); if (sp < 0) sp = 2464;
    cps = kw_int(kev, (size_t)hend, "TRACE_CYCLES_PER_SEC"); if (cps < 0) cps = 1000000000L;
    start = (size_t)hend + strlen("TRACE_HEADER_END::") + (size_t)sp;
    if (start > len) return -1;
    if (parse_logical(kev, len, start, &evs)) { free(evs.v); return -1; }
    qsort(evs.v, evs.n, sizeof(ev_t), cmp_ev);
    cap = evs.n ? evs.n * 32 : 32;
    buf = malloc(body_off + cap);
    if (!buf) { free(evs.v); return -1; }
    body = buf + body_off;
    for (i = 0; i < evs.n; i++)
        if (map_event(&evs.v[i], body + nmap * 32)) nmap++;
    free(evs.v);
    wr32(buf + 0, 0x31564B52u);            /* "RKV1" */
    wr32(buf + 4, 1);                       /* version */
    wr32(buf + 8, 0);                       /* flags */
    wr32(buf + 12, (unsigned int)nmap);     /* n_events */
    wr32(buf + 16, (unsigned int)cps);      /* time_unit */
    wr32(buf + 20, crc32_iso(body, nmap * 32));
    wr32(buf + 24, 0); wr32(buf + 28, 0);
    *out = buf; *outlen = body_off + nmap * 32;
    return 0;
}

int kev_rkv1_file(const char *in_path, const char *out_path) {
    FILE *f = fopen(in_path, "rb");
    long sz; unsigned char *in, *out; size_t olen;
    int rc;
    if (!f) return -1;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return -1; }
    in = malloc((size_t)sz);
    if (!in || fread(in, 1, (size_t)sz, f) != (size_t)sz) { free(in); fclose(f); return -1; }
    fclose(f);
    rc = kev_rkv1_convert(in, (size_t)sz, &out, &olen);
    free(in);
    if (rc) return rc;
    f = fopen(out_path, "wb");
    if (!f) { free(out); return -1; }
    fwrite(out, 1, olen, f); fclose(f); free(out);
    return 0;
}