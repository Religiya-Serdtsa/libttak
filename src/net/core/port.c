#include <ttak/net/core/port.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>
#endif

#include <errno.h>

static int ttak_baremetal_stub(void) {
    return -ENOSYS;
}

static void ttak_fill_baremetal(ttak_net_driver_ops_t *ops,
                                const ttak_net_baremetal_spec_t *bm) {
    if (bm && bm->driver_ops) {
        *ops = *bm->driver_ops;
        return;
    }
    ops->socket_open = (int (*)(int, int, int))ttak_baremetal_stub;
    ops->socket_close = (int (*)(int))ttak_baremetal_stub;
    ops->socket_bind = (int (*)(int, const void *, size_t))ttak_baremetal_stub;
    ops->socket_listen = (int (*)(int, int))ttak_baremetal_stub;
    ops->socket_connect = (int (*)(int, const void *, size_t))ttak_baremetal_stub;
    ops->socket_send = (int (*)(int, const void *, size_t))ttak_baremetal_stub;
    ops->socket_recv = (int (*)(int, void *, size_t))ttak_baremetal_stub;
    ops->socket_setopt = (int (*)(int, int, const void *, size_t))ttak_baremetal_stub;
    ops->poll_wait = (int (*)(int, uint32_t, int))ttak_baremetal_stub;
}

#if defined(_WIN32)
static int ttak_win_socket_open(int domain, int type, int protocol) {
    return (int)WSASocket(domain, type, protocol, NULL, 0, 0);
}
static int ttak_win_socket_close(int fd) {
    return closesocket(fd);
}
static int ttak_win_socket_bind(int fd, const void *addr, size_t len) {
    return bind(fd, (const struct sockaddr *)addr, (int)len);
}
static int ttak_win_socket_listen(int fd, int backlog) {
    return listen(fd, backlog);
}
static int ttak_win_socket_connect(int fd, const void *addr, size_t len) {
    return connect(fd, (const struct sockaddr *)addr, (int)len);
}
static int ttak_win_socket_send(int fd, const void *buf, size_t len) {
    return send(fd, buf, (int)len, 0);
}
static int ttak_win_socket_recv(int fd, void *buf, size_t len) {
    return recv(fd, buf, (int)len, 0);
}
static int ttak_win_poll_wait(int fd, uint32_t events, int timeout_ms) {
    WSAPOLLFD pfd = { .fd = fd, .events = (SHORT)events };
    return WSAPoll(&pfd, 1, timeout_ms);
}
#endif

#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
static int ttak_posix_socket_open(int domain, int type, int protocol) {
    return socket(domain, type, protocol);
}
static int ttak_posix_socket_close(int fd) { return close(fd); }
static int ttak_posix_socket_bind(int fd, const void *addr, size_t len) {
    return bind(fd, (const struct sockaddr *)addr, len);
}
static int ttak_posix_socket_listen(int fd, int backlog) {
    return listen(fd, backlog);
}
static int ttak_posix_socket_connect(int fd, const void *addr, size_t len) {
    return connect(fd, (const struct sockaddr *)addr, len);
}
static int ttak_posix_socket_send(int fd, const void *buf, size_t len) {
    return (int)send(fd, buf, len, 0);
}
static int ttak_posix_socket_recv(int fd, void *buf, size_t len) {
    return (int)recv(fd, buf, len, 0);
}
static int ttak_posix_poll_wait(int fd, uint32_t events, int timeout_ms) {
    struct pollfd pfd = { .fd = fd, .events = (short)events };
    return poll(&pfd, 1, timeout_ms);
}
#endif

void ttak_net_driver_detect(ttak_net_driver_ops_t *ops,
                            ttak_net_os_t *os,
                            const ttak_net_baremetal_spec_t *bm) {
    if (!ops || !os) return;
#if defined(__BAREMETAL__)
    *os = TTAK_NET_OS_BAREMETAL;
    ttak_fill_baremetal(ops, bm);
#elif defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    *os = TTAK_NET_OS_WINDOWS;
    ops->socket_open = ttak_win_socket_open;
    ops->socket_close = ttak_win_socket_close;
    ops->socket_bind = ttak_win_socket_bind;
    ops->socket_listen = ttak_win_socket_listen;
    ops->socket_connect = ttak_win_socket_connect;
    ops->socket_send = ttak_win_socket_send;
    ops->socket_recv = ttak_win_socket_recv;
    ops->socket_setopt = (int (*)(int, int, const void *, size_t))setsockopt;
    ops->poll_wait = ttak_win_poll_wait;
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
    *os = TTAK_NET_OS_POSIX;
    ops->socket_open = ttak_posix_socket_open;
    ops->socket_close = ttak_posix_socket_close;
    ops->socket_bind = ttak_posix_socket_bind;
    ops->socket_listen = ttak_posix_socket_listen;
    ops->socket_connect = ttak_posix_socket_connect;
    ops->socket_send = ttak_posix_socket_send;
    ops->socket_recv = ttak_posix_socket_recv;
    ops->socket_setopt = (int (*)(int, int, const void *, size_t))setsockopt;
    ops->poll_wait = ttak_posix_poll_wait;
#else
    *os = TTAK_NET_OS_BAREMETAL;
    ttak_fill_baremetal(ops, bm);
#endif
}
