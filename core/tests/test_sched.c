/* test_sched.c — modèle d'ordonnanceur (CONTRACT.md §5) : configuration
 * par défaut, tables de threads, élection par priorité, égalité FIFO,
 * rétrogradation de l'élu, héritage et restitution de priorité, mode
 * « et si » (override), propriété des ressources.
 *
 * Priorités QNX : 0 = plus haute, 255 = plus basse.
 */
#include "replique/sched.h"
#include "replique/types.h"
#include "test_util.h"

static replique_sched g_sched;

/* Thread prêt : le modèle ne connaît que l'état, on le pose donc à la main. */
static replique_thread *add_ready(replique_sched *s, uint32_t tid, uint32_t pid,
                                  uint32_t prio, uint64_t ready_ts)
{
  replique_thread *t = replique_sched_intern(s, tid, pid);

  t->alive = 1u;
  t->state = REPLIQUE_TH_READY;
  t->base_prio = prio;
  t->eff_prio = prio;
  t->ready_ts = ready_ts;
  return t;
}

static void test_config_default(void)
{
  replique_sched_config cfg;

  replique_sched_config_default(&cfg);
  TU_EQ(cfg.n_cpus, 1u, "n_cpus par défaut = 1");
  TU_EQ(cfg.inheritance, 1u, "héritage actif par défaut");
  TU_EQ(cfg.rr_quantum, 0u, "pas de préemption par quantum");
  TU_EQ(cfg.n_overrides, 0u, "aucun override");
}

static void test_intern_find(void)
{
  replique_thread *first;
  replique_thread *again;

  replique_sched_init(&g_sched, NULL);
  TU_EQ(g_sched.n_threads, 0u, "aucun thread au départ");
  TU_CHECK(replique_sched_find(&g_sched, 7u) == NULL, "find : absent -> NULL");
  first = replique_sched_intern(&g_sched, 7u, 3u);
  TU_CHECK(first != NULL, "intern : thread créé");
  TU_EQ(g_sched.n_threads, 1u, "un seul thread");
  TU_EQ(first->tid, 7u, "tid posé");
  TU_EQ(first->pid, 3u, "pid posé");
  TU_CHECK(replique_sched_find(&g_sched, 7u) == first, "find : même pointeur");
  again = replique_sched_intern(&g_sched, 7u, 3u);
  TU_CHECK(again == first, "intern : pas de doublon");
  TU_EQ(g_sched.n_threads, 1u, "toujours un seul thread");
  TU_EQ(first->base_prio, REPLIQUE_PRIO_LEVELS - 1u, "priorité par défaut basse");
}

static void test_pick_by_priority(void)
{
  replique_sched_init(&g_sched, NULL);
  add_ready(&g_sched, 1u, 1u, 30u, 1000u);
  add_ready(&g_sched, 2u, 1u, 10u, 2000u);
  add_ready(&g_sched, 3u, 1u, 20u, 3000u);
  TU_EQ(replique_sched_pick(&g_sched, 4000u), 2u,
        "élection : la plus petite priorité (0 = plus haute) gagne");
  g_sched.threads[1].eff_prio = 5u;
  TU_EQ(replique_sched_pick(&g_sched, 4000u), 2u,
        "élection : priorité effective la plus basse en nombre");
  /* tid 3 (threads[2]) sort du jeu : le plus prioritaire restant gagne. */
  g_sched.threads[2].alive = 0u;
  TU_EQ(replique_sched_pick(&g_sched, 4000u), 2u,
        "élection : un thread mort n'est pas candidat");
  g_sched.threads[2].alive = 1u;
  g_sched.threads[2].state = REPLIQUE_TH_BLOCKED;
  TU_EQ(replique_sched_pick(&g_sched, 4000u), 2u,
        "élection : un thread bloqué n'est pas candidat");
  g_sched.threads[1].alive = 0u;
  TU_EQ(replique_sched_pick(&g_sched, 4000u), 1u,
        "élection : reste le thread vivant le plus prioritaire");
}

static void test_pick_fifo_ties(void)
{
  replique_sched_init(&g_sched, NULL);
  add_ready(&g_sched, 20u, 1u, 10u, 5000u);
  add_ready(&g_sched, 10u, 1u, 10u, 1000u);
  TU_EQ(replique_sched_pick(&g_sched, 6000u), 10u,
        "égalité FIFO : ready_ts le plus ancien d'abord");
  g_sched.threads[0].ready_ts = g_sched.threads[1].ready_ts;
  TU_EQ(replique_sched_pick(&g_sched, 6000u), 10u,
        "égalité FIFO : ready_ts égal -> plus petit tid");
}

static void test_elect_demotes_previous(void)
{
  replique_thread *first;
  replique_thread *second;

  replique_sched_init(&g_sched, NULL);
  first = add_ready(&g_sched, 1u, 1u, 10u, 1000u);
  second = add_ready(&g_sched, 2u, 1u, 20u, 2000u);
  replique_sched_elect(&g_sched, 1u, 1000u);
  TU_EQ(first->state, REPLIQUE_TH_RUNNING, "élu : RUNNING");
  TU_EQ(g_sched.running_tid, 1u, "running_tid");
  replique_sched_elect(&g_sched, 2u, 2000u);
  TU_EQ(first->state, REPLIQUE_TH_READY, "élu précédent rétrogradé en READY");
  TU_EQ(first->ready_ts, 2000u, "ready_ts de l'élu précédent = instant");
  TU_EQ(second->state, REPLIQUE_TH_RUNNING, "nouvel élu : RUNNING");
  TU_EQ(g_sched.running_tid, 2u, "running_tid mis à jour");
}

static void test_inherit_and_release(void)
{
  replique_sched_config cfg;
  replique_thread *holder;
  replique_thread *waiter;

  replique_sched_config_default(&cfg);
  replique_sched_init(&g_sched, &cfg);
  holder = add_ready(&g_sched, 10u, 1u, 100u, 1000u);
  waiter = add_ready(&g_sched, 11u, 1u, 10u, 1000u);
  replique_sched_inherit(&g_sched, 10u, 11u);
  TU_EQ(holder->eff_prio, 10u, "héritage : le détenteur prend la priorité haute");
  TU_EQ(waiter->eff_prio, 10u, "héritage : le demandeur garde sa priorité");
  replique_sched_release(&g_sched, 10u);
  TU_EQ(holder->eff_prio, 100u, "restitution : retour à la priorité de base");
  waiter->state = REPLIQUE_TH_BLOCKED;
  waiter->holder = 10u;
  replique_sched_inherit(&g_sched, 10u, 11u);
  replique_sched_release(&g_sched, 10u);
  TU_EQ(holder->eff_prio, 10u, "restitution : priorité conservée (attente en cours)");
  cfg.inheritance = 0u;
  replique_sched_init(&g_sched, &cfg);
  holder = add_ready(&g_sched, 10u, 1u, 100u, 1000u);
  add_ready(&g_sched, 11u, 1u, 10u, 1000u);
  replique_sched_inherit(&g_sched, 10u, 11u);
  TU_EQ(holder->eff_prio, 100u, "héritage désactivé : rien n'est transmis");
}

static void test_override_prio(void)
{
  replique_sched_config cfg;

  replique_sched_config_default(&cfg);
  cfg.n_overrides = 2u;
  cfg.overrides[0].tid = 7u;
  cfg.overrides[0].prio = 3u;
  cfg.overrides[1].tid = 9u;
  cfg.overrides[1].prio = 42u;
  TU_EQ(replique_sched_override_prio(&cfg, 7u, 100u), 3u,
        "override : priorité de base remplacée");
  TU_EQ(replique_sched_override_prio(&cfg, 9u, 100u), 42u,
        "override : second thread");
  TU_EQ(replique_sched_override_prio(&cfg, 8u, 100u), 100u,
        "override : thread absent -> priorité d'origine");
  TU_EQ(replique_sched_override_prio(NULL, 7u, 100u), 100u,
        "override : configuration absente -> priorité d'origine");
}

static void test_resources(void)
{
  replique_sched_init(&g_sched, NULL);
  TU_EQ(replique_sched_owner(&g_sched, 16u), 0u, "ressource libre : owner 0");
  replique_sched_take(&g_sched, 16u, 101u);
  TU_EQ(replique_sched_owner(&g_sched, 16u), 101u, "prise : owner = détenteur");
  TU_EQ(g_sched.n_resources, 1u, "une ressource");
  replique_sched_take(&g_sched, 32u, 102u);
  TU_EQ(replique_sched_owner(&g_sched, 32u), 102u, "prise : seconde ressource");
  TU_EQ(g_sched.n_resources, 2u, "deux ressources");
  replique_sched_take(&g_sched, 16u, 102u);
  TU_EQ(replique_sched_owner(&g_sched, 16u), 102u, "prise : détenteur mis à jour");
  TU_EQ(g_sched.n_resources, 2u, "toujours deux ressources");
  replique_sched_drop(&g_sched, 16u);
  TU_EQ(replique_sched_owner(&g_sched, 16u), 0u, "libération : ressource libre");
  TU_EQ(g_sched.n_resources, 1u, "une ressource restante");
  TU_EQ(replique_sched_owner(&g_sched, 32u), 102u, "l'autre ressource intacte");
}

int main(void)
{
  test_config_default();
  test_intern_find();
  test_pick_by_priority();
  test_pick_fifo_ties();
  test_elect_demotes_previous();
  test_inherit_and_release();
  test_override_prio();
  test_resources();
  return tu_report("sched");
}
