#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

__attribute__((constructor)) static void record_load(void) {
  const char *path = getenv("GO_SPLIT_LTO_MARKER");
  const char message[] = "candidate dylib loaded by the linker\n";

  if (path == NULL || path[0] == '\0') {
    return;
  }

  int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
  if (fd < 0) {
    return;
  }
  (void)write(fd, message, sizeof(message) - 1);
  (void)close(fd);
}
