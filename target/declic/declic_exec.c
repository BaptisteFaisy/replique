/* declic_exec.c — capture de sortie de commande sans pipe(2) (cf. declic_exec.h). */
#include "declic_exec.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Seul point d'écriture garanti de l'image minimale (le système de fichiers
 * IFS est en lecture seule). Le suffixe <-pid de Déclic> sérialise deux appels
 * successifs d'une même instance. */
#define DECLIC_EXEC_TMP "/dev/shmem/declic_capture"

int declic_run_capture(const char *cmd, unsigned char *buf, size_t cap,
                       size_t *len)
{
  char path[64];
  int fd;
  pid_t child;
  int status;

  *len = 0u;
  snprintf(path, sizeof path, "%s_%d", DECLIC_EXEC_TMP, (int)getpid());
  child = fork();
  if (child < 0) {
    return -1;
  }
  if (child == 0) {
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
      _exit(127);
    }
    (void)dup2(fd, 1);
    (void)dup2(fd, 2);
    (void)close(fd);
    execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
    _exit(127);
  }
  (void)waitpid(child, &status, 0);
  fd = open(path, O_RDONLY);
  if (fd < 0) {
    (void)unlink(path);
    return -1;
  }
  *len = (size_t)read(fd, buf, cap);
  (void)close(fd);
  (void)unlink(path);
  return 0;
}