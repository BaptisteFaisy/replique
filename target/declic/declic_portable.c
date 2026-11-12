/* declic_portable.c — logique PORTABLE de Déclic.
 *
 * Trois blocs, tous sans horloge, sans fichier, sans en-tête QNX :
 *   (1) le conteneur d'archive « RDA1 » (construction, parcours, vérification) ;
 *   (2) le manifeste (ordre de clés figé, horodatage fourni par l'appelant) ;
 *   (3) la décision de déclenchement (événement de terminaison + politique).
 *
 * Testé sur PC par core/tests/declic_test.c (cible `build/declic-test`).
 * La colle QNX (target/declic/declic_qnx.c) ne fait qu'appeler cette API.
 */
#include "declic.h"
#include "sha256_portable.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Lecture / écriture petit-boutien (champ par champ, jamais par cast)  */
/* ------------------------------------------------------------------ */

static uint32_t rd32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p)
{
  return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static void wr32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v & 0xffu);
  p[1] = (uint8_t)((v >> 8) & 0xffu);
  p[2] = (uint8_t)((v >> 16) & 0xffu);
  p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static void wr64(uint8_t *p, uint64_t v)
{
  wr32(p, (uint32_t)(v & 0xffffffffu));
  wr32(p + 4, (uint32_t)((v >> 32) & 0xffffffffu));
}

const char *declic_status_name(int status)
{
  switch (status) {
  case DECLIC_OK:
    return "ok";
  case DECLIC_ERR_ARG:
    return "bad-argument";
  case DECLIC_ERR_TRUNCATED:
    return "truncated";
  case DECLIC_ERR_MAGIC:
    return "bad-magic";
  case DECLIC_ERR_VERSION:
    return "bad-version";
  case DECLIC_ERR_SHA:
    return "bad-sha256";
  case DECLIC_ERR_NOT_FOUND:
    return "section-not-found";
  case DECLIC_ERR_OVERFLOW:
    return "buffer-too-small";
  case DECLIC_ERR_FORMAT:
    return "bad-format";
  default:
    return "unknown";
  }
}

/* ------------------------------------------------------------------ */
/* (1) Conteneur RDA1                                                  */
/* ------------------------------------------------------------------ */

/* Taille totale nécessaire pour une table de n sections. */
static size_t rda1_total_size(const declic_section *secs, size_t n_sections)
{
  size_t total = DECLIC_RDA1_HEADER_SIZE + n_sections * DECLIC_RDA1_ENTRY_SIZE;
  size_t i;

  for (i = 0u; i < n_sections; ++i) {
    total += secs[i].len;
  }
  return total;
}

/* Écrit une entrée de table (nom, décalage, longueur, empreinte). */
static void rda1_write_entry(uint8_t *p, const char *name, uint64_t offset,
                             uint64_t length, const uint8_t digest[32])
{
  size_t n = strlen(name);

  memset(p, 0, DECLIC_RDA1_ENTRY_SIZE);
  if (n >= DECLIC_RDA1_NAME_MAX) {
    n = DECLIC_RDA1_NAME_MAX - 1u;
  }
  memcpy(p, name, n);
  wr64(p + 32u, offset);
  wr64(p + 40u, length);
  memcpy(p + 48u, digest, 32u);
}

int declic_archive_build_sections(const declic_section *secs, size_t n_sections,
                                  uint8_t *out, size_t cap, size_t *out_len)
{
  size_t table_off = DECLIC_RDA1_HEADER_SIZE;
  size_t blob_off;
  size_t total;
  size_t cur;
  size_t i;

  if (!secs || !out || !out_len) {
    return DECLIC_ERR_ARG;
  }
  if (n_sections == 0u || n_sections > DECLIC_RDA1_MAX_SECTIONS) {
    return DECLIC_ERR_ARG;
  }
  for (i = 0u; i < n_sections; ++i) {
    if (secs[i].name[0] == '\0' ||
        strlen(secs[i].name) >= DECLIC_RDA1_NAME_MAX) {
      return DECLIC_ERR_ARG;
    }
    if (!secs[i].data && secs[i].len > 0u) {
      return DECLIC_ERR_ARG;
    }
  }

  total = rda1_total_size(secs, n_sections);
  if (cap < total) {
    return DECLIC_ERR_OVERFLOW;
  }

  /* En-tête. */
  memcpy(out, DECLIC_RDA1_MAGIC, 4u);
  wr32(out + 4u, DECLIC_RDA1_VERSION);
  wr32(out + 8u, (uint32_t)n_sections);
  wr32(out + 12u, (uint32_t)table_off);
  wr32(out + 16u, DECLIC_RDA1_ENTRY_SIZE);
  wr32(out + 20u, 0u);
  wr64(out + 24u, (uint64_t)total);

  /* Table + blobs. */
  blob_off = table_off + n_sections * DECLIC_RDA1_ENTRY_SIZE;
  cur = blob_off;
  for (i = 0u; i < n_sections; ++i) {
    uint8_t digest[32];
    uint8_t *entry = out + table_off + i * DECLIC_RDA1_ENTRY_SIZE;

    sha256(secs[i].data, secs[i].len, digest);
    rda1_write_entry(entry, secs[i].name, (uint64_t)cur,
                     (uint64_t)secs[i].len, digest);
    if (secs[i].len > 0u) {
      memcpy(out + cur, secs[i].data, secs[i].len);
    }
    cur += secs[i].len;
  }

  *out_len = total;
  return DECLIC_OK;
}

int declic_archive_build(const declic_artifacts *art, uint8_t *out, size_t cap,
                          size_t *out_len)
{
  declic_section secs[5];
  const uint8_t *manifest_bytes = (const uint8_t *)(art ? art->manifest : NULL);

  if (!art) {
    return DECLIC_ERR_ARG;
  }
  memset(secs, 0, sizeof secs);
  memcpy(secs[0].name, DECLIC_SECTION_TRACE, sizeof DECLIC_SECTION_TRACE);
  secs[0].data = art->trace;
  secs[0].len = art->trace_len;
  memcpy(secs[1].name, DECLIC_SECTION_PIDIN, sizeof DECLIC_SECTION_PIDIN);
  secs[1].data = art->pidin;
  secs[1].len = art->pidin_len;
  memcpy(secs[2].name, DECLIC_SECTION_SLOG2, sizeof DECLIC_SECTION_SLOG2);
  secs[2].data = art->slog2;
  secs[2].len = art->slog2_len;
  memcpy(secs[3].name, DECLIC_SECTION_CORE, sizeof DECLIC_SECTION_CORE);
  secs[3].data = art->core;
  secs[3].len = art->core_len;
  memcpy(secs[4].name, DECLIC_SECTION_MANIFEST,
         sizeof DECLIC_SECTION_MANIFEST);
  secs[4].data = manifest_bytes;
  secs[4].len = art->manifest_len;

  return declic_archive_build_sections(secs, 5u, out, cap, out_len);
}

int declic_archive_open(const uint8_t *buf, size_t len,
                         declic_archive_view *view)
{
  uint32_t version;
  uint32_t n_sections;
  uint32_t table_off;
  uint32_t entry_size;
  uint64_t total;

  if (!buf || !view) {
    return DECLIC_ERR_ARG;
  }
  if (len < DECLIC_RDA1_HEADER_SIZE) {
    return DECLIC_ERR_TRUNCATED;
  }
  if (memcmp(buf, DECLIC_RDA1_MAGIC, 4u) != 0) {
    return DECLIC_ERR_MAGIC;
  }
  version = rd32(buf + 4u);
  if (version != DECLIC_RDA1_VERSION) {
    return DECLIC_ERR_VERSION;
  }
  n_sections = rd32(buf + 8u);
  table_off = rd32(buf + 12u);
  entry_size = rd32(buf + 16u);
  total = rd64(buf + 24u);

  if (n_sections == 0u || n_sections > DECLIC_RDA1_MAX_SECTIONS) {
    return DECLIC_ERR_FORMAT;
  }
  if (table_off != DECLIC_RDA1_HEADER_SIZE ||
      entry_size != DECLIC_RDA1_ENTRY_SIZE) {
    return DECLIC_ERR_FORMAT;
  }
  if (total > (uint64_t)len) {
    return DECLIC_ERR_TRUNCATED;
  }
  if ((uint64_t)table_off + (uint64_t)n_sections * entry_size > total) {
    return DECLIC_ERR_TRUNCATED;
  }

  view->buf = buf;
  view->len = (size_t)total;
  view->version = version;
  view->n_sections = n_sections;
  return DECLIC_OK;
}

/* Décodage d'une entrée de table + contrôle des bornes de la section. */
static int rda1_read_entry(const declic_archive_view *view, size_t index,
                            declic_section_info *info)
{
  const uint8_t *entry;
  uint64_t offset;
  uint64_t length;
  size_t name_len;

  if (!view || !info) {
    return DECLIC_ERR_ARG;
  }
  if (index >= (size_t)view->n_sections) {
    return DECLIC_ERR_NOT_FOUND;
  }
  entry = view->buf + DECLIC_RDA1_HEADER_SIZE +
          index * DECLIC_RDA1_ENTRY_SIZE;
  offset = rd64(entry + 32u);
  length = rd64(entry + 40u);

  if (offset > (uint64_t)view->len ||
      length > (uint64_t)view->len - offset) {
    return DECLIC_ERR_TRUNCATED;
  }

  memset(info, 0, sizeof *info);
  name_len = 0u;
  while (name_len < DECLIC_RDA1_NAME_MAX && entry[name_len] != '\0') {
    ++name_len;
  }
  memcpy(info->name, entry, name_len);
  info->name[name_len] = '\0';
  info->offset = offset;
  info->length = length;
  memcpy(info->sha256, entry + 48u, 32u);
  return DECLIC_OK;
}

int declic_archive_section(const declic_archive_view *view, size_t index,
                            declic_section_info *info)
{
  return rda1_read_entry(view, index, info);
}

int declic_archive_find(const declic_archive_view *view, const char *name,
                         declic_section_info *info)
{
  declic_section_info tmp;
  size_t i;
  int status;

  if (!view || !name) {
    return DECLIC_ERR_ARG;
  }
  for (i = 0u; i < (size_t)view->n_sections; ++i) {
    status = rda1_read_entry(view, i, &tmp);
    if (status != DECLIC_OK) {
      return status;
    }
    if (strcmp(tmp.name, name) == 0) {
      if (info) {
        *info = tmp;
      }
      return DECLIC_OK;
    }
  }
  return DECLIC_ERR_NOT_FOUND;
}

int declic_archive_verify(const declic_archive_view *view, size_t index)
{
  declic_section_info info;
  uint8_t digest[32];
  int status;

  status = rda1_read_entry(view, index, &info);
  if (status != DECLIC_OK) {
    return status;
  }
  sha256(view->buf + info.offset, (size_t)info.length, digest);
  if (memcmp(digest, info.sha256, 32u) != 0) {
    return DECLIC_ERR_SHA;
  }
  return DECLIC_OK;
}

int declic_archive_verify_all(const declic_archive_view *view)
{
  size_t i;

  if (!view) {
    return DECLIC_ERR_ARG;
  }
  for (i = 0u; i < (size_t)view->n_sections; ++i) {
    int status = declic_archive_verify(view, i);

    if (status != DECLIC_OK) {
      return status;
    }
  }
  return DECLIC_OK;
}

/* ------------------------------------------------------------------ */
/* (2) Manifeste                                                       */
/* ------------------------------------------------------------------ */

int declic_manifest_render(const declic_manifest *m, char *out, size_t cap,
                            size_t *out_len)
{
  int n;

  if (!m || !out) {
    return DECLIC_ERR_ARG;
  }
  if (m->archive_name[0] == '\0') {
    return DECLIC_ERR_ARG;
  }
  /* Ordre de clés FIGÉ : schema, archive, timestamp, pid, status, signal. */
  n = snprintf(out, cap,
               "schema=%s\n"
               "archive=%s\n"
               "timestamp=%llu\n"
               "pid=%lu\n"
               "status=%lu\n"
               "signal=%lu\n",
               DECLIC_MANIFEST_SCHEMA, m->archive_name,
               (unsigned long long)m->timestamp, (unsigned long)m->pid,
               (unsigned long)m->status, (unsigned long)m->signal);
  if (n < 0) {
    return DECLIC_ERR_FORMAT;
  }
  if ((size_t)n >= cap) {
    return DECLIC_ERR_OVERFLOW;
  }
  if (out_len) {
    *out_len = (size_t)n;
  }
  return DECLIC_OK;
}

/* ------------------------------------------------------------------ */
/* (3) Décision de déclenchement                                       */
/* ------------------------------------------------------------------ */

void declic_policy_init(declic_policy *pol)
{
  if (!pol) {
    return;
  }
  memset(pol, 0, sizeof *pol);
  pol->watch_all = 1u;
}

static int in_list(const uint32_t *list, uint32_t n, uint32_t value)
{
  uint32_t i;

  for (i = 0u; i < n; ++i) {
    if (list[i] == value) {
      return 1;
    }
  }
  return 0;
}

int declic_should_capture(const declic_term_event *ev, const declic_policy *pol)
{
  if (!ev || !pol) {
    return DECLIC_ERR_ARG;
  }
  /* 1. L'exclusion l'emporte sur tout le reste. */
  if (in_list(pol->exclusions, pol->n_exclusions, ev->pid) != 0) {
    return 0;
  }
  /* 2. Liste de pid surveillés (si la surveillance globale est off). */
  if (pol->watch_all == 0u &&
      in_list(pol->watch, pol->n_watch, ev->pid) == 0) {
    return 0;
  }
  /* 3. Filtre de signaux (liste non vide = liste blanche). */
  if (pol->n_signals > 0u &&
      in_list(pol->signals, pol->n_signals, ev->signal) == 0) {
    return 0;
  }
  /* 4. Sinon : capturer. */
  return 1;
}
