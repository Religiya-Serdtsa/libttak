#include <ttak/net/view.h>

#include <ttak/io/io.h>

void ttak_net_view_init(ttak_net_view_t *view) {
    if (!view) return;
    view->data = NULL;
    view->len = 0;
    view->birth_ns = 0;
    ttak_io_zerocopy_region_init(&view->region);
}

ttak_io_status_t ttak_net_view_from_endpoint(ttak_net_view_t *view,
                                             ttak_shared_net_endpoint_t *endpoint,
                                             ttak_owner_t *owner,
                                             size_t max_len,
                                             int flags,
                                             uint64_t now) {
    if (!view || !endpoint || !owner) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }

    ttak_net_guard_snapshot_t snap;
    ttak_io_status_t status = ttak_net_endpoint_snapshot_guard(endpoint, owner, &snap, now);
    if (status != TTAK_IO_SUCCESS) {
        return status;
    }

    status = ttak_io_zerocopy_recv_fd(snap.fd, &view->region, max_len, flags, now);
    if (status == TTAK_IO_SUCCESS) {
        view->data = view->region.data;
        view->len = view->region.len;
        view->birth_ns = now;
        ttak_net_endpoint_guard_commit(&snap, now);
    } else {
        ttak_io_zerocopy_release(&view->region);
        ttak_net_view_init(view);
    }
    return status;
}

const uint8_t *ttak_net_view_data(const ttak_net_view_t *view) {
    return view ? view->data : NULL;
}

void ttak_net_view_release(ttak_net_view_t *view) {
    if (!view) return;
    ttak_io_zerocopy_release(&view->region);
    view->data = NULL;
    view->len = 0;
    view->birth_ns = 0;
}
