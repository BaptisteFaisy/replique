/* test_kev.c — conteneur RKV1 (CONTRACT.md §1) : écriture, relecture, les
 * 8 causes d'erreur avec leur décalage EXACT, CRC-32 et FNV-1a 64.
 *
 * Aucun fichier, aucune horloge : tout est construit en mémoire, octet par
 * octet (jamais de cast de structure), comme l'exige §1.3.
 */
#include "replique/kev.h"
#include "replique/types.h"
#include "test_util.h"

#define MAX_EVENTS 4u
#define BUF_MAX (REPLIQUE_HEADER_SIZE + MAX_EVENTS * REPLIQUE_RECORD_SIZE)

/* ------------------------------------------------- écriture octet par octet */

static void put16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put32(uint8_t *p, uint32_t v)
{
  put16(p, (uint16_t)(v & 0xFFFFu));
  put16(p + 2, (uint16_t)((v >> 16) & 0xFFFFu));
}

static void put64(uint8_t *p, uint64_t v)
{
  put32(p, (uint32_t)(v & 0xFFFFFFFFu));
  put32(p + 4, (uint32_t)(v >> 32));
}

/* En-tête 32 octets + enregistrements 32 octets, champ par champ (§1.2/1.3). */
static size_t build_container(uint8_t *buf, const replique_event *ev, size_t n,
                              uint32_t flags, uint32_t time_unit)
{
  size_t i;

  memcpy(buf, REPLIQUE_MAGIC, REPLIQUE_MAGIC_LEN);
  put32(buf + 4, REPLIQUE_VERSION);
  put32(buf + 8, flags);
  put32(buf + 12, (uint32_t)n);
  put32(buf + 16, time_unit);
  put32(buf + 20, 0u);
  put32(buf + 24, 0u);
  put32(buf + 28, 0u);
  for (i = 0u; i < n; ++i) {
    uint8_t *rec = buf + REPLIQUE_HEADER_SIZE + i * REPLIQUE_RECORD_SIZE;
    put64(rec, ev[i].ts);
    put32(rec + 8, ev[i].pid);
    put32(rec + 12, ev[i].tid);
    put16(rec + 16, ev[i].cls);
    put16(rec + 18, ev[i].subcode);
    put32(rec + 20, ev[i].a);
    put32(rec + 24, ev[i].b);
    put32(rec + 28, ev[i].c);
  }
  put32(buf + 20, replique_crc32(buf + REPLIQUE_HEADER_SIZE,
                                n * REPLIQUE_RECORD_SIZE));
  return REPLIQUE_HEADER_SIZE + n * REPLIQUE_RECORD_SIZE;
}

/* --------------------------------------------------- jeu d'événements type */

static void fill_sample(replique_event *ev)
{
  memset(ev, 0, 3u * sizeof *ev);
  ev[0].ts = 1000u;
  ev[0].pid = 10u;
  ev[0].tid = 101u;
  ev[0].cls = REPLIQUE_EV_THREAD_CREATE;
  ev[0].subcode = REPLIQUE_SCHED_FIFO;
  ev[0].a = 50u;
  ev[1].ts = 1100u;
  ev[1].pid = 10u;
  ev[1].tid = 101u;
  ev[1].cls = REPLIQUE_EV_MUTEX_LOCK;
  ev[1].subcode = 0u;
  ev[1].a = 32u;
  ev[1].b = 101u;
  ev[2].ts = 1200u;
  ev[2].pid = 10u;
  ev[2].tid = 102u;
  ev[2].cls = REPLIQUE_EV_THREAD_STATE;
  ev[2].subcode = REPLIQUE_TH_BLOCKED;
  ev[2].a = REPLIQUE_BR_MUTEX;
  ev[2].b = 32u;
  ev[2].c = 101u;
}

/* ------------------------------------------------------------ aller-retour */

static void test_roundtrip(void)
{
  replique_event ev[3];
  replique_event back[3];
  uint8_t buf[BUF_MAX];
  uint8_t manual[BUF_MAX];
  replique_header hdr;
  size_t len = 0u;
  size_t mlen = 0u;
  size_t n_out = 0u;
  size_t i;

  fill_sample(ev);
  /* L'API renvoie le STATUT et ecrit la taille dans n_out : meme
   * convention que parse et header. */
  TU_EQ(replique_kev_write(ev, 3u, REPLIQUE_FLAG_SYNTHETIC,
                            REPLIQUE_TIME_UNIT_NS, buf, sizeof buf, &n_out),
        REPLIQUE_STATUS_OK, "écriture sans erreur");
  len = n_out;
  TU_EQ(len, REPLIQUE_HEADER_SIZE + 3u * REPLIQUE_RECORD_SIZE,
        "taille écrite = 32 + 32*n");
  TU_EQ(n_out, len, "n_out = taille écrite");
  mlen = build_container(manual, ev, 3u, REPLIQUE_FLAG_SYNTHETIC,
                         REPLIQUE_TIME_UNIT_NS);
  TU_EQ(mlen, len, "container construit à la main : même taille");
  TU_CHECK(memcmp(buf, manual, len) == 0,
           "écrivain RKV1 == octets construits octet par octet");
  TU_EQ(replique_kev_parse(buf, len, back, 3u, &n_out), REPLIQUE_STATUS_OK,
        "relecture sans erreur");
  TU_EQ(n_out, 3u, "3 événements relus");
  for (i = 0u; i < 3u; ++i) {
    TU_EQ(back[i].ts, ev[i].ts, "ts conservé");
    TU_EQ(back[i].pid, ev[i].pid, "pid conservé");
    TU_EQ(back[i].tid, ev[i].tid, "tid conservé");
    TU_EQ(back[i].cls, ev[i].cls, "classe conservée");
    TU_EQ(back[i].subcode, ev[i].subcode, "subcode conservé");
    TU_EQ(back[i].a, ev[i].a, "champ a conservé");
    TU_EQ(back[i].b, ev[i].b, "champ b conservé");
    TU_EQ(back[i].c, ev[i].c, "champ c conservé");
  }
  TU_EQ(replique_kev_header(buf, len, &hdr), REPLIQUE_STATUS_OK,
        "en-tête seul validé");
  TU_EQ(hdr.version, 1u, "version 1");
  TU_EQ(hdr.flags, REPLIQUE_FLAG_SYNTHETIC, "flags conservés");
  TU_EQ(hdr.n_events, 3u, "n_events de l'en-tête");
  TU_EQ(hdr.time_unit, REPLIQUE_TIME_UNIT_NS, "time_unit");
  TU_EQ(hdr.crc32, replique_crc32(buf + REPLIQUE_HEADER_SIZE, 96u),
        "crc32 de l'en-tête = crc du corps seul");
}

/* --------------------------------------------------- tailles et Hachages */

static void test_sizes_and_hashes(void)
{
  static const char check[] = "123456789";

  TU_EQ(REPLIQUE_HEADER_SIZE, 32u, "en-tête : 32 octets");
  TU_EQ(REPLIQUE_RECORD_SIZE, 32u, "enregistrement : 32 octets");
  TU_EQ(replique_crc32((const uint8_t *)"", 0u), 0u,
        "CRC-32 de référence : chaîne vide -> 0");
  TU_EQ(replique_crc32((const uint8_t *)check, 9u), 0xCBF43926u,
        "CRC-32 de référence : \"123456789\" -> 0xCBF43926");
  TU_EQ(replique_fnv1a64("", 0u), 14695981039346656037ULL,
        "FNV-1a 64 de référence : chaîne vide");
  TU_EQ(replique_fnv1a64("a", 1u), 12638187200555641996ULL,
        "FNV-1a 64 de référence : \"a\"");
  TU_EQ(replique_fnv1a64_begin(), 14695981039346656037ULL,
        "FNV-1a 64 : base = décalage d'origine");
  TU_EQ(replique_fnv1a64_update(replique_fnv1a64_begin(), "a", 1u),
        12638187200555641996ULL, "FNV-1a 64 : update incrémental == one-shot");
}

/* -------------------------------------------------- les 8 causes (§1.4) */

static void expect_parse_error(const uint8_t *buf, size_t len, int status,
                               unsigned cause, size_t offset, const char *label)
{
  replique_event out[MAX_EVENTS];
  size_t n_out = 123u;
  unsigned got_cause = 0u;
  size_t got_offset = 0u;
  int rc;

  rc = replique_kev_parse_diag(buf, len, out, MAX_EVENTS, &n_out, &got_cause,
                                &got_offset);
  TU_EQ(rc, status, label);
  TU_EQ(got_cause, cause, label);
  TU_EQ(got_offset, offset, label);
  TU_EQ(n_out, 0u, label);
}

static void test_short_header(void)
{
  uint8_t buf[BUF_MAX];
  size_t len = build_container(buf, NULL, 0u, 0u, REPLIQUE_TIME_UNIT_NS);

  TU_EQ(len, 32u, "conteneur vide : 32 octets");
  expect_parse_error(buf, 10u, REPLIQUE_ERR_TRUNCATED,
                     REPLIQUE_CAUSE_SHORT_HEADER, 10u,
                     "tampon plus court que l'en-tête -> short-header");
}

static void test_bad_magic(void)
{
  uint8_t buf[BUF_MAX];
  replique_event ev[1];
  size_t len;

  fill_sample(ev);
  len = build_container(buf, ev, 1u, 0u, REPLIQUE_TIME_UNIT_NS);
  buf[0] = (uint8_t)'X';
  expect_parse_error(buf, len, REPLIQUE_ERR_MAGIC, REPLIQUE_CAUSE_BAD_MAGIC,
                     0u, "magic != RKV1 -> bad-magic à l'octet 0");
}

static void test_bad_version(void)
{
  uint8_t buf[BUF_MAX];
  replique_event ev[1];
  size_t len;

  fill_sample(ev);
  len = build_container(buf, ev, 1u, 0u, REPLIQUE_TIME_UNIT_NS);
  put32(buf + 4, 2u);
  expect_parse_error(buf, len, REPLIQUE_ERR_VERSION,
                     REPLIQUE_CAUSE_BAD_VERSION, 4u,
                     "version != 1 -> bad-version à l'octet 4");
}

static void test_bad_flags(void)
{
  uint8_t buf[BUF_MAX];
  replique_event ev[1];
  size_t len;

  fill_sample(ev);
  len = build_container(buf, ev, 1u, 0u, REPLIQUE_TIME_UNIT_NS);
  put32(buf + 8, 0x00000004u);
  expect_parse_error(buf, len, REPLIQUE_ERR_FLAGS, REPLIQUE_CAUSE_BAD_FLAGS,
                     8u, "bit réservé de flags non nul -> bad-flags (octet 8)");
}

static void test_bad_length(void)
{
  uint8_t buf[BUF_MAX];
  replique_event ev[2];
  size_t len;

  fill_sample(ev);
  len = build_container(buf, ev, 2u, 0u, REPLIQUE_TIME_UNIT_NS);
  expect_parse_error(buf, len - 1u, REPLIQUE_ERR_TRUNCATED,
                     REPLIQUE_CAUSE_BAD_LENGTH, len - 1u,
                     "taille != 32 + 32*n -> bad-length (décalage = taille)");
}

static void test_bad_crc(void)
{
  uint8_t buf[BUF_MAX];
  replique_event ev[2];
  size_t len;

  fill_sample(ev);
  len = build_container(buf, ev, 2u, 0u, REPLIQUE_TIME_UNIT_NS);
  buf[REPLIQUE_HEADER_SIZE + 3u] ^= 0xFFu;
  expect_parse_error(buf, len, REPLIQUE_ERR_CRC, REPLIQUE_CAUSE_BAD_CRC, 20u,
                     "CRC invalide -> bad-crc (octet 20, champ crc32)");
}

static void test_bad_class(void)
{
  uint8_t buf[BUF_MAX];
  replique_event ev[1];
  size_t len;

  fill_sample(ev);
  ev[0].cls = (uint16_t)(REPLIQUE_EV_CLASS_MAX + 1u);
  len = build_container(buf, ev, 1u, 0u, REPLIQUE_TIME_UNIT_NS);
  expect_parse_error(buf, len, REPLIQUE_ERR_CLASS, REPLIQUE_CAUSE_BAD_CLASS,
                     REPLIQUE_HEADER_SIZE + 16u,
                     "classe hors table -> bad-class (décalage de l'enregistrement)");
}

static void test_ts_not_monotonic(void)
{
  uint8_t buf[BUF_MAX];
  replique_event ev[2];
  size_t len;

  fill_sample(ev);
  ev[1].ts = 900u;
  len = build_container(buf, ev, 2u, 0u, REPLIQUE_TIME_UNIT_NS);
  expect_parse_error(buf, len, REPLIQUE_ERR_ORDER,
                     REPLIQUE_CAUSE_TS_MONOTONIC,
                     REPLIQUE_HEADER_SIZE + REPLIQUE_RECORD_SIZE,
                     "ts strictement décroissant -> ts-not-monotonic");
}

/* ts égal : monotone NON décroissant, accepté. */
static void test_ts_equal_allowed(void)
{
  uint8_t buf[BUF_MAX];
  replique_event ev[2];
  replique_event back[2];
  size_t len;
  size_t n_out = 0u;

  fill_sample(ev);
  ev[1].ts = ev[0].ts;
  len = build_container(buf, ev, 2u, 0u, REPLIQUE_TIME_UNIT_NS);
  TU_EQ(replique_kev_parse(buf, len, back, 2u, &n_out), REPLIQUE_STATUS_OK,
        "ts égal accepté (monotone non décroissant)");
  TU_EQ(n_out, 2u, "2 événements relus");
}

/* --------------------------------------------------------- débordements */

static void test_overflow(void)
{
  uint8_t buf[BUF_MAX];
  replique_event ev[3];
  replique_event back[3];
  size_t len = 0u;
  size_t n_out = 0u;

  fill_sample(ev);
  TU_EQ(replique_kev_write(ev, 3u, 0u, REPLIQUE_TIME_UNIT_NS, buf,
                            REPLIQUE_HEADER_SIZE + 3u * REPLIQUE_RECORD_SIZE - 1u,
                            &n_out),
        REPLIQUE_ERR_OVERFLOW, "écriture : tampon trop petit -> OVERFLOW");
  len = build_container(buf, ev, 3u, 0u, REPLIQUE_TIME_UNIT_NS);
  n_out = 0u;
  TU_EQ(replique_kev_parse(buf, len, back, 1u, &n_out), REPLIQUE_ERR_OVERFLOW,
        "lecture : tampon de sortie trop petit -> OVERFLOW");
  TU_EQ(n_out, 1u, "lecture partielle : 1 événement écrit, pas de reprise");
}

int main(void)
{
  test_roundtrip();
  test_sizes_and_hashes();
  test_short_header();
  test_bad_magic();
  test_bad_version();
  test_bad_flags();
  test_bad_length();
  test_bad_crc();
  test_bad_class();
  test_ts_not_monotonic();
  test_ts_equal_allowed();
  test_overflow();
  return tu_report("kev");
}
