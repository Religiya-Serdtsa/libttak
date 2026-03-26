#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <ttak/net/session.h>
#include <ttak/net/endpoint.h>
#include <ttak/mem/owner.h>
#include <ttak/timing/timing.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

static int make_client_socket(struct sockaddr_in *addr) {
    if (!addr) return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(49152);
    addr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return fd;
}

int main(void) {
    enum { FILL_PATTERN = 0xAB };
    for (int i = 0; i < 64; ++i) {
        uint64_t now = ttak_get_tick_count();
        ttak_owner_t *owner = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
        assert(owner != NULL);

        ttak_shared_net_endpoint_t *endpoint = ttak_net_endpoint_create(owner, now);
        assert(endpoint != NULL);

        struct sockaddr_in addr;
        int fd = make_client_socket(&addr);
        assert(fd >= 0);

        ttak_io_status_t bind_status =
            ttak_net_endpoint_bind_fd(endpoint,
                                      owner,
                                      fd,
                                      AF_INET,
                                      SOCK_STREAM,
                                      0,
                                      &addr,
                                      (uint8_t)sizeof(addr),
                                      TTAK_NET_ENDPOINT_IPV4,
                                      UINT64_MAX,
                                      now);
        if (bind_status != TTAK_IO_SUCCESS) {
#ifdef _WIN32
            closesocket(fd);
#else
            close(fd);
#endif
            ttak_net_endpoint_destroy(endpoint, owner, now);
            ttak_owner_destroy(owner);
            continue;
        }

        ttak_net_session_mgr_t mgr;
        memset(&mgr, FILL_PATTERN, sizeof(mgr));
        ttak_net_session_mgr_init(&mgr, false);

        ttak_net_session_t *session =
            ttak_net_session_mgr_create(&mgr, endpoint, NULL, owner, now);
        assert(session != NULL);

        ttak_net_session_mgr_close(&mgr, session, now);
        ttak_net_session_mgr_destroy(&mgr, now);

        ttak_net_endpoint_destroy(endpoint, owner, now);
        ttak_owner_destroy(owner);
    }

    puts("test_net_session_reinit passed");
    return 0;
}
