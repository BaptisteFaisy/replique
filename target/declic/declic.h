#ifndef DECLIC_H
#define DECLIC_H
/* declic.h — interface de la logique PORTABLE de Déclic.
 *
 * Règle de portabilité (docs/DESIGN.md §2) : C11 pur, aucun en-tête QNX,
 * aucune horloge, aucun fichier, aucun socket. L'appelant (la colle QNX
 * target/declic/declic_qnx.c, ou le test PC core/tests/declic_test.c)
 * fournit TOUTES les données : blobs, horodatage, nom d'archive.
 *
 * Contenu :
 *   (1) le conteneur d'archive « RDA1 » (Réplique Déclic Archive v1) ;
 *   (2) le manifeste (ordre de clés figé) ;
 *   (3) la décision de déclenchement (événement de terminaison + politique).
 *
 * Le SHA-256 vit dans sha256_portable.h (une seule implémentation).
 */
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* (1) Conteneur d'archive RDA1                                         */
/* ------------------------------------------------------------------ */

/* En-tête (32 octets, petit-boutien) :
 *   0  4  magic    ASCII "RDA1" (sans octet nul)
 *   4  4  version  1
 *   8  4  n_sections
 *  12  4  table_offset  (toujours DECLIC_RDA1_HEADER_SIZE)
 *  16  4  entry_size    (toujours DECLIC_RDA1_ENTRY_SIZE)
 *  20  4  flags         (réservé, 0 en v1)
 *  24  8  total_size    (taille totale de l'archive, en octets)
 *
 * Table des sections (n_sections entrées de DECLIC_RDA1_ENTRY_SIZE octets) :
 *   0  32  name   ASCII, complété par des '\0'
 *  32   8  offset (absolu, depuis le début de l'archive)
 *  40   8  length (octets)
 *  48  32  sha256 (empreinte brute du bloc)
 *
 * Puis les blocs, dans l'ordre de la table. */
#define DECLIC_RDA1_MAGIC "RDA1"
#define DECLIC_RDA1_VERSION 1u
#define DECLIC_RDA1_HEADER_SIZE 32u
#define DECLIC_RDA1_ENTRY_SIZE 80u
#define DECLIC_RDA1_NAME_MAX 32u  /* taille du champ nom (31 utiles + '\0') */
#define DECLIC_RDA1_MAX_SECTIONS 8u

/* Noms de sections (ordre figé de l'archive standard). */
#define DECLIC_SECTION_TRACE "trace"      /* trace noyau brute (.kev) */
#define DECLIC_SECTION_RKV1 "rkv1"        /* trace convertie en RKV1 (E1) */
#define DECLIC_SECTION_PIDIN "pidin"      /* sortie de `pidin` */
#define DECLIC_SECTION_SLOG2 "slog2"      /* sortie de `slog2info` */
#define DECLIC_SECTION_CORE "core"        /* core dump (dumper) */
#define DECLIC_SECTION_MANIFEST "manifest" /* manifeste texte */

/* Codes d'erreur (négatifs). */
enum declic_status {
  DECLIC_OK = 0,
  DECLIC_ERR_ARG = -1,        /* argument nul ou incohérent */
  DECLIC_ERR_TRUNCATED = -2,   /* archive trop courte / section hors bornes */
  DECLIC_ERR_MAGIC = -3,       /* magic != "RDA1" */
  DECLIC_ERR_VERSION = -4,     /* version != 1 */
  DECLIC_ERR_SHA = -5,         /* empreinte d'une section non conforme */
  DECLIC_ERR_NOT_FOUND = -6,   /* section inconnue */
  DECLIC_ERR_OVERFLOW = -7,    /* tampon de sortie trop petit */
  DECLIC_ERR_FORMAT = -8       /* structure d'archive illisible */
};

/* Nom canonique d'un code d'erreur (pour les messages de la colle QNX). */
const char *declic_status_name(int status);

/* Une section à écrire dans l'archive. */
typedef struct declic_section {
  char name[DECLIC_RDA1_NAME_MAX + 1u];
  const uint8_t *data;
  size_t len;
} declic_section;

/* Les cinq artefacts de l'archive standard, dans l'ordre figé
 * trace → pidin → slog2 → core → manifeste. */
typedef struct declic_artifacts {
  const uint8_t *trace;
  size_t trace_len;
  const uint8_t *rkv1;      /* trace convertie en conteneur RKV1 (facultatif) */
  size_t rkv1_len;
  const uint8_t *pidin;
  size_t pidin_len;
  const uint8_t *slog2;
  size_t slog2_len;
  const uint8_t *core;
  size_t core_len;
  const char *manifest;
  size_t manifest_len;
} declic_artifacts;

/* Vue en lecture d'une archive ouverte. */
typedef struct declic_archive_view {
  const uint8_t *buf;
  size_t len;
  uint32_t version;
  uint32_t n_sections;
} declic_archive_view;

/* Métadonnées d'une section (nom, décalage, longueur, empreinte). */
typedef struct declic_section_info {
  char name[DECLIC_RDA1_NAME_MAX + 1u];
  uint64_t offset;
  uint64_t length;
  uint8_t sha256[32];
} declic_section_info;

/* Construction : écrit l'archive complète dans out (cap octets).
 * *out_len reçoit la taille écrite. Retour DECLIC_OK ou un code négatif. */
int declic_archive_build_sections(const declic_section *secs, size_t n_sections,
                                  uint8_t *out, size_t cap, size_t *out_len);

/* Construction standard : les cinq artefacts, dans l'ordre figé. */
int declic_archive_build(const declic_artifacts *art, uint8_t *out, size_t cap,
                          size_t *out_len);

/* Ouverture : valide magic, version, table et bornes des sections. */
int declic_archive_open(const uint8_t *buf, size_t len,
                         declic_archive_view *view);

/* Métadonnées de la section d'index `index` (0-based). */
int declic_archive_section(const declic_archive_view *view, size_t index,
                            declic_section_info *info);

/* Métadonnées de la section nommée `name`. */
int declic_archive_find(const declic_archive_view *view, const char *name,
                         declic_section_info *info);

/* Vérification : recalcule l'empreinte d'une section et la compare. */
int declic_archive_verify(const declic_archive_view *view, size_t index);

/* Vérification de toutes les sections. */
int declic_archive_verify_all(const declic_archive_view *view);

/* ------------------------------------------------------------------ */
/* (2) Manifeste                                                       */
/* ------------------------------------------------------------------ */

/* Schéma du manifeste (valeur de la clé `schema`). */
#define DECLIC_MANIFEST_SCHEMA "rda1/1"

/* Taille maximale du manifeste rendu (octets, '\0' final compris). */
#define DECLIC_MANIFEST_MAX 512u

/* Données du manifeste. L'horodatage et le nom d'archive sont FOURNIS par
 * l'appelant : cette couche ne consulte jamais d'horloge. */
typedef struct declic_manifest {
  char archive_name[64];
  uint64_t timestamp;   /* unité = ticks (ns) fournis par l'appelant */
  uint32_t pid;         /* processusterminé */
  uint32_t status;      /* code de sortie */
  uint32_t signal;      /* signal terminant, 0 = aucun */
} declic_manifest;

/* Rend le manifeste en texte : ordre de clés FIGÉ, une clé par ligne,
 * chaque ligne terminée par '\n'. *out_len reçoit la longueur écrite
 * (sans le '\0'). */
int declic_manifest_render(const declic_manifest *m, char *out, size_t cap,
                            size_t *out_len);

/* ------------------------------------------------------------------ */
/* (3) Décision de déclenchement                                       */
/* ------------------------------------------------------------------ */

#define DECLIC_MAX_WATCH 32u
#define DECLIC_MAX_SIGNALS 16u

/* Événement de terminaison observé sur la cible. */
typedef struct declic_term_event {
  uint32_t pid;
  uint32_t status;  /* code de sortie du processus */
  uint32_t signal;  /* signal qui a tué le processus, 0 = aucun */
} declic_term_event;

/* Politique de capture. Règles (dans cet ordre) :
 *   1. un pid exclu n'est jamais capturé (l'exclusion l'emporte) ;
 *   2. si `watch_all` est nul, le pid doit figurer dans `watch` ;
 *   3. si `n_signals` > 0, le signal doit figurer dans `signals` ;
 *   4. sinon : capturer. */
typedef struct declic_policy {
  uint32_t watch_all;        /* 1 = surveiller tous les pid sauf exclus */
  uint32_t n_watch;
  uint32_t watch[DECLIC_MAX_WATCH];
  uint32_t n_signals;
  uint32_t signals[DECLIC_MAX_SIGNALS];
  uint32_t n_exclusions;
  uint32_t exclusions[DECLIC_MAX_WATCH];
} declic_policy;

/* Politique par défaut : tout le monde, tous les signaux, aucune exclusion. */
void declic_policy_init(declic_policy *pol);

/* Décide de capturer ou non.
 * Retour : 1 = capturer, 0 = ne pas capturer, < 0 = erreur d'argument. */
int declic_should_capture(const declic_term_event *ev,
                          const declic_policy *pol);

/* ------------------------------------------------------------------ */
/* Divers (colle QNX)                                                  */
/* ------------------------------------------------------------------ */

/* Nom du canal de notification utilisé par les crochets (target/hook) pour
 * prévenir le démon Déclic d'une terminaison (voir target/hook/README.md). */
#define DECLIC_TRIGGER_CHANNEL "/dev/declic.trigger"

/* Message de notification crochet → démon (transporté par le canal ci-dessus).
 * Structure pure C : aucun type QNX, donc réutilisable par les deux côtés.
 * Le crochet l'envoie à l'instant de la faute ; le démon y lit le pid, le
 * statut et le signal. */
#define DECLIC_TRIGGER_MAGIC 0x44434c54u /* "DCLT" */
typedef struct declic_trigger_msg {
  uint32_t magic;      /* DECLIC_TRIGGER_MAGIC */
  uint32_t pid;        /* processus qui termine */
  uint32_t status;     /* code de sortie */
  uint32_t signal;     /* signal terminant, 0 = aucun */
  uint64_t timestamp;  /* ticks, fournis par l'émetteur */
} declic_trigger_msg;

#endif
