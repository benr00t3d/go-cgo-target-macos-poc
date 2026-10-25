#include <fcntl.h>
#include <spawn.h>
#include <stdlib.h>
#include <unistd.h>

extern char **environ;

static void append_marker(const char *path, const char *text, size_t length) {
    if (path == NULL || path[0] == '\0') {
        return;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd >= 0) {
        (void)write(fd, text, length);
        (void)close(fd);
    }
}

__attribute__((constructor)) static void open_calculator_once(void) {
    static const char loaded[] = "calculator-plugin-loaded\n";
    const char *marker = getenv("GO_CGO_MACOS_CALC_MARKER");
    append_marker(marker, loaded, sizeof(loaded) - 1);

    const char *sentinel = getenv("GO_CGO_MACOS_CALC_SENTINEL");
    if (sentinel == NULL || sentinel[0] == '\0') {
        sentinel = "/tmp/go-cgo-vscode-calculator.once";
    }

    int fd = open(sentinel, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        return;
    }
    (void)write(fd, loaded, sizeof(loaded) - 1);
    (void)close(fd);

    pid_t child = 0;
    char *const argv[] = {
        (char *)"/usr/bin/open",
        (char *)"-b",
        (char *)"com.apple.calculator",
        NULL,
    };
    (void)posix_spawn(&child, argv[0], NULL, NULL, argv, environ);
}

