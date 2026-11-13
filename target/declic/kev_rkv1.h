/* kev_rkv1.h — décodeur .kev QNX 8.0 -> conteneur RKV1 (C11 pur, sans QNX).
 *
 * Jumeau C de host/replique/kev_reader.py + kev2rkv1.py : même lecture binaire
 * (format de sys/trace.h), même mapping RKV1, mêmes octets en sortie. Utilisable
 * sur la cible (Déclic) pour archiver directement du RKV1, ou sur l'hôte.
 */
#ifndef REPLIQUE_KEV_RKV1_H
#define REPLIQUE_KEV_RKV1_H

#include <stddef.h>

/* Décode `kev[0..len)` et alloue un buffer RKV1 dans *out (à libérer par free).
 * Rend 0 en cas de succès, une valeur négative sinon. */
int kev_rkv1_convert(const unsigned char *kev, size_t len,
                     unsigned char **out, size_t *outlen);

/* Lit un fichier .kev et écrit un .rkv1. Rend 0 si succès. */
int kev_rkv1_file(const char *in_path, const char *out_path);

#endif