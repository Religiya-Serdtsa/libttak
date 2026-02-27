#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h> // For rand() seed if needed
#include <ttak/mem/mem.h>
#include <ttak/timing/timing.h> // For ttak_get_tick_count
#include "../internal/ttak/mem_internal.h" // For internal definitions like ttak_allocation_tier_t

// A simple macro for reporting test results
#define TEST_ASSERT(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, message); \
        exit(EXIT_FAILURE); \
    } \
    fprintf(stderr, "[PASS] %s\n", message); \
} while(0)

#if EMBEDDED
#define EXPECTED_GENERAL_TIER TTAK_ALLOC_TIER_BUDDY
#define GENERAL_TIER_LABEL "BUDDY"
#else
#define EXPECTED_GENERAL_TIER TTAK_ALLOC_TIER_GENERAL
#define GENERAL_TIER_LABEL "GENERAL"
#endif

// Function to get a dummy current timestamp
static uint64_t get_test_tick_count(void) {
    static uint64_t tick = 1;
    return tick++;
}

void test_pocket_allocator(void) {
    fprintf(stderr, "\n--- Running Pocket Allocator Tests ---\n");
    size_t sizes[] = {8, 16, 32, 64};
    for (int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        size_t test_size = sizes[i];
        fprintf(stderr, "Testing pocket alloc for size %zu...\n", test_size);

        // Allocate
        uint64_t now = get_test_tick_count();
        void* ptr = ttak_mem_alloc_safe(test_size, 100, now, false, false, true, false, TTAK_MEM_DEFAULT);
        TEST_ASSERT(ptr != NULL, "Pocket alloc returned non-NULL");

        ttak_mem_header_t* header = (ttak_mem_header_t*)ptr - 1;
        TEST_ASSERT(header->magic == TTAK_MAGIC_NUMBER, "Header magic is correct");
        TEST_ASSERT(header->size == test_size, "Header reports correct user size");
        TEST_ASSERT(header->allocation_tier == TTAK_ALLOC_TIER_POCKET, "Allocation tier is POCKET");
        TEST_ASSERT(header->freed == false, "Header reports not freed");

        // Write some data
        memset(ptr, 0xAA, test_size);
        TEST_ASSERT(*(char*)ptr == (char)0xAA, "Data written correctly");

        // Free
        ttak_mem_free(ptr);
        fprintf(stderr, "Pocket alloc for size %zu passed.\n", test_size);
    }
}

void test_vma_allocator(void) {
    fprintf(stderr, "\n--- Running VMA Allocator Tests ---\n");
    size_t sizes[] = {150, 200, 250}; // Medium VMA sizes (above pocket limit of 128, below VMA limit of 256)
    for (int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        size_t test_size = sizes[i];
        fprintf(stderr, "Testing VMA alloc for size %zu...\n", test_size);

        // Allocate
        uint64_t now = get_test_tick_count();
        void* ptr = ttak_mem_alloc_safe(test_size, 100, now, false, false, true, false, TTAK_MEM_DEFAULT);
        TEST_ASSERT(ptr != NULL, "VMA alloc returned non-NULL");

        ttak_mem_header_t* header = (ttak_mem_header_t*)ptr - 1;
        TEST_ASSERT(header->magic == TTAK_MAGIC_NUMBER, "Header magic is correct");
        TEST_ASSERT(header->size == test_size, "Header reports correct user size");
        TEST_ASSERT(header->allocation_tier == TTAK_ALLOC_TIER_VMA, "Allocation tier is VMA");
        TEST_ASSERT(header->freed == false, "Header reports not freed");

        // Write some data
        memset(ptr, 0xBB, test_size);
        TEST_ASSERT(*(char*)ptr == (char)0xBB, "Data written correctly");

        // Free
        ttak_mem_free(ptr);
        fprintf(stderr, "VMA alloc for size %zu passed.\n", test_size);
    }
}

void test_general_allocator(void) {
    fprintf(stderr, "\n--- Running General Allocator Tests ---\n");
    size_t sizes[] = {1024 * 64, 1024 * 1024}; // Large sizes
    for (int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        size_t test_size = sizes[i];
        fprintf(stderr, "Testing General alloc for size %zu...\n", test_size);

        // Allocate
        uint64_t now = get_test_tick_count();
        void* ptr = ttak_mem_alloc_safe(test_size, 100, now, false, false, true, false, TTAK_MEM_DEFAULT);
        TEST_ASSERT(ptr != NULL, "General alloc returned non-NULL");

        ttak_mem_header_t* header = (ttak_mem_header_t*)ptr - 1;
        TEST_ASSERT(header->magic == TTAK_MAGIC_NUMBER, "Header magic is correct");
        TEST_ASSERT(header->size == test_size, "Header reports correct user size");
        TEST_ASSERT(header->allocation_tier == EXPECTED_GENERAL_TIER, "Allocation tier is " GENERAL_TIER_LABEL);
        TEST_ASSERT(header->freed == false, "Header reports not freed");

        // Write some data
        memset(ptr, 0xCC, test_size);
        TEST_ASSERT(*(char*)ptr == (char)0xCC, "Data written correctly");

        // Free
        ttak_mem_free(ptr);
        fprintf(stderr, "General alloc for size %zu passed.\n", test_size);
    }
}

int main(void) {
    ttak_mem_set_trace(0); // Disable tracing for cleaner test output
    // Initialize global_mem_tree and global_ptr_map if they weren't used by earlier tests
    // (ensure_global_map in ttak_mem_alloc_safe should handle this)

    test_pocket_allocator();
    test_vma_allocator();
    test_general_allocator();

    fprintf(stderr, "\nAll new memory module tests completed.\n");
    return 0;
}
