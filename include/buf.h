#pragma once

#include <stdbool.h>
#include <sys/types.h>

struct buf {
  char* data;
  size_t len, cap;
};

// append data to the buffer
// if data is null-terminated string, len can be -1
bool buf_append(struct buf* buf, const char* data, ssize_t len);
// append a single byte to buffer
bool buf_append_byte(struct buf* buf, char ch);
// clear the buffer and free all data
void buf_clear(struct buf* buf);
