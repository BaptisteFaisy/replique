#ifndef REPLIQUE_DECLIC_EXEC_H
#define REPLIQUE_DECLIC_EXEC_H
/* declic_exec.h — exécution d'une commande et capture de sa sortie, côté QNX.
 *
 * QNX 8.0 n'implémente pas pipe(2) (ENOSYS « Function not implemented ») :
 * popen(3), construit sur pipe, échoue donc toujours sur la cible. On passe
 * par un fichier temporaire sous /dev/shmem (seul point de montage inscriptible
 * de l'image minimale) : fork + redirection de stdout/stderr vers ce fichier,
 * wait, relecture. Aucune dépendance à un tube.
 */
#include <stddef.h>

/* Exécute `cmd` via `/bin/sh -c` et capture stdout + stderr dans `buf`
 * (au plus `cap` octets ; *len reçoit le nombre réel). Retourne 0 si la
 * commande a été exécutée, -1 sinon (fork/ouverture impossible). */
int declic_run_capture(const char *cmd, unsigned char *buf, size_t cap,
                       size_t *len);

#endif