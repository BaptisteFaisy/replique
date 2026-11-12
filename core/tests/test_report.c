/* test_report.c — sorties canoniques (CONTRACT.md §4) : ligne d'événement,
 * ligne de verdict, ligne de décision, sanitation du détail (ASCII, pas de
 * « | », troncature propre) et tri gelé des verdicts (§4.4).
 *
 * Les chaînes attendues sont LITTÉRALES : le format est figé, le moindre
 * écart est un bug du cœur (ou du test).
 */
#include "replique/report.h"
#include "replique/types.h"
#include "replique/verdict.h"
#include "test_util.h"

#define LINE_MAX 256u

static void test_event_line(void)
{
  replique_event ev;
  char buf[LINE_MAX];
  unsigned truncated = 1u;
  size_t len;

  ev.ts = 1000u;
  ev.pid = 10u;
  ev.tid = 101u;
  ev.cls = REPLIQUE_EV_THREAD_STATE;
  ev.subcode = REPLIQUE_TH_BLOCKED;
  ev.a = REPLIQUE_BR_MUTEX;
  ev.b = 32u;
  ev.c = 102u;
  len = replique_report_event(&ev, buf, sizeof buf, &truncated);
  TU_EQ(truncated, 0u, "ligne d'événement : pas de troncature");
  TU_EQ(len, strlen("ev|ts=1000|pid=10|tid=101|class=THREAD_STATE|sub=3"
                    "|a=1|b=32|c=102\n"),
        "longueur de la ligne d'événement");
  TU_EQ_STR(buf, "ev|ts=1000|pid=10|tid=101|class=THREAD_STATE|sub=3"
                 "|a=1|b=32|c=102\n",
             "ligne d'événement conforme à §4.1");
}

static void test_verdict_line(void)
{
  replique_verdict v;
  char buf[LINE_MAX];
  unsigned truncated = 1u;
  size_t len;

  replique_verdict_set(&v, REPLIQUE_V_DEADLOCK_FOUND, 1700u, 0u, 102u, 0u,
                       16u, 0u, "");
  len = replique_report_verdict(&v, buf, sizeof buf, &truncated);
  TU_EQ(truncated, 0u, "ligne de verdict : pas de troncature");
  TU_EQ(len, strlen("vd|code=DEADLOCK_FOUND|sev=error|ts=1700|pid=0|tid=102"
                    "|a=0|b=16|c=0|hash=0|chain=|detail=\n"),
        "longueur de la ligne de verdict");
  TU_EQ_STR(buf, "vd|code=DEADLOCK_FOUND|sev=error|ts=1700|pid=0|tid=102"
                 "|a=0|b=16|c=0|hash=0|chain=|detail=\n",
             "ligne de verdict conforme à §4.2 (chaîne vide, détail vide)");
}

static void test_verdict_line_with_chain(void)
{
  replique_verdict v;
  char buf[LINE_MAX];
  unsigned truncated = 1u;

  replique_verdict_set(&v, REPLIQUE_V_CULPRIT, 1700u, 0u, 102u, 0u, 32u, 0u,
                       "holds-resource-32-at-prio-60");
  v.chain_len = 2u;
  v.chain[0] = 101u;
  v.chain[1] = 102u;
  replique_report_verdict(&v, buf, sizeof buf, &truncated);
  TU_EQ(truncated, 0u, "ligne de verdict : pas de troncature");
  TU_EQ_STR(buf, "vd|code=CULPRIT|sev=error|ts=1700|pid=0|tid=102|a=0|b=32"
                 "|c=0|hash=0|chain=101,102"
                 "|detail=holds-resource-32-at-prio-60\n",
             "ligne de verdict avec chaîne et détail");
}

static void test_decision_line(void)
{
  replique_decision d;
  char buf[LINE_MAX];
  unsigned truncated = 1u;
  size_t len;

  d.ts = 4000u;
  d.tid = 1u;
  d.reason = REPLIQUE_SR_UNBLOCK;
  len = replique_report_decision(&d, buf, sizeof buf, &truncated);
  TU_EQ(truncated, 0u, "ligne de décision : pas de troncature");
  TU_EQ(len, strlen("dec|ts=4000|tid=1|reason=UNBLOCK\n"),
        "longueur de la ligne de décision");
  TU_EQ_STR(buf, "dec|ts=4000|tid=1|reason=UNBLOCK\n",
             "ligne de décision conforme à §4.3");
}

/* Détail : ASCII, pas de « | », pas de contrôle ; le reste devient « _ ». */
static void test_detail_sanitized(void)
{
  replique_verdict v;
  char raw[8];
  char buf[LINE_MAX];
  unsigned truncated = 1u;

  raw[0] = 'a';
  raw[1] = '|';
  raw[2] = (char)0xC3;      /* « é » en UTF-8 : deux octets hors ASCII */
  raw[3] = (char)0xA9;
  raw[4] = '\n';
  raw[5] = (char)0x7F;
  raw[6] = '\0';
  replique_verdict_set(&v, REPLIQUE_V_TRACE_INCOMPLETE, 1800u, 40u, 401u, 1u,
                       64u, 0u, raw);
  TU_EQ_STR(v.detail, "a_____", "détail sanitisé à la fabrication");
  replique_report_verdict(&v, buf, sizeof buf, &truncated);
  TU_CHECK(strstr(buf, "detail=a_____\n") != NULL,
           "détail sanitisé dans la ligne canonique");
  /* Un verdict fabriqué à la main (détail non sanitisé) doit l'être à
   * l'écriture : la sortie reste ASCII. */
  memset(&v, 0, sizeof v);
  v.code = REPLIQUE_V_TRACE_INCOMPLETE;
  v.sev = REPLIQUE_SEV_WARNING;
  v.ts = 1800u;
  v.tid = 401u;
  memcpy(v.detail, raw, sizeof raw);
  replique_report_verdict(&v, buf, sizeof buf, &truncated);
  TU_CHECK(strstr(buf, "detail=a_____\n") != NULL,
           "détail non ASCII converti en « _ » à l'écriture");
  {
    const char *detail = strstr(buf, "detail=");
    const char *end = detail ? strchr(detail, '\n') : NULL;

    TU_CHECK(detail != NULL, "détail présent dans la ligne canonique");
    TU_CHECK(end != NULL && memchr(detail, '|', (size_t)(end - detail)) == NULL,
             "aucun « | » dans le détail (§4.2)");
  }
}

static void test_truncation(void)
{
  replique_verdict v;
  char small[16];
  unsigned truncated = 0u;
  size_t len;

  replique_verdict_set(&v, REPLIQUE_V_DEADLOCK_FOUND, 1700u, 0u, 102u, 0u,
                       16u, 0u, "cycle-detected");
  len = replique_report_verdict(&v, small, sizeof small, &truncated);
  TU_EQ(truncated, 1u, "tampon trop petit : troncature signalée");
  TU_CHECK(len > sizeof small, "longueur nécessaire retournée");
  TU_EQ(small[sizeof small - 1u], '\0', "tampon terminé par un octet nul");
}

static void test_many_lines(void)
{
  replique_event ev[2];
  replique_decision dec[2];
  replique_verdict v[2];
  char buf[LINE_MAX];
  unsigned truncated = 1u;
  size_t len;

  memset(ev, 0, sizeof ev);
  ev[0].ts = 1000u;
  ev[0].pid = 1u;
  ev[0].tid = 1u;
  ev[0].cls = REPLIQUE_EV_PROCESS_CREATE;
  ev[1].ts = 1100u;
  ev[1].pid = 1u;
  ev[1].tid = 1u;
  ev[1].cls = REPLIQUE_EV_THREAD_STATE;
  ev[1].subcode = REPLIQUE_TH_READY;
  len = replique_report_events(ev, 2u, buf, sizeof buf, &truncated);
  TU_EQ(truncated, 0u, "deux lignes d'événement : pas de troncature");
  TU_EQ_STR(buf, "ev|ts=1000|pid=1|tid=1|class=PROCESS_CREATE|sub=0"
                 "|a=0|b=0|c=0\n"
                 "ev|ts=1100|pid=1|tid=1|class=THREAD_STATE|sub=2"
                 "|a=0|b=0|c=0\n",
             "ordre du fichier conservé (aucun tri des événements)");
  TU_EQ(len, strlen(buf), "longueur = taille du texte produit");

  dec[0].ts = 1000u;
  dec[0].tid = 1u;
  dec[0].reason = REPLIQUE_SR_INITIAL;
  dec[1].ts = 1400u;
  dec[1].tid = 2u;
  dec[1].reason = REPLIQUE_SR_PREEMPT;
  replique_report_decisions(dec, 2u, buf, sizeof buf, &truncated);
  TU_EQ_STR(buf, "dec|ts=1000|tid=1|reason=INITIAL\n"
                 "dec|ts=1400|tid=2|reason=PREEMPT\n",
             "décisions : ordre de production du rejeu");

  replique_verdict_set(&v[0], REPLIQUE_V_OK, 0u, 0u, 0u, 0u, 0u, 0u, "ok");
  replique_verdict_set(&v[1], REPLIQUE_V_DEADLOCK_FOUND, 1700u, 0u, 0u, 0u,
                       16u, 0u, "cycle-detected");
  replique_report_verdicts(v, 2u, buf, sizeof buf, &truncated);
  TU_EQ_STR(buf, "vd|code=OK|sev=info|ts=0|pid=0|tid=0|a=0|b=0|c=0|hash=0"
                 "|chain=|detail=ok\n"
                 "vd|code=DEADLOCK_FOUND|sev=error|ts=1700|pid=0|tid=0|a=0"
                 "|b=16|c=0|hash=0|chain=|detail=cycle-detected\n",
             "verdicts : ordre de production conservé");
}

static void test_verdict_sort(void)
{
  replique_verdict v[8];
  size_t i;

  replique_verdict_set(&v[0], REPLIQUE_V_OK, 10u, 0u, 1u, 0u, 0u, 0u, "info");
  replique_verdict_set(&v[1], REPLIQUE_V_DEADLOCK_FOUND, 5u, 0u, 0u, 0u, 16u,
                       0u, "cycle-detected");
  replique_verdict_set(&v[2], REPLIQUE_V_CULPRIT, 5u, 0u, 102u, 0u, 32u, 0u,
                       "holds-resource-32-at-prio-60");
  replique_verdict_set(&v[3], REPLIQUE_V_PRIO_INVERSION, 1600u, 0u, 201u, 0u,
                       0u, 0u, "resource-48");
  replique_verdict_set(&v[4], REPLIQUE_V_REPLAY_DETERMINISTIC, 0u, 0u, 0u, 0u,
                       0u, 0u, "decisions-4");
  replique_verdict_set(&v[5], REPLIQUE_V_TRACE_INCOMPLETE, 1800u, 0u, 401u, 1u,
                       64u, 0u, "holder-dead");
  replique_verdict_set(&v[6], REPLIQUE_V_PARSE_ERROR, 0u, 0u, 0u, 0u, 0u, 0u,
                       "bad-crc");
  replique_verdict_set(&v[7], REPLIQUE_V_DEADLOCK_FOUND, 5u, 0u, 0u, 0u, 16u,
                       0u, "cycle-detected-2");
  replique_verdict_sort(v, 8u);
  /* Tri : sévérité décroissante, code croissant, ts croissant, tid croissant,
   * puis ordre de production (stable). */
  TU_EQ(v[0].code, REPLIQUE_V_PARSE_ERROR, "tri : PARSE_ERROR toujours seul et premier");
  TU_EQ(v[1].code, REPLIQUE_V_DEADLOCK_FOUND, "tri : erreur suivante");
  TU_EQ(v[2].code, REPLIQUE_V_DEADLOCK_FOUND, "tri : erreur suivante (doublon)");
  TU_EQ_STR(v[1].detail, "cycle-detected", "tri : stabilité (ordre de production)");
  TU_EQ_STR(v[2].detail, "cycle-detected-2", "tri : stabilité (ordre de production)");
  TU_EQ(v[3].code, REPLIQUE_V_CULPRIT, "tri : code croissant dans les erreurs");
  TU_EQ(v[4].code, REPLIQUE_V_PRIO_INVERSION, "tri : warning avant info");
  TU_EQ(v[5].code, REPLIQUE_V_TRACE_INCOMPLETE, "tri : warning suivant");
  TU_EQ(v[6].code, REPLIQUE_V_OK, "tri : info, code croissant");
  TU_EQ(v[7].code, REPLIQUE_V_REPLAY_DETERMINISTIC, "tri : dernier verdict");
  for (i = 0u; i < 8u; ++i) {
    TU_CHECK(v[i].sev >= (i + 1u < 8u ? v[i + 1u].sev : 0u),
             "tri : sévérité décroissante");
  }
}

int main(void)
{
  test_event_line();
  test_verdict_line();
  test_verdict_line_with_chain();
  test_decision_line();
  test_detail_sanitized();
  test_truncation();
  test_many_lines();
  test_verdict_sort();
  return tu_report("report");
}
