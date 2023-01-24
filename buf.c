#include "buf.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// buf_append appends data to buffer. To append a null-terminated c-string
// specify -1 for the len.
bool buf_append(struct buf* buf, const char* data, ssize_t len) {
  if (len < 0) {
    len = strlen(data);
  }
  if (buf->len + len >= buf->cap) {
    size_t cap = buf->cap ? buf->cap * 2 : 1;
    while (buf->len + len > cap) {
      cap *= 2;
    }
    char* data = malloc(cap + 1);
    if (!data) {
      return false;
    }
    memcpy(data, buf->data, buf->len);
    free(buf->data);
    buf->data = data;
    buf->cap = cap;
  }
  memcpy(buf->data + buf->len, data, len);
  buf->len += len;
  buf->data[buf->len] = '\0';
  return true;
}

// buf_append_byte appends a single byte to buffer.
// Returns false if the
bool buf_append_byte(struct buf* buf, char ch) {
  if (buf->len == buf->cap) {
    return buf_append(buf, &ch, 1);
  }
  buf->data[buf->len] = ch;
  buf->len++;
  buf->data[buf->len] = '\0';
  return true;
}

// buf_clear clears the buffer and frees all data
void buf_clear(struct buf* buf) {
  if (buf->data) {
    free(buf->data);
  }
  memset(buf, 0, sizeof(struct buf));
}
