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

/*** data ***/

struct erow {
  int size;
  int rsize; // render size
  std::string chars;
  std::string render;
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
  struct termios orig_termios;
};

editorConfig E;
/** prototype**/
void editorStatusMessage(const char*fmt,...);
void editorRefreshScreen();
char *editorPrompt(char* prompt);
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

/*** row operations ***/
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
}
void editorInsertRow(int at,const char *s, size_t len) {
  if(at<0 || at>E.numrows) return;
  E.row.insert(E.row.begin()+at,erow());
  E.row[at].size = len;
  E.row[at].chars = std::string(s, len);
  E.row[at].rsize = 0;
  editorUpdateRow(&E.row[at]);
  E.numrows++;
  E.dirty++;
}
void editorFreeRow(erow *row){
  row->render.clear();
  row->chars.clear();
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
  if(E.filename==NULL)
  E.filename=editorPrompt("save as: %s (ESC to cancel)");
  if(E.filename==NULL){
    editorStatusMessage("save aborted");
    return;
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
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
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
  int rlen=snprintf(rstatus,sizeof(rstatus),"%d/%d",
    E.cy+1,E.numrows);
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
char* editorPrompt(char*prompt) {
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
            free(buf);
        } else if (c == '\r') {
            if (buflen != 0) {
                editorStatusMessage("");
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

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) killswitch("getWindowSize");
  E.screenrows-=2;
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }
  editorStatusMessage("HELP:Ctrl-S:save| Ctrl-Q=quit");
  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
