#include <ttak/net/core/arp.h>
#include <string.h>

void ttak_net_arp_eth_ipv4_build_request(ttak_net_arp_eth_ipv4_t *pkt,
                                          const uint8_t src_mac[6],
                                          const uint8_t src_ip[4],
                                          const uint8_t dst_ip[4]) {
    if (!pkt) return;
    pkt->htype = TTAK_ARP_HTYPE_ETHERNET;
    pkt->ptype = TTAK_ARP_PTYPE_IPV4;
    pkt->hlen  = TTAK_ARP_HLEN_ETHERNET;
    pkt->plen  = TTAK_ARP_PLEN_IPV4;
    pkt->oper  = TTAK_ARP_OPER_REQUEST;
    memcpy(pkt->sha, src_mac, TTAK_ARP_HLEN_ETHERNET);
    memcpy(pkt->spa, src_ip, TTAK_ARP_PLEN_IPV4);
    memset(pkt->tha, 0, TTAK_ARP_HLEN_ETHERNET);
    memcpy(pkt->tpa, dst_ip, TTAK_ARP_PLEN_IPV4);
}

void ttak_net_arp_eth_ipv4_build_reply(ttak_net_arp_eth_ipv4_t *pkt,
                                        const uint8_t src_mac[6],
                                        const uint8_t src_ip[4],
                                        const uint8_t dst_mac[6],
                                        const uint8_t dst_ip[4]) {
    if (!pkt) return;
    pkt->htype = TTAK_ARP_HTYPE_ETHERNET;
    pkt->ptype = TTAK_ARP_PTYPE_IPV4;
    pkt->hlen  = TTAK_ARP_HLEN_ETHERNET;
    pkt->plen  = TTAK_ARP_PLEN_IPV4;
    pkt->oper  = TTAK_ARP_OPER_REPLY;
    memcpy(pkt->sha, src_mac, TTAK_ARP_HLEN_ETHERNET);
    memcpy(pkt->spa, src_ip, TTAK_ARP_PLEN_IPV4);
    memcpy(pkt->tha, dst_mac, TTAK_ARP_HLEN_ETHERNET);
    memcpy(pkt->tpa, dst_ip, TTAK_ARP_PLEN_IPV4);
}
