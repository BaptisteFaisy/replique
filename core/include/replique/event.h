#ifndef REPLIQUE_EVENT_H
#define REPLIQUE_EVENT_H
/* event.h — tables gelées du contrat (CONTRACT.md §2). C11 pur, aucun I/O. */

enum replique_event_class {
  REPLIQUE_EV_NONE = 0,
  REPLIQUE_EV_THREAD_STATE = 1,
  REPLIQUE_EV_PROCESS_CREATE = 2,
  REPLIQUE_EV_PROCESS_DESTROY = 3,
  REPLIQUE_EV_THREAD_CREATE = 4,
  REPLIQUE_EV_THREAD_DESTROY = 5,
  REPLIQUE_EV_MSG_SEND = 6,
  REPLIQUE_EV_MSG_RECEIVE = 7,
  REPLIQUE_EV_MSG_REPLY = 8,
  REPLIQUE_EV_MSG_ERROR = 9,
  REPLIQUE_EV_MUTEX_LOCK = 10,
  REPLIQUE_EV_MUTEX_UNLOCK = 11,
  REPLIQUE_EV_SEM_WAIT = 12,
  REPLIQUE_EV_SEM_POST = 13,
  REPLIQUE_EV_TIMER = 14,
  REPLIQUE_EV_INTERRUPT = 15,
  REPLIQUE_EV_SCHED = 16,
  REPLIQUE_EV_PRIO_CHANGE = 17,
  REPLIQUE_EV_CLASS_MAX = 17
};

enum replique_thread_state {
  REPLIQUE_TH_DEAD = 0,
  REPLIQUE_TH_RUNNING = 1,
  REPLIQUE_TH_READY = 2,
  REPLIQUE_TH_BLOCKED = 3,
  REPLIQUE_TH_STOPPED = 4,
  REPLIQUE_TH_STATE_MAX = 4
};

enum replique_block_reason {
  REPLIQUE_BR_UNKNOWN = 0,
  REPLIQUE_BR_MUTEX = 1,
  REPLIQUE_BR_MSGSEND = 2,
  REPLIQUE_BR_SEMAPHORE = 3,
  REPLIQUE_BR_TIMER = 4,
  REPLIQUE_BR_INTERRUPT = 5,
  REPLIQUE_BR_REPLYWAIT = 6,
  REPLIQUE_BR_JOIN = 7
};

enum replique_sched_policy {
  REPLIQUE_SCHED_FIFO = 0,
  REPLIQUE_SCHED_RR = 1,
  REPLIQUE_SCHED_SPORADIC = 2
};

enum replique_sched_reason {
  REPLIQUE_SR_INITIAL = 0,
  REPLIQUE_SR_PREEMPT = 1,
  REPLIQUE_SR_BLOCK = 2,
  REPLIQUE_SR_UNBLOCK = 3,
  REPLIQUE_SR_YIELD = 4,
  REPLIQUE_SR_TIMER = 5,
  REPLIQUE_SR_TERMINATE = 6,
  REPLIQUE_SR_WHATIF = 7,
  REPLIQUE_SR_MAX = 7
};

/* Noms canoniques (CONTRACT.md §2.1, §2.6) — la sortie texte en dépend. */
const char *replique_class_name(unsigned cls);
const char *replique_state_name(unsigned st);
const char *replique_block_reason_name(unsigned br);
const char *replique_sched_reason_name(unsigned sr);
#endif
