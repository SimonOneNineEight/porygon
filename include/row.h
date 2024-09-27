
#ifndef ROW_H
#define ROW_H

#include <stddef.h>

#include "abuf.h"

typedef struct erow {
  int index;
  int size;
  int rsize; // render size
  char *chars;
  char *render;
  unsigned char *hl;
  int hl_open_comment;
} erow;


void editorInsertRow(int at, char *s, size_t len);
void editorDelRow(int at);
void editorRowInsertChar(erow *row, int at, int c);
void editorRowDelChar(erow *row, int at);
void editorRowAppendString(erow *row, char *s, size_t len);
void editorUpdateRow(erow *row);
int editorRowCxToRx(erow *row, int cx);
int editorRowRxToCx(erow *row, int rx);
void editorDrawRows(struct abuf *ab);
void editorScroll();
void editorFreeRow();

#endif
