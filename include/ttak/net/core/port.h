#ifndef TTAK_NET_CORE_PORT_H
#define TTAK_NET_CORE_PORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ttak_net_os {
    TTAK_NET_OS_POSIX,
    TTAK_NET_OS_WINDOWS,
    TTAK_NET_OS_BAREMETAL
} ttak_net_os_t;

typedef struct ttak_net_driver_ops {
    int  (*socket_open)(int domain, int type, int protocol);
    int  (*socket_close)(int fd);
    int  (*socket_bind)(int fd, const void *addr, size_t len);
    int  (*socket_listen)(int fd, int backlog);
    int  (*socket_connect)(int fd, const void *addr, size_t len);
    int  (*socket_send)(int fd, const void *buf, size_t len);
    int  (*socket_recv)(int fd, void *buf, size_t len);
    int  (*socket_setopt)(int fd, int opt, const void *val, size_t len);
    int  (*poll_wait)(int fd, uint32_t events, int timeout_ms);
} ttak_net_driver_ops_t;

typedef struct ttak_net_baremetal_spec {
    const ttak_net_driver_ops_t *driver_ops;
    void *(*nic_map_io)(uintptr_t base, size_t len);
    void  (*nic_unmap_io)(void *addr, size_t len);
    void *(*buddy_alloc)(size_t bytes);
    void  (*buddy_free)(void *ptr);
} ttak_net_baremetal_spec_t;

void ttak_net_driver_detect(ttak_net_driver_ops_t *ops,
                            ttak_net_os_t *os,
                            const ttak_net_baremetal_spec_t *bm);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_NET_CORE_PORT_H */
#if !defined(__BAREMETAL__)
#include <ttak/phys/mem/buddy.h>
#endif
