#include <stdlib.h>
#include <string.h>

#include "abuf.h"

void abAppend(struct abuf *ab, const char *s, int len) {
  // reallocte memory for the appended string
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL)
    return;

  // copy the appended string to new allocated memory start from s to len
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) { free(ab->b); }
