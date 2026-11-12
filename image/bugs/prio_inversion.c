/* Construit avec qcc (SDP 8.0) dans l'image invitée ; jamais compilé par make test. */
/* image/bugs/prio_inversion.c — scénario « prio-inversion » de
 * scenarios/SPEC.md §3 (variante SANS héritage de priorité).
 *
 * Séquence gelée du scénario :
 *   tid 202 (priorité 100, basse) acquiert le mutex 48 ;
 *   tid 201 (priorité 10, haute) tente le mutex 48 → bloqué ;
 *   tid 203 (priorité 50, intermédiaire) devient prêt puis RUNNING
 *   (préemption) PENDANT que tid 201 est bloqué : le thread intermédiaire
 *   vole le CPU au thread de haute priorité.
 *
 * Aucun PRIO_CHANGE d'héritage ne doit apparaître dans la trace : le mutex
 * est un « normal » (sans PTHREAD_PRIO_INHERIT), conformément à SPEC.md §3
 * (« aucun PRIO_CHANGE d'héritage dans la trace »).
 *
 * À VÉRIFIER SUR LE SDP (E3) : que QNX n'applique pas d'héritage
 * implicite sur un mutex pthread « normal ».
 */
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PRIO_HIGH 10 /* tid 201 */
#define PRIO_LOW 100 /* tid 202 */
#define PRIO_MID 50  /* tid 203 */
#define MUTEX 48     /* 0x30 */
#define WORK_MS 400  /* durée pendant laquelle le thread intermédiaire tourne */

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t sync_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sync_cond = PTHREAD_COND_INITIALIZER;
static int low_holds;

static void set_priority(int prio)
{
  struct sched_param sp;
  memset(&sp, 0, sizeof sp);
  sp.sched_priority = prio;
  (void)pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
}


static void sleep_ns(long ns)
{
  struct timespec ts;
  ts.tv_sec = ns / 1000000000L;
  ts.tv_nsec = ns % 1000000000L;
  (void)nanosleep(&ts, NULL);
}

/* tid 202 (basse) : prend le mutex et le garde. */
static void *thread_low(void *arg)
{
  (void)arg;
  set_priority(PRIO_LOW);
  pthread_mutex_lock(&m);
  pthread_mutex_lock(&sync_lock);
  low_holds = 1;
  pthread_cond_broadcast(&sync_cond);
  pthread_mutex_unlock(&sync_lock);
  printf("bug: tid 202 (prio %d) tient le mutex %d\n", PRIO_LOW, MUTEX);
  for (;;) {
    sleep_ns(50000000L); /* travaille en gardant le mutex */
  }
  return NULL;
}

/* tid 201 (haute) : se bloque sur le mutex détenu par tid 202. */
static void *thread_high(void *arg)
{
  (void)arg;
  set_priority(PRIO_HIGH);
  pthread_mutex_lock(&sync_lock);
  while (!low_holds) {
    pthread_cond_wait(&sync_cond, &sync_lock);
  }
  pthread_mutex_unlock(&sync_lock);
  printf("bug: tid 201 (prio %d) veut le mutex %d → bloque\n", PRIO_HIGH, MUTEX);
  pthread_mutex_lock(&m);
  printf("bug: tid 201 a obtenu le mutex (inversion resolue ?)\n");
  return NULL;
}

/* tid 203 (intermédiaire) : préempte le thread basse pendant le blocage. */
static void *thread_mid(void *arg)
{
  long elapsed = 0;
  (void)arg;
  set_priority(PRIO_MID);
  printf("bug: tid 203 (prio %d) tourne pendant que 201 est bloque\n", PRIO_MID);
  while (elapsed < WORK_MS) {
    sleep_ns(1000000L); /* 1 ms de travail, en boucle */
    elapsed += 1;
  }
  printf("bug: tid 203 a fini son travail (inversion constatee)\n");
  return NULL;
}

int main(void)
{
  pthread_t low;
  pthread_t high;
  pthread_t mid;
  printf("bug: prio_inversion (scenarios/SPEC.md §3) — demarrage\n");
  pthread_create(&low, NULL, thread_low, NULL);
  pthread_create(&high, NULL, thread_high, NULL);
  pthread_create(&mid, NULL, thread_mid, NULL);
  pthread_join(mid, NULL);
  pthread_join(high, NULL);
  pthread_join(low, NULL);
  return 0;
}
