/**
 * @file icmp.h
 * @brief Standard ICMPv4/v6 header definitions and utilities for LibTTAK.
 */

#ifndef TTAK_NET_CORE_ICMP_H
#define TTAK_NET_CORE_ICMP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TTAK_ICMP_ECHO_REQUEST 8
#define TTAK_ICMP_ECHO_REPLY   0
#define TTAK_ICMP_TIME_EXCEEDED 11

/**
 * @brief ICMPv4 Header structure.
 */
typedef struct ttak_net_icmpv4_hdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    union {
        struct {
            uint16_t id;
            uint16_t sequence;
        } echo;
        uint32_t gateway;
        struct {
            uint16_t unused;
            uint16_t mtu;
        } frag;
    } un;
} ttak_net_icmpv4_hdr_t;

/**
 * @brief Calculates RFC 1071 standard checksum.
 * 
 * @param data Data buffer to check.
 * @param len Length in bytes.
 * @return Calculated 16-bit checksum.
 */
__attribute__((visibility("default"))) uint16_t ttak_net_icmp_calculate_checksum(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_NET_CORE_ICMP_H */
