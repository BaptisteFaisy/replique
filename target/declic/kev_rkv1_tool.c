/* kev_rkv1_tool.c — outil minimal : .kev -> .rkv1 (hôte ou cible). */
#include "kev_rkv1.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage : %s <entree.kev> <sortie.rkv1>\n", argv[0]);
        return 2;
    }
    if (kev_rkv1_file(argv[1], argv[2]) != 0) {
        fprintf(stderr, "echec : %s\n", argv[1]);
        return 1;
    }
    return 0;
}