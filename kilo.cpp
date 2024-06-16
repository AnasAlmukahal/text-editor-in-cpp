#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
using namespace std;
struct termios orig_termios;
void killswitch(const char *s){
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
int main()
{
	RawmodeEnable();
	while(1){
		char c='\0';
		if(read(STDIN_FILENO,&c,1)==-1&&errno!=EAGAIN)killswitch("read");
			if(iscntrl(c)){
				printf("%d\r\n",c);
			}
			else{
				printf("%d('%c')\r\n",c,c);
			}
		if(c=='q')break;
	}
	return 0;}
