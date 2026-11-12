/* Construit avec qcc (SDP 8.0) dans l'image invitée ; jamais compilé par make test. */
/* image/bugs/deadlock.c — scénario « deadlock » de scenarios/SPEC.md §2.
 *
 * Reproduit EXACTEMENT la séquence gelée du scénario de référence :
 *   tid 102 (priorité de base 60) prend le mutex 32 PUIS se bloque sur une
 *   temporisation ; tid 101 (priorité de base 50) devient prêt, préempte,
 *   prend le mutex 16 ; la temporisation de 102 échoit (102 redevient prêt
 *   sans préempter 101) ; 101 tente le mutex 32 → bloqué ; 102 tente le
 *   mutex 16 → bloqué. Plus aucun thread prêt : l'interblocage est
 *   constaté par Déclic (aucun PROCESS_DESTROY dans la trace).
 *
 * Les priorités QNX suivent l'ordre gelé du projet : 0 = plus haute.
 * Le mutex est un mutex pthread « normal » (sans héritage de priorité) :
 * c'est le scénario « deadlock » de SPEC.md §2, pas la variante avec
 * héritage.
 *
 * À VÉRIFIER SUR LE SDP (E3) : la granularité du quanta d'ordonnancement
 * observée dans la trace (l'ordre des événements doit correspondre à la
 * séquence gelée).
 */
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PRIO_TID_101 50 /* pthread_attr_setschedparam : plus petit = plus haut */
#define PRIO_TID_102 60
#define MUTEX_A 16 /* 0x10, Held par tid 101 */
#define MUTEX_B 32 /* 0x20, held par tid 102 */
#define TIMEOUT_NS 200000000L /* 200 ms : temporisation de tid 102 */

static pthread_mutex_t m_a = PTHREAD_MUTEX_INITIALIZER; /* mutex 16 */
static pthread_mutex_t m_b = PTHREAD_MUTEX_INITIALIZER; /* mutex 32 */
static pthread_mutex_t sync_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sync_cond = PTHREAD_COND_INITIALIZER;
static int b_held;

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

/* tid 102 : prend le mutex 32, attend la temporisation, puis veut le 16. */
static void *thread_102(void *arg)
{
  (void)arg;
  set_priority(PRIO_TID_102);
  printf("bug: tid 102 (prio %d) prend le mutex %d\n", PRIO_TID_102, MUTEX_B);
  pthread_mutex_lock(&m_b);
  pthread_mutex_lock(&sync_lock);
  b_held = 1;
  pthread_cond_broadcast(&sync_cond);
  pthread_mutex_unlock(&sync_lock);
  sleep_ns(TIMEOUT_NS); /* la temporisation de 102 echoit */
  printf("bug: tid 102 veut le mutex %d (bloque)\n", MUTEX_A);
  pthread_mutex_lock(&m_a); /* interblocage : 101 le retient */
  return NULL;
}

/* tid 101 : attend que 102 tienne le mutex 32, prend le 16, puis veut le 32. */
static void *thread_101(void *arg)
{
  (void)arg;
  set_priority(PRIO_TID_101);
  pthread_mutex_lock(&sync_lock);
  while (!b_held) {
    pthread_cond_wait(&sync_cond, &sync_lock);
  }
  pthread_mutex_unlock(&sync_lock);
  printf("bug: tid 101 (prio %d) prend le mutex %d\n", PRIO_TID_101, MUTEX_A);
  pthread_mutex_lock(&m_a);
  printf("bug: tid 101 veut le mutex %d (bloque)\n", MUTEX_B);
  pthread_mutex_lock(&m_b); /* interblocage : 102 le retient */
  return NULL;
}

int main(void)
{
  pthread_t t102;
  pthread_t t101;
  printf("bug: deadlock (scenarios/SPEC.md §2) — demarrage\n");
  pthread_create(&t102, NULL, thread_102, NULL);
  sleep_ns(50000000L); /* 102 prend le mutex 32 avant que 101 ne soit pret */
  pthread_create(&t101, NULL, thread_101, NULL);
  /* Les deux joins ne reviendront jamais : c'est l'interblocage. */
  pthread_join(t101, NULL);
  pthread_join(t102, NULL);
  printf("bug: deadlock — fin inattendue (le bogue a disparu !)\n");
  return 0;
}
