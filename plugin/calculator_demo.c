#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef CALCULATOR_OPEN_PATH
#define CALCULATOR_OPEN_PATH "/usr/bin/open"
#endif

extern char **environ;

static const unsigned char dylib_identity_anchor = 0;

struct plugin_identity {
    uint64_t hash;
    int path_rc;
    int lock_path_rc;
    char dylib_path[PATH_MAX];
    char lock_path[PATH_MAX];
    char receipt_path[PATH_MAX];
};

struct launch_result {
    int attr_rc;
    int spawn_rc;
    pid_t child;
    int wait_rc;
    int wait_status;
};

static int append_file(const char *path, const char *text, size_t length) {
    if (path == NULL || path[0] == '\0') {
        return EINVAL;
    }
    int fd = open(path,
                  O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW,
                  0600);
    if (fd < 0) {
        return errno;
    }

    struct stat metadata;
    if (fstat(fd, &metadata) != 0 || metadata.st_uid != getuid()) {
        int rc = errno != 0 ? errno : EACCES;
        (void)close(fd);
        return rc;
    }
    ssize_t written = write(fd, text, length);
    int rc = written == (ssize_t)length ? 0 : (errno != 0 ? errno : EIO);
    (void)close(fd);
    return rc;
}

static uint64_t fnv1a64(const char *text) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *cursor = (const unsigned char *)text;
         *cursor != '\0'; cursor++) {
        hash ^= *cursor;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int copy_path(char *destination, size_t capacity, const char *source) {
    int length = snprintf(destination, capacity, "%s", source);
    return length >= 0 && (size_t)length < capacity ? 0 : ENAMETOOLONG;
}

static void scrub_for_receipt(char *destination, size_t capacity,
                              const char *source) {
    if (capacity == 0) {
        return;
    }
    size_t output = 0;
    while (*source != '\0' && output + 1 < capacity) {
        unsigned char value = (unsigned char)*source++;
        destination[output++] = value <= 0x20 || value == '=' ? '_' : (char)value;
    }
    destination[output] = '\0';
}

static struct plugin_identity derive_identity(void) {
    struct plugin_identity identity;
    memset(&identity, 0, sizeof(identity));

    Dl_info info;
    memset(&info, 0, sizeof(info));
    if (dladdr((const void *)&dylib_identity_anchor, &info) == 0 ||
        info.dli_fname == NULL) {
        identity.path_rc = ENOENT;
        (void)copy_path(identity.dylib_path, sizeof(identity.dylib_path),
                        "unknown-dylib");
    } else if (realpath(info.dli_fname, identity.dylib_path) == NULL) {
        identity.path_rc = errno != 0 ? errno : ENOENT;
        if (copy_path(identity.dylib_path, sizeof(identity.dylib_path),
                      info.dli_fname) != 0) {
            (void)copy_path(identity.dylib_path, sizeof(identity.dylib_path),
                            "unresolved-dylib");
        }
    }
    identity.hash = fnv1a64(identity.dylib_path);

    const char *lock_directory = getenv("GO_CGO_MACOS_CALC_LOCK_DIR");
    if (lock_directory == NULL || lock_directory[0] == '\0') {
        lock_directory = "/tmp";
    }
    int length = snprintf(identity.lock_path, sizeof(identity.lock_path),
                          "%s/go-cgo-vscode-readme-%u-%016" PRIx64 ".lock",
                          lock_directory, (unsigned)getuid(), identity.hash);
    identity.lock_path_rc =
        length >= 0 && (size_t)length < sizeof(identity.lock_path)
            ? 0
            : ENAMETOOLONG;

    const char *receipt_override = getenv("GO_CGO_MACOS_CALC_RECEIPT");
    if (receipt_override != NULL && receipt_override[0] != '\0') {
        if (copy_path(identity.receipt_path, sizeof(identity.receipt_path),
                      receipt_override) != 0) {
            identity.receipt_path[0] = '\0';
        }
    } else {
        length = snprintf(identity.receipt_path, sizeof(identity.receipt_path),
                          "/tmp/go-cgo-vscode-readme-launch-%u.log",
                          (unsigned)getuid());
        if (length < 0 || (size_t)length >= sizeof(identity.receipt_path)) {
            identity.receipt_path[0] = '\0';
        }
    }
    return identity;
}

static int acquire_lock(const char *path) {
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        return -1;
    }

    struct stat metadata;
    if (fstat(fd, &metadata) != 0 || metadata.st_uid != getuid()) {
        (void)close(fd);
        errno = EACCES;
        return -1;
    }

    int rc;
    do {
        rc = flock(fd, LOCK_EX);
    } while (rc != 0 && errno == EINTR);
    if (rc != 0) {
        int saved_errno = errno;
        (void)close(fd);
        errno = saved_errno;
        return -1;
    }
    return fd;
}

static struct launch_result open_calculator_and_wait(void) {
    struct launch_result result = {
        .attr_rc = -1,
        .spawn_rc = -1,
        .child = 0,
        .wait_rc = -1,
        .wait_status = 0,
    };
    char *const argv[] = {
        (char *)CALCULATOR_OPEN_PATH,
        (char *)"-b",
        (char *)"com.apple.calculator",
        NULL,
    };

    posix_spawnattr_t attr;
    result.attr_rc = posix_spawnattr_init(&attr);
    int attr_initialized = result.attr_rc == 0;
    sigset_t empty_mask;
    if (result.attr_rc == 0) {
        result.attr_rc = sigemptyset(&empty_mask) == 0 ? 0 : errno;
    }
    if (result.attr_rc == 0) {
        result.attr_rc = posix_spawnattr_setsigmask(&attr, &empty_mask);
    }
    if (result.attr_rc == 0) {
        short flags = POSIX_SPAWN_SETSIGMASK;
#ifdef __APPLE__
        flags |= POSIX_SPAWN_SETSID | POSIX_SPAWN_CLOEXEC_DEFAULT;
#else
        result.attr_rc = posix_spawnattr_setpgroup(&attr, 0);
        flags |= POSIX_SPAWN_SETPGROUP;
#endif
        if (result.attr_rc == 0) {
            result.attr_rc = posix_spawnattr_setflags(&attr, flags);
        }
    }

    result.spawn_rc = result.attr_rc;
    if (result.attr_rc == 0) {
        result.spawn_rc = posix_spawn(&result.child, argv[0], NULL, &attr,
                                      argv, environ);
    }
    if (attr_initialized) {
        (void)posix_spawnattr_destroy(&attr);
    }

    if (result.spawn_rc == 0) {
        pid_t waited;
        do {
            waited = waitpid(result.child, &result.wait_status, 0);
        } while (waited < 0 && errno == EINTR);
        result.wait_rc =
            waited == result.child ? 0 : (errno != 0 ? errno : ECHILD);
    }
    return result;
}

static int launch_succeeded(const struct launch_result *result) {
    return result->spawn_rc == 0 && result->wait_rc == 0 &&
           WIFEXITED(result->wait_status) &&
           WEXITSTATUS(result->wait_status) == 0;
}

static void append_launch_receipt(const struct plugin_identity *identity,
                                  int lock_rc,
                                  const struct launch_result *result) {
    int exit_code = -1;
    int signal_code = -1;
    if (result->wait_rc == 0 && WIFEXITED(result->wait_status)) {
        exit_code = WEXITSTATUS(result->wait_status);
    }
    if (result->wait_rc == 0 && WIFSIGNALED(result->wait_status)) {
        signal_code = WTERMSIG(result->wait_status);
    }

    char safe_dylib[PATH_MAX];
    scrub_for_receipt(safe_dylib, sizeof(safe_dylib), identity->dylib_path);
    char line[PATH_MAX + 512];
    int length = snprintf(
        line, sizeof(line),
        "event=%s id=%016" PRIx64 " host=%d path_rc=%d lock_path_rc=%d "
        "lock_rc=%d attr_rc=%d spawn_rc=%d child=%d wait_rc=%d exit=%d "
        "signal=%d path=%s\n",
        launch_succeeded(result) ? "launch-success" : "launch-failure",
        identity->hash, (int)getpid(), identity->path_rc,
        identity->lock_path_rc, lock_rc, result->attr_rc, result->spawn_rc,
        (int)result->child, result->wait_rc, exit_code, signal_code,
        safe_dylib);
    if (length <= 0) {
        return;
    }
    size_t write_length = (size_t)length;
    if (write_length >= sizeof(line)) {
        write_length = sizeof(line) - 1;
    }
    (void)append_file(identity->receipt_path, line, write_length);
}

__attribute__((constructor)) static void open_calculator_every_load(void) {
    static const char loaded[] = "calculator-plugin-loaded\n";
    const char *marker = getenv("GO_CGO_MACOS_CALC_MARKER");
    (void)append_file(marker, loaded, sizeof(loaded) - 1);

    struct plugin_identity identity = derive_identity();
    int lock_fd = -1;
    int lock_rc = identity.lock_path_rc;
    if (identity.lock_path_rc == 0) {
        lock_fd = acquire_lock(identity.lock_path);
        lock_rc = lock_fd >= 0 ? 0 : (errno != 0 ? errno : EIO);
    }

    struct launch_result result = open_calculator_and_wait();
    append_launch_receipt(&identity, lock_rc, &result);

    if (lock_fd >= 0) {
        (void)flock(lock_fd, LOCK_UN);
        (void)close(lock_fd);
    }
}
