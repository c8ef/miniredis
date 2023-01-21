#include "buf.h"

#include <assert.h>
#include <stdlib.h>
#include <time.h>

int main() {
  srand(time(NULL));
  struct buf buf = {0};
  int N = 2000;

  char* str = malloc(N + 1);
  for (int i = 0; i < N; ++i) {
    str[i] = rand();
  }

  for (int i = 0; i < N; ++i) {
    buf_append_byte(&buf, str[i]);
  }

  for (int i = 0; i < N; ++i) {
    assert(str[i] == buf.data[i]);
  }
  buf_clear(&buf);
  free(str);
  return 0;
}