#include <systemd/sd-bus.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>

static sd_bus *system_bus = NULL;
static sd_bus *user_bus = NULL;
static int inhibit_fd = -1;
static int mpd_fd = -1;
static volatile int running = 1;

static void log_info(const char *msg) {
    fprintf(stderr, "mpd-inhibit: %s\n", msg);
}

static void on_signal(int sig) {
    (void)sig;
    running = 0;
}

static int is_mpd_active(void) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    char *state = NULL;

    int r = sd_bus_get_property_string(
        user_bus,
        "org.freedesktop.systemd1",
        "/org/freedesktop/systemd1/unit/mpd_2eservice",
        "org.freedesktop.systemd1.Unit",
        "ActiveState",
        &err,
        &state
    );

    if (r < 0) {
        fprintf(stderr, "mpd-inhibit: Failed to get mpd.service state: %s\n", err.message);
        sd_bus_error_free(&err);
        return 0;
    }

    int active = (strcmp(state, "active") == 0);
    free(state);
    return active;
}

static int mpd_connect(void) {
    if (mpd_fd >= 0) close(mpd_fd);

    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(6600), .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    mpd_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (mpd_fd < 0 || connect(mpd_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno != ECONNREFUSED) perror("mpd_connect");
        mpd_fd = -1;
        return -1;
    }

    char banner[128];
    ssize_t n = read(mpd_fd, banner, sizeof(banner) - 1);
    if (n <= 0 || strncmp(banner, "OK MPD", 6) != 0) {
        if (n > 0) log_info("invalid MPD banner");
        close(mpd_fd);
        mpd_fd = -1;
        return -1;
    }
    banner[n] = '\0';  // Null-terminate for safety
    return 0;
}

static int mpd_command(const char *cmd, char *buf, size_t buf_size, const char *expected) {
    if (mpd_fd < 0) return -1;

    size_t cmd_len = strlen(cmd);
    if (write(mpd_fd, cmd, cmd_len) != (ssize_t)cmd_len) {
        close(mpd_fd);
        mpd_fd = -1;
        return -1;
    }

    memset(buf, 0, buf_size);
    size_t off = 0;
    while (off < buf_size - 1 && running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(mpd_fd, &rfds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };  // 1s timeout for signal check

        int retval = select(mpd_fd + 1, &rfds, NULL, NULL, &tv);
        if (retval == -1) {
            if (errno == EINTR) continue;
            close(mpd_fd);
            mpd_fd = -1;
            return -1;
        } else if (retval == 0) {
            continue;  // Timeout, check running
        }

        ssize_t n = read(mpd_fd, buf + off, buf_size - 1 - off);
        if (n <= 0) {
            close(mpd_fd);
            mpd_fd = -1;
            return -1;
        }
        off += n;
        if (strstr(buf, "\nOK\n")) break;
    }

    if (!running) return -1;

    return (expected == NULL || strstr(buf, expected) != NULL) ? 0 : -1;
}

static int mpd_is_playing(void) {
    char buf[512];
    if (mpd_command("status\n", buf, sizeof(buf), "state: play") < 0) return 0;
    return 1;
}

static int mpd_idle(void) {
    char buf[512];
    return mpd_command("idle player\n", buf, sizeof(buf), "changed: player");
}

static int inhibit_take(void) {
    sd_bus_message *m = NULL;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int fd;

    int r = sd_bus_call_method(system_bus, "org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager", "Inhibit", &err, &m,
                               "ssss", "idle", "mpd-inhibit", "MPD is playing", "block");
    if (r < 0 || sd_bus_message_read(m, "h", &fd) < 0) {
        if (r < 0) fprintf(stderr, "mpd-inhibit: inhibit call failed: %s\n", err.message);
        sd_bus_error_free(&err);
        sd_bus_message_unref(m);
        return -1;
    }

    inhibit_fd = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    sd_bus_message_unref(m);
    if (inhibit_fd < 0) return -1;

    log_info("Inhibit taken");
    return 0;
}

static void inhibit_release(void) {
    if (inhibit_fd >= 0) {
        close(inhibit_fd);
        inhibit_fd = -1;
        log_info("Inhibit released");
    }
}

int main(void) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);  // Ignore SIGPIPE to prevent exit on broken pipe

    if (sd_bus_open_system(&system_bus) < 0) {
        log_info("failed to connect system bus");
        return 1;
    }

    if (sd_bus_open_user(&user_bus) < 0) {
        log_info("failed to connect user bus");
        sd_bus_unref(system_bus);
        return 1;
    }

    int inhibited = 0;

    while (running) {
        if (mpd_fd < 0) {
            if (!is_mpd_active()) {
                log_info("Waiting for mpd.service to activate");
                sleep(5);
                continue;
            }

            if (mpd_connect() < 0) {
                sleep(5);
                continue;
            }
            log_info("Connected to MPD");
        }

        int playing = mpd_is_playing();

        if (playing && !inhibited) {
            if (inhibit_take() == 0) inhibited = 1;
        } else if (!playing && inhibited) {
            inhibit_release();
            inhibited = 0;
        }

        if (mpd_idle() < 0) {
            if (mpd_fd == -1) {
                log_info("Lost connection from MPD");
                if (inhibited) {
                    inhibit_release();
                    inhibited = 0;
                }
            }
            sleep(1);  // Avoid busy loop
        }
    }

    inhibit_release();
    if (system_bus) sd_bus_unref(system_bus);
    if (user_bus) sd_bus_unref(user_bus);
    if (mpd_fd >= 0) close(mpd_fd);
    return 0;
}
