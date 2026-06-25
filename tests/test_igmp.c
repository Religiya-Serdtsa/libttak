#include <ttak/net/core/igmp.h>
#include "test_macros.h"

#include <string.h>

static void test_igmp_constants(void) {
    ASSERT(TTAK_IGMP_PROTOCOL == 2);
    ASSERT(TTAK_IGMP_MEMBERSHIP_QUERY == 0x11);
    ASSERT(TTAK_IGMP_V1_MEMBERSHIP_REPORT == 0x12);
    ASSERT(TTAK_IGMP_V2_MEMBERSHIP_REPORT == 0x16);
    ASSERT(TTAK_IGMP_V2_LEAVE_GROUP == 0x17);
    ASSERT(TTAK_IGMP_V3_MEMBERSHIP_REPORT == 0x22);

    ASSERT(TTAK_IGMP_MODE_IS_INCLUDE == 1);
    ASSERT(TTAK_IGMP_MODE_IS_EXCLUDE == 2);
    ASSERT(TTAK_IGMP_CHANGE_TO_INCLUDE_MODE == 3);
    ASSERT(TTAK_IGMP_CHANGE_TO_EXCLUDE_MODE == 4);
    ASSERT(TTAK_IGMP_ALLOW_NEW_SOURCES == 5);
    ASSERT(TTAK_IGMP_BLOCK_OLD_SOURCES == 6);
}

static void test_igmpv2_checksum(void) {
    /* Manually build an IGMPv2 membership report for 224.0.0.1.
     * Expected checksum verified by RFC 1071 reduction. */
    ttak_net_igmpv2_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = TTAK_IGMP_V2_MEMBERSHIP_REPORT;
    hdr.max_resp_time = 0;
    hdr.checksum = 0;
    hdr.group_addr = 0x010000E0u; /* 224.0.0.1 in little-endian host form */

    uint16_t csum = ttak_net_igmp_calculate_checksum(&hdr, sizeof(hdr));
    /* With the chosen host value, the checksum should be non-zero and the
     * standard RFC 1071 verification (sum + checksum == 0xFFFF) must hold. */
    ASSERT(csum != 0);

    hdr.checksum = csum;
    uint16_t verify = ttak_net_igmp_calculate_checksum(&hdr, sizeof(hdr));
    ASSERT(verify == 0);
}

static void test_igmpv3_report_checksum(void) {
    /* Build a minimal IGMPv3 report with one group record. */
    uint8_t buf[20];
    memset(buf, 0, sizeof(buf));

    buf[0] = TTAK_IGMP_V3_MEMBERSHIP_REPORT;
    buf[2] = 0; /* checksum placeholder */
    buf[3] = 0;
    buf[6] = 0; /* number of group records */
    buf[7] = 1;

    /* Group record: MODE_IS_INCLUDE, 0 sources, 232.0.0.1 */
    buf[8] = TTAK_IGMP_MODE_IS_INCLUDE;
    buf[9] = 0;
    buf[10] = 0;
    buf[11] = 0;
    buf[12] = 0x01u;
    buf[13] = 0x00u;
    buf[14] = 0x00u;
    buf[15] = 0xE8u;

    uint16_t csum = ttak_net_igmp_calculate_checksum(buf, sizeof(buf));
    ASSERT(csum != 0);

    buf[2] = (uint8_t)(csum & 0xFFu);
    buf[3] = (uint8_t)(csum >> 8);
    uint16_t verify = ttak_net_igmp_calculate_checksum(buf, sizeof(buf));
    ASSERT(verify == 0);
}

static void test_igmp_struct_sizes(void) {
    ASSERT(sizeof(ttak_net_igmpv2_hdr_t) == 8);
    ASSERT(sizeof(ttak_net_igmpv3_query_t) == 12);
    ASSERT(sizeof(ttak_net_igmpv3_report_t) == 8);
    ASSERT(sizeof(ttak_net_igmpv3_group_record_t) == 8);
}

int main(void) {
    RUN_TEST(test_igmp_constants);
    RUN_TEST(test_igmpv2_checksum);
    RUN_TEST(test_igmpv3_report_checksum);
    RUN_TEST(test_igmp_struct_sizes);
    puts("test_igmp passed");
    return 0;
}
