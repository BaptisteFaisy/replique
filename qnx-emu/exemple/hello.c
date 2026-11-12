/*
 * exemple/hello.c — binaire de démonstration minimal pour un invité QNX 8.0.
 *
 * Sert de témoin : le scénario de démarrage lance « hello » et la console
 * série doit afficher cette bannière. Si elle s'affiche, l'invité démarre,
 * le shell fonctionne et la capture d'écran de la vitrine est la bonne.
 *
 * Compilation (dans le SDP 8.0) :
 *   qcc -V gcc_ntox86_64 -Wall -Wextra -Werror -o hello hello.c
 */
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    int i;

    for (i = 0; i < 60; i++) {
        printf("hello depuis l'invite QNX 8.0 (tour %d)\n", i);
        fflush(stdout);
        sleep(1);
    }
    return 0;
}
