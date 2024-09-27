#ifndef TERMINAL_H
#define TERMINAL_H

void enableRawMode();
void disableRawMode();
int editorReadKey();
int getCursorPosition(int *rows, int *cols);
int getWindowSize(int *rows, int *cols);
void die(const char *s);

#endif
