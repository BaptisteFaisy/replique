/* declic_test.c — test SUR PC de la logique PORTABLE de Déclic.
 *
 * Nom imposé par le Makefile (build/declic-test) : ce fichier n'est PAS
 * aspiré par la règle `core/tests/test_*.c`, car il a besoin des objets de
 * target/declic.
 *
 * Couverture :
 *   1. SHA-256 contre les vecteurs publics (FIPS 180-4) ;
 *   2. construction puis relecture d'une archive RDA1 à 3 sections
 *      (noms, longueurs, empreintes) + aller-retour de la trace .rkv1 ;
 *   3. archive tronquée → erreur propre (jamais de segmentation) ;
 *   4. manifeste exact (chaîne littérale attendue) ;
 *   5. décision de déclenchement (par pid, par signal, exclusion).
 *
 * Aucun en-tête QNX, aucune horloge : le test tourne partout.
 */
#include <stdio.h>
#include <string.h>

#include "declic.h"
#include "replique/kev.h"
#include "replique/types.h"
#include "sha256_portable.h"

static int g_failures;

/* Textes des sections de test (longueurs dérivées, jamais en dur). */
static const char PIDIN_TEXT[] = "PID TID PRI STATE\n4242 7 10 READY\n";
static const char MANIFEST_TEXT[] = "schema=rda1/1\narchive=t\n";

static void check(int cond, const char *what)
{
  if (cond) {
    printf("ok   : %s\n", what);
  } else {
    printf("FAIL : %s\n", what);
    ++g_failures;
  }
}

static void check_str(const char *got, const char *expected, const char *what)
{
  if (strcmp(got, expected) == 0) {
    printf("ok   : %s\n", what);
  } else {
    printf("FAIL : %s\n  attendu : %s\n  obtenu  : %s\n", what, expected, got);
    ++g_failures;
  }
}

/* ------------------------------------------------------------------ */
/* 1. SHA-256 : vecteurs publics                                       */
/* ------------------------------------------------------------------ */

static void test_sha256_vectors(void)
{
  static const char *empty_hex =
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
  static const char *abc_hex =
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  /* Vecteur FIPS 180-4 « abcdefghijklmnopqrstuvwxyz… » (deux blocs). */
  static const char *long_hex =
      "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
  static const char long_msg[] =
      "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  char hex[SHA256_HEX_SIZE];

  sha256_hex("", 0u, hex);
  check_str(hex, empty_hex, "sha256(\"\") = e3b0c442...b7852b855");

  sha256_hex("abc", 3u, hex);
  check_str(hex, abc_hex, "sha256(\"abc\") = ba7816bf...f20015ad");

  sha256_hex(long_msg, sizeof long_msg - 1u, hex);
  check_str(hex, long_hex, "sha256(56 octets) = 248d6a61...19db06c1");
}

static void test_sha256_split(void)
{
  static const char long_msg[] =
      "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  static const char *long_hex =
      "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
  sha256_ctx ctx;
  uint8_t digest[SHA256_DIGEST_SIZE];
  char hex[SHA256_HEX_SIZE];

  /* Le résultat ne doit pas dépendre du découpage des appels update(). */
  sha256_init(&ctx);
  sha256_update(&ctx, long_msg, 30u);
  sha256_update(&ctx, long_msg + 30u, 26u);
  sha256_final(&ctx, digest);
  sha256_hex_buf(digest, hex);
  check_str(hex, long_hex, "sha256(update fractionné) = même empreinte");
}

/* ------------------------------------------------------------------ */
/* 2 + 3. Archive RDA1 : construction, relecture, troncature            */
/* ------------------------------------------------------------------ */

/* Trace RKV1 minimale (deux événements), construite par le cœur portable. */
static size_t build_trace(uint8_t *out, size_t cap)
{
  replique_event ev[2];
  size_t n = 0u;

  memset(ev, 0, sizeof ev);
  ev[0].ts = 1000u;
  ev[0].pid = 4242u;
  ev[0].tid = 7u;
  ev[0].cls = (uint16_t)REPLIQUE_EV_PROCESS_CREATE;
  ev[0].a = 1u;
  ev[1].ts = 2000u;
  ev[1].pid = 4242u;
  ev[1].tid = 7u;
  ev[1].cls = (uint16_t)REPLIQUE_EV_PROCESS_DESTROY;
  ev[1].b = 11u;

  if (replique_kev_write(ev, 2u, REPLIQUE_FLAG_SYNTHETIC,
                         REPLIQUE_TIME_UNIT_NS, out, cap, &n) !=
      REPLIQUE_STATUS_OK) {
    return 0u;
  }
  return n;
}

/* Construit l'archive de test à 3 sections. */
static size_t build_archive(uint8_t *archive, size_t cap, uint8_t *trace,
                            size_t *trace_len)
{
  declic_section secs[3];
  size_t archive_len = 0u;

  *trace_len = build_trace(trace, 128u);
  memset(secs, 0, sizeof secs);
  memcpy(secs[0].name, DECLIC_SECTION_TRACE, sizeof DECLIC_SECTION_TRACE);
  secs[0].data = trace;
  secs[0].len = *trace_len;
  memcpy(secs[1].name, DECLIC_SECTION_PIDIN, sizeof DECLIC_SECTION_PIDIN);
  secs[1].data = (const uint8_t *)PIDIN_TEXT;
  secs[1].len = sizeof PIDIN_TEXT - 1u;
  memcpy(secs[2].name, DECLIC_SECTION_MANIFEST, sizeof DECLIC_SECTION_MANIFEST);
  secs[2].data = (const uint8_t *)MANIFEST_TEXT;
  secs[2].len = sizeof MANIFEST_TEXT - 1u;

  check(declic_archive_build_sections(secs, 3u, archive, cap, &archive_len) ==
            DECLIC_OK,
        "archive RDA1 construite (3 sections)");
  return archive_len;
}

/* Relecture : noms, longueurs, empreintes. */
static void test_archive_sections(const uint8_t *archive, size_t archive_len,
                                   const uint8_t *trace, size_t trace_len)
{
  declic_archive_view view;
  declic_section_info info;

  check(declic_archive_open(archive, archive_len, &view) == DECLIC_OK,
        "archive rouverte");
  check(view.n_sections == 3u, "archive rouverte : 3 sections");

  check(declic_archive_find(&view, DECLIC_SECTION_TRACE, &info) == DECLIC_OK,
        "section « trace » trouvée");
  check_str(info.name, DECLIC_SECTION_TRACE, "nom de la section « trace »");
  check(info.length == (uint64_t)trace_len, "longueur de la section « trace »");
  {
    uint8_t digest[SHA256_DIGEST_SIZE];

    sha256(trace, trace_len, digest);
    check(memcmp(digest, info.sha256, SHA256_DIGEST_SIZE) == 0,
          "empreinte SHA-256 de la section « trace » conforme");
  }

  check(declic_archive_find(&view, DECLIC_SECTION_PIDIN, &info) == DECLIC_OK,
        "section « pidin » trouvée");
  check(info.length == (uint64_t)(sizeof PIDIN_TEXT - 1u),
        "longueur de la section « pidin »");
  {
    char hex[SHA256_HEX_SIZE];

    sha256_hex(PIDIN_TEXT, sizeof PIDIN_TEXT - 1u, hex);
    check(strlen(hex) == 64u, "empreinte hexadécimale sur 64 caractères");
  }

  check(declic_archive_find(&view, "inconnu", &info) == DECLIC_ERR_NOT_FOUND,
        "section inconnue → erreur NOT_FOUND");
  check(declic_archive_verify_all(&view) == DECLIC_OK,
        "vérification des 3 empreintes : conforme");
}

/* La trace extraite de l'archive se relit avec le parseur du cœur. */
static void test_archive_trace(const uint8_t *archive, size_t archive_len)
{
  declic_archive_view view;
  declic_section_info info;
  replique_event ev[4];
  size_t n = 0u;

  check(declic_archive_open(archive, archive_len, &view) == DECLIC_OK,
        "archive rouverte (aller-retour de la trace)");
  check(declic_archive_find(&view, DECLIC_SECTION_TRACE, &info) == DECLIC_OK,
        "section « trace » relue");
  check(replique_kev_parse(archive + info.offset, (size_t)info.length, ev, 4u,
                            &n) == REPLIQUE_STATUS_OK,
        "trace extraite de l'archive : parseable par le cœur");
  check(n == 2u, "trace extraite : 2 événements");
  check(n == 2u && ev[1].cls == (uint16_t)REPLIQUE_EV_PROCESS_DESTROY,
        "trace extraite : PROCESS_DESTROY retrouvé");
}

/* Archive tronquée → erreur propre ; section altérée → erreur d'empreinte. */
static void test_archive_broken(const uint8_t *archive, size_t archive_len)
{
  static uint8_t bad[1024];
  declic_archive_view view;

  check(declic_archive_open(archive, archive_len / 2u, &view) ==
            DECLIC_ERR_TRUNCATED,
        "archive tronquée → DECLIC_ERR_TRUNCATED");
  check(declic_archive_open(archive, 8u, &view) == DECLIC_ERR_TRUNCATED,
        "en-tête tronqué → DECLIC_ERR_TRUNCATED");
  check(declic_status_name(DECLIC_ERR_TRUNCATED) != NULL,
        "code d'erreur traduisible");

  memcpy(bad, archive, archive_len);
  bad[archive_len - 1u] ^= 0x01u;
  check(declic_archive_open(bad, archive_len, &view) == DECLIC_OK,
        "archive altérée : en-tête encore lisible");
  check(declic_archive_verify_all(&view) == DECLIC_ERR_SHA,
        "archive altérée → DECLIC_ERR_SHA");
}

/* Archive standard : les 5 artefacts, dans l'ordre figé. */
static void test_standard_archive(void)
{
  static uint8_t archive[1024];
  static const char pidin_text[] = "pidin\n";
  static const char slog2_text[] = "slog2\n";
  static const char core_text[] = "CORE\n";
  static const char manifest_text[] = "schema=rda1/1\n";
  declic_artifacts art;
  declic_archive_view view;
  declic_section_info info;
  size_t archive_len = 0u;

  memset(&art, 0, sizeof art);
  art.trace = (const uint8_t *)"RKV1-trace";
  art.trace_len = 9u;
  art.pidin = (const uint8_t *)pidin_text;
  art.pidin_len = sizeof pidin_text - 1u;
  art.slog2 = (const uint8_t *)slog2_text;
  art.slog2_len = sizeof slog2_text - 1u;
  art.core = (const uint8_t *)core_text;
  art.core_len = sizeof core_text - 1u;
  art.manifest = manifest_text;
  art.manifest_len = sizeof manifest_text - 1u;

  check(declic_archive_build(&art, archive, sizeof archive, &archive_len) ==
            DECLIC_OK,
        "archive standard construite (5 artefacts)");
  check(declic_archive_open(archive, archive_len, &view) == DECLIC_OK,
        "archive standard rouverte");
  check(view.n_sections == 5u, "archive standard : 5 sections");
  check(declic_archive_find(&view, DECLIC_SECTION_TRACE, &info) == DECLIC_OK &&
            strcmp(info.name, DECLIC_SECTION_TRACE) == 0,
        "ordre figé : 1re section = trace");
  check(declic_archive_section(&view, 4u, &info) == DECLIC_OK &&
            strcmp(info.name, DECLIC_SECTION_MANIFEST) == 0,
        "ordre figé : dernière section = manifest");
  check(declic_archive_verify_all(&view) == DECLIC_OK,
        "archive standard : toutes les empreintes sont conformes");
}

/* ------------------------------------------------------------------ */
/* 4. Manifeste : chaîne littérale attendue                           */
/* ------------------------------------------------------------------ */

static void test_manifest(void)
{
  static const char expected[] =
      "schema=rda1/1\n"
      "archive=declic-test-001\n"
      "timestamp=1794468000000000000\n"
      "pid=4242\n"
      "status=0\n"
      "signal=11\n";
  declic_manifest m;
  char out[DECLIC_MANIFEST_MAX];
  size_t len = 0u;

  memset(&m, 0, sizeof m);
  memcpy(m.archive_name, "declic-test-001", sizeof "declic-test-001");
  m.timestamp = 1794468000000000000ull;
  m.pid = 4242u;
  m.status = 0u;
  m.signal = 11u;

  check(declic_manifest_render(&m, out, sizeof out, &len) == DECLIC_OK,
        "manifeste rendu");
  check_str(out, expected, "manifeste : chaîne littérale attendue");
  check(len == sizeof expected - 1u, "manifeste : longueur annoncée exacte");

  /* Un nom d'archive vide est refusé (l'appelant doit le fournir). */
  {
    declic_manifest bad = m;

    bad.archive_name[0] = '\0';
    check(declic_manifest_render(&bad, out, sizeof out, &len) ==
              DECLIC_ERR_ARG,
          "manifeste sans nom d'archive → erreur");
  }
}

/* ------------------------------------------------------------------ */
/* 5. Décision de déclenchement                                        */
/* ------------------------------------------------------------------ */

static void test_trigger_pid(void)
{
  declic_policy pol;
  declic_term_event ev;

  declic_policy_init(&pol);
  memset(&ev, 0, sizeof ev);
  ev.pid = 100u;
  ev.signal = 11u;
  check(declic_should_capture(&ev, &pol) == 1, "politique par défaut : capture");

  declic_policy_init(&pol);
  pol.watch_all = 0u;
  pol.n_watch = 1u;
  pol.watch[0] = 4242u;
  ev.pid = 4242u;
  check(declic_should_capture(&ev, &pol) == 1, "pid surveillé → capture");
  ev.pid = 4243u;
  check(declic_should_capture(&ev, &pol) == 0,
        "pid non surveillé → pas de capture");
}

static void test_trigger_signal(void)
{
  declic_policy pol;
  declic_term_event ev;

  declic_policy_init(&pol);
  pol.n_signals = 1u;
  pol.signals[0] = 11u;
  memset(&ev, 0, sizeof ev);
  ev.pid = 4242u;
  ev.signal = 11u;
  check(declic_should_capture(&ev, &pol) == 1, "signal retenu (SIGSEGV) → capture");
  ev.signal = 15u;
  check(declic_should_capture(&ev, &pol) == 0,
        "signal non retenu → pas de capture");
}

static void test_trigger_exclusion(void)
{
  declic_policy pol;
  declic_term_event ev;

  declic_policy_init(&pol);
  pol.n_exclusions = 1u;
  pol.exclusions[0] = 4242u;
  memset(&ev, 0, sizeof ev);
  ev.pid = 4242u;
  ev.signal = 11u;
  check(declic_should_capture(&ev, &pol) == 0, "pid exclu → pas de capture");
  ev.pid = 4243u;
  check(declic_should_capture(&ev, &pol) == 1, "pid non exclu → capture");

  /* L'exclusion l'emporte sur la liste de pid surveillés. */
  declic_policy_init(&pol);
  pol.watch_all = 0u;
  pol.n_watch = 1u;
  pol.watch[0] = 4242u;
  pol.n_exclusions = 1u;
  pol.exclusions[0] = 4242u;
  ev.pid = 4242u;
  check(declic_should_capture(&ev, &pol) == 0,
        "exclusion prioritaire sur la liste de pid");

  /* Argument nul → erreur (pas un booléen). */
  check(declic_should_capture(NULL, &pol) < 0, "événement nul → erreur");
  check(declic_should_capture(&ev, NULL) < 0, "politique nulle → erreur");
}

int main(void)
{
  static uint8_t archive[1024];
  static uint8_t trace[128];
  size_t trace_len = 0u;
  size_t archive_len;

  test_sha256_vectors();
  test_sha256_split();

  archive_len = build_archive(archive, sizeof archive, trace, &trace_len);
  check(trace_len > 0u, "trace RKV1 de test construite (cœur portable)");
  check(archive_len == DECLIC_RDA1_HEADER_SIZE + 3u * DECLIC_RDA1_ENTRY_SIZE +
                         trace_len + (sizeof PIDIN_TEXT - 1u) +
                         (sizeof MANIFEST_TEXT - 1u),
        "taille de l'archive = en-tête + table + blocs");

  test_archive_sections(archive, archive_len, trace, trace_len);
  test_archive_trace(archive, archive_len);
  test_archive_broken(archive, archive_len);
  test_standard_archive();
  test_manifest();
  test_trigger_pid();
  test_trigger_signal();
  test_trigger_exclusion();

  if (g_failures != 0) {
    printf("declic_test KO : %d échec(s)\n", g_failures);
    return 1;
  }
  printf("declic_test OK\n");
  return 0;
}
