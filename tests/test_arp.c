#include <ttak/net/core/arp.h>
#include "test_macros.h"

#include <string.h>

static void test_arp_constants(void) {
    ASSERT(TTAK_ARP_HTYPE_ETHERNET == 1);
    ASSERT(TTAK_ARP_PTYPE_IPV4 == 0x0800u);
    ASSERT(TTAK_ARP_HLEN_ETHERNET == 6);
    ASSERT(TTAK_ARP_PLEN_IPV4 == 4);
    ASSERT(TTAK_ARP_OPER_REQUEST == 1);
    ASSERT(TTAK_ARP_OPER_REPLY == 2);
    ASSERT(TTAK_ARP_ETH_IPV4_LEN == 28);
    ASSERT(sizeof(ttak_net_arp_eth_ipv4_t) == 28);
}

static void test_arp_request_build(void) {
    ttak_net_arp_eth_ipv4_t pkt;
    const uint8_t src_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    const uint8_t src_ip[4]  = {192, 168, 1, 10};
    const uint8_t dst_ip[4]  = {192, 168, 1, 1};

    ttak_net_arp_eth_ipv4_build_request(&pkt, src_mac, src_ip, dst_ip);

    ASSERT(pkt.htype == TTAK_ARP_HTYPE_ETHERNET);
    ASSERT(pkt.ptype == TTAK_ARP_PTYPE_IPV4);
    ASSERT(pkt.hlen == TTAK_ARP_HLEN_ETHERNET);
    ASSERT(pkt.plen == TTAK_ARP_PLEN_IPV4);
    ASSERT(pkt.oper == TTAK_ARP_OPER_REQUEST);
    ASSERT(memcmp(pkt.sha, src_mac, 6) == 0);
    ASSERT(memcmp(pkt.spa, src_ip, 4) == 0);
    ASSERT(memcmp(pkt.tpa, dst_ip, 4) == 0);

    uint8_t zero_mac[6] = {0};
    ASSERT(memcmp(pkt.tha, zero_mac, 6) == 0);
}

static void test_arp_reply_build(void) {
    ttak_net_arp_eth_ipv4_t pkt;
    const uint8_t src_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    const uint8_t src_ip[4]  = {192, 168, 1, 1};
    const uint8_t dst_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    const uint8_t dst_ip[4]  = {192, 168, 1, 10};

    ttak_net_arp_eth_ipv4_build_reply(&pkt, src_mac, src_ip, dst_mac, dst_ip);

    ASSERT(pkt.htype == TTAK_ARP_HTYPE_ETHERNET);
    ASSERT(pkt.ptype == TTAK_ARP_PTYPE_IPV4);
    ASSERT(pkt.hlen == TTAK_ARP_HLEN_ETHERNET);
    ASSERT(pkt.plen == TTAK_ARP_PLEN_IPV4);
    ASSERT(pkt.oper == TTAK_ARP_OPER_REPLY);
    ASSERT(memcmp(pkt.sha, src_mac, 6) == 0);
    ASSERT(memcmp(pkt.spa, src_ip, 4) == 0);
    ASSERT(memcmp(pkt.tha, dst_mac, 6) == 0);
    ASSERT(memcmp(pkt.tpa, dst_ip, 4) == 0);
}

static void test_arp_null_safety(void) {
    const uint8_t mac[6] = {0};
    const uint8_t ip[4]  = {0};
    ttak_net_arp_eth_ipv4_build_request(NULL, mac, ip, ip);
    ttak_net_arp_eth_ipv4_build_reply(NULL, mac, ip, mac, ip);
}

int main(void) {
    RUN_TEST(test_arp_constants);
    RUN_TEST(test_arp_request_build);
    RUN_TEST(test_arp_reply_build);
    RUN_TEST(test_arp_null_safety);
    puts("test_arp passed");
    return 0;
}
