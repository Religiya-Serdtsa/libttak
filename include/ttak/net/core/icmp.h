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

/* ICMPv4 Constants */
#define TTAK_ICMP_ECHO_REQUEST 8
#define TTAK_ICMP_ECHO_REPLY   0
#define TTAK_ICMP_TIME_EXCEEDED 11

/* ICMPv6 Constants */
#define TTAK_ICMP6_ECHO_REQUEST 128
#define TTAK_ICMP6_ECHO_REPLY   129
#define TTAK_ICMP6_TIME_EXCEEDED 3

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
    } un;
} ttak_net_icmpv4_hdr_t;

/**
 * @brief ICMPv6 Header structure.
 */
typedef struct ttak_net_icmpv6_hdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    union {
        struct {
            uint16_t id;
            uint16_t sequence;
        } echo;
    } un;
} ttak_net_icmpv6_hdr_t;

#ifdef _MSC_VER
#define TTAK_ICMP_API
#else
#define TTAK_ICMP_API __attribute__((visibility("default")))
#endif

/**
 * @brief Calculates RFC 1071 standard checksum.
 */
TTAK_ICMP_API uint16_t ttak_net_icmp_calculate_checksum(const void *data, size_t len);

/**
 * @brief Calculates ICMPv6 checksum with pseudo-header.
 */
TTAK_ICMP_API uint16_t ttak_net_icmp6_calculate_checksum(const void *src, const void *dst, const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_NET_CORE_ICMP_H */
