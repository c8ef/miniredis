#include "match.h"

#include <stdbool.h>
#include <string.h>

bool match(const char* pat, long plen, const char* str, long slen) {
  if (plen < 0) plen = strlen(pat);
  if (slen < 0) slen = strlen(str);

  while (plen > 0) {
    if (pat[0] == '\\') {
      if (plen == 1) return false;
      pat++;
      plen--;
    } else if (pat[0] == '*') {
      if (plen == 1) return true;
      if (pat[1] == '*') {
        pat++;
        plen--;
        continue;
      }
      if (match(pat + 1, plen - 1, str, slen)) return true;
      if (slen == 0) return false;
      str++;
      slen--;
      continue;
    }
    if (slen == 0) return false;
    if (pat[0] != '?' && str[0] != pat[0]) return false;
    pat++;
    plen--;
    str++;
    slen--;
  }
  return plen == 0 && slen == 0;
}