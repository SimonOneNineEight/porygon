#ifndef EDITOR_H
#define EDITOR_H

#include <termios.h>
#include <time.h>
#include <stddef.h>

#include "row.h"
#include "syntax.h"
#include "abuf.h"

#define PORYGON_VERSION "0.01"
#define PORYGON_TAB_STOP 8
#define PORYGON_QUIT_TIMES 3

#define CTRL_KEY(k) ((k) & 0x1f)

struct editorConfig {
    int cx, cy;
    int rx;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    struct erow *row;
    int dirty;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct editorSyntax *syntax;
    struct termios orig_termios;
};

extern struct editorConfig E;

void initEditor();
void editorSetStatusMessage(const char *fmt, ...);
void editorDrawStatusBar(struct abuf *ab); 
void editorDrawMessageBar(struct abuf *ab); 
void editorRefreshScreen();
void editorProcessKeyPress();
void enableRawMode();
void editorInsertChar(int c);
void editorInsertNewline();
void editorDelChar();
void editorFindCallback(char *query, int key);
void editorFind();
char *editorPrompt(char *prompt, void (*callback)(char *, int)); 
void editorMoveCursor(int key); 
void editorScroll();

#endif
