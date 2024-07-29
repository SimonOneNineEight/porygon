#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.01"
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3

enum editorKey {
	BACKSPACE = 127,
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN,
};

/*** data ***/
typedef struct erow {
	int size;
	int rsize; //render size
	char *chars;
	char *render;
} erow;

struct editorConfig {
	int cx, cy;
	int rx; // index render starts to render, when have tabs, will not render from the beginning.
	int rowoff;
	int coloff;
	int screenrows;
	int screencols;
	int numrows;
	erow *row;
	int dirty;
	char *filename;
	char statusmsg[80];
	time_t statusmsg_time;
	struct termios orig_termios;
};

struct editorConfig E;

/*** prototypes ***/
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char*, int));

/*** terminal ***/
void die(const char *s) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	// perror sees the global error `errno` and print the error message
	perror(s);
	exit(1);
}

void disableRawMode(){
	// Reset user termial to echoing	
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
		die("tcsetattr");
	}
}

void enableRawMode(){
	if (tcgetattr(STDIN_FILENO, &E.orig_termios)) die("tcgetattr");
	atexit(disableRawMode);

	struct termios raw = E.orig_termios;
	// reverse all below to turn off the functions
	// ECHO: echo mode, ICANON: canonical mode, ISIG: Ctrl-c and Ctrl-z, IEXTEN: fix Ctrl-o
	// IXON: Ctrl-s and Ctrl-q, IEXTEN: Ctrl-v
	// ICRNL: fix Ctrl-m to provide correct ACSII key (10 -> 13)
	// OPOST: all output processing
	raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);

	// set timeout for read()
	// VMIN = minimum number of bytes of input needed before read()
	// VTIME = max amount of time to wait before read() returns
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw)) {
		die("tcsetattr");
	}
}

int editorReadKey() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}

	if (c == '\x1b') {
		char seq[3];

		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
						case '1':
						case '7':
							return HOME_KEY;
						case '3': return DEL_KEY;
						case '4':
						case '8':
							return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
					}
				}
			} else {
				switch (seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
				}
			}
		} else if (seq[0] == 'O') {
				switch(seq[1]) {
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
		}
	}
	return c;
}

int getCursorPosition(int *rows, int *cols) {
	char buf[32];
	unsigned int i = 0;

	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

	while (i < sizeof(buf) - 2) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if (buf[i] == 'R') break;
		i ++;
	}
	buf[i] = '\0';
	
	// make sure the 
	if (buf[0] != '\x1b' || buf[i] != '[') return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

	return 0;
}

int getWindowSize(int *rows, int *cols) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		// move the cursor to bottom right, then use escape sequences to query the position of the cursor
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return getCursorPosition(rows, cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

/*** row operations ***/
int editorRowCxToRx(erow *row, int cx) {
	int rx = 0;
	int j;
	for (j = 0; j < cx; j++) {
		if (row->chars[j] == '\t')
			rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
		rx ++;
	}
	return rx;
}

int editorRowRxToCx(erow *row, int rx) {
	int cur_rx = 0;
	int cx;
	for (cx = 0; cx < row->size; cx++) {
		if (row->chars[cx] == '\t') {
			cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
		}
		cur_rx ++;

		if (cur_rx > rx) return cx;
	}
	return cx;
}

void editorUpdateRow(erow *row) {
	int tabs = 0;
	int j;
	for (j = 0; j<row->size; j++) {
		if (row->chars[j] == '\t') tabs++;
	}
	
	free(row->render);
	row->render = malloc(row->size + tabs * KILO_TAB_STOP + 1);
	
	int index = 0;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			row->render[index ++] = ' ';
			while (index % KILO_TAB_STOP != 0) row->render[index ++] = ' ';
		} else {
			row->render[index++] = row->chars[j];
		}
	}
	row->render[index] = '\0';
	row->rsize = index;
}

void editorInsertRow(int at, char *s, size_t len) {
	if (at < 0 || at > E.numrows) return;
	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
	memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';

	E.row[at].rsize = 0;
	E.row[at].render = NULL;
	editorUpdateRow(&E.row[at]);

	E.numrows ++;
	E.dirty ++;
}

void editorFreeRow(erow *row) {
	free(row->render);
	free(row->chars);
}

void editorDelRow(int at) {
	if (at < 0 || at >= E.numrows) return;
	editorFreeRow(&E.row[at]);
	memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
	E.numrows --;
	E.dirty ++;
}

void editorRowInsertChar(erow *row, int at, int c) {
	if (at < 0 || at > row->size) at = row->size;

	// add 2 byte because also make room for null bype
	row->chars = realloc(row->chars, row->size + 2);
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size ++;
	row->chars[at] = c;
	editorUpdateRow(row);
	E.dirty ++;
}

void editorRowDelChar(erow *row, int at) {
	if (at < 0 || at >= row->size) return;
	memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
	row->size --;
	editorUpdateRow(row);
	E.dirty ++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	E.dirty ++;
}


/*** edior operations ***/
void editorInsertChar(int c) {
	if (E.cy == E.numrows) {
		editorInsertRow(E.numrows, "", 0);
	}
	editorRowInsertChar(&E.row[E.cy], E.cx, c);
	E.cx ++;
}

void editorInsertNewline() {
	if (E.cx == 0) {
		editorInsertRow(E.cy, "", 0); 
	} else {
		erow *row = &E.row[E.cy];
		editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
		row = &E.row[E.cy];
		row->size = E.cx;
		row->chars[row->size] = '\0';
		editorUpdateRow(row);
	}
	E.cy ++;
	E.cx = 0;
}

void editorDelChar() {
	if (E.cy == E.numrows) return;
	if (E.cx == 0 && E.cy == 0) return;

	erow *row = &E.row[E.cy];
	if (E.cx > 0) {
		editorRowDelChar(row, E.cx - 1);
		E.cx --;
	} else {
		E.cx = E.row[E.cy - 1].size;
		editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
		editorDelRow(E.cy);
		E.cy --;
	}
}

/*** file i/o ***/
char *editorRowsToString(int *buflen) {
	int totallen = 0;
	int j;
	for (j = 0; j < E.numrows; j++) {
		totallen += E.row[j].size + 1;
	}
	*buflen = totallen;

	char *buf = malloc(totallen);
	char *p = buf;
	for (j = 0; j < E.numrows; j++) {
		memcpy(p, E.row[j].chars, E.row[j].size);
		p += E.row[j].size;
		*p = '\n';
		p ++;
	}
	
	return buf;
}

void editorOpen(char *filename) {
	free(E.filename);
	E.filename = strdup(filename);

	FILE *fp = fopen(filename, "r");
	if (!fp) die("fopen");

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	// getline is useful tool when we don't know how many memory to allocate
	// the arg[0] is where we want the memory point, arg[1] is how much memory allocated and arg[2] is the file we want to read
	while ((linelen = getline(&line, &linecap, fp)) != -1) {
		while(linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
			linelen --;
		}
		editorInsertRow(E.numrows, line, linelen);
	}
	
	free(line);
	fclose(fp);
	E.dirty = 0;
}

void editorSave() {
	if (E.filename == NULL) {
		E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
		if (E.filename == NULL) {
			editorSetStatusMessage("Save aborted");
			return;
		}
	}

	int len;
	char *buf = editorRowsToString(&len);

	int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1) {
		if (ftruncate(fd, len) != -1) {
			if (write(fd, buf, len) == len) {
				close(fd);
				free(buf);
				E.dirty = 0;
				editorSetStatusMessage("%d bytes written to dist", len);
				return;
			}
		}
		close(fd);
	}
	free(buf);
	editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** find ***/
void editorFindCallback(char *query, int key) {
	static int last_match = -1;
	static int direction = 1;

	if (key == '\r' || key == '\x1b') {
		last_match = -1;
		direction = 1;
		return;
	} else if (key == ARROW_UP) {
		direction = 1;
	} else if (key == ARROW_DOWN) {
		direction = -1;
	} else {
		last_match = -1;
		direction = 1;
	} 

	if (last_match == -1) direction = 1;
	int current = last_match;

	int i;
	for (i = 0; i < E.numrows; i ++) {
		current += direction;
		if (current == -1) current = E.numrows - 1;
		else if (current == E.numrows) current = 0;
		
		erow *row = &E.row[current];
		char *match = strstr(row->render, query);

		if (match) {
			last_match = current;
			E.cy = current;
			E.cx = editorRowRxToCx(row, match - row->render);
			// make the cursor to top of the screen
			E.rowoff = E.numrows;
			break;
		}
	}
}
void editorFind() {
	int saved_cx = E.cx;
	int saved_cy = E.cy;
	int saved_coloff = E.coloff;
	int saved_rowoff = E.rowoff;

	char *query = editorPrompt("Search: %s (ESC to cancel)", editorFindCallback);

	if (query) free(query);
	else {
		E.cx = saved_cx;
		E.cy = saved_cy;
		E.coloff = saved_coloff;
		E.rowoff = saved_rowoff;
	}
}

/*** append buffer ***/
//use buffer to collect small writes into a big write to enhance performance (all screen update at once), this abuf is a dynamic string that we created by ourself
struct abuf {
	char *b;
	int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
	// reallocte memory for the appended string
	char *new = realloc(ab->b, ab->len + len);
	
	if (new == NULL) return;
	
	// copy the appended string to new allocated memory start from s to len
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf *ab) {
	free(ab->b);
}

/*** output ***/
void editorScroll() {
	E.rx = 0;
	if (E.cy < E.numrows) {
		E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
	}
	// check if the cursor above visible window, if so, scroll up to where the cursor is
	if (E.cy < E.rowoff) {
		E.rowoff = E.cy;
	}

	// check if the cursor is below the visible window
	if (E.cy >= E.rowoff + E.screenrows) {
		E.rowoff = E.cy - E.screenrows + 1;
	}

	if (E.rx < E.coloff) {
		E.coloff = E.cx;
	}

	if (E.rx >= E.coloff + E.screencols) {
		E.coloff = E.rx - E.screencols + 1;
	}
}

void editorDrawRows(struct abuf *ab) {
	int y;
	for (y = 0; y < E.screenrows; y ++) {
		int filerow = y + E.rowoff;
		// check if we are drawing the text buffer
		if (filerow >= E.numrows) {
			// Add a welcome message when no args are passed (no file open)
			if (E.numrows == 0 && y == E.screenrows / 3) {
				char welcome[80];

				int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);

				if (welcomelen > E.screencols) welcomelen = E.screencols;

				// center the welcome message
				int padding = (E.screencols - welcomelen) / 2;
				if (padding) {
					abAppend(ab, "~", 1);
					padding --;
				}
				while (padding --) abAppend(ab, " ", 1);
				
				abAppend(ab, welcome, welcomelen);
			} else {
				abAppend(ab, "~", 1);
			}		
		} else {
			int len = E.row[filerow].rsize - E.coloff;
			if (len < 0) len = 0;
			if (len > E.screencols) len = E.screencols;
			abAppend(ab, &E.row[filerow].render[E.coloff], len);
		}


		abAppend(ab, "\x1b[K", 3);
		abAppend(ab, "\r\n", 2);
	}
}

void editorDrawStatusBar(struct abuf *ab) {
	// <Esc>[7m will switch to invert color and <Esc[m will switch back
	abAppend(ab, "\x1b[7m", 4);

	char status[80], rstatus[80];
	int len = snprintf(status, sizeof(status), "%.20s - %d lines", E.filename ? E.filename : "[No Name]", E.numrows, E.dirty ? "(modified)" : "");
	int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);

	if (len > E.screencols) len = E.screencols;
	abAppend(ab, status, len);

	while (len < E.screencols) {
		if (E.screencols - len == rlen) {
			abAppend(ab, rstatus, rlen);
			break;
		} else {
			abAppend(ab, " ", 1);
			len ++;
		}
	}

	abAppend(ab, "\x1b[m", 3);
	abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
	abAppend(ab, "\x1b[K", 3);
	int msglen = strlen(E.statusmsg);
	if (msglen > E.screencols) msglen = E.screencols;
	if (msglen && time(NULL) - E.statusmsg_time < 5)
		abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen() {
	editorScroll();

	struct abuf ab = ABUF_INIT;

	// hide the cursor before refresh the screen
	abAppend(&ab, "\x1b[?25l", 6);
	// [H reposition the cursor tor the top left corner
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);
	editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
	abAppend(&ab, buf, strlen(buf));
	
	// show the cursor after refresh
	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void editorSetStatusMessage(const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), format, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

/*** input ***/
char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
	size_t bufsize = 128;
	char *buf = malloc(bufsize);

	size_t buflen = 0;
	buf[0] = '\0';

	while(1) {
		editorSetStatusMessage(prompt, buf);
		editorRefreshScreen();

		int c = editorReadKey();

		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
			if (buflen != 0) buf[--buflen] = '\0';
		} else if (c == '\x1b') {
			editorSetStatusMessage("");
			if (callback) callback(buf, c);
			free(buf);
			return NULL;
		} else if (c == 'r') {
			if (buflen != 0) {
				editorSetStatusMessage("");
				return buf;
			}
		} else if (!iscntrl(c) && c < 128) {
			if (buflen == bufsize - 1) {
				bufsize *= 2;
				buf = realloc(buf, bufsize);
			}
			buf[buflen ++] = c;
			buf[buflen] = '\0';
		}

		if (callback) callback(buf, c);
	}
}

void editorMoveCursor(int key) {
	erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

	switch (key) {
		case ARROW_LEFT:
			if (E.cx != 0) E.cx --;
			else if (E.cy > 0) {
				// move the cursor to the last char of previous line if press left when cursor is at first char of the line.
				E.cy --;
				E.cx = E.row[E.cy].size;
			}
			break;
		case ARROW_RIGHT:
			// cursor will only move to the end of the text instead of the window
			if (row && E.cx < row->size) E.cx ++;
			else if (row && E.cx == row->size) {
				// cursor will move to the beginning of next line if press right at the last char
				E.cy ++;
				E.cx = 0;
			}
			break;
		case ARROW_UP:
			if (E.cy != 0) E.cy --;
			break;
		case ARROW_DOWN:
			if (E.cy != E.numrows) E.cy ++;
			break;
	}
	
	// avoid cursor pass the text
	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

	int rowlen = row ? row->size : 0;
	if (E.cx > rowlen) {
		E.cx = rowlen;
	}
}

void editorProcessKeyPress() {
// Map all the special key press to actions
	int c = editorReadKey();

	static int quit_times = KILO_QUIT_TIMES;

	switch (c) {
		case '\r':
			editorInsertNewline();
			break;
		case CTRL_KEY('q'):
			if (E.dirty && quit_times > 0) {
				editorSetStatusMessage("WARNING!!! File has unsaved changeds. ""Press Ctrl-Q %d more times to quit.", quit_times);
				quit_times --;
				return;
			}
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);

			exit(0);
			break;

		case CTRL_KEY('s'):
			editorSave();
			break;

		case HOME_KEY:
			E.cx = 0;
			break;

		case END_KEY:
			if (E.cy < E.numrows) E.cx = E.row[E.cy].size;
			break;

		case CTRL_KEY('f'):
			editorFind();
			break;

		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL_KEY:
			// delete the char at the cursor when press delete, delete char in front of cursor when backspace
			if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
			editorDelChar();
			break;

		case PAGE_UP:
		case PAGE_DOWN:
			{
				if (c == PAGE_UP) {
					E.cy = E.rowoff;
				} else if (c == PAGE_DOWN) {
					E.cy = E.rowoff + E.screenrows - 1;
					if (E.cy > E.numrows) E.cy = E.numrows;
				}

				int times = E.screenrows;
				while (times --) editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;

		case ARROW_LEFT:
		case ARROW_RIGHT:
		case ARROW_UP:
		case ARROW_DOWN:
			editorMoveCursor(c);
			break;
		
		case CTRL_KEY('l'):
		case '\x1b':
			// Ctrl-l is refresh, but we refresh everytime when editing so ignore it.
			// <ESC> is use in too many place so also ignore for now
			break;

		default:
			editorInsertChar(c);
			break;
	}
	
	quit_times = KILO_QUIT_TIMES;
}

/*** init ***/
void initEditor() {
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.row = NULL;
	E.dirty = 0;
	E.filename = NULL;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;

	// get screen size and store into the global variable
	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
	
	// save 1 line for status bar
	E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
	enableRawMode();
	initEditor();
	if (argc >= 2) {
		editorOpen(argv[1]);
	}

	editorSetStatusMessage("HELP: Ctrl-S = save || Ctrl-Q = quit | Ctrl-F = find");

	while (1) {
		editorRefreshScreen();
		editorProcessKeyPress();
	}

	return 0;
}
