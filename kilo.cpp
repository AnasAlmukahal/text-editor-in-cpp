#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
using namespace std;
#define CTRL_KEY(k) ((k)&0x1f)
struct termios orig_termios;
void killswitch(const char *s){
	write(STDOUT_FILENO,"\x1b[2j",4);
	write(STDOUT_FILENO,"\x1b[H",3);
	perror(s);
	exit(1);
}
void RawmodeDisable(){
	if(tcsetattr(STDIN_FILENO,TCSAFLUSH, &orig_termios)==-1)killswitch("tcgetattr");
}
void RawmodeEnable(){
	tcgetattr(STDIN_FILENO, &orig_termios);
	atexit(RawmodeDisable);
	struct termios raw=orig_termios;
	raw.c_iflag&=~(BRKINT|IXON|ICRNL|INPCK|ISTRIP);
	raw.c_oflag&=~(OPOST);
	raw.c_cflag|=(CS8);
	raw.c_lflag&=~(ECHO | ICANON | ISIG| IEXTEN);
	raw.c_cc[VMIN]=0;
	raw.c_cc[VTIME]=1;
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH,&raw)==-1)killswitch("tcsetattr");
}
char editorReadKey(){
	int nread;
	char c;
	while((nread=read(STDIN_FILENO, &c, 1))!=1){
		if(nread==-1&&errno!=EAGAIN)killswitch("read");
	}
	return c;
}
void editorProcessKeypress(){
	char c=editorReadKey();
	switch(c){
		case CTRL_KEY('q'):
			write(STDOUT_FILENO,"\x1b[2j",4);
			write(STDOUT_FILENO,"\x1b[H",3);
			exit(0);
			break;
	}
}
void editorRefreshScreen(){
	write(STDOUT_FILENO,"\x1b[2J",4);
	write(STDOUT_FILENO, "\x1b[H",3);
}
int main()
{
	RawmodeEnable();
	while(1){
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}
