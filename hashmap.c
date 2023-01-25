#include "hashmap.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xxhash.h>

#define panic(msg)                                                     \
  {                                                                    \
    fprintf(stderr, "panic: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1);                                                           \
  }

struct bucket {
  uint64_t hash : 48;
  uint64_t psl : 16;
};

// hashmap is an open addressed hash map using robinhood hashing.
struct hashmap {
  bool oom;
  size_t elsize;
  size_t cap;
  uint64_t (*hash)(const void* item);
  int (*compare)(const void* a, const void* b);
  size_t bucketsz;
  size_t nbuckets;
  size_t count;
  size_t mask;
  size_t growat;
  size_t shrinkat;
  void* buckets;
  void* spare;
  void* edata;
};

static struct bucket* bucket_at(struct hashmap* map, size_t index) {
  return (struct bucket*)(((char*)map->buckets) + (map->bucketsz * index));
}

static void* bucket_item(struct bucket* entry) {
  return ((char*)entry) + sizeof(struct bucket);
}

static uint64_t get_hash(struct hashmap* map, const void* key) {
  return map->hash(key) << 16 >> 16;
}

// hashmap_new returns a new hash map.
// Param `elsize` is the size of each element in the tree. Every element that
// is inserted, deleted, or retrieved will be this size.
// Param `cap` is the default lower capacity of the hashmap. Setting this to
// zero will default to 16.
// Param `hash` is a function that generates a hash value for an item. It's
// important that you provide a good hash function, otherwise it will perform
// poorly or be vulnerable to Denial-of-service attacks.
// Param `compare` is a function that compares items in the tree. See the
// qsort stdlib function for an example of how this function works.
// The hashmap must be freed with hashmap_free().
// Param `elfree` is a function that frees a specific item. This should be NULL
// unless you're storing some kind of reference data in the hash.
struct hashmap* hashmap_new(size_t elsize, size_t cap,
                            uint64_t (*hash)(const void* item),
                            int (*compare)(const void* a, const void* b)) {
  size_t ncap = 16;
  if (cap < ncap) {
    cap = ncap;
  } else {
    while (ncap < cap) {
      ncap *= 2;
    }
    cap = ncap;
  }
  size_t bucketsz = sizeof(struct bucket) + elsize;
  while (bucketsz & (sizeof(uintptr_t) - 1)) {
    bucketsz++;
  }
  // hashmap + spare + edata
  size_t size = sizeof(struct hashmap) + bucketsz * 2;
  struct hashmap* map = malloc(size);
  if (!map) {
    return NULL;
  }
  memset(map, 0, sizeof(struct hashmap));
  map->elsize = elsize;
  map->bucketsz = bucketsz;
  map->hash = hash;
  map->compare = compare;
  map->spare = ((char*)map) + sizeof(struct hashmap);
  map->edata = (char*)map->spare + bucketsz;
  map->cap = cap;
  map->nbuckets = cap;
  map->mask = map->nbuckets - 1;
  map->buckets = malloc(map->bucketsz * map->nbuckets);
  if (!map->buckets) {
    free(map);
    return NULL;
  }
  memset(map->buckets, 0, map->bucketsz * map->nbuckets);
  map->growat = map->nbuckets * 0.75;
  map->shrinkat = map->nbuckets * 0.10;
  return map;
}

static bool resize(struct hashmap* map, size_t new_cap) {
  struct hashmap* map2 =
      hashmap_new(map->elsize, new_cap, map->hash, map->compare);
  if (!map2) {
    return false;
  }
  for (size_t i = 0; i < map->nbuckets; i++) {
    struct bucket* entry = bucket_at(map, i);
    if (!entry->psl) {
      continue;
    }
    entry->psl = 1;
    size_t j = entry->hash & map2->mask;
    for (;;) {
      struct bucket* bucket = bucket_at(map2, j);
      if (bucket->psl == 0) {
        memcpy(bucket, entry, map->bucketsz);
        break;
      }
      if (bucket->psl < entry->psl) {
        memcpy(map2->spare, bucket, map->bucketsz);
        memcpy(bucket, entry, map->bucketsz);
        memcpy(entry, map2->spare, map->bucketsz);
      }
      j = (j + 1) & map2->mask;
      entry->psl += 1;
    }
  }
  free(map->buckets);
  map->buckets = map2->buckets;
  map->nbuckets = map2->nbuckets;
  map->mask = map2->mask;
  map->growat = map2->growat;
  map->shrinkat = map2->shrinkat;
  free(map2);
  return true;
}

// hashmap_set inserts or replaces an item in the hash map. If an item is
// replaced then it is returned otherwise NULL is returned. This operation
// may allocate memory. If the system is unable to allocate additional
// memory then NULL is returned and hashmap_oom() returns true.
void* hashmap_set(struct hashmap* map, const void* item) {
  if (!item) {
    panic("item is null");
  }
  map->oom = false;
  if (map->count == map->growat) {
    if (!resize(map, map->nbuckets * 2)) {
      map->oom = true;
      return NULL;
    }
  }

  struct bucket* entry = map->edata;
  entry->hash = get_hash(map, item);
  entry->psl = 1;
  memcpy(bucket_item(entry), item, map->elsize);

  size_t i = entry->hash & map->mask;
  for (;;) {
    struct bucket* bucket = bucket_at(map, i);
    if (bucket->psl == 0) {
      memcpy(bucket, entry, map->bucketsz);
      map->count++;
      return NULL;
    }
    if (entry->hash == bucket->hash &&
        map->compare(bucket_item(entry), bucket_item(bucket)) == 0) {
      memcpy(map->spare, bucket_item(bucket), map->elsize);
      memcpy(bucket_item(bucket), bucket_item(entry), map->elsize);
      return map->spare;
    }
    if (bucket->psl < entry->psl) {
      memcpy(map->spare, bucket, map->bucketsz);
      memcpy(bucket, entry, map->bucketsz);
      memcpy(entry, map->spare, map->bucketsz);
    }
    i = (i + 1) & map->mask;
    entry->psl += 1;
  }
}

// hashmap_get returns the item based on the provided key. If the item is not
// found then NULL is returned.
void* hashmap_get(struct hashmap* map, const void* key) {
  if (!key) {
    panic("key is null");
  }
  uint64_t hash = get_hash(map, key);
  size_t i = hash & map->mask;
  for (;;) {
    struct bucket* bucket = bucket_at(map, i);
    if (!bucket->psl) {
      return NULL;
    }
    if (bucket->hash == hash && map->compare(key, bucket_item(bucket)) == 0) {
      return bucket_item(bucket);
    }
    i = (i + 1) & map->mask;
  }
}

// hashmap_probe returns the item in the bucket at position or NULL if an item
// is not set for that bucket. The position is 'moduloed' by the number of
// buckets in the hashmap.
void* hashmap_probe(struct hashmap* map, uint64_t position) {
  size_t i = position & map->mask;
  struct bucket* bucket = bucket_at(map, i);
  if (!bucket->psl) {
    return NULL;
  }
  return bucket_item(bucket);
}

// hashmap_delete removes an item from the hash map and returns it. If the
// item is not found then NULL is returned.
void* hashmap_delete(struct hashmap* map, void* key) {
  if (!key) {
    panic("key is null");
  }
  map->oom = false;
  uint64_t hash = get_hash(map, key);
  size_t i = hash & map->mask;
  for (;;) {
    struct bucket* bucket = bucket_at(map, i);
    if (!bucket->psl) {
      return NULL;
    }
    if (bucket->hash == hash && map->compare(key, bucket_item(bucket)) == 0) {
      memcpy(map->spare, bucket_item(bucket), map->elsize);
      bucket->psl = 0;
      for (;;) {
        struct bucket* prev = bucket;
        i = (i + 1) & map->mask;
        bucket = bucket_at(map, i);
        if (bucket->psl <= 1) {
          prev->psl = 0;
          break;
        }
        memcpy(prev, bucket, map->bucketsz);
        prev->psl--;
      }
      map->count--;
      if (map->nbuckets > map->cap && map->count <= map->shrinkat) {
        // Ignore the return value. It's ok for the resize operation to
        // fail to allocate enough memory because a shrink operation
        // does not change the integrity of the data.
        resize(map, map->nbuckets / 2);
      }
      return map->spare;
    }
    i = (i + 1) & map->mask;
  }
}

// hashmap_count returns the number of items in the hash map.
size_t hashmap_count(struct hashmap* map) { return map->count; }

// hashmap_free frees the hash map
// Every item is called with the element-freeing function given in hashmap_new,
// if present, to free any data referenced in the elements of the hashmap.
void hashmap_free(struct hashmap* map) {
  if (!map) return;
  free(map->buckets);
  free(map);
}

// hashmap_oom returns true if the last hashmap_set() call failed due to the
// system being out of memory.
bool hashmap_oom(struct hashmap* map) { return map->oom; }

// hashmap_scan iterates over all items in the hash map
// Param `iter` can return false to stop iteration early.
// Returns false if the iteration has been stopped early.
bool hashmap_scan(struct hashmap* map,
                  bool (*iter)(const void* item, void* udata), void* udata) {
  for (size_t i = 0; i < map->nbuckets; i++) {
    struct bucket* bucket = bucket_at(map, i);
    if (bucket->psl) {
      if (!iter(bucket_item(bucket), udata)) {
        return false;
      }
    }
  }
  return true;
}

uint64_t hashmap_xxhash(const void* data, size_t len) {
  return XXH3_64bits(data, len);
}
