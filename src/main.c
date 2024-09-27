#include "editor.h"
#include "terminal.h"
#include "row.h"
#include "fileio.h"
#include "syntax.h"

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
 }

  editorSetStatusMessage(
      "HELP: Ctrl-S = save || Ctrl-Q = quit | Ctrl-F = find");

  while (1) {
    editorRefreshScreen();
    editorProcessKeyPress();
  }

  return 0;
}
