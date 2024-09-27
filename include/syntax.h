#ifndef SYNTAX_H
#define SYNTAX_H

#include "row.h"

enum editorHighlight {
  HL_NORMAL = 0,
  HL_MLCOMMENT,
  HL_COMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_STRING,
  HL_NUMBER,
  HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)

struct editorSyntax {
  char *filetype;
  char **filematch;
  char **keywords;
  char *singleline_comment_start;
  char *multiline_comment_start;
  char *multiline_comment_end;
  int flags;
};
void editorUpdateSyntax(struct erow *row);
void editorSelectSyntaxHighlight();
int editorSyntaxToColor(int hl);

#endif
