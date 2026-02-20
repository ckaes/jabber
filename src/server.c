#include "server.h"
#include "session.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

static struct pollfd  pollfds[MAX_CLIENTS];
static session_t     *sessions[MAX_CLIENTS];
static int            nfds = 0;
static int            listen_fd = -1;
static volatile sig_atomic_t shutdown_flag = 0;

static void signal_handler(int sig) {
    (void)sig;
    shutdown_flag = 1;
}

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int server_init(config_t *cfg) {
    /* Install signal handlers */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Ignore SIGPIPE */
    signal(SIGPIPE, SIG_IGN);

    /* Create listening socket */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        log_write(LOG_ERROR, "socket(): %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_nonblocking(listen_fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)cfg->port);
    if (inet_pton(AF_INET, cfg->bind_address, &addr.sin_addr) <= 0) {
        log_write(LOG_ERROR, "Invalid bind address: %s", cfg->bind_address);
        close(listen_fd);
        return -1;
    }

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_write(LOG_ERROR, "bind(): %s", strerror(errno));
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, 16) < 0) {
        log_write(LOG_ERROR, "listen(): %s", strerror(errno));
        close(listen_fd);
        return -1;
    }

    /* Set up poll array with listener at index 0 */
    memset(pollfds, 0, sizeof(pollfds));
    memset(sessions, 0, sizeof(sessions));
    pollfds[0].fd = listen_fd;
    pollfds[0].events = POLLIN;
    sessions[0] = NULL; /* no session for listener */
    nfds = 1;

    log_write(LOG_INFO, "Listening on %s:%d", cfg->bind_address, cfg->port);
    return 0;
}

static void server_accept(void) {
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addrlen);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            log_write(LOG_WARN, "accept(): %s", strerror(errno));
        return;
    }

    if (nfds >= MAX_CLIENTS) {
        log_write(LOG_WARN, "Max clients reached, rejecting connection");
        close(client_fd);
        return;
    }

    set_nonblocking(client_fd);

    session_t *s = session_create(client_fd);
    if (!s) {
        log_write(LOG_ERROR, "Failed to allocate session");
        close(client_fd);
        return;
    }

    s->poll_index = nfds;
    pollfds[nfds].fd = client_fd;
    pollfds[nfds].events = POLLIN;
    pollfds[nfds].revents = 0;
    sessions[nfds] = s;
    nfds++;

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
    log_write(LOG_INFO, "Client connected from %s:%d (fd %d)",
              ip, ntohs(client_addr.sin_port), client_fd);
}

void server_run(void) {
    while (!shutdown_flag) {
        int ready = poll(pollfds, (nfds_t)nfds, 1000);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            log_write(LOG_ERROR, "poll(): %s", strerror(errno));
            break;
        }

        /* Check listener for new connections */
        if (pollfds[0].revents & POLLIN)
            server_accept();

        /* Process client events */
        for (int i = 1; i < nfds; i++) {
            if (!sessions[i])
                continue;

            if (pollfds[i].revents & (POLLERR | POLLHUP)) {
                session_teardown(sessions[i]);
                /* After teardown, sessions[i] may have changed (swap) */
                i--;
                continue;
            }
            if (pollfds[i].revents & POLLIN) {
                session_on_readable(sessions[i]);
                if (!sessions[i] || sessions[i]->state == STATE_DISCONNECTED) {
                    i--;
                    continue;
                }
            }
            if (pollfds[i].revents & POLLOUT) {
                session_on_writable(sessions[i]);
                if (!sessions[i] || sessions[i]->state == STATE_DISCONNECTED) {
                    i--;
                    continue;
                }
            }
        }
    }
}

void server_shutdown(void) {
    log_write(LOG_INFO, "Shutting down server");

    /* Send stream close to all active sessions */
    for (int i = 1; i < nfds; i++) {
        if (sessions[i]) {
            session_write_str(sessions[i],
                "<stream:error>"
                "<system-shutdown xmlns='urn:ietf:params:xml:ns:xmpp-streams'/>"
                "</stream:error>"
                "</stream:stream>");
            session_flush(sessions[i]);
            session_destroy(sessions[i]);
            sessions[i] = NULL;
        }
    }
    nfds = 1;

    if (listen_fd >= 0) {
        close(listen_fd);
        listen_fd = -1;
    }
}

/* --- Accessors for session module --- */

struct pollfd *server_get_pollfd(int index) {
    if (index >= 0 && index < MAX_CLIENTS)
        return &pollfds[index];
    return NULL;
}

session_t **server_get_sessions(void) {
    return sessions;
}

int server_get_nfds(void) {
    return nfds;
}

void server_remove_session(session_t *s) {
    if (!s)
        return;

    int idx = s->poll_index;
    if (idx < 1 || idx >= nfds)
        return;

    /* Swap with last entry to compact the array */
    int last = nfds - 1;
    if (idx != last) {
        pollfds[idx] = pollfds[last];
        sessions[idx] = sessions[last];
        if (sessions[idx])
            sessions[idx]->poll_index = idx;
    }

    pollfds[last].fd = -1;
    pollfds[last].events = 0;
    pollfds[last].revents = 0;
    sessions[last] = NULL;
    nfds--;
}
