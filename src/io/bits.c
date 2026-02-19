#include <ttak/io/bits.h>

#include <ttak/mem/mem.h>
#include <ttak/timing/timing.h>

#include <string.h>

uint32_t ttak_io_bits_fnv32(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t hash = 2166136261u;
    const uint32_t prime = 16777619u;

    if (!bytes) return hash;

    for (size_t i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= prime;
    }
    return hash;
}

ttak_io_status_t ttak_io_bits_verify(const void *data,
                                     size_t len,
                                     uint32_t expected_checksum) {
    if (!data && len > 0) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }
    uint32_t checksum = ttak_io_bits_fnv32(data, len);
    if (checksum != expected_checksum) {
        return TTAK_IO_ERR_NEEDS_RETRY;
    }
    return TTAK_IO_SUCCESS;
}

ttak_io_status_t ttak_io_bits_recover(const void *snapshot,
                                      size_t len,
                                      void *dst,
                                      uint32_t expected_checksum) {
    if ((!snapshot || !dst) && len > 0) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }

    ttak_io_status_t status = ttak_io_bits_verify(snapshot, len, expected_checksum);
    if (status != TTAK_IO_SUCCESS) {
        return status;
    }

    uint64_t now = ttak_get_tick_count();
    void *safe_snapshot = ttak_mem_access((void *)snapshot, now);
    void *safe_dst = ttak_mem_access(dst, now);

    if ((!safe_snapshot && snapshot) || (!safe_dst && dst)) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }

    memcpy(safe_dst ? safe_dst : dst, safe_snapshot ? safe_snapshot : snapshot, len);
    return TTAK_IO_SUCCESS;
}
