#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct hashmap;

struct hashmap* hashmap_new(size_t elsize, size_t cap,
                            uint64_t (*hash)(const void* item),
                            int (*compare)(const void* a, const void* b));
void hashmap_free(struct hashmap* map);
size_t hashmap_count(struct hashmap* map);
bool hashmap_oom(struct hashmap* map);
void* hashmap_get(struct hashmap* map, const void* item);
void* hashmap_set(struct hashmap* map, const void* item);
void* hashmap_delete(struct hashmap* map, void* item);
void* hashmap_probe(struct hashmap* map, uint64_t position);
bool hashmap_scan(struct hashmap* map,
                  bool (*iter)(const void* item, void* udata), void* udata);
uint64_t hashmap_xxhash(const void* data, size_t len);
