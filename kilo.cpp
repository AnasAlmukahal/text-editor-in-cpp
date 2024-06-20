#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>
using namespace std;

#define CTRL_KEY(k) ((k) & 0x1f)

struct termios orig_termios;

void killswitch(const char *s) {
	    write(STDOUT_FILENO, "\x1b[2J", 4);
	        write(STDOUT_FILENO, "\x1b[H", 3);
		    perror(s);
		        exit(1);
}

struct editorConfig {
	    int screenrows;
	        int screencols;
		    struct termios orig_termios;
};

struct editorConfig E;

void RawmodeDisable() {
	    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		            killswitch("tcsetattr");
}

void RawmodeEnable() {
	    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
		            killswitch("tcgetattr");
	        atexit(RawmodeDisable);
		    struct termios raw = E.orig_termios;
		        raw.c_iflag &= ~(BRKINT | IXON | ICRNL | INPCK | ISTRIP);
			    raw.c_oflag &= ~(OPOST);
			        raw.c_cflag |= (CS8);
				    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
				        raw.c_cc[VMIN] = 0;
					    raw.c_cc[VTIME] = 1;
					        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
							        killswitch("tcsetattr");
}

char editorReadKey() {
	    int nread;
	        char c;
		    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
			            if (nread == -1 && errno != EAGAIN)
					                killswitch("read");
				        }
		        return c;
}

int getWindowSize(int *rows, int *cols) {
	    struct winsize ws;
	        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
			        return -1;
		    else {
			            *cols = ws.ws_col;
				            *rows = ws.ws_row;
					            return 0;
						        }
}

void editorProcessKeypress() {
	    char c = editorReadKey();
	        switch (c) {
			        case CTRL_KEY('q'):
					            write(STDOUT_FILENO, "\x1b[2J", 4);
						                write(STDOUT_FILENO, "\x1b[H", 3);
								            exit(0);
									                break;
											    }
}

void editorDrawRows() {
	    for (int i = 0; i < E.screenrows; i++)
		            write(STDOUT_FILENO, "~\r\n", 3);
}

void editorRefreshScreen() {
	    write(STDOUT_FILENO, "\x1b[2J", 4);
	        write(STDOUT_FILENO, "\x1b[H", 3);
		    editorDrawRows();
		        write(STDOUT_FILENO, "\x1b[H", 3);
}

void initEditor() {
	    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
		            killswitch("getWindowSize");
}

int main() {
	    RawmodeEnable();
	        initEditor();
		    while (1) {
			            editorRefreshScreen();
				            editorProcessKeypress();
					        }
		        return 0;
}
