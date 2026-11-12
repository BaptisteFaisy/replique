#ifndef REPLIQUE_DECLIC_WATCH_H
#define REPLIQUE_DECLIC_WATCH_H
/* declic_watch.h — observation des terminaisons (essai E3), côté QNX 8.0.
 *
 * Faits vérifiés sur le SDP 8.0 (r630, 12/11/2026) :
 *   - le HAM (High Availability Manager) N'EXISTE PLUS : pas d'en-tête
 *     <ham/ham.h>, pas de binaire ham ;
 *   - pas de service de noms : pas de <name.h>, donc pas de canal nommé ;
 *   - on observe donc la mort d'un processus en scrutant /proc/<pid>, et on
 *     récupère son signal dans la liste des morts de `pidin dead`.
 */
#include <stdint.h>

int declic_process_alive(uint32_t pid);
unsigned declic_list_pids(uint32_t *out, unsigned cap);
int declic_wait_for_death(uint32_t pid, unsigned poll_ms);
int declic_wait_for_any_death(uint32_t *pid_out, unsigned poll_ms);
void declic_detect_termination(uint32_t pid, uint32_t *status, uint32_t *signal);
#endif
