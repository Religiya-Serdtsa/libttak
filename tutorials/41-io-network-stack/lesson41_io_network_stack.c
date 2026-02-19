#include <ttak/net/endpoint.h>
#include <ttak/net/session.h>
#include <ttak/net/view.h>
#include <ttak/timing/timing.h>
#include <ttak/mem/owner.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

static void fail(const char *msg) {
    perror(msg);
}

int main(void) {
    uint64_t now = ttak_get_tick_count();
    ttak_owner_t *owner = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
    if (!owner) {
        fprintf(stderr, "failed to allocate owner\n");
        return 1;
    }

    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
        fail("socketpair");
        ttak_owner_destroy(owner);
        return 1;
    }

    ttak_shared_net_endpoint_t *endpoint = ttak_net_endpoint_create(owner, now);
    if (!endpoint) {
        fprintf(stderr, "failed to allocate shared endpoint\n");
        close(pair[0]);
        close(pair[1]);
        ttak_owner_destroy(owner);
        return 1;
    }

    const char addr_tag[] = "lesson41_socketpair";
    ttak_io_status_t status = ttak_net_endpoint_bind_fd(
        endpoint,
        owner,
        pair[0],
        AF_UNIX,
        SOCK_STREAM,
        0,
        addr_tag,
        (uint8_t)sizeof(addr_tag),
        TTAK_NET_ENDPOINT_UNIX,
        TT_SECOND(5),
        now);
    if (status != TTAK_IO_SUCCESS) {
        fprintf(stderr, "bind failed (%d)\n", status);
        ttak_net_endpoint_destroy(endpoint, owner, now);
        close(pair[0]);
        close(pair[1]);
        ttak_owner_destroy(owner);
        return 1;
    }

    ttak_net_session_mgr_t mgr;
    ttak_net_session_mgr_init(&mgr, true);
    ttak_net_session_mgr_set_policy(&mgr, TTAK_SOCK_ALERT);
    ttak_net_session_t *session = ttak_net_session_mgr_create(&mgr, endpoint, NULL, owner, now);
    if (!session) {
        fprintf(stderr, "failed to register session\n");
        ttak_net_endpoint_destroy(endpoint, owner, now);
        close(pair[0]);
        close(pair[1]);
        ttak_owner_destroy(owner);
        return 1;
    }

    const char payload[] = "zero-copy hello from Lesson 41";
    if (write(pair[1], payload, sizeof(payload)) < 0) {
        fail("write");
    }

    ttak_net_view_t view;
    ttak_net_view_init(&view);
    status = ttak_net_view_from_endpoint(&view, endpoint, owner, sizeof(payload), 0, ttak_get_tick_count());
    if (status == TTAK_IO_SUCCESS) {
        printf("[lesson41] received %zu bytes via zero-copy: \"%.*s\"\n",
               view.len,
               (int)view.len,
               view.data ? (const char *)view.data : "(null)");
    } else {
        fprintf(stderr, "zero-copy view failed (%d)\n", status);
    }
    ttak_net_view_release(&view);

    ttak_net_session_mgr_tick(&mgr, ttak_get_tick_count());

    ttak_net_session_mgr_close(&mgr, session, ttak_get_tick_count());
    ttak_net_session_mgr_destroy(&mgr, ttak_get_tick_count());

    ttak_net_endpoint_destroy(endpoint, owner, ttak_get_tick_count());
    close(pair[1]); /* peer */
    ttak_owner_destroy(owner);
    return 0;
}
