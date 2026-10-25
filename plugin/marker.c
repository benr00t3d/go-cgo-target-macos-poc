#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

__attribute__((constructor)) static void record_load(void) {
    const char *path = getenv("GO_CGO_MACOS_POC_MARKER");
    if (path == NULL || path[0] == '\0') {
        return;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd >= 0) {
        (void)write(fd, "apple-clang-loaded\n", 19);
        (void)close(fd);
    }
}

