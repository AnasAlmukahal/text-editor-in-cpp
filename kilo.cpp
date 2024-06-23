#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
#include <string>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
/*** defines ***/
#define KILO_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)
enum editorkey{
	Arrow_L=1000,
	Arrow_R,
	Arrow_U,
	Arrow_D,
	Delete_key,
	Home_key,
	End_key,
	Page_U,
	Page_D
};

/*** data ***/
typedef struct erow{
	int size;
	char *chars;
}erow;
struct editorConfig {
	int cx,cy;
	 int screenrows;
	 int screencols;
	 int numrows;
	 erow row;
	  struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

void killswitch(const char *s) {
	  write(STDOUT_FILENO, "\x1b[2J", 4);
	    write(STDOUT_FILENO, "\x1b[H", 3);

	      perror(s);
	        exit(1);
}

void disableRawMode() {
	  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		      killswitch("tcsetattr");
}

void enableRawMode() {
	  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) killswitch("tcgetattr");
	    atexit(disableRawMode);

	      struct termios raw = E.orig_termios;
	        raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
		  raw.c_oflag &= ~(OPOST);
		    raw.c_cflag |= (CS8);
		      raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
		        raw.c_cc[VMIN] = 0;
			  raw.c_cc[VTIME] = 1;

			    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) killswitch("tcsetattr");
}

int editorReadKey() {
	  int nread;
	    char c;
	      while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		          if (nread == -1 && errno != EAGAIN) killswitch("read");
			    }
	      if(c=='\x1b'){
		      char seq[3];
		      if(read(STDIN_FILENO,&seq[0],1)!=1)return '\x1b';
		      if(read(STDIN_FILENO,&seq[1],1)!=1)return '\x1b';
		      if(seq[0]=='['){
			      if(seq[1]>='0'&&seq[1]<='9'){
				      if(seq[2]=='~'){
				      switch(seq[1]){
					      case'1':return Home_key;
					      case'3':return Delete_key;
					      case'4':return End_key;
					      case'5':return Page_U;
					      case'6':return Page_D;
					      case'7':return Home_key;
					      case'8':return End_key;
				      }
			      }
			      }else{
			      switch(seq[1]){
				      case'A':return Arrow_U;
				      case 'B':return Arrow_D;
				      case'C':return Arrow_R;
				      case 'D':return Arrow_L;
				      case'H':return Home_key;
				      case'F':return End_key;
			      }
		      }
		      }else if(seq[0]=='o'){
			      switch(seq[1]){
				      case'H':return Home_key;
				      case'F':return End_key;
			      }
		      }
		      return '\x1b';

	      }else{return c;
	      }
}
int getCursorPosition(int *rows, int *cols){
	char buf[32];
	unsigned int i=0;
	if(write(STDOUT_FILENO,"\x1b[6n",4)!=4)return -1;
	while(i<sizeof(buf)-1){
		if(read(STDIN_FILENO,&buf[i],1)!=1)break;
		if(buf[i]=='R')break;
	}
	buf[i]='\0';
	if(buf[0]!='\x1b'||buf[1]!='[')return -1;
	if(sscanf(&buf[2],"%d;%d",rows,cols)!=2)return -1;

	return 0;
}
int getWindowSize(int *rows, int *cols) {
	  struct winsize ws;

	    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col ==0 ) {
			    if(write(STDOUT_FILENO,"\x1b[999C\x1b[999B",12)!=12)
				    return -1;
			    return getCursorPosition(rows,cols);
			  } else {
				      *cols = ws.ws_col;
				      *rows = ws.ws_row;
					      return 0;
					      }
}
/**file i/o**/
void editorOpen(char *filename){
	FILE *fp=fopen(filename,"r");
	if(!fp)killswitch("fopen");
	char*line=NULL;
	size_t linecap=0;
	ssize_t linelen;
	linelen= getline(&line, &linecap, fp);
	if(linelen!=-1){
		while(linelen>0&&(line[linelen-1]=='\n'||line[linelen-1]=='\r'))
			linelen--;
	
	E.row.size=linelen;
	E.row.chars=(char*)malloc(linelen+1);
	memcpy(E.row.chars,line,linelen);
	E.row.chars[linelen]='\0';
	E.numrows=1;
	}
	free(line);
	fclose(fp);

}
/***buffer ***/
struct abuf{
	char *b;
	int len;
};
#define ABUF_INIT {NULL,0}
void abAppend(struct abuf *ab, const char*s, int len){
	char *newbuf=static_cast<char*>(realloc(ab->b,ab->len+len));
	if(newbuf==NULL)return;
	memcpy(&newbuf[ab->len],s,len);
	ab->b=newbuf;
	ab->len+=len;
}
void abFree(struct abuf*ab){
	free(ab->b);
}
/*** output ***/

void editorDrawRows(struct abuf *ab) {
	  int i;
	    for (i = 0; i < E.screenrows;i++){
		    if(i>=E.numrows){
			    if(E.numrows==0&&i==E.screenrows/3){
				    char welcome[80];
				    int welcomelen=snprintf(welcome,sizeof(welcome),
						    "kilo editor --version %s",KILO_VERSION);
				    if(welcomelen>E.screencols)welcomelen=E.screencols;
				    int padding=(E.screencols-welcomelen)/2;
				    if(padding){
					    abAppend(ab,"~",1);
					    padding--;
				    }
				    while(padding--)abAppend(ab," ",1);
				    abAppend(ab,welcome,welcomelen);
			    }else{
				    int len=E.row.size;
				    if(len>E.screencols)len=E.screencols;
				    abAppend(ab,E.row.chars,len);
			    }
			    abAppend(ab,"\x1b[K",3);
			    if(i>E.screenrows-1){
				    abAppend(ab,"\r\n",2);
			    }
		    }
	    }
			  }


void editorRefreshScreen() {
	  struct abuf ab=ABUF_INIT;
	  abAppend(&ab,"\x1b[?25l",6);
	  abAppend(&ab,"\x1b[H",3);

	      editorDrawRows(&ab);
	      char buf[32];
	      snprintf(buf,sizeof(buf),"\x1b[%d;%dH",E.cy+1,E.cx+1);
	      abAppend(&ab,buf,strlen(buf));
	      abAppend(&ab,"\x1b[?25h",6);
	        write(STDOUT_FILENO, ab.b, ab.len);
		abFree(&ab);
}

/*** input ***/
void editorMoveCursor(int key){
	switch(key){
		case Arrow_L:
			if(E.cx!=0){
			E.cx--;}
			break;
		case Arrow_U:
			if(E.cy!=0){
			E.cy--;}
			break;
		case Arrow_D:
			if(E.cy!=E.screenrows-1){
			E.cy++;}
			break;
		case Arrow_R:
			if(E.cx!=E.screencols-1){
			E.cx++;}
			break;
	}
}
void editorProcessKeypress() {
	  int c = editorReadKey();

	    switch (c) {
		        case CTRL_KEY('q'):
				      write(STDOUT_FILENO, "\x1b[2J", 4);
				            write(STDOUT_FILENO, "\x1b[H", 3);
					          exit(0);
						        break;
			case Home_key:
							E.cx=0;
							break;
			case End_key:
							E.cx=E.screencols-1;
							break;
			case Page_U:
			case Page_D:{
					    int time=E.screenrows;
					    while(time--)
						    editorMoveCursor(c==Page_U?Arrow_U:Arrow_D);
				    }
				    break;
			case Arrow_U:
			case Arrow_D:
			case Arrow_L:
			case Arrow_R:
	   
							editorMoveCursor(c);
	  
						       	break; }
}

/*** init ***/

void initEditor() {
	E.cx=0;
	E.cy=0;
	E.numrows=0;
	  if (getWindowSize(&E.screenrows, &E.screencols) == -1) killswitch("getWindowSize");
}

int main(int argc, char *argv[]) {
	  enableRawMode();
	    initEditor();
	    if(argc>=2){
		    editorOpen(argv[1]);
	    }

	      while (1) {
		          editorRefreshScreen();
			      editorProcessKeypress();
			        }return 0;}
