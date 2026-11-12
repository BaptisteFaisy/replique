/* event.c — tables de noms gelées (CONTRACT.md §1.4, §2, §3.1). Aucune logique
 * ici : la sortie texte canonique en dépend, elle doit rester identique au
 * jumeau Python (host/replique/event.py). */
#include "replique/event.h"
#include "replique/types.h"

static const char *const CLASS_NAMES[] = {
  "NONE", "THREAD_STATE", "PROCESS_CREATE", "PROCESS_DESTROY", "THREAD_CREATE",
  "THREAD_DESTROY", "MSG_SEND", "MSG_RECEIVE", "MSG_REPLY", "MSG_ERROR",
  "MUTEX_LOCK", "MUTEX_UNLOCK", "SEM_WAIT", "SEM_POST", "TIMER", "INTERRUPT",
  "SCHED", "PRIO_CHANGE"
};

static const char *const STATE_NAMES[] = {
  "DEAD", "RUNNING", "READY", "BLOCKED", "STOPPED"
};

static const char *const BLOCK_REASON_NAMES[] = {
  "UNKNOWN", "MUTEX", "MSGSEND", "SEMAPHORE", "TIMER", "INTERRUPT",
  "REPLYWAIT", "JOIN"
};

static const char *const SCHED_REASON_NAMES[] = {
  "INITIAL", "PREEMPT", "BLOCK", "UNBLOCK", "YIELD", "TIMER", "TERMINATE",
  "WHATIF"
};

static const char *const VERDICT_NAMES[] = {
  "OK", "PARSE_ERROR", "DEADLOCK_FOUND", "CULPRIT", "PRIO_INVERSION",
  "REPLAY_DETERMINISTIC", "REPLAY_DIVERGED", "TRACE_INCOMPLETE",
  "CORRELATION_OK", "WHATIF"
};

static const char *const SEVERITY_NAMES[] = { "info", "warning", "error" };

/* Sévérités gelées (CONTRACT.md §3.1) — l'index est le code du verdict. */
static const unsigned VERDICT_SEVERITY[] = {
  REPLIQUE_SEV_INFO, REPLIQUE_SEV_ERROR, REPLIQUE_SEV_ERROR, REPLIQUE_SEV_ERROR,
  REPLIQUE_SEV_WARNING, REPLIQUE_SEV_INFO, REPLIQUE_SEV_ERROR,
  REPLIQUE_SEV_WARNING, REPLIQUE_SEV_INFO, REPLIQUE_SEV_INFO
};

static const char *const CAUSE_NAMES[] = {
  "", "short-header", "bad-magic", "bad-version", "bad-length", "bad-crc",
  "bad-class", "ts-not-monotonic", "bad-flags"
};

#define NAME_OF(table, index, max_index) \
  ((index) <= (unsigned)(max_index) ? (table)[(index)] : "UNKNOWN")

const char *replique_class_name(unsigned cls)
{
  return NAME_OF(CLASS_NAMES, cls, REPLIQUE_EV_CLASS_MAX);
}

const char *replique_state_name(unsigned st)
{
  return NAME_OF(STATE_NAMES, st, REPLIQUE_TH_STATE_MAX);
}

const char *replique_block_reason_name(unsigned br)
{
  return NAME_OF(BLOCK_REASON_NAMES, br, REPLIQUE_BR_JOIN);
}

const char *replique_sched_reason_name(unsigned sr)
{
  return NAME_OF(SCHED_REASON_NAMES, sr, REPLIQUE_SR_MAX);
}

const char *replique_verdict_name(unsigned code)
{
  return NAME_OF(VERDICT_NAMES, code, REPLIQUE_V_MAX);
}

const char *replique_severity_name(unsigned sev)
{
  return NAME_OF(SEVERITY_NAMES, sev, REPLIQUE_SEV_ERROR);
}

const char *replique_cause_name(unsigned cause)
{
  return NAME_OF(CAUSE_NAMES, cause, REPLIQUE_CAUSE_BAD_FLAGS);
}

unsigned replique_verdict_severity(unsigned code)
{
  if (code > (unsigned)REPLIQUE_V_MAX) {
    return REPLIQUE_SEV_INFO;
  }
  return VERDICT_SEVERITY[code];
}
