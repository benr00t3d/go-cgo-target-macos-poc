#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
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

static void append_launch_receipt(int attr_rc, int spawn_rc, pid_t child,
                                  int wait_rc, int wait_status) {
    const char *path = getenv("GO_CGO_MACOS_CALC_RECEIPT");
    if (path == NULL || path[0] == '\0') {
        path = "/tmp/go-cgo-vscode-readme-launch-v2-20261025.log";
    }

    int exit_code = -1;
    int signal_code = -1;
    if (wait_rc == 0 && WIFEXITED(wait_status)) {
        exit_code = WEXITSTATUS(wait_status);
    }
    if (wait_rc == 0 && WIFSIGNALED(wait_status)) {
        signal_code = WTERMSIG(wait_status);
    }

    char line[256];
    int length = snprintf(line, sizeof(line),
                          "host=%d attr_rc=%d spawn_rc=%d child=%d "
                          "wait_rc=%d exit=%d signal=%d\n",
                          (int)getpid(), attr_rc, spawn_rc, (int)child,
                          wait_rc, exit_code, signal_code);
    if (length <= 0) {
        return;
    }
    size_t write_length = (size_t)length;
    if (write_length >= sizeof(line)) {
        write_length = sizeof(line) - 1;
    }
    append_marker(path, line, write_length);
}

static int open_calculator_and_wait(void) {
    pid_t child = 0;
    char *const argv[] = {
        (char *)"/usr/bin/open",
        (char *)"-b",
        (char *)"com.apple.calculator",
        NULL,
    };

    posix_spawnattr_t attr;
    int attr_rc = posix_spawnattr_init(&attr);
    int attr_initialized = attr_rc == 0;
    if (attr_rc == 0) {
        attr_rc = posix_spawnattr_setpgroup(&attr, 0);
    }
    if (attr_rc == 0) {
        attr_rc = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    }

    int spawn_rc = attr_rc;
    if (attr_rc == 0) {
        spawn_rc = posix_spawn(&child, argv[0], NULL, &attr, argv, environ);
    }
    if (attr_initialized) {
        (void)posix_spawnattr_destroy(&attr);
    }

    int wait_rc = -1;
    int wait_status = 0;
    if (spawn_rc == 0) {
        pid_t waited;
        do {
            waited = waitpid(child, &wait_status, 0);
        } while (waited < 0 && errno == EINTR);
        wait_rc = waited == child ? 0 : (errno != 0 ? errno : -1);
    }

    append_launch_receipt(attr_rc, spawn_rc, child, wait_rc, wait_status);
    return spawn_rc == 0 && wait_rc == 0 && WIFEXITED(wait_status) &&
           WEXITSTATUS(wait_status) == 0;
}

__attribute__((constructor)) static void open_calculator_once(void) {
    static const char loaded[] = "calculator-plugin-loaded\n";
    const char *marker = getenv("GO_CGO_MACOS_CALC_MARKER");
    append_marker(marker, loaded, sizeof(loaded) - 1);

    const char *sentinel = getenv("GO_CGO_MACOS_CALC_SENTINEL");
    if (sentinel == NULL || sentinel[0] == '\0') {
        sentinel = "/tmp/go-cgo-vscode-readme-startup-v2-20261025.once";
    }

    int fd = open(sentinel, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        (void)close(fd);
        return;
    }

    if (!open_calculator_and_wait()) {
        return;
    }

    fd = open(sentinel, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd >= 0) {
        (void)write(fd, loaded, sizeof(loaded) - 1);
        (void)close(fd);
    }
}

