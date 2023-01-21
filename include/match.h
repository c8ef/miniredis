#pragma once

#include <stdbool.h>

// '*'     matches any sequence of non-separator characters
// '?'     matches any single non-separator
// c       matches character c (c != '*', '?')
// '\\' c  matches character c
bool match(const char* pat, long plen, const char* str, long slen);
