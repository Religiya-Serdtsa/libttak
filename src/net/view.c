#include <ttak/net/view.h>
#include <ttak/io/io.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif


void ttak_net_view_init(ttak_net_view_t *view) {
    if (!view) return;
    view->data = NULL;
    view->len = 0;
    view->birth_ns = 0;
    view->slot = NULL;
    view->slot_lattice = NULL;
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
    view->slot = NULL;
    view->slot_lattice = NULL;

    ttak_shared_result_t res;
    ttak_net_endpoint_t *payload = ttak_shared_net_endpoint_access(endpoint, owner, &res);
    if (!payload || res != TTAK_OWNER_SUCCESS) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }

    ttak_net_lattice_t *lat = payload->lattice;
    int fd = payload->guard.fd;
    uint32_t tid = ttak_net_lattice_get_worker_id();

    /* Transparent Optimization: Use Choi Seok-jeong's Latin Square isolation if lattice is present and enabled */
    if (lat && (payload->role_flags & TTAK_NET_ROLE_LATTICE_ACCEL)) {
        uint32_t mask = lat->mask;
        uint32_t my_tid = tid & mask;

        for (ttak_net_lattice_t *node = lat; node; ) {
            uint32_t dim = node->dim;
            for (uint32_t r = 0; r < dim; r++) {
                for (uint32_t c = 0; c < dim; c++) {
                    if (((r + c) & mask) == my_tid) {
                        ttak_net_lattice_slot_t *slot = &node->slots[r * dim + c];
                        if (ttak_atomic_read64(&slot->state) == 0) {
                            ttak_atomic_write64(&slot->state, 1);
                            ssize_t valread = recv(fd, slot->data, TTAK_LATTICE_SLOT_SIZE, flags);
                            if (valread > 0) {
                                slot->len = (uint32_t)valread;
                                slot->timestamp = now;
                                slot->seq++;
                                ttak_atomic_write64(&slot->state, 2);
                                ttak_atomic_add64(&node->total_ingress, 1);
                                ttak_net_lattice_mark_slot_acquired(node, now);

                                /* Wire the view directly to the lattice slot */
                                view->data = slot->data;
                                view->len = (size_t)valread;
                                view->birth_ns = now;
                                view->slot = slot;
                                view->slot_lattice = node;
                                /* We mark it as 'reading' so other won't overwrite while view is active */
                                ttak_atomic_write64(&slot->state, 3); 
                                
                                ttak_shared_net_endpoint_release(endpoint);
                                return TTAK_IO_SUCCESS;
                            }
                            ttak_atomic_write64(&slot->state, 0);
                        }
                    }
                }
            }

            ttak_net_lattice_t *next = node->next;
            if (!next) {
                next = ttak_net_lattice_ensure_next(node, now);
            }
            node = next;
        }
    }

    ttak_shared_net_endpoint_release(endpoint);

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

    /* If this view was backed by a lattice slot, release it */
    if (view->slot && view->slot_lattice) {
        ttak_atomic_write64(&view->slot->state, 0);
        ttak_net_lattice_mark_slot_released(view->slot_lattice);
    } else {
        ttak_net_lattice_t *default_lat = ttak_net_lattice_get_default();
        if (default_lat && view->data) {
            _Bool released = false;
            for (ttak_net_lattice_t *node = default_lat; node && !released; node = node->next) {
                uint32_t dim = node->dim;
                for (uint32_t i = 0; i < dim * dim; i++) {
                    if (node->slots[i].data == view->data) {
                        ttak_atomic_write64(&node->slots[i].state, 0);
                        ttak_net_lattice_mark_slot_released(node);
                        released = true;
                        break;
                    }
                }
            }
        }
    }

    ttak_io_zerocopy_release(&view->region);
    view->data = NULL;
    view->len = 0;
    view->birth_ns = 0;
    view->slot = NULL;
    view->slot_lattice = NULL;
}
