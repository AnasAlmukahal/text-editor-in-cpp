#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
using namespace std;
struct termios orig_termios;
void RawmodeDisable(){
	tcsetattr(STDIN_FILENO,TCSAFLUSH, &orig_termios);
}
void RawmodeEnable(){
	tcgetattr(STDIN_FILENO, &orig_termios);
	atexit(RawmodeDisable);
	struct termios raw=orig_termios;
	raw.c_iflag&=~(IXON|ICRNL);
	raw.c_lflag&=~(ECHO | ICANON | ISIG| IEXTEN);
	tcsetattr(STDIN_FILENO, TCSAFLUSH,&raw);
}
int main()
{
	RawmodeEnable();
char c;
while(read(STDIN_FILENO,&c,1)==1&&c!='q'){
	if(iscntrl(c)){
		printf("%d\n",c);
	}
	else{
		printf("%d('%c')\n",c,c);
	}
}
return 0;}
