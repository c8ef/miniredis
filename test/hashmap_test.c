#include "hashmap.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <time.h>

static int compare_ints(const void* a, const void* b) {
  return *(int*)a - *(int*)b;
}

static uint64_t hash_int(const void* item) {
  return hashmap_xxhash(item, sizeof(int));
}

int main() {
  const int N = 20000;
  srand(time(NULL));

  int vals[N];
  for (int i = 0; i < N; ++i) {
    vals[i] = rand();
  }

  struct hashmap* map = hashmap_new(sizeof(int), 0, hash_int, compare_ints);
  assert(map != NULL);

  for (int i = 0; i < N; ++i) {
    assert(map->count == i);
    assert(!hashmap_get(map, &vals[i]));
    assert(!hashmap_delete(map, &vals[i]));

    hashmap_set(map, &vals[i]);

    int* v = NULL;
    for (int j = 0; j < i; ++j) {
      v = hashmap_get(map, &vals[j]);
      assert(v && *v == vals[j]);
    }
  }

  for (int i = 0; i < N; ++i) {
    hashmap_delete(map, &vals[i]);
    assert(map->count == (N - i - 1));
  }
}