/* kev.c — conteneur RKV1 (CONTRACT.md §1) : parseur, écrivain, CRC-32 et
 * FNV-1a 64. C11 pur : aucun fichier, aucune horloge, aucune allocation.
 * Lecture champ par champ (jamais de cast de structure) pour rester
 * indépendant de l'alignement et du boutisme de l'hôte. */
#include "replique/kev.h"
#include <string.h>

#define CRC_POLY 0xEDB88320u
#define CRC_INIT 0xFFFFFFFFu
#define FNV_BASE 14695981039346656037ULL
#define FNV_PRIME 1099511628211ULL

/* ------------------------------------------------------------- accès octets */

static uint16_t rd16(const uint8_t *p)
{
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p)
{
  return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static void wr16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void wr32(uint8_t *p, uint32_t v)
{
  wr16(p, (uint16_t)(v & 0xFFFFu));
  wr16(p + 2, (uint16_t)((v >> 16) & 0xFFFFu));
}

static void wr64(uint8_t *p, uint64_t v)
{
  wr32(p, (uint32_t)(v & 0xFFFFFFFFu));
  wr32(p + 4, (uint32_t)(v >> 32));
}

/* ---------------------------------------------------------------- hachages */

uint32_t replique_crc32(const uint8_t *data, size_t len)
{
  uint32_t crc = CRC_INIT;
  size_t i;
  int bit;

  if (!data) {
    return 0u;
  }
  for (i = 0u; i < len; ++i) {
    crc ^= data[i];
    for (bit = 0; bit < 8; ++bit) {
      crc = (crc & 1u) ? (crc >> 1) ^ CRC_POLY : crc >> 1;
    }
  }
  return crc ^ CRC_INIT;
}

uint64_t replique_fnv1a64_begin(void)
{
  return FNV_BASE;
}

uint64_t replique_fnv1a64_update(uint64_t hash, const char *data, size_t len)
{
  size_t i;

  if (!data) {
    return hash;
  }
  for (i = 0u; i < len; ++i) {
    hash ^= (uint64_t)(unsigned char)data[i];
    hash *= FNV_PRIME;
  }
  return hash;
}

uint64_t replique_fnv1a64(const char *data, size_t len)
{
  return replique_fnv1a64_update(replique_fnv1a64_begin(), data, len);
}

/* ------------------------------------------------------- validation par lots */

static int fail(unsigned *cause, size_t *offset, unsigned c, size_t o, int status)
{
  if (cause) {
    *cause = c;
  }
  if (offset) {
    *offset = o;
  }
  return status;
}

static int check_header(const uint8_t *buf, size_t len, replique_header *hdr,
                        unsigned *cause, size_t *offset)
{
  if (!buf || !hdr) {
    return fail(cause, offset, REPLIQUE_CAUSE_SHORT_HEADER, 0u, REPLIQUE_ERR_ARG);
  }
  if (len < REPLIQUE_HEADER_SIZE) {
    return fail(cause, offset, REPLIQUE_CAUSE_SHORT_HEADER, len,
                REPLIQUE_ERR_TRUNCATED);
  }
  if (memcmp(buf, REPLIQUE_MAGIC, REPLIQUE_MAGIC_LEN) != 0) {
    return fail(cause, offset, REPLIQUE_CAUSE_BAD_MAGIC, 0u, REPLIQUE_ERR_MAGIC);
  }
  hdr->version = rd32(buf + 4);
  hdr->flags = rd32(buf + 8);
  hdr->n_events = rd32(buf + 12);
  hdr->time_unit = rd32(buf + 16);
  hdr->crc32 = rd32(buf + 20);
  hdr->reserved0 = rd32(buf + 24);
  hdr->reserved1 = rd32(buf + 28);
  if (hdr->version != REPLIQUE_VERSION) {
    return fail(cause, offset, REPLIQUE_CAUSE_BAD_VERSION, 4u,
                REPLIQUE_ERR_VERSION);
  }
  if (hdr->flags & REPLIQUE_FLAG_RESERVED_MASK) {
    return fail(cause, offset, REPLIQUE_CAUSE_BAD_FLAGS, 8u, REPLIQUE_ERR_FLAGS);
  }
  if (len != REPLIQUE_HEADER_SIZE + (size_t)hdr->n_events * REPLIQUE_RECORD_SIZE) {
    return fail(cause, offset, REPLIQUE_CAUSE_BAD_LENGTH, len,
                REPLIQUE_ERR_TRUNCATED);
  }
  return REPLIQUE_STATUS_OK;
}

static int check_body(const uint8_t *buf, const replique_header *hdr,
                      unsigned *cause, size_t *offset)
{
  size_t body_len = (size_t)hdr->n_events * REPLIQUE_RECORD_SIZE;

  if (replique_crc32(buf + REPLIQUE_HEADER_SIZE, body_len) != hdr->crc32) {
    return fail(cause, offset, REPLIQUE_CAUSE_BAD_CRC, 20u, REPLIQUE_ERR_CRC);
  }
  return REPLIQUE_STATUS_OK;
}

static int decode_one(const uint8_t *rec, replique_event *ev)
{
  ev->ts = rd64(rec);
  ev->pid = rd32(rec + 8);
  ev->tid = rd32(rec + 12);
  ev->cls = rd16(rec + 16);
  ev->subcode = rd16(rec + 18);
  ev->a = rd32(rec + 20);
  ev->b = rd32(rec + 24);
  ev->c = rd32(rec + 28);
  return ev->cls > (unsigned)REPLIQUE_EV_CLASS_MAX ? REPLIQUE_ERR_CLASS
                                                    : REPLIQUE_STATUS_OK;
}

/* Validation complète des enregistrements AVANT toute copie : une trace n'est
 * jamais reprise approximativement (CONTRACT §1.4). */
static int scan_events(const uint8_t *buf, uint32_t n, unsigned *cause,
                       size_t *offset)
{
  uint64_t previous = 0u;
  uint32_t i;

  for (i = 0u; i < n; ++i) {
    const uint8_t *rec = buf + REPLIQUE_HEADER_SIZE + (size_t)i * REPLIQUE_RECORD_SIZE;
    replique_event ev;
    if (decode_one(rec, &ev) != REPLIQUE_STATUS_OK) {
      return fail(cause, offset, REPLIQUE_CAUSE_BAD_CLASS,
                  REPLIQUE_HEADER_SIZE + (size_t)i * REPLIQUE_RECORD_SIZE + 16u,
                  REPLIQUE_ERR_CLASS);
    }
    if (i > 0u && ev.ts < previous) {
      return fail(cause, offset, REPLIQUE_CAUSE_TS_MONOTONIC,
                  REPLIQUE_HEADER_SIZE + (size_t)i * REPLIQUE_RECORD_SIZE,
                  REPLIQUE_ERR_ORDER);
    }
    previous = ev.ts;
  }
  return REPLIQUE_STATUS_OK;
}

int replique_kev_header(const uint8_t *buf, size_t len, replique_header *hdr)
{
  int status = check_header(buf, len, hdr, NULL, NULL);

  if (status != REPLIQUE_STATUS_OK) {
    return status;
  }
  return check_body(buf, hdr, NULL, NULL);
}

int replique_kev_parse_diag(const uint8_t *buf, size_t len, replique_event *out,
                            size_t cap, size_t *n_out, unsigned *cause,
                            size_t *offset)
{
  replique_header hdr;
  int status;
  size_t written = 0u;
  uint32_t i;

  if (n_out) {
    *n_out = 0u;
  }
  status = check_header(buf, len, &hdr, cause, offset);
  if (status == REPLIQUE_STATUS_OK) {
    status = check_body(buf, &hdr, cause, offset);
  }
  if (status == REPLIQUE_STATUS_OK && out) {
    status = scan_events(buf, hdr.n_events, cause, offset);
  }
  if (status != REPLIQUE_STATUS_OK || !out) {
    return status;
  }
  for (i = 0u; i < hdr.n_events && written < cap; ++i) {
    decode_one(buf + REPLIQUE_HEADER_SIZE + (size_t)i * REPLIQUE_RECORD_SIZE,
               out + written);
    ++written;
  }
  if (n_out) {
    *n_out = written;
  }
  if (written < hdr.n_events) {
    return fail(cause, offset, REPLIQUE_CAUSE_NONE, 0u, REPLIQUE_ERR_OVERFLOW);
  }
  return REPLIQUE_STATUS_OK;
}

int replique_kev_parse(const uint8_t *buf, size_t len, replique_event *out,
                       size_t cap, size_t *n_out)
{
  return replique_kev_parse_diag(buf, len, out, cap, n_out, NULL, NULL);
}

int replique_kev_write(const replique_event *ev, size_t n, uint32_t flags,
                       uint32_t time_unit, uint8_t *out, size_t cap,
                       size_t *n_out)
{
  size_t needed = REPLIQUE_HEADER_SIZE + n * REPLIQUE_RECORD_SIZE;
  size_t i;

  if (n_out) {
    *n_out = 0u;
  }
  if (!out || (n > 0u && !ev)) {
    return REPLIQUE_ERR_ARG;
  }
  if (cap < needed) {
    return REPLIQUE_ERR_OVERFLOW;
  }
  memcpy(out, REPLIQUE_MAGIC, REPLIQUE_MAGIC_LEN);
  wr32(out + 4, REPLIQUE_VERSION);
  wr32(out + 8, flags);
  wr32(out + 12, (uint32_t)n);
  wr32(out + 16, time_unit ? time_unit : REPLIQUE_TIME_UNIT_NS);
  wr32(out + 24, 0u);
  wr32(out + 28, 0u);
  for (i = 0u; i < n; ++i) {
    uint8_t *rec = out + REPLIQUE_HEADER_SIZE + i * REPLIQUE_RECORD_SIZE;
    wr64(rec, ev[i].ts);
    wr32(rec + 8, ev[i].pid);
    wr32(rec + 12, ev[i].tid);
    wr16(rec + 16, ev[i].cls);
    wr16(rec + 18, ev[i].subcode);
    wr32(rec + 20, ev[i].a);
    wr32(rec + 24, ev[i].b);
    wr32(rec + 28, ev[i].c);
  }
  wr32(out + 20, replique_crc32(out + REPLIQUE_HEADER_SIZE,
                                n * REPLIQUE_RECORD_SIZE));
  if (n_out) {
    *n_out = needed;
  }
  return REPLIQUE_STATUS_OK;
}
