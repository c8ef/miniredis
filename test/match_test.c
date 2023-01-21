#include "match.h"

#include <assert.h>

int main() {
  assert(match("*", -1, "", -1));
  assert(match("", -1, "", -1));
  assert(!match("", -1, "hello world", -1));
  assert(!match("jello world", -1, "hello world", -1));
  assert(match("*", -1, "hello world", -1));
  assert(match("*world*", -1, "hello world", -1));
  assert(match("*world", -1, "hello world", -1));
  assert(match("hello*", -1, "hello world", -1));
  assert(!match("jello*", -1, "hello world", -1));
  assert(match("hello?world", -1, "hello world", -1));
  assert(!match("jello?world", -1, "hello world", -1));
  assert(match("he*o?world", -1, "hello world", -1));
  assert(match("he*o?wor*", -1, "hello world", -1));
  assert(match("he*o?*r*", -1, "hello world", -1));
  assert(match("h\\*ello", -1, "h*ello", -1));
  assert(!match("hello\\", -1, "hello\\", -1));
  assert(match("hello\\?", -1, "hello?", -1));
  assert(match("hello\\\\", -1, "hello\\", -1));
}