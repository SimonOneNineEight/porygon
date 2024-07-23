#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.01"

enum editorKey {
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
struct editorConfig {
	int cx, cy;
	int screenrows;
	int screencols;
	struct termios orig_termios;
};

struct editorConfig E;

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
void editorDrawRows(struct abuf *ab) {
	int y;
	for (y = 0; y < E.screenrows; y ++) {
		// Add a welcome message
		if (y == E.screenrows / 3) {
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

		abAppend(ab, "\x1b[K", 3);
		if (y < E.screenrows - 1) {
			abAppend(ab, "\r\n", 2);
		}
	}
}

void editorRefreshScreen() {
	struct abuf ab = ABUF_INIT;

	// hide the cursor before refresh the screen
	abAppend(&ab, "\x1b[?25l", 6);
	// [H reposition the cursor tor the top left corner
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
	abAppend(&ab, buf, strlen(buf));
	
	// show the cursor after refresh
	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

/*** input ***/
void editorMoveCursor(int key) {
	switch (key) {
		case ARROW_LEFT:
			if (E.cx != 0) E.cx --;
			break;
		case ARROW_RIGHT:
			if (E.cx != E.screencols - 1) E.cx ++;
			break;
		case ARROW_UP:
			if (E.cy != 0) E.cy --;
			break;
		case ARROW_DOWN:
			if (E.cy != E.screenrows - 1) E.cy ++;
			break;
	}

    printf("Cursor position: (%d, %d)\n", E.cy, E.cx); // Debug statement
	fflush(stdout);
}
void editorProcessKeyPress() {
// Map all the special key press to actions
	int c = editorReadKey();
	printf("Processed key: %d\n", c); // Debug statement
    fflush(stdout);
	switch (c) {
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);

			exit(0);
			break;

		case HOME_KEY:
			E.cx = 0;
			break;

		case END_KEY:
			E.cx = E.screenrows - 1;
			break;

		case PAGE_UP:
		case PAGE_DOWN:
			{
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
	}
    printf("Processed key: %d\n", c); // Debug statement
	fflush(stdout);
}

/*** init ***/
void initEditor() {
	E.cx = 0;
	E.cy = 0;
	// get screen size and store into the global variable
	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() {
	enableRawMode();
	initEditor();

	while (1) {
		editorRefreshScreen();
		editorProcessKeyPress();
	}

	return 0;
}