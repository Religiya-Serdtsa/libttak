#ifndef TTAK_NET_VIEW_H
#define TTAK_NET_VIEW_H

#include <stddef.h>
#include <stdint.h>

#include <ttak/net/endpoint.h>
#include <ttak/io/zerocopy.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read-only zero-copy snapshot returned by ttak_net_view_from_endpoint().
 */
typedef struct ttak_net_view {
    const uint8_t *data;
    size_t len;
    uint64_t birth_ns;
    ttak_io_zerocopy_region_t region;
    ttak_net_lattice_slot_t *slot;
    ttak_net_lattice_t *slot_lattice;
} ttak_net_view_t;

/**
 * @brief Initializes a view structure.
 */
void ttak_net_view_init(ttak_net_view_t *view);

/**
 * @brief Reads from @p endpoint into a detachable buffer and wires it to @p view.
 */
ttak_io_status_t ttak_net_view_from_endpoint(ttak_net_view_t *view,
                                             ttak_shared_net_endpoint_t *endpoint,
                                             ttak_owner_t *owner,
                                             size_t max_len,
                                             int flags,
                                             uint64_t now);

/**
 * @brief Returns the immutable payload pointer held by the view.
 */
const uint8_t *ttak_net_view_data(const ttak_net_view_t *view);

/**
 * @brief Releases detachable resources associated with the view.
 */
void ttak_net_view_release(ttak_net_view_t *view);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_NET_VIEW_H */
