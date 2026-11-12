#ifndef DECLIC_SHA256_PORTABLE_H
#define DECLIC_SHA256_PORTABLE_H
/* sha256_portable.h — SHA-256 maison, sans aucune dépendance (FIPS 180-4).
 * Utilisé par la seule implémentation du projet : target/declic/sha256_portable.c.
 * C11 pur : ni horloge, ni fichier, ni en-tête QNX. */
#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_SIZE 32u
#define SHA256_HEX_SIZE 65u /* 64 chiffres hexadécimaux + '\0' */

typedef struct sha256_ctx {
  uint32_t state[8];   /* H0..H7 */
  uint64_t total;      /* octets absorbés (bits calculés à la fin) */
  uint8_t block[64];   /* bloc en cours */
  size_t used;         /* octets déjà dans le bloc */
} sha256_ctx;

void sha256_init(sha256_ctx *ctx);
void sha256_update(sha256_ctx *ctx, const void *data, size_t len);
void sha256_final(sha256_ctx *ctx, uint8_t out[SHA256_DIGEST_SIZE]);

/* Calcul direct sur un tampon. */
void sha256(const void *data, size_t len, uint8_t out[SHA256_DIGEST_SIZE]);

/* Empreinte en hexadécimal minuscule (64 caractères + '\0'). */
void sha256_hex(const void *data, size_t len, char out[SHA256_HEX_SIZE]);

/* Hexadécimal d'une empreinte binaire déjà calculée. */
void sha256_hex_buf(const uint8_t digest[SHA256_DIGEST_SIZE],
                    char out[SHA256_HEX_SIZE]);

#endif
