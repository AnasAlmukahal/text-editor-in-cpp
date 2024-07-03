/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <iostream>
#include <time.h>
#include <vector>

/*** defines ***/

#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3
#define CTRL_KEY(k) ((k) & 0x1f)


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
  PAGE_DOWN
};
enum editorHighlight{
  HL_NORMAL=0,
  HL_COMMENT,
  HL_STRING,
  HL_NUMBER,//every character that's part of a number will have that
  HL_MATCH
};
#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1<<1)
/*** data ***/
struct editorSyntax{
  char *filetype;
  char **filematch;//array of file extensions
  char *singleline_comment_start;
  int flags;//bit field, will contain flag for highlighting numbers or strings

};
struct erow {
  int size;
  int rsize; // render size
  std::string chars;
  std::string render;
  unsigned char * hl;
};

struct editorConfig {
  int cx, cy;
  int rx;
  int rowoff; // row offset
  int coloff; // column offset
  int screenrows;
  int screencols;
  int numrows;
  std::vector<erow> row;
  int dirty;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct editorSyntax *syntax;
  struct termios orig_termios;
};

editorConfig E;
/** filetypes **/
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))//store length of HLDB array
char* C_HL_extensions[]={".c",".h", ".cpp",NULL};
struct editorSyntax HLDB[]={
  {
    "c",
    C_HL_extensions,
    "//",
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
  },
};
//HLDB: Highlight DataBase

/** prototype**/
void editorStatusMessage(const char*fmt,...);
void editorRefreshScreen();
char *editorPrompt(const char* prompt, void(*callback)(char*,int));
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

  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }

    return '\x1b';
  } else {
    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}
/** syntax highlighting **/
int is_separator(int c){
  return isspace(c)||c=='\0'||strchr(",.()+-/*=~<>[];",c)!=NULL;
}
void editorUpdateSyntax(erow *row){
  row->hl=(unsigned char*)realloc(row->hl,row->rsize);//size of hl array= size of render array, so we use rsize for hl.
  memset(row->hl,HL_NORMAL,row->rsize);//set all characters to HL_NORMAL by default before loop
  if(E.syntax==NULL)return;
  char *scs=E.syntax->singleline_comment_start;
  int scs_len=scs?strlen(scs):0;
  int prev_sep=1;//consider beggining of line to be a separator
  int in_string = 0;
  int i=0;
  while(i<row->rsize){//changed to while to consume multiple characters for each iteration
    char c=row->render[i];
    unsigned char prev_hl=(i>0)?row->hl[i-1]:HL_NORMAL;
    if(scs_len && !in_string){
      if(!strncmp(&row->render[i],scs,scs_len)){
        memset(&row->hl[i],HL_COMMENT,row->rsize-i);
        break;
      }
    }
    if(E.syntax->flags & HL_HIGHLIGHT_STRINGS){
      if(in_string){
        row->hl[i]=HL_STRING;
        if(c=='\\'&& i+1<row->rsize){
          row->hl[i+1]=HL_STRING;
          i+=2;
          continue;
        }
        if(c==in_string) in_string=0;
        i++;
        prev_sep=1;
        continue;
      }else{
        if(c=='"'|| c=='\''){//highlight double and single quote
          in_string=c;//stored here to know which one closes the string
          row->hl[i]=HL_STRING;
          i++;
          continue;
        }
      }
    }
    if(E.syntax->flags & HL_HIGHLIGHT_NUMBERS){
    if((isdigit(c)&&(prev_sep||prev_hl==HL_NUMBER))||(c=='.'&&prev_hl==HL_NUMBER)){
      row->hl[i]=HL_NUMBER;
      i++;
      prev_sep=0;
      continue;
    }
    }
    prev_sep=is_separator(c);
    i++;
  }
}
int editorSyntaxToColor(int hl){
  switch(hl){
    case HL_COMMENT: return 36;//36: cyan
    case HL_STRING: return 35;//35: magenta
    case HL_NUMBER: return 31;//31: foreground red
    case HL_MATCH: return 34;//34: blue
    default: return 37;//37: foreground white
  }
}
void editorSelectSyntaxHighlight(){
  E.syntax=NULL;
  if(E.filename==NULL) return;
  char *ext=strrchr(E.filename, '.');
  for(unsigned int i =0;i<HLDB_ENTRIES;i++){
    struct editorSyntax *s=&HLDB[i];
    unsigned j=0;
    while(s->filematch[j]){
      int is_ext=(s->filematch[j][0]=='.');
      if((is_ext&&ext&&!strcmp(ext,s->filematch[j]))||
      (!is_ext&&strstr(E.filename,s->filematch[j]))){
        E.syntax=s;
        int filerow;
        for(filerow=0;filerow< E.numrows;filerow++){
          editorUpdateSyntax(&E.row[filerow]);
        }
        return;
      }
      j++;
    }
  }
}
/*** row operations ***/
int editorRowRxToCx(erow *row, int rx){
  int cur_rx=0;
  int cx;
  for(cx=0;cx<row->size;cx++){
    if(row->chars[cx]=='\t')
    cur_rx+=(KILO_TAB_STOP-1)-(cur_rx%KILO_TAB_STOP);
  cur_rx++;
  if(cur_rx>rx)return cx;

  }
  return cx;
}
int editorRowCxtoRx(erow*row,int cx){
  int rx=0;
  for(int i=0;i<cx;i++){
    if(row->chars[i]=='\t')
    //if its a tab, we use rx%KILO_TAB_STOP
    //to find how many columns we are to the right of the last tab stop
    rx+=(KILO_TAB_STOP-1)-(rx%KILO_TAB_STOP);
    rx++;
  }
  return rx;
}
void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;

  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;

  row->render.resize(row->size + tabs * (KILO_TAB_STOP - 1) + 1);
  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
  editorUpdateSyntax(row);
}
void editorInsertRow(int at,const char *s, size_t len) {
  if(at<0 || at>E.numrows) return;
  E.row.insert(E.row.begin()+at,erow());
  E.row[at].size = len;
  E.row[at].chars = std::string(s, len);
  E.row[at].rsize = 0;
  E.row[at].hl=NULL;
  editorUpdateRow(&E.row[at]);
  E.numrows++;
  E.dirty++;
}
void editorFreeRow(erow *row){
  row->render.clear();
  row->chars.clear();
  free(row->hl);
}
void editorDelrow(int at){
  if(at<0 || at>=E.numrows)return;//validate at
  editorFreeRow(&E.row[at]);//free memory owned by the row
  E.row.erase(E.row.begin()+at);
  E.numrows--;
  E.dirty++;
}
void editorRowInsert(erow *row,int at,int x){
  if (at < 0 || at > row->size) at = row->size;
  row->chars.insert(row->chars.begin()+at,x);
  row->size++;
  editorUpdateRow(row);
  E.dirty++;
}
/** editor operations**/
void editorInsertChar(int c){
  if(E.cy==E.numrows){
    editorInsertRow(E.numrows,"",0);
  }
  editorRowInsert(&E.row[E.cy],E.cx,c);
  E.cx++;
}
void editorInsertNewline(){
  if(E.cx==0){
    editorInsertRow(E.cy,"",0);

  }else{
    erow *row=&E.row[E.cy];
    editorInsertRow(E.cy+1,&row->chars[E.cx],row->size-E.cx);
    row=&E.row[E.cy];
    row->size=E.cx;
    row->chars[row->size]='\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx=0;
}
void editorRowAppendString(erow *row, const std::string &s, size_t len){
  row->chars.append(s,0,len);
  row->size=row->chars.size();
  editorUpdateRow(row);
  E.dirty++;
}
void editorRowDelChar(erow *row, int at){
  if(at<0 || at>= row->size) return;
  memmove(&row->chars[at], &row->chars[at+1],row->size-at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}
void editorDelChar(){
  if(E.cy==E.numrows) return;
  if(E.cx==0 &&E.cy==9)return;
  erow *row=&E.row[E.cy];
  if(E.cx>0){
    editorRowDelChar(row,E.cx-1);
    E.cx--;
  }else{
  E.cx=E.row[E.cy-1].size;
  editorRowAppendString(&E.row[E.cy-1],row->chars,row->size);
  editorDelrow(E.cy);
  E.cy--;}
}
/*** file i/o ***/
char* editorRowsToString(int *buflen){
  int totlen=0;
  int i;
  for(i=0;i<E.numrows;i++)
  totlen+=E.row[i].size+1;
  *buflen=totlen;
  char* buf=(char*)malloc(totlen);
  char*p=buf;
  for(i=0;i<E.numrows;i++){
    memcpy(p,E.row[i].chars.c_str(),E.row[i].size);
    p+=E.row[i].size;
    *p='\n';
    p++;
  }
  return buf;
}
void editorOpen(const char *filename) {
  free(E.filename);
  E.filename=strdup(filename);
  editorSelectSyntaxHighlight();
  FILE *fp = fopen(filename, "r");
  if (!fp) killswitch("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    editorInsertRow(E.numrows ,line, linelen);
  }
  free(line);
  fclose(fp);
  E.dirty=0;
}
void editorSave(){
  if(E.filename==NULL){
    E.filename=editorPrompt("save as: %s (ESC to cancel)",NULL);
    if(E.filename==NULL){
      editorStatusMessage("save aborted");
      return;
    }
    editorSelectSyntaxHighlight();
  }
  int len;
  char *buf=editorRowsToString(&len);
  int fd=open(E.filename,O_RDWR |O_CREAT,0644);
  //O_CREAT: create a new file,, RDWR: read&write..0644:standard perm. for text file
  
  if(fd!=-1){
    if(ftruncate(fd,len)!=1){//ftruncate:set file size to specified length
      if(write(fd,buf,len)==len){
        close(fd);
        free(buf);
        E.dirty=0;
        editorStatusMessage("%d bytes written to disk",len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  editorStatusMessage("can't save! I/O error: %s",strerror(errno));
}
/** find **/
void editorFindCallBack(char* query, int key){
  static int last_match=-1;
  static int direction=1;
  static int saved_hl_line;
  static char *saved_hl=NULL;
  if(saved_hl){
    memcpy(E.row[saved_hl_line].hl,saved_hl,E.row[saved_hl_line].rsize);
    free(saved_hl);
    saved_hl=NULL;
  }
  if(key=='\r'|| key=='\x1b'){
    last_match=-1;
    direction=1;
    return;
  }
  else if(key==ARROW_RIGHT||key==ARROW_DOWN){
    direction=1;
  }else if(key==ARROW_LEFT||key==ARROW_UP){
    direction=-1;
  }else{
    last_match=-1;
    direction=1;
  }
  if(last_match==-1)direction=1;
  int current=last_match;
  for(int i=0;i<E.numrows;i++){
    current+=direction;
    if(current==-1)current=E.numrows-1;
    else if(current==E.numrows)current=0;
    erow *row=&E.row[current];
    const char *match=strstr(row->render.c_str(),query);
    if(match){
      last_match=current;
      E.cy=current;
      E.cx=editorRowRxToCx(row,match-row->render.c_str());
      E.rowoff=E.numrows;
      saved_hl_line=current;
      saved_hl=(char*)malloc(row->size);
      memset(&row->hl[match - row->render.c_str()],HL_MATCH,strlen(query));
      break;
    }
  }
}
void editorFind(){
  int saved_cx=E.cx;
  int saved_cy=E.cy;
  int saved_coloff=E.coloff;
  int saved_rowoff=E.rowoff;

  char *query = editorPrompt("Search: &s (use ESC/Arrows/Enter)",editorFindCallBack);
  if(query){free(query);}
  else{
    E.cx=saved_cx;
    E.cy=saved_cy;
    E.coloff=saved_coloff;
    E.rowoff=saved_rowoff;
  }
}
/*** append buffer ***/

struct abuf {
  char *b;
  int len;

  abuf() : b(nullptr), len(0) {}
  ~abuf() {
    delete[] b;
  }

  void append(const char *s, int new_len) {
    char *newbuf = new char[len + new_len];
    if (b) {
      memcpy(newbuf, b, len);
      delete[] b;
    }
    memcpy(newbuf + len, s, new_len);
    b = newbuf;
    len += new_len;
  }
};

void abAppend(abuf *ab, const char *s, int len) {
  ab->append(s, len);
}

void abFree(abuf *ab) {
  delete[] ab->b;
  ab->b = nullptr;
  ab->len = 0;
}

/*** output ***/
void editorScroll() { // to set the E.rowoff value
  E.rx=0;
  if(E.cy<E.numrows){
    E.rx=editorRowCxtoRx(&E.row[E.cy],E.cx);
  }
  if (E.cy < E.rowoff) { // check if cursor above window
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) { // check if moved outside window
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}
void editorDrawRows(abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff; // to display each y position
    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "Kilo editor -- version %s", KILO_VERSION);
        if (welcomelen > E.screencols) welcomelen = E.screencols;
        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0) len = 0;
      if (len > E.screencols) len = E.screencols;
      char *c=&E.row[filerow].render[E.coloff];
      unsigned char *hl=&E.row[filerow].hl[E.coloff];
      int current_color=-1;
      int j;
      for(j=0;j<len;j++){
        if(hl[j]==HL_NORMAL){
          if(current_color!=-1){
          abAppend(ab,"\x1b[39m",5);//39m:reset color
          current_color=-1;
          }
          abAppend(ab,&c[j],1);
        }else{
          int color=editorSyntaxToColor(hl[j]);
          if(color!=current_color){
            current_color=color;
          char buf[16];
          int clen=snprintf(buf,sizeof(buf),"\x1b[%dm",color);
          abAppend(ab,buf,clen);
          }
          abAppend(ab,&c[j],1);
        }
      }
          abAppend(ab,"\x1b[39m",5);//31m is red
        }
    abAppend(ab, "\x1b[K", 3);
    if(y<E.screenrows-1)
      abAppend(ab, "\r\n", 2);
    
  }
}
void editorStatusBar(struct abuf *ab){
  abAppend(ab,"\x1b[7m",4);//color inversion
  char status[80],rstatus[80];
  int len=snprintf(status,sizeof(status),"%.20s- %d lines %s",
  E.filename?E.filename:"[No Name]",E.numrows,
  E.dirty ?"(modified)": "");
  int rlen=snprintf(rstatus,sizeof(rstatus),"%s | %d/%d",
    E.syntax?E.syntax->filetype: "no ft",E.cy+1,E.numrows);
  if(len>E.screencols)len=E.screencols;//ensure bar doesnt exceed screen width
  abAppend(ab,status,len);
  while(len<E.screencols){
    if(E.screencols-len==rlen){
      abAppend(ab,rstatus,rlen);
      break;
    }else{
    abAppend(ab," ",1);
    len++;
  }
  }
  abAppend(ab,"\x1b[m",3);
  abAppend(ab,"\r\n",2);

}
void editorMessageBar(struct abuf *ab){
  abAppend(ab,"\x1b[K",3);
  int msglen=strlen(E.statusmsg);
  if(msglen>E.screencols)msglen=E.screencols;
  if(msglen && time(NULL)- E.statusmsg_time<5)
  abAppend(ab,E.statusmsg,msglen);
}
void editorRefreshScreen() {
  editorScroll();
  abuf ab;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);
  editorDrawRows(&ab);
  editorStatusBar(&ab);
  editorMessageBar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}
void editorStatusMessage(const char* fmt,...){
  va_list ap;
  va_start(ap,fmt);
  vsnprintf(E.statusmsg,sizeof(E.statusmsg),fmt,ap);
  va_end(ap);
  E.statusmsg_time=time(NULL);
}

/*** input ***/
char* editorPrompt(const char*prompt,void(*callback)(char*,int)) {
    size_t bufsize = 128;
    char* buf= (char*)malloc(bufsize); // Initialize buf with 128 characters of '\0'
    size_t buflen = 0;
    buf[0] = '\0';

    while (true) {
        editorStatusMessage(prompt,buf);
        editorRefreshScreen();
        int c = editorReadKey();
        if(c==DEL_KEY||c==CTRL_KEY('h')||c==BACKSPACE){
          if(buflen!=0)buf[--buflen]='\0';
        }else if (c == '\x1b') {
            editorStatusMessage("");
            if(callback) callback(buf,c);
            free(buf);
        } else if (c == '\r') {
            if (buflen != 0) {
                editorStatusMessage("");
                if(callback) callback(buf,c);
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            if (buflen == bufsize - 1) {
                bufsize *= 2;
                buf=(char*)realloc(buf,bufsize); // Resize buf if only needed
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }
        if(callback)callback(buf,c);
    }
}
void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? nullptr : &E.row[E.cy];
  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      } else if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;
    case ARROW_RIGHT:
      if (row && E.cx < row->size) {
        E.cx++;
      } else if (row && E.cx == row->size) {
        E.cy++;
        E.cx = 0;
      }
      break;
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      if (E.cy < E.numrows - 1) {
        E.cy++;
      }
      break;
  }
  row = (E.cy >= E.numrows) ? nullptr : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

void editorProcessKeypress() {
  static int quit_times=KILO_QUIT_TIMES;
  int c = editorReadKey();

  switch (c) {
    case '\r':
    editorInsertNewline();
      break;
    case CTRL_KEY('q'):
    if(E.dirty&&quit_times>0){
      editorStatusMessage("WARNING: file has unsaved changes."
      "press Ctril-Q %d one more time to quit.", quit_times);
      quit_times--;
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
      if(E.cy<E.numrows)
      E.cx=E.row[E.cy].size;
      break;
      case CTRL_KEY('f'):
        editorFind();
        break;
      case BACKSPACE:
      case CTRL_KEY('h'):
        break;
      case DEL_KEY:
        if(c==DEL_KEY)editorMoveCursor(ARROW_RIGHT);
        editorDelChar();
        break;

    case PAGE_UP:
    case PAGE_DOWN:
      {
        if(c==PAGE_UP){
          E.cy=E.rowoff;
        }else if(c==PAGE_DOWN){
          E.cy=E.rowoff+E.screenrows-1;
          if(E.cy>E.numrows) E.cy=E.numrows;
        }
      }
      break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;
      case CTRL_KEY('l'):
      case '\x1b':
        break;

      default:
        editorInsertChar(c);
        break;
  }
  quit_times=KILO_QUIT_TIMES;
  
}

/*** init ***/

void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.rx=0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.dirty=0;
  E.filename=NULL;
  E.statusmsg[0]='\0';
  E.statusmsg_time=0;
  E.syntax =NULL;//no filetype currently

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) killswitch("getWindowSize");
  E.screenrows-=2;
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }
  editorStatusMessage("HELP:Ctrl-S:save | Ctrl-F=find | Ctrl-Q=quit");
  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
