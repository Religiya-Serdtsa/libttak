#include <ttak/net/core/igmp.h>

uint16_t ttak_net_igmp_calculate_checksum(const void *data, size_t len) {
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
