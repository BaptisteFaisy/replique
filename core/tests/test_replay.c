/* test_replay.c — rejeu déterministe (CONTRACT.md §5, §6) : séquence de
 * décisions EXACTE, hachage FNV-1a 64 recalculé à la main, fidélité P2
 * (fidèle puis divergente) et politique SPORADIC (réservé non modélisé).
 *
 * Trace construite EN MÉMOIRE (scénario « nominal » de scenarios/SPEC.md) :
 * aucun fichier n'est lu ni écrit.
 */
#include "replique/replay.h"
#include "replique/sched.h"
#include "replique/types.h"
#include "replique/verdict.h"
#include "test_util.h"

#define MAX_DECISIONS 64u

/* Scénario « nominal » : serveur (prio 10) / client (prio 20), MsgSend,
 * temporisation, préemption, terminaison (SPEC.md §1). */
static const replique_event NOMINAL[] = {
  { .ts = 1000, .pid = 1, .tid = 1, .cls = REPLIQUE_EV_PROCESS_CREATE },
  { .ts = 1000, .pid = 1, .tid = 1, .cls = REPLIQUE_EV_THREAD_CREATE,
    .subcode = REPLIQUE_SCHED_FIFO, .a = 10 },
  { .ts = 1000, .pid = 1, .tid = 1, .cls = REPLIQUE_EV_THREAD_STATE,
    .subcode = REPLIQUE_TH_READY },
  { .ts = 1000, .pid = 1, .tid = 1, .cls = REPLIQUE_EV_SCHED,
    .subcode = REPLIQUE_SR_INITIAL, .a = 1 },
  { .ts = 1000, .pid = 1, .tid = 1, .cls = REPLIQUE_EV_THREAD_STATE,
    .subcode = REPLIQUE_TH_RUNNING },
  { .ts = 2000, .pid = 2, .tid = 2, .cls = REPLIQUE_EV_PROCESS_CREATE, .a = 1 },
  { .ts = 2000, .pid = 2, .tid = 2, .cls = REPLIQUE_EV_THREAD_CREATE,
    .subcode = REPLIQUE_SCHED_FIFO, .a = 20 },
  { .ts = 2000, .pid = 2, .tid = 2, .cls = REPLIQUE_EV_THREAD_STATE,
    .subcode = REPLIQUE_TH_READY },
  { .ts = 3000, .pid = 2, .tid = 2, .cls = REPLIQUE_EV_MSG_SEND,
    .a = 1, .b = 1, .c = 1 },
  { .ts = 3000, .pid = 2, .tid = 2, .cls = REPLIQUE_EV_THREAD_STATE,
    .subcode = REPLIQUE_TH_BLOCKED, .a = REPLIQUE_BR_MSGSEND, .b = 1, .c = 1 },
  { .ts = 3100, .pid = 1, .tid = 1, .cls = REPLIQUE_EV_MSG_RECEIVE,
    .a = 2, .b = 2, .c = 1 },
  { .ts = 3200, .pid = 1, .tid = 1, .cls = REPLIQUE_EV_MSG_REPLY,
    .a = 2, .b = 1 },
  { .ts = 3200, .pid = 2, .tid = 2, .cls = REPLIQUE_EV_THREAD_STATE,
    .subcode = REPLIQUE_TH_READY },
  { .ts = 3400, .pid = 1, .tid = 1, .cls = REPLIQUE_EV_THREAD_STATE,
    .subcode = REPLIQUE_TH_BLOCKED, .a = REPLIQUE_BR_TIMER, .b = 1 },
  { .ts = 3400, .pid = 2, .tid = 2, .cls = REPLIQUE_EV_SCHED,
    .subcode = REPLIQUE_SR_BLOCK, .a = 2 },
  { .ts = 3400, .pid = 2, .tid = 2, .cls = REPLIQUE_EV_THREAD_STATE,
    .subcode = REPLIQUE_TH_RUNNING },
  { .ts = 4000, .pid = 1, .tid = 1, .cls = REPLIQUE_EV_TIMER,
    .a = 1, .b = 600 },
  { .ts = 4000, .pid = 1, .tid = 1, .cls = REPLIQUE_EV_THREAD_STATE,
    .subcode = REPLIQUE_TH_READY },
  { .ts = 4000, .pid = 1, .tid = 1, .cls = REPLIQUE_EV_SCHED,
    .subcode = REPLIQUE_SR_UNBLOCK, .a = 1 },
  { .ts = 4000, .pid = 1, .tid = 1, .cls = REPLIQUE_EV_THREAD_STATE,
    .subcode = REPLIQUE_TH_RUNNING },
  { .ts = 4000, .pid = 2, .tid = 2, .cls = REPLIQUE_EV_THREAD_STATE,
    .subcode = REPLIQUE_TH_READY },
  { .ts = 5000, .pid = 1, .tid = 1, .cls = REPLIQUE_EV_PROCESS_DESTROY },
  { .ts = 5000, .pid = 1, .tid = 1, .cls = REPLIQUE_EV_THREAD_STATE,
    .subcode = REPLIQUE_TH_DEAD },
  { .ts = 5000, .pid = 2, .tid = 2, .cls = REPLIQUE_EV_SCHED,
    .subcode = REPLIQUE_SR_TERMINATE, .a = 2 },
  { .ts = 5000, .pid = 2, .tid = 2, .cls = REPLIQUE_EV_THREAD_STATE,
    .subcode = REPLIQUE_TH_RUNNING }
};

/* Décisions attendues (ordre de production du rejeu) et lignes « dec| ». */
typedef struct replay_expect {
  uint64_t ts;
  uint32_t tid;
  uint32_t reason;
  const char *line;
} replay_expect;

static const replay_expect EXPECTED[] = {
  { 1000u, 1u, REPLIQUE_SR_INITIAL, "dec|ts=1000|tid=1|reason=INITIAL\n" },
  { 3400u, 2u, REPLIQUE_SR_BLOCK, "dec|ts=3400|tid=2|reason=BLOCK\n" },
  { 4000u, 1u, REPLIQUE_SR_UNBLOCK, "dec|ts=4000|tid=1|reason=UNBLOCK\n" },
  { 5000u, 2u, REPLIQUE_SR_TERMINATE, "dec|ts=5000|tid=2|reason=TERMINATE\n" }
};

#define EXPECTED_N (sizeof EXPECTED / sizeof EXPECTED[0])

/* FNV-1a 64 de référence, recalculé ici (indépendant du cœur). */
static uint64_t ref_fnv1a(const char *data, size_t len)
{
  uint64_t hash = 14695981039346656037ULL;
  size_t i;

  for (i = 0u; i < len; ++i) {
    hash ^= (uint64_t)(unsigned char)data[i];
    hash *= 1099511628211ULL;
  }
  return hash;
}

static void test_decision_sequence(void)
{
  replique_decision decisions[MAX_DECISIONS];
  replique_replay_result res;
  replique_sched_config cfg;
  size_t i;

  replique_sched_config_default(&cfg);
  memset(&res, 0, sizeof res);
  res.decisions = decisions;
  res.cap = MAX_DECISIONS;
  TU_EQ(replique_replay(NOMINAL, sizeof NOMINAL / sizeof NOMINAL[0], &cfg, &res),
        REPLIQUE_STATUS_OK, "rejeu terminé sans erreur");
  TU_EQ(res.n_decisions, EXPECTED_N, "nombre de décisions");
  for (i = 0u; i < EXPECTED_N; ++i) {
    TU_EQ(decisions[i].ts, EXPECTED[i].ts, "décision : ts exact");
    TU_EQ(decisions[i].tid, EXPECTED[i].tid, "décision : tid élu exact");
    TU_EQ(decisions[i].reason, EXPECTED[i].reason, "décision : raison exacte");
  }
}

static void test_hash_reference(void)
{
  replique_decision decisions[MAX_DECISIONS];
  replique_decision again[MAX_DECISIONS];
  replique_replay_result res;
  replique_replay_result res2;
  replique_sched_config cfg;
  char reference[256];
  size_t used = 0u;
  size_t i;

  replique_sched_config_default(&cfg);
  memset(&res, 0, sizeof res);
  res.decisions = decisions;
  res.cap = MAX_DECISIONS;
  replique_replay(NOMINAL, sizeof NOMINAL / sizeof NOMINAL[0], &cfg, &res);
  for (i = 0u; i < EXPECTED_N; ++i) {
    size_t len = strlen(EXPECTED[i].line);
    memcpy(reference + used, EXPECTED[i].line, len);
    used += len;
  }
  TU_EQ(res.hash, ref_fnv1a(reference, used),
        "hachage du rejeu == FNV-1a 64 recalculé à la main");
  TU_EQ(res.hash, 12655629535735537574ULL,
        "hachage == référence gelée (scenarios/nominal.json)");
  TU_EQ(replique_replay_hash(decisions, res.n_decisions), res.hash,
        "replique_replay_hash == hachage du rejeu");
  memset(&res2, 0, sizeof res2);
  res2.decisions = again;
  res2.cap = MAX_DECISIONS;
  replique_replay(NOMINAL, sizeof NOMINAL / sizeof NOMINAL[0], &cfg, &res2);
  TU_EQ(res2.hash, res.hash, "deux rejeux -> même hachage (P1)");
  TU_EQ(res2.n_decisions, res.n_decisions, "deux rejeux -> même nombre");
}

static void test_fidelity_faithful(void)
{
  replique_decision decisions[MAX_DECISIONS];
  replique_replay_result res;
  replique_sched_config cfg;
  replique_verdict out;

  replique_sched_config_default(&cfg);
  memset(&res, 0, sizeof res);
  res.decisions = decisions;
  res.cap = MAX_DECISIONS;
  replique_replay(NOMINAL, sizeof NOMINAL / sizeof NOMINAL[0], &cfg, &res);
  memset(&out, 0, sizeof out);
  out.code = 4242u;
  out.ts = 99u;
  TU_EQ(replique_replay_check_fidelity(NOMINAL,
                                       sizeof NOMINAL / sizeof NOMINAL[0],
                                       decisions, res.n_decisions, &out),
        0, "fidélité : trace fidèle -> aucun verdict");
  TU_EQ(out.code, 4242u, "fidélité : aucun verdict émis");
  TU_EQ(out.ts, 99u, "fidélité : verdict laissé intact");
}

static void test_fidelity_diverged(void)
{
  replique_decision decisions[MAX_DECISIONS];
  replique_replay_result res;
  replique_sched_config cfg;
  replique_verdict out;
  replique_event broken[sizeof NOMINAL / sizeof NOMINAL[0]];
  size_t i;

  replique_sched_config_default(&cfg);
  memset(&res, 0, sizeof res);
  res.decisions = decisions;
  res.cap = MAX_DECISIONS;
  replique_replay(NOMINAL, sizeof NOMINAL / sizeof NOMINAL[0], &cfg, &res);
  for (i = 0u; i < (sizeof NOMINAL / sizeof NOMINAL[0]); ++i) {
    broken[i] = NOMINAL[i];
  }
  /* Le SCHED de 4000 annonce PREEMPT alors que le rejeu tranche UNBLOCK. */
  for (i = 0u; i < (sizeof NOMINAL / sizeof NOMINAL[0]); ++i) {
    if (broken[i].cls == REPLIQUE_EV_SCHED && broken[i].ts == 4000u) {
      broken[i].subcode = REPLIQUE_SR_PREEMPT;
    }
  }
  memset(&out, 0, sizeof out);
  TU_EQ(replique_replay_check_fidelity(broken,
                                       sizeof NOMINAL / sizeof NOMINAL[0],
                                       decisions, res.n_decisions, &out),
        1, "fidélité : divergence détectée");
  TU_EQ(out.code, REPLIQUE_V_REPLAY_DIVERGED, "verdict REPLAY_DIVERGED");
  TU_EQ(out.sev, REPLIQUE_SEV_ERROR, "sévérité error");
  TU_EQ(out.ts, 4000u, "divergence : ts de l'événement fautif");
  TU_EQ(out.tid, 1u, "divergence : tid annoncé par le noyau");
  TU_EQ(out.a, REPLIQUE_SR_PREEMPT, "divergence : raison attendue");
  TU_EQ(out.b, REPLIQUE_SR_UNBLOCK, "divergence : raison trouvée");
}

static void test_sporadic_not_modelled(void)
{
  replique_event ev[1];
  replique_decision decisions[MAX_DECISIONS];
  replique_replay_result res;
  replique_sched_config cfg;

  ev[0].ts = 1000u;
  ev[0].pid = 1u;
  ev[0].tid = 1u;
  ev[0].cls = REPLIQUE_EV_THREAD_CREATE;
  ev[0].subcode = REPLIQUE_SCHED_SPORADIC;
  ev[0].a = 10u;
  replique_sched_config_default(&cfg);
  memset(&res, 0, sizeof res);
  res.decisions = decisions;
  res.cap = MAX_DECISIONS;
  replique_replay(ev, 1u, &cfg, &res);
  TU_EQ(res.incomplete, 1u, "SPORADIC : modèle non couvert");
  TU_EQ_STR(res.incomplete_detail, "sporadic-not-modelled",
            "SPORADIC : détail exact");
}

int main(void)
{
  test_decision_sequence();
  test_hash_reference();
  test_fidelity_faithful();
  test_fidelity_diverged();
  test_sporadic_not_modelled();
  return tu_report("replay");
}
