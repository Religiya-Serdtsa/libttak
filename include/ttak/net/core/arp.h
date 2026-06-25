/**
 * @file arp.h
 * @brief ARP (RFC 826) header definitions and helpers for IPv4 over Ethernet.
 */

#ifndef TTAK_NET_CORE_ARP_H
#define TTAK_NET_CORE_ARP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hardware and protocol types. */
#define TTAK_ARP_HTYPE_ETHERNET 1
#define TTAK_ARP_PTYPE_IPV4     0x0800u

/* Address lengths for IPv4 over Ethernet. */
#define TTAK_ARP_HLEN_ETHERNET  6
#define TTAK_ARP_PLEN_IPV4      4

/* ARP operations. */
#define TTAK_ARP_OPER_REQUEST   1
#define TTAK_ARP_OPER_REPLY     2

/* Total packet length for IPv4 over Ethernet. */
#define TTAK_ARP_ETH_IPV4_LEN   28

/**
 * @brief ARP packet for IPv4 over Ethernet (RFC 826).
 *
 * Fields are expected to be in network byte order when transmitted.
 */
typedef struct ttak_net_arp_eth_ipv4 {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;
    uint8_t  sha[TTAK_ARP_HLEN_ETHERNET];
    uint8_t  spa[TTAK_ARP_PLEN_IPV4];
    uint8_t  tha[TTAK_ARP_HLEN_ETHERNET];
    uint8_t  tpa[TTAK_ARP_PLEN_IPV4];
} ttak_net_arp_eth_ipv4_t;

#ifdef _MSC_VER
#define TTAK_ARP_API
#else
#define TTAK_ARP_API __attribute__((visibility("default")))
#endif

/**
 * @brief Fills an ARP request packet for IPv4 over Ethernet.
 */
TTAK_ARP_API void ttak_net_arp_eth_ipv4_build_request(ttak_net_arp_eth_ipv4_t *pkt,
                                                       const uint8_t src_mac[6],
                                                       const uint8_t src_ip[4],
                                                       const uint8_t dst_ip[4]);

/**
 * @brief Fills an ARP reply packet for IPv4 over Ethernet.
 */
TTAK_ARP_API void ttak_net_arp_eth_ipv4_build_reply(ttak_net_arp_eth_ipv4_t *pkt,
                                                     const uint8_t src_mac[6],
                                                     const uint8_t src_ip[4],
                                                     const uint8_t dst_mac[6],
                                                     const uint8_t dst_ip[4]);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_NET_CORE_ARP_H */
