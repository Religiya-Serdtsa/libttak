#define _DEFAULT_SOURCE
#include <unistd.h>
#include <ttak/mem/mem.h>
#include <ttak/mem/owner.h>
#include <ttak/log/logger.h>
#include <ttak/timing/timing.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HASH_SIZE 16

typedef struct lru_entry {
	char *key;
	void *data;
	size_t size;
	uint64_t last_access;
	struct lru_entry *next;  /* For Doubly Linked List */
	struct lru_entry *prev;  /* For Doubly Linked List */
	struct lru_entry *h_next; /* For Hash Bucket Chaining */
} lru_entry_t;

typedef struct {
	ttak_owner_t *owner;
	int capacity;
	int current_count;
	size_t total_usage;
	lru_entry_t *head;
	lru_entry_t *tail;
	lru_entry_t *buckets[HASH_SIZE]; /* Hash table for O(1) lookup */
} lru_cache_t;

/* Simple string hash function */
static int get_hash(const char *key) {
	unsigned int hash = 0;
	while (*key) hash = (hash << 5) + *key++;
	return hash % HASH_SIZE;
}

lru_cache_t* create_lru(int capacity) {
	lru_cache_t *cache = (lru_cache_t*)calloc(1, sizeof(lru_cache_t));
	cache->owner = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
	cache->capacity = capacity;
	return cache;
}

static void detach_node(lru_cache_t *cache, lru_entry_t *node) {
	if (node->prev) node->prev->next = node->next;
	else cache->head = node->next;
	if (node->next) node->next->prev = node->prev;
	else cache->tail = node->prev;
}

static void attach_to_head(lru_cache_t *cache, lru_entry_t *node) {
	node->next = cache->head;
	node->prev = NULL;
	if (cache->head) cache->head->prev = node;
	cache->head = node;
	if (!cache->tail) cache->tail = node;
}

/* Remove from Hash Bucket */
static void hash_remove(lru_cache_t *cache, lru_entry_t *node) {
	int idx = get_hash(node->key);
	lru_entry_t **curr = &cache->buckets[idx];
	while (*curr) {
		if (*curr == node) {
			*curr = node->h_next;
			return;
		}
		curr = &((*curr)->h_next);
	}
}

void lru_put_ex(lru_cache_t *cache, const char *key, size_t val_size, uint64_t now, uint64_t ttl) {
	/* 1. Synchronous Eviction based on Capacity */
	while (cache->current_count >= cache->capacity) {
		lru_entry_t *victim = cache->tail;
		if (!victim) break;

		detach_node(cache, victim);
		hash_remove(cache, victim);

		cache->total_usage -= victim->size;
		ttak_mem_free(victim->data);
		ttak_mem_free(victim->key);
		ttak_mem_free(victim);

		cache->current_count--;
		tt_autoclean_dirty_pointers(now);
	}

	/* 2. Optimized Allocation */
	lru_entry_t *node = (lru_entry_t*)ttak_mem_alloc_with_flags(sizeof(lru_entry_t), ttl, now, TTAK_MEM_STRICT_CHECK);
	node->key = (char*)ttak_mem_alloc_with_flags(strlen(key) + 1, ttl, now, TTAK_MEM_STRICT_CHECK);
	strcpy(node->key, key);
	node->data = ttak_mem_alloc_with_flags(val_size, ttl, now, TTAK_MEM_STRICT_CHECK);
	node->size = val_size;
	node->last_access = now;

	/* Hash insertion */
	int idx = get_hash(key);
	node->h_next = cache->buckets[idx];
	cache->buckets[idx] = node;

	/* Structural Update */
	ttak_mem_access(node->data, now);
	ttak_owner_register_resource(cache->owner, key, node->data);
	attach_to_head(cache, node);

	cache->current_count++;
	cache->total_usage += val_size;

	/* Throttling for visualizer stability */
	tt_autoclean_dirty_pointers(now);
	usleep(80000);
}

void lru_get(lru_cache_t *cache, const char *key, uint64_t now) {
	int idx = get_hash(key);
	lru_entry_t *curr = cache->buckets[idx];
	while (curr) {
		if (strcmp(curr->key, key) == 0) {
			ttak_mem_access(curr->data, now);
			curr->last_access = now;
			detach_node(cache, curr);
			attach_to_head(cache, curr);
			return;
		}
		curr = curr->h_next;
	}
}

int main() {
	uint64_t now = ttak_get_tick_count();
	printf("--- [TTAK TUTORIAL 36] Production-Level LRU Trace ---\n");

	ttak_mem_configure_gc(TT_MILLI_SECOND(5), TT_MILLI_SECOND(20), 32);
	ttak_mem_set_trace(1);

	lru_cache_t *cache = create_lru(5);

	for (int i = 0; i < 20; i++) {
		char key[16];
		sprintf(key, "item_%d", i);
		now += 10;
		lru_put_ex(cache, key, 100 * (i + 1), now, TT_MILLI_SECOND(50));

		if (i % 5 == 0) {
			printf("  > Step %d: Usage %zu Bytes\n", i, cache->total_usage);
		}
	}

	tt_autoclean_dirty_pointers(now + 100);
	ttak_mem_set_trace(0);
	return 0;
}
