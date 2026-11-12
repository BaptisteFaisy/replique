#ifndef REPLIQUE_TYPES_H
#define REPLIQUE_TYPES_H
/* types.h — structures gelées du contrat (CONTRACT.md). C11 pur :
 * aucune horloge, aucun fichier, aucun socket, aucun en-tête QNX. */
#include <stddef.h>
#include <stdint.h>
#include "event.h"

#define REPLIQUE_MAGIC "RKV1"
#define REPLIQUE_MAGIC_LEN 4
#define REPLIQUE_VERSION 1u
#define REPLIQUE_HEADER_SIZE 32u
#define REPLIQUE_RECORD_SIZE 32u
#define REPLIQUE_TIME_UNIT_NS 1000000000u
#define REPLIQUE_FLAG_TRUNCATED 1u
#define REPLIQUE_FLAG_SYNTHETIC 2u
#define REPLIQUE_FLAG_RESERVED_MASK 0xFFFFFFFCu

#define REPLIQUE_MAX_VERDICTS 128
#define REPLIQUE_MAX_EDGES 512
#define REPLIQUE_MAX_DECISIONS 4096
#define REPLIQUE_MAX_EVENTS 8192
#define REPLIQUE_MAX_OVERRIDES 16
#define REPLIQUE_MAX_CHAIN 32
#define REPLIQUE_DETAIL_MAX 64
#define REPLIQUE_PRIO_LEVELS 256u

enum replique_status {
  REPLIQUE_STATUS_OK = 0,
  REPLIQUE_ERR_ARG = -1,
  REPLIQUE_ERR_TRUNCATED = -2,
  REPLIQUE_ERR_MAGIC = -3,
  REPLIQUE_ERR_VERSION = -4,
  REPLIQUE_ERR_CRC = -5,
  REPLIQUE_ERR_ORDER = -6,
  REPLIQUE_ERR_CLASS = -7,
  REPLIQUE_ERR_OVERFLOW = -8,
  REPLIQUE_ERR_FLAGS = -9,
  REPLIQUE_ERR_NOMEM = -10
};

/* Cause gelée (CONTRACT.md §1.4) — imprimée dans le champ detail. */
enum replique_parse_cause {
  REPLIQUE_CAUSE_NONE = 0,
  REPLIQUE_CAUSE_SHORT_HEADER,   /* "short-header" */
  REPLIQUE_CAUSE_BAD_MAGIC,      /* "bad-magic" */
  REPLIQUE_CAUSE_BAD_VERSION,    /* "bad-version" */
  REPLIQUE_CAUSE_BAD_LENGTH,     /* "bad-length" */
  REPLIQUE_CAUSE_BAD_CRC,        /* "bad-crc" */
  REPLIQUE_CAUSE_BAD_CLASS,      /* "bad-class" */
  REPLIQUE_CAUSE_TS_MONOTONIC,   /* "ts-not-monotonic" */
  REPLIQUE_CAUSE_BAD_FLAGS       /* "bad-flags" */
};
const char *replique_cause_name(unsigned cause);

enum replique_verdict_code {
  REPLIQUE_V_OK = 0,
  REPLIQUE_V_PARSE_ERROR = 1,
  REPLIQUE_V_DEADLOCK_FOUND = 2,
  REPLIQUE_V_CULPRIT = 3,
  REPLIQUE_V_PRIO_INVERSION = 4,
  REPLIQUE_V_REPLAY_DETERMINISTIC = 5,
  REPLIQUE_V_REPLAY_DIVERGED = 6,
  REPLIQUE_V_TRACE_INCOMPLETE = 7,
  REPLIQUE_V_CORRELATION_OK = 8,
  REPLIQUE_V_WHATIF = 9,
  REPLIQUE_V_MAX = 9
};

enum replique_severity {
  REPLIQUE_SEV_INFO = 0,
  REPLIQUE_SEV_WARNING = 1,
  REPLIQUE_SEV_ERROR = 2
};

const char *replique_verdict_name(unsigned code);
unsigned replique_verdict_severity(unsigned code);
const char *replique_severity_name(unsigned sev);

typedef struct replique_header {
  uint32_t version;
  uint32_t flags;
  uint32_t n_events;
  uint32_t time_unit;
  uint32_t crc32;
  uint32_t reserved0;
  uint32_t reserved1;
} replique_header;

/* 32 octets sur le disque (CONTRACT.md §1.3) ; rempli champ par champ. */
typedef struct replique_event {
  uint64_t ts;
  uint32_t pid;
  uint32_t tid;
  uint16_t cls;      /* enum replique_event_class (canonique : "class") */
  uint16_t subcode;
  uint32_t a;
  uint32_t b;
  uint32_t c;
} replique_event;

typedef struct replique_verdict {
  uint32_t code;
  uint32_t sev;
  uint64_t ts;
  uint32_t pid;
  uint32_t tid;
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint64_t hash;
  uint32_t chain_len;
  uint32_t chain[REPLIQUE_MAX_CHAIN];
  char detail[REPLIQUE_DETAIL_MAX];
} replique_verdict;

typedef struct replique_edge {
  uint32_t from_tid;
  uint32_t to_tid;
  uint32_t kind;      /* enum replique_block_reason */
  uint32_t resource;  /* id mutex / canal / sémaphore */
  uint64_t ts;
} replique_edge;

typedef struct replique_decision {
  uint64_t ts;
  uint32_t tid;
  uint32_t reason;    /* enum replique_sched_reason */
} replique_decision;

typedef struct replique_prio_override {
  uint32_t tid;
  uint32_t prio;
} replique_prio_override;

typedef struct replique_sched_config {
  uint32_t n_cpus;          /* v1 : 1 (CONTRACT.md §5) */
  uint32_t inheritance;     /* 1 = héritage de priorité actif */
  uint32_t rr_quantum;      /* ticks, 0 = pas de préemption par quantum */
  uint32_t n_overrides;
  replique_prio_override overrides[REPLIQUE_MAX_OVERRIDES];
} replique_sched_config;

void replique_sched_config_default(replique_sched_config *cfg);

typedef struct replique_replay_result {
  int status;               /* enum replique_status */
  replique_decision *decisions;  /* tampon fourni par l'appelant */
  size_t n_decisions;
  size_t cap;
  uint64_t hash;            /* FNV-1a 64 (CONTRACT.md §6) */
  uint32_t diverged;
  uint64_t diverge_ts;
  uint32_t diverge_tid;
  uint32_t expected_reason;
  uint32_t found_reason;
  uint32_t incomplete;      /* 1 = modèle non couvert (ex. SPORADIC) */
  char incomplete_detail[REPLIQUE_DETAIL_MAX];
} replique_replay_result;
#endif
