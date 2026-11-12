/* Construit avec qcc (SDP 8.0) dans l'image invitée ; jamais compilé par make test. */
/* image/bugs/race.c — scénario « race » de scenarios/SPEC.md §4.
 *
 * Séquence gelée du scénario (deux PROCESSUS distincts : pid 40 et pid 41) :
 *   1. tid 401 (pid 40, priorité 30) acquiert le mutex 64, travaille, puis
 *      le libère (MUTEX_UNLOCK a=64) ;
 *   2. tid 402 (pid 41, priorité 30, même priorité, politique RR) devient
 *      READY, le quantum expire (TIMER) → tid 402 tourne ;
 *   3. tid 402 acquiert le mutex 64 puis se termine brutalement
 *      (PROCESS_DESTROY, b=11 = SIGSEGV) SANS libérer le mutex ;
 *   4. tid 401 tente le mutex 64 → bloqué sur un détenteur MORT : attente
 *      sans fin, mais AUCUN cycle (donc pas de DEADLOCK_FOUND).
 *
 * Le mutex est partagé entre deux processus (segment partagé + mutex
 * PTHREAD_PROCESS_SHARED) : c'est ce qui permet à un processus mort de
 * laisser un mutex verrouillé.
 *
 * À VÉRIFIER SUR LE SDP (E3/E5) : (a) le quantum d'ordonnancement réel en
 * politique RR (QNX règle le quantum d'horloge, pas 1000 ns : à ajuster) ;
 * (b) que QNX ne libère PAS automatiquement un mutex dont le propriétaire
 * est mort (sinon l'étape 4 ne bloque pas et le scénario n'est pas
 * reproduit) ; (c) que le core dump est bien produit par `dumper`.
 */
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PRIO_TID_401 30
#define PRIO_TID_402 30
#define MUTEX 64 /* 0x40 */

struct shared_area {
  pthread_mutex_t m;
  int unlocked;     /* tid 401 a libéré le mutex */
  int child_holds;  /* tid 402 tient le mutex */
};

static void set_priority(int prio, int policy)
{
  struct sched_param sp;
  memset(&sp, 0, sizeof sp);
  sp.sched_priority = prio;
  (void)pthread_setschedparam(pthread_self(), policy, &sp);
}


static void sleep_ns(long ns)
{
  struct timespec ts;
  ts.tv_sec = ns / 1000000000L;
  ts.tv_nsec = ns % 1000000000L;
  (void)nanosleep(&ts, NULL);
}

/* tid 402 (pid 41) : prend le mutex puis meurt brutalement en le gardant. */
static void child_body(struct shared_area *sh)
{
  set_priority(PRIO_TID_402, SCHED_RR);
  while (!sh->unlocked) {
    sleep_ns(1000000L);
  }
  printf("bug: tid 402 (prio %d) prend le mutex %d\n", PRIO_TID_402, MUTEX);
  pthread_mutex_lock(&sh->m);
  sh->child_holds = 1;
  printf("bug: tid 402 meurt (SIGSEGV) SANS liberer le mutex %d\n", MUTEX);
  (void)raise(SIGSEGV); /* terminaison brutale, comme dans SPEC.md §4 */
  _exit(1); /* atteint seulement si SIGSEGV est ignore */
}

/* tid 401 (pid 40) : prend puis libere le mutex, puis se bloque dessus. */
int main(void)
{
  struct shared_area *sh;
  pthread_mutexattr_t attr;
  pid_t child;
  printf("bug: race (scenarios/SPEC.md §4) — demarrage\n");
  sh = mmap(NULL, sizeof *sh, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON,
            -1, 0);
  if (sh == MAP_FAILED) {
    fprintf(stderr, "bug: mmap : %s\n", strerror(errno));
    return 1;
  }
  memset(sh, 0, sizeof *sh);
  pthread_mutexattr_init(&attr);
  (void)pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(&sh->m, &attr);
  set_priority(PRIO_TID_401, SCHED_RR);
  pthread_mutex_lock(&sh->m);
  printf("bug: tid 401 (prio %d) prend le mutex %d\n", PRIO_TID_401, MUTEX);
  sleep_ns(100000000L); /* travaille puis libere */
  pthread_mutex_unlock(&sh->m);
  sh->unlocked = 1;
  printf("bug: tid 401 libere le mutex %d\n", MUTEX);
  child = fork();
  if (child == 0) {
    child_body(sh);
    return 1;
  }
  if (child < 0) {
    fprintf(stderr, "bug: fork : %s\n", strerror(errno));
    return 1;
  }
  while (!sh->child_holds) {
    sleep_ns(1000000L);
  }
  (void)waitpid(child, NULL, 0);  /* tid 402 est mort */
  printf("bug: tid 401 veut le mutex %d → bloque sur un detenteur MORT\n",
         MUTEX);
  pthread_mutex_lock(&sh->m); /* attente sans fin : pas de cycle */
  printf("bug: tid 401 a obtenu le mutex (le mutex a ete libere ?)\n");
  return 0;
}
