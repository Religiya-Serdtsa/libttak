#include <ttak/async/sched.h>
#include <ttak/io/async.h>
#include <ttak/io/io.h>
#include <ttak/io/sync.h>
#include <ttak/mem/mem.h>
#include <ttak/mem/owner.h>
#include <ttak/timing/timing.h>

#include <stdbool.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void poll_logger(int fd, short revents, void *user) {
    const char *tag = user ? (const char *)user : "io";
    printf("[lesson42] poll(%s) fd=%d events=0x%x\n", tag, fd, (unsigned)revents);
    (void)fd;
}

typedef struct lesson42_async_state {
    volatile bool completed;
    size_t capacity;
    char *buffer;
    ttak_io_status_t status;
    size_t bytes;
} lesson42_async_state_t;

static void lesson42_async_read_done(ttak_io_status_t status, size_t bytes, void *user) {
    lesson42_async_state_t *state = (lesson42_async_state_t *)user;
    state->status = status;
    state->bytes = bytes;
    if (status == TTAK_IO_SUCCESS && state->buffer && bytes < state->capacity) {
        state->buffer[bytes] = '\0';
    }
    state->completed = true;
}

static int duplicate_fd(int fd, const char *label) {
    int dup_fd = dup(fd);
    if (dup_fd < 0) {
        perror(label);
    }
    return dup_fd;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[lesson42] booting lesson42_io_guarded_streams\n");
    ttak_owner_t *owner = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
    if (!owner) {
        fprintf(stderr, "[lesson42] failed to create owner\n");
        return 1;
    }

    int pipe_base[2];
    if (pipe(pipe_base) != 0) {
        perror("pipe");
        ttak_owner_destroy(owner);
        return 1;
    }

    int reader_fd = duplicate_fd(pipe_base[0], "dup(reader)");
    int writer_fd = duplicate_fd(pipe_base[1], "dup(writer)");
    if (reader_fd < 0 || writer_fd < 0) {
        if (reader_fd >= 0) close(reader_fd);
        if (writer_fd >= 0) close(writer_fd);
        close(pipe_base[0]);
        close(pipe_base[1]);
        ttak_owner_destroy(owner);
        return 1;
    }

    ttak_io_guard_t reader_guard;
    ttak_io_guard_t writer_guard;
    char *write_slot = NULL;
    char *read_slot = NULL;
    bool async_initialized = false;
    const uint64_t reader_ttl = 300;  /* milliseconds */
    const uint64_t writer_ttl = 2000; /* milliseconds */
    uint64_t now = ttak_get_tick_count();

    ttak_io_status_t status = ttak_io_guard_init(&reader_guard, reader_fd, owner, reader_ttl, now);
    if (status != TTAK_IO_SUCCESS) {
        fprintf(stderr, "[lesson42] reader guard init failed (%d)\n", status);
        close(pipe_base[0]);
        close(pipe_base[1]);
        close(writer_fd);
        ttak_owner_destroy(owner);
        return 1;
    }

    status = ttak_io_guard_init(&writer_guard, writer_fd, owner, writer_ttl, now);
    if (status != TTAK_IO_SUCCESS) {
        fprintf(stderr, "[lesson42] writer guard init failed (%d)\n", status);
        ttak_io_guard_close(&reader_guard, ttak_get_tick_count());
        close(pipe_base[0]);
        close(pipe_base[1]);
        ttak_owner_destroy(owner);
        return 1;
    }

    const size_t slot_capacity = 256;
    uint64_t buffer_lifetime = TT_SECOND(5);
    write_slot = ttak_mem_alloc(slot_capacity, buffer_lifetime, ttak_get_tick_count());
    read_slot = ttak_mem_alloc(slot_capacity, buffer_lifetime, ttak_get_tick_count());
    if (!write_slot || !read_slot) {
        fprintf(stderr, "[lesson42] failed to allocate IO buffers\n");
        goto cleanup;
    }
    printf("[lesson42] allocated %zu-byte IO staging buffers\n", slot_capacity);

    printf("[lesson42] pipe + guard setup complete\n");

    const char *chunk1 = "guarded io chunk #1";
    size_t chunk1_len = strlen(chunk1);
    size_t bytes = 0;
    printf("[lesson42] sending first payload via ttak_io_sync_write\n");
    memcpy(write_slot, chunk1, chunk1_len);
    status = ttak_io_sync_write(&writer_guard, write_slot, chunk1_len, &bytes, ttak_get_tick_count());
    if (status != TTAK_IO_SUCCESS) {
        fprintf(stderr, "[lesson42] initial write failed (%d)\n", status);
        goto cleanup;
    }
    printf("[lesson42] wrote %zu bytes: \"%s\"\n", bytes, chunk1);

    short revents = 0;
    status = ttak_io_poll_wait(&reader_guard,
                               POLLIN,
                               1000,
                               poll_logger,
                               "chunk1",
                               &revents,
                               false,
                               ttak_get_tick_count());
    if (status != TTAK_IO_SUCCESS) {
        fprintf(stderr, "[lesson42] poll failed (%d)\n", status);
        goto cleanup;
    }

    size_t read_bytes = 0;
    status = ttak_io_sync_read(&reader_guard, read_slot, chunk1_len, &read_bytes, ttak_get_tick_count());
    if (status != TTAK_IO_SUCCESS) {
        fprintf(stderr, "[lesson42] read failed (%d)\n", status);
        goto cleanup;
    }
    if (read_bytes < slot_capacity) {
        read_slot[read_bytes] = '\0';
    }
    printf("[lesson42] read %zu bytes: \"%s\"\n", read_bytes, read_slot);

    printf("[lesson42] sleeping 350ms to force guard expiry\n");
    usleep(350000);
    read_bytes = 0;
    status = ttak_io_sync_read(&reader_guard, read_slot, 1, &read_bytes, ttak_get_tick_count());
    if (status != TTAK_IO_SUCCESS) {
        printf("[lesson42] guard expired as expected (status=%d)\n", status);
    } else {
        printf("[lesson42] unexpected read after expiry (%zu bytes)\n", read_bytes);
    }

    ttak_io_guard_close(&reader_guard, ttak_get_tick_count());
    int refreshed_fd = duplicate_fd(pipe_base[0], "dup(reader-refresh)");
    if (refreshed_fd < 0) {
        goto cleanup;
    }
    status = ttak_io_guard_init(&reader_guard, refreshed_fd, owner, reader_ttl, ttak_get_tick_count());
    if (status != TTAK_IO_SUCCESS) {
        fprintf(stderr, "[lesson42] reader guard re-init failed (%d)\n", status);
        goto cleanup;
    }

    const char *chunk2 = "guarded io chunk #2";
    size_t chunk2_len = strlen(chunk2);
    memcpy(write_slot, chunk2, chunk2_len);
    status = ttak_io_sync_write(&writer_guard, write_slot, chunk2_len, &bytes, ttak_get_tick_count());
    if (status != TTAK_IO_SUCCESS) {
        fprintf(stderr, "[lesson42] second write failed (%d)\n", status);
        goto cleanup;
    }
    status = ttak_io_poll_wait(&reader_guard,
                               POLLIN,
                               1000,
                               poll_logger,
                               "chunk2",
                               &revents,
                               false,
                               ttak_get_tick_count());
    if (status != TTAK_IO_SUCCESS) {
        fprintf(stderr, "[lesson42] poll (chunk2) failed (%d)\n", status);
        goto cleanup;
    }
    read_bytes = 0;
    status = ttak_io_sync_read(&reader_guard, read_slot, chunk2_len, &read_bytes, ttak_get_tick_count());
    if (status != TTAK_IO_SUCCESS) {
        fprintf(stderr, "[lesson42] read chunk2 failed (%d)\n", status);
        goto cleanup;
    }
    if (read_bytes < slot_capacity) {
        read_slot[read_bytes] = '\0';
    }
    printf("[lesson42] read %zu bytes after rehydrating guard: \"%s\"\n", read_bytes, read_slot);

    printf("[lesson42] waiting 250ms before manual refresh\n");
    usleep(250000);
    status = ttak_io_guard_refresh(&reader_guard, ttak_get_tick_count());
    if (status != TTAK_IO_SUCCESS) {
        fprintf(stderr, "[lesson42] manual refresh failed (%d)\n", status);
        goto cleanup;
    }
    printf("[lesson42] guard TTL manually extended\n");
    usleep(100000);

    const char *chunk3 = "chunk #3 after refresh";
    size_t chunk3_len = strlen(chunk3);
    memcpy(write_slot, chunk3, chunk3_len);
    status = ttak_io_sync_write(&writer_guard, write_slot, chunk3_len, &bytes, ttak_get_tick_count());
    if (status != TTAK_IO_SUCCESS) {
        fprintf(stderr, "[lesson42] third write failed (%d)\n", status);
        goto cleanup;
    }
    status = ttak_io_poll_wait(&reader_guard,
                               POLLIN,
                               1000,
                               poll_logger,
                               "chunk3",
                               &revents,
                               false,
                               ttak_get_tick_count());
    if (status != TTAK_IO_SUCCESS) {
        fprintf(stderr, "[lesson42] poll (chunk3) failed (%d)\n", status);
        goto cleanup;
    }
    read_bytes = 0;
    status = ttak_io_sync_read(&reader_guard, read_slot, chunk3_len, &read_bytes, ttak_get_tick_count());
    if (status != TTAK_IO_SUCCESS) {
        fprintf(stderr, "[lesson42] read chunk3 failed (%d)\n", status);
        goto cleanup;
    }
    if (read_bytes < slot_capacity) {
        read_slot[read_bytes] = '\0';
    }
    printf("[lesson42] manual refresh kept guard alive long enough for: \"%s\"\n", read_slot);

    printf("[lesson42] starting async read demo via ttak_io_async_read\n");
    ttak_async_init(0);
    async_initialized = true;

    status = ttak_io_guard_refresh(&reader_guard, ttak_get_tick_count());
    if (status != TTAK_IO_SUCCESS) {
        fprintf(stderr, "[lesson42] guard refresh before async read failed (%d)\n", status);
        goto cleanup;
    }

    const char *chunk4 = "chunk #4 via async read";
    size_t chunk4_len = strlen(chunk4);
    lesson42_async_state_t async_state = {
        .completed = false,
        .capacity = slot_capacity,
        .buffer = read_slot,
        .status = TTAK_IO_ERR_SYS_FAILURE,
        .bytes = 0
    };

    status = ttak_io_async_read(&reader_guard,
                                read_slot,
                                chunk4_len,
                                1000,
                                lesson42_async_read_done,
                                &async_state,
                                ttak_get_tick_count());
    if (status != TTAK_IO_SUCCESS) {
        fprintf(stderr, "[lesson42] async read dispatch failed (%d)\n", status);
        goto cleanup;
    }

    usleep(50000);
    memcpy(write_slot, chunk4, chunk4_len);
    status = ttak_io_sync_write(&writer_guard, write_slot, chunk4_len, &bytes, ttak_get_tick_count());
    if (status != TTAK_IO_SUCCESS) {
        fprintf(stderr, "[lesson42] async demo write failed (%d)\n", status);
        goto cleanup;
    }

    while (!async_state.completed) {
        usleep(1000);
    }

    if (async_state.status == TTAK_IO_SUCCESS) {
        printf("[lesson42] async read completed with %zu bytes: \"%s\"\n", async_state.bytes, read_slot);
    } else {
        printf("[lesson42] async read failed (status=%d, bytes=%zu)\n", async_state.status, async_state.bytes);
    }

cleanup:
    ttak_io_guard_close(&reader_guard, ttak_get_tick_count());
    ttak_io_guard_close(&writer_guard, ttak_get_tick_count());
    if (async_initialized) {
        ttak_async_shutdown();
    }
    if (write_slot) {
        ttak_mem_free(write_slot);
    }
    if (read_slot) {
        ttak_mem_free(read_slot);
    }
    close(pipe_base[0]);
    close(pipe_base[1]);
    ttak_owner_destroy(owner);
    return 0;
}
