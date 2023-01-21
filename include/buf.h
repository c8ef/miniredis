#pragma once

#include <stdbool.h>
#include <sys/types.h>

struct buf {
  char* data;
  size_t len, cap;
};

bool buf_append(struct buf* buf, const char* data, ssize_t len);
bool buf_append_byte(struct buf* buf, char ch);
void buf_clear(struct buf* buf);
