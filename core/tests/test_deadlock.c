/* test_deadlock.c — Autopsie : cycle, coupable, inversion de priorité.
 * Scénarios de référence de scenarios/SPEC.md (§2, §3, §4), construits EN
 * MÉMOIRE : aucune lecture de fichier, aucun accès au disque. */
#include "replique/deadlock.h"
#include "replique/sched.h"
#include "replique/verdict.h"
#include "test_util.h"

#define MAX_EVENTS 64

static replique_event g_events[MAX_EVENTS];
static replique_verdict g_verdicts[16];

static replique_event ev(uint64_t ts, uint32_t pid, uint32_t tid, uint16_t cls,
                         uint16_t sub, uint32_t a, uint32_t b, uint32_t c)
{
  replique_event e;

  e.ts = ts;
  e.pid = pid;
  e.tid = tid;
  e.cls = cls;
  e.subcode = sub;
  e.a = a;
  e.b = b;
  e.c = c;
  return e;
}

static size_t build(const replique_event *evs, size_t n)
{
  size_t i;

  for (i = 0u; i < n; ++i) {
    g_events[i] = evs[i];
  }
  return n;
}

/* Scénario « deadlock » de SPEC §2 : 102 prend le mutex 32, 101 prend le 16,
 * puis chacun attend celui de l'autre. */
static size_t scenario_deadlock(void)
{
  replique_event evs[] = {
    ev(1000, 10, 101, REPLIQUE_EV_PROCESS_CREATE, 0, 0, 0, 0),
    ev(1000, 10, 101, REPLIQUE_EV_THREAD_CREATE, REPLIQUE_SCHED_FIFO, 50, 0, 0),
    ev(1000, 10, 102, REPLIQUE_EV_THREAD_CREATE, REPLIQUE_SCHED_FIFO, 60, 0, 0),
    ev(1000, 10, 102, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_READY, 0, 0, 0),
    ev(1000, 10, 102, REPLIQUE_EV_SCHED, REPLIQUE_SR_INITIAL, 102, 0, 0),
    ev(1000, 10, 102, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_RUNNING, 0, 0, 0),
    ev(1200, 10, 102, REPLIQUE_EV_MUTEX_LOCK, 0, 32, 102, 0),
    ev(1300, 10, 102, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_BLOCKED,
       REPLIQUE_BR_TIMER, 7, 0),
    ev(1400, 10, 101, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_READY, 0, 0, 0),
    ev(1400, 10, 101, REPLIQUE_EV_SCHED, REPLIQUE_SR_PREEMPT, 101, 0, 0),
    ev(1400, 10, 101, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_RUNNING, 0, 0, 0),
    ev(1500, 10, 101, REPLIQUE_EV_MUTEX_LOCK, 0, 16, 101, 0),
    ev(1550, 10, 102, REPLIQUE_EV_TIMER, 0, 7, 0, 0),
    ev(1550, 10, 102, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_READY, 0, 0, 0),
    ev(1600, 10, 101, REPLIQUE_EV_MUTEX_LOCK, 1, 32, 102, 0),
    ev(1600, 10, 101, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_BLOCKED,
       REPLIQUE_BR_MUTEX, 32, 102),
    ev(1600, 10, 102, REPLIQUE_EV_SCHED, REPLIQUE_SR_BLOCK, 102, 0, 0),
    ev(1600, 10, 102, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_RUNNING, 0, 0, 0),
    ev(1700, 10, 102, REPLIQUE_EV_MUTEX_LOCK, 1, 16, 101, 0),
    ev(1700, 10, 102, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_BLOCKED,
       REPLIQUE_BR_MUTEX, 16, 101)
  };

  return build(evs, sizeof evs / sizeof evs[0]);
}

/* Scénario « prio-inversion » de SPEC §3 : 202 (prio 100) tient le mutex 48,
 * 201 (prio 10) s'y bloque, 203 (prio 50) vole le CPU. */
static size_t scenario_inversion(int with_inheritance)
{
  replique_event evs[] = {
    ev(1000, 20, 201, REPLIQUE_EV_PROCESS_CREATE, 0, 0, 0, 0),
    ev(1000, 20, 201, REPLIQUE_EV_THREAD_CREATE, REPLIQUE_SCHED_FIFO, 10, 0, 0),
    ev(1000, 20, 202, REPLIQUE_EV_THREAD_CREATE, REPLIQUE_SCHED_FIFO, 100, 0, 0),
    ev(1000, 20, 203, REPLIQUE_EV_THREAD_CREATE, REPLIQUE_SCHED_FIFO, 50, 0, 0),
    ev(1100, 20, 202, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_READY, 0, 0, 0),
    ev(1100, 20, 202, REPLIQUE_EV_SCHED, REPLIQUE_SR_INITIAL, 202, 0, 0),
    ev(1100, 20, 202, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_RUNNING, 0, 0, 0),
    ev(1200, 20, 202, REPLIQUE_EV_MUTEX_LOCK, 0, 48, 202, 0),
    ev(1300, 20, 202, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_BLOCKED,
       REPLIQUE_BR_TIMER, 9, 0),
    ev(1400, 20, 203, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_READY, 0, 0, 0),
    ev(1400, 20, 203, REPLIQUE_EV_SCHED, REPLIQUE_SR_PREEMPT, 203, 0, 0),
    ev(1400, 20, 203, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_RUNNING, 0, 0, 0),
    ev(1500, 20, 201, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_READY, 0, 0, 0),
    ev(1500, 20, 201, REPLIQUE_EV_SCHED, REPLIQUE_SR_PREEMPT, 201, 0, 0),
    ev(1500, 20, 201, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_RUNNING, 0, 0, 0),
    ev(1600, 20, 201, REPLIQUE_EV_MUTEX_LOCK, 1, 48, 202, 0),
    ev(1600, 20, 201, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_BLOCKED,
       REPLIQUE_BR_MUTEX, 48, 202),
    ev(1600, 20, 203, REPLIQUE_EV_SCHED, REPLIQUE_SR_BLOCK, 203, 0, 0),
    ev(1600, 20, 203, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_RUNNING, 0, 0, 0)
  };
  size_t n = build(evs, sizeof evs / sizeof evs[0]);

  if (with_inheritance) {
    /* Le noyau élève 202 à la priorité de 201 : l'inversion est atténuée. */
    g_events[n++] = ev(1600, 20, 202, REPLIQUE_EV_PRIO_CHANGE, 1, 10, 100, 201);
    g_events[n++] = ev(1600, 20, 202, REPLIQUE_EV_SCHED, REPLIQUE_SR_PREEMPT,
                       202, 0, 0);
    g_events[n++] = ev(1600, 20, 203, REPLIQUE_EV_THREAD_STATE,
                       REPLIQUE_TH_READY, 0, 0, 0);
  }
  return n;
}

/* Scénario « race » de SPEC §4 : 402 meurt sans libérer le mutex 64 ; 401
 * attend ensuite un détenteur mort — attente sans fin, mais PAS de cycle. */
static size_t scenario_race(void)
{
  replique_event evs[] = {
    ev(1000, 40, 401, REPLIQUE_EV_PROCESS_CREATE, 0, 0, 0, 0),
    ev(1000, 40, 401, REPLIQUE_EV_THREAD_CREATE, REPLIQUE_SCHED_RR, 30, 0, 0),
    ev(1000, 41, 402, REPLIQUE_EV_PROCESS_CREATE, 0, 0, 0, 0),
    ev(1000, 41, 402, REPLIQUE_EV_THREAD_CREATE, REPLIQUE_SCHED_RR, 30, 0, 0),
    ev(1100, 40, 401, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_READY, 0, 0, 0),
    ev(1100, 40, 401, REPLIQUE_EV_SCHED, REPLIQUE_SR_INITIAL, 401, 0, 0),
    ev(1100, 40, 401, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_RUNNING, 0, 0, 0),
    ev(1200, 40, 401, REPLIQUE_EV_MUTEX_LOCK, 0, 64, 401, 0),
    ev(1300, 40, 401, REPLIQUE_EV_MUTEX_UNLOCK, 0, 64, 0, 0),
    ev(1400, 41, 402, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_READY, 0, 0, 0),
    ev(1600, 41, 402, REPLIQUE_EV_MUTEX_LOCK, 0, 64, 402, 0),
    ev(1700, 41, 402, REPLIQUE_EV_PROCESS_DESTROY, 0, 0, 11, 0),
    ev(1700, 41, 402, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_DEAD, 0, 0, 0),
    ev(1800, 40, 401, REPLIQUE_EV_MUTEX_LOCK, 1, 64, 402, 0),
    ev(1800, 40, 401, REPLIQUE_EV_THREAD_STATE, REPLIQUE_TH_BLOCKED,
       REPLIQUE_BR_MUTEX, 64, 402)
  };

  return build(evs, sizeof evs / sizeof evs[0]);
}

static const replique_verdict *find_verdict(const replique_verdict *v, size_t n,
                                            unsigned code)
{
  size_t i;

  for (i = 0u; i < n; ++i) {
    if (v[i].code == code) {
      return &v[i];
    }
  }
  return NULL;
}

static void test_cycle_and_culprit(void)
{
  size_t n = scenario_deadlock();
  size_t n_verdicts = 0u;
  const replique_verdict *found;
  const replique_verdict *culprit;

  replique_deadlock_detect(g_events, n, g_verdicts, 16u, &n_verdicts);
  found = find_verdict(g_verdicts, n_verdicts, REPLIQUE_V_DEADLOCK_FOUND);
  TU_CHECK(found != NULL, "cycle détecté");
  if (found) {
    TU_EQ(found->chain_len, 2u, "chaîne de 2 threads");
    TU_EQ(found->chain[0], 101u, "chaîne : plus petit tid en tête");
    TU_EQ(found->chain[1], 102u, "chaîne : second thread");
    TU_EQ(found->b, 16u, "ressource qui ferme le cycle");
    TU_EQ(found->ts, 1700u, "horodatage du dernier blocage");
  }
  culprit = find_verdict(g_verdicts, n_verdicts, REPLIQUE_V_CULPRIT);
  TU_CHECK(culprit != NULL, "coupable désigné");
  if (culprit) {
    TU_EQ(culprit->tid, 102u, "coupable : priorité de base la plus basse");
    TU_EQ_STR(culprit->detail, "holds-resource-32-at-prio-60",
              "coupable : motif exact");
  }
  TU_CHECK(find_verdict(g_verdicts, n_verdicts, REPLIQUE_V_PRIO_INVERSION) ==
           NULL, "aucune inversion de priorité dans ce scénario");
}

static void test_prio_inversion(int with_inheritance)
{
  size_t n = scenario_inversion(with_inheritance);
  size_t n_verdicts = 0u;
  const replique_verdict *found;

  replique_prio_inversion_detect(g_events, n, g_verdicts, 16u, &n_verdicts);
  found = find_verdict(g_verdicts, n_verdicts, REPLIQUE_V_PRIO_INVERSION);
  TU_CHECK(found != NULL, "inversion détectée");
  if (found) {
    TU_EQ(found->chain_len, 3u, "chaîne [victime, intermédiaire, bloqueur]");
    TU_EQ(found->chain[0], 201u, "victime");
    TU_EQ(found->chain[1], 203u, "intermédiaire");
    TU_EQ(found->chain[2], 202u, "bloqueur");
    TU_EQ(found->a, with_inheritance ? 1u : 0u, "héritage appliqué ?");
    TU_EQ(found->ts, 1600u, "début de la fenêtre de blocage");
  }
}

static void test_holder_dead(void)
{
  size_t n = scenario_race();
  size_t n_verdicts = 0u;
  const replique_verdict *found;

  replique_deadlock_detect(g_events, n, g_verdicts, 16u, &n_verdicts);
  found = find_verdict(g_verdicts, n_verdicts, REPLIQUE_V_TRACE_INCOMPLETE);
  TU_CHECK(found != NULL, "attente orpheline signalée");
  if (found) {
    TU_EQ_STR(found->detail, "holder-dead", "détail : holder-dead");
    TU_EQ(found->tid, 401u, "thread en attente");
  }
  TU_CHECK(find_verdict(g_verdicts, n_verdicts, REPLIQUE_V_DEADLOCK_FOUND) ==
           NULL, "un détenteur mort n'est PAS un cycle");
  TU_CHECK(find_verdict(g_verdicts, n_verdicts, REPLIQUE_V_CULPRIT) == NULL,
           "aucun coupable sans cycle");
}

static void test_no_cycle(void)
{
  size_t n = scenario_inversion(0);
  size_t n_verdicts = 0u;

  replique_deadlock_detect(g_events, n, g_verdicts, 16u, &n_verdicts);
  TU_CHECK(find_verdict(g_verdicts, n_verdicts, REPLIQUE_V_DEADLOCK_FOUND) ==
           NULL, "aucun cycle dans le scénario d'inversion");
}

int main(void)
{
  test_cycle_and_culprit();
  test_prio_inversion(0);
  test_prio_inversion(1);
  test_holder_dead();
  test_no_cycle();
  return tu_report("deadlock");
}
