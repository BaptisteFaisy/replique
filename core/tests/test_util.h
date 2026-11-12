#ifndef REPLIQUE_TEST_UTIL_H
#define REPLIQUE_TEST_UTIL_H
/* test_util.h — assertions maison des tests unitaires du cœur C11.
 *
 * Aucun framework, aucune dépendance : deux compteurs et des macros.
 * Un binaire de test = une seule unité de compilation : les compteurs sont
 * donc statiques (pas de symbole commun entre binaires).
 *
 * Convention du lot : chaque test affiche « test_<nom> OK (<n> assertions) »
 * et renvoie 0 (tout est vert) ou 1 (au moins un échec).
 */
#include <stdio.h>
#include <string.h>

static int tu_checks;    /* assertions évaluées */
static int tu_failures;  /* assertions en échec */

/* Assertion booléenne : le libellé est affiché en cas d'échec. */
#define TU_CHECK(cond, label)                                               \
  do {                                                                      \
    ++tu_checks;                                                            \
    if (!(cond)) {                                                          \
      ++tu_failures;                                                        \
      printf("  FAIL %s:%d : %s\n", __FILE__, __LINE__, (label));           \
    }                                                                       \
  } while (0)

/* Assertion d'égalité (entiers, pointeurs convertis en unsigned long long). */
#define TU_EQ(got, expected, label)                                          \
  do {                                                                      \
    unsigned long long tu_got_ = (unsigned long long)(got);                  \
    unsigned long long tu_exp_ = (unsigned long long)(expected);             \
    ++tu_checks;                                                            \
    if (tu_got_ != tu_exp_) {                                                \
      ++tu_failures;                                                        \
      printf("  FAIL %s:%d : %s (attendu %llu, obtenu %llu)\n", __FILE__,   \
             __LINE__, (label), tu_exp_, tu_got_);                           \
    }                                                                       \
  } while (0)

/* Assertion d'égalité de chaînes (octets exacts, comparés avec strcmp). */
#define TU_EQ_STR(got, expected, label)                                      \
  do {                                                                      \
    const char *tu_got_ = (got);                                             \
    const char *tu_exp_ = (expected);                                        \
    ++tu_checks;                                                            \
    if (!tu_got_ || !tu_exp_ || strcmp(tu_got_, tu_exp_) != 0) {             \
      ++tu_failures;                                                        \
      printf("  FAIL %s:%d : %s\n  attendu : %s\n  obtenu  : %s\n",         \
             __FILE__, __LINE__, (label), tu_exp_ ? tu_exp_ : "(null)",      \
             tu_got_ ? tu_got_ : "(null)");                                  \
    }                                                                       \
  } while (0)

/* Clôture du test : compte des assertions et code de retour 0/1. */
static inline int tu_report(const char *name)
{
  if (tu_failures) {
    printf("test_%s KO (%d assertions, %d echecs)\n", name, tu_checks,
           tu_failures);
    return 1;
  }
  printf("test_%s OK (%d assertions)\n", name, tu_checks);
  return 0;
}

#endif
