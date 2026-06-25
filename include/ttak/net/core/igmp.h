/**
 * @file igmp.h
 * @brief IGMPv2/v3 header definitions and checksum utilities for LibTTAK.
 */

#ifndef TTAK_NET_CORE_IGMP_H
#define TTAK_NET_CORE_IGMP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* IGMP protocol number used in IPv4 Protocol field. */
#define TTAK_IGMP_PROTOCOL 2

/* IGMP message types. */
#define TTAK_IGMP_MEMBERSHIP_QUERY        0x11
#define TTAK_IGMP_V1_MEMBERSHIP_REPORT    0x12
#define TTAK_IGMP_V2_MEMBERSHIP_REPORT    0x16
#define TTAK_IGMP_V2_LEAVE_GROUP          0x17
#define TTAK_IGMP_V3_MEMBERSHIP_REPORT    0x22

/* IGMPv3 group record types. */
#define TTAK_IGMP_MODE_IS_INCLUDE          1
#define TTAK_IGMP_MODE_IS_EXCLUDE          2
#define TTAK_IGMP_CHANGE_TO_INCLUDE_MODE   3
#define TTAK_IGMP_CHANGE_TO_EXCLUDE_MODE   4
#define TTAK_IGMP_ALLOW_NEW_SOURCES        5
#define TTAK_IGMP_BLOCK_OLD_SOURCES        6

/**
 * @brief IGMPv2 message header (RFC 2236).
 *
 * Fields are expected to be in network byte order when transmitted.
 */
typedef struct ttak_net_igmpv2_hdr {
    uint8_t  type;
    uint8_t  max_resp_time;
    uint16_t checksum;
    uint32_t group_addr;
} ttak_net_igmpv2_hdr_t;

/**
 * @brief IGMPv3 query header (RFC 3376).
 *
 * The base header is 12 bytes; source addresses follow immediately.
 */
typedef struct ttak_net_igmpv3_query {
    uint8_t  type;
    uint8_t  max_resp_code;
    uint16_t checksum;
    uint32_t group_addr;
    uint8_t  resv_sqrv;   /**< resv (4 bits) | S (1 bit) | QRV (3 bits) */
    uint8_t  qqic;        /**< Querier's Query Interval Code */
    uint16_t num_sources;
} ttak_net_igmpv3_query_t;

/**
 * @brief IGMPv3 group record header (RFC 3376).
 *
 * Source addresses and auxiliary data follow immediately.
 */
typedef struct ttak_net_igmpv3_group_record {
    uint8_t  type;
    uint8_t  aux_len;
    uint16_t num_sources;
    uint32_t multicast_addr;
} ttak_net_igmpv3_group_record_t;

/**
 * @brief IGMPv3 membership report header (RFC 3376).
 *
 * Group records follow immediately after this 8-byte header.
 */
typedef struct ttak_net_igmpv3_report {
    uint8_t  type;
    uint8_t  reserved1;
    uint16_t checksum;
    uint16_t reserved2;
    uint16_t num_group_records;
} ttak_net_igmpv3_report_t;

#ifdef _MSC_VER
#define TTAK_IGMP_API
#else
#define TTAK_IGMP_API __attribute__((visibility("default")))
#endif

/**
 * @brief Calculates RFC 1071 standard checksum over an IGMP message.
 */
TTAK_IGMP_API uint16_t ttak_net_igmp_calculate_checksum(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_NET_CORE_IGMP_H */
