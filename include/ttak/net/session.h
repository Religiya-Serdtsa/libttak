#ifndef TTAK_NET_SESSION_H
#define TTAK_NET_SESSION_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <ttak/net/endpoint.h>
#include <ttak/net/core/port.h>
#include <ttak/mem/epoch.h>
#include <ttak/sync/sync.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ttak_net_session_state {
    TTAK_NET_SESSION_ACTIVE   = (1u << 0),
    TTAK_NET_SESSION_ZOMBIE   = (1u << 1),
    TTAK_NET_SESSION_ALERTED  = (1u << 2),
    TTAK_NET_SESSION_NEEDS_RESTART = (1u << 3),
    TTAK_NET_SESSION_IMMORTAL = (1u << 4),
    TTAK_NET_SESSION_FAULTING = (1u << 5),
    TTAK_NET_SESSION_DETACHED = (1u << 6)
} ttak_net_session_state_t;

typedef enum ttak_net_session_policy {
    TTAK_SOCK_ALERT   = (1u << 0),
    TTAK_SOCK_RESTART = (1u << 1)
} ttak_net_session_policy_t;

/**
 * @brief Logical connection wrapper that ties an endpoint to parent/child relationships.
 */
typedef struct ttak_net_session {
    uint64_t id;
    uint64_t generation;
    ttak_shared_net_endpoint_t *endpoint;
    struct ttak_net_session *parent;
    struct ttak_net_session *first_child;
    struct ttak_net_session *next_sibling;
    ttak_owner_t *owner;
    uint32_t state_flags;
    uint64_t lifetime_ns;
    uint64_t next_sanity_ns;
    struct ttak_net_session *next_retire;
    struct ttak_net_session *sanity_next;
    struct ttak_net_session *fault_next;
} ttak_net_session_t;

typedef struct ttak_net_session_mgr {
    ttak_rwlock_t lock;
    ttak_net_session_t *head;
    uint64_t next_id;
    uint32_t policy_flags;
    ttak_net_session_t *retire_head;
    ttak_net_session_t *fault_head;
    bool async_offload;
    const ttak_net_driver_ops_t *net_ops;
} ttak_net_session_mgr_t;

/**
 * @brief Initializes the manager state.
 */
void ttak_net_session_mgr_init(ttak_net_session_mgr_t *mgr, bool async_offload);

/**
 * @brief Tears down all sessions asynchronously and releases resources.
 */
void ttak_net_session_mgr_destroy(ttak_net_session_mgr_t *mgr, uint64_t now);

/**
 * @brief Registers a new session, optionally attaching it to a parent.
 */
ttak_net_session_t *ttak_net_session_mgr_create(ttak_net_session_mgr_t *mgr,
                                                ttak_shared_net_endpoint_t *endpoint,
                                                ttak_net_session_t *parent,
                                                ttak_owner_t *owner,
                                                uint64_t now);

/**
 * @brief Closes a session and retires its descendants.
 */
void ttak_net_session_mgr_close(ttak_net_session_mgr_t *mgr,
                                ttak_net_session_t *session,
                                uint64_t now);

/**
 * @brief Periodic heartbeat for immortal sockets: runs sanity checks and applies policy.
 */
void ttak_net_session_mgr_tick(ttak_net_session_mgr_t *mgr, uint64_t now);

/**
 * @brief Configures alert/restart behavior.
 */
void ttak_net_session_mgr_set_policy(ttak_net_session_mgr_t *mgr, uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_NET_SESSION_H */
