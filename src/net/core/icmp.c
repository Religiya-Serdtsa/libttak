#include <ttak/net/core/icmp.h>
#include <string.h>

uint16_t ttak_net_icmp_calculate_checksum(const void *data, size_t len) {
    const uint16_t *ptr = (const uint16_t *)data;
    uint32_t sum = 0xffff;
    for (size_t i = 0; i + 1 < len; i += 2) {
        sum += *ptr++;
        if (sum > 0xffff) sum -= 0xffff;
    }
    if (len & 1) {
        sum += (uint32_t)(*(const uint8_t *)ptr);
        if (sum > 0xffff) sum -= 0xffff;
    }
    return (uint16_t)~sum;
}

uint16_t ttak_net_icmp6_calculate_checksum(const void *src, const void *dst, const void *data, size_t len) {
    // Standard ICMPv6 checksum includes IPv6 pseudo-header
    uint32_t sum = 0;
    const uint16_t *s = (const uint16_t *)src;
    const uint16_t *d = (const uint16_t *)dst;

    for (int i = 0; i < 8; i++) sum += s[i];
    for (int i = 0; i < 8; i++) sum += d[i];
    sum += (uint32_t)len;
    sum += 58; // IPPROTO_ICMPV6

    const uint16_t *p = (const uint16_t *)data;
    for (size_t i = 0; i + 1 < len; i += 2) sum += *p++;
    if (len & 1) sum += (uint32_t)(*(const uint8_t *)p);

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}
