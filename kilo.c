#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

struct termios orig_termios;

void die(const char *s) {
	// perror sees the global error `errno` and print the error message
	perror(s);
	exit(1);
}

void disableRawMode(){
	// Reset user termial to echoing	
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios)) {
		die("tcsetattr");
	}
}

void enableRawMode(){
	if (tcgetattr(STDIN_FILENO, &orig_termios)) die("tcgetattr");
	atexit(disableRawMode);

	struct termios raw = orig_termios;
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

int main() {
	enableRawMode();
	

	while (1) {
		char c = '\0';
		if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
		
		// Only print uncontrolled chars
		if (iscntrl(c)) {
			printf("%d\r\n", c);
		} else {
			printf("%d ('%c')\r\n", c, c);
		}
		if (c == 'q') break;
	}
	return 0;
}
