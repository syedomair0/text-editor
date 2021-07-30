/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define KILO_VERSION "0.0.1"

// 0x1f <--> 00011111 => turing off the first 3 bits to mimick the CTRL key

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey{
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

void disableRawMode();
void enableRawMode();

/*** data ***/

struct editorConfig{
    int cx, cy;
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;


/*** terminal ***/

void die(const char *s) {
    /* perror reads the global errorno variable which indicates what the error was and print the descriptive error msg along with the
     *                                                                                                          string that caused it
     */

    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode(){
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
}

void enableRawMode(){

     /*
      * tcgetattr reads the current attribures of a terminal into a struct 
      * modify the struct by hand 
      * pass the modifed struct to tcsetattr 
      * TCSAFLUSH discards any input that has not been read and also waits for pending output to be written to the terminal
      * disabling the ICANON flag turns off the CANONICAL mode meaning the input is read byte-by-byte 
      *                                                    (the program exits as soon as q is pressed)
      * disabling the ECHO flag does not show the input to the screen
      * disabling the ISIG disables the CTRL-C and CTRL-Z keybindings producing SIGINT and SIGSTP signals respectively
      * disabling the IXON disables the CTRL-S and CTRL-Q keybindings which stops and resumes data transmission.
      * when CTRL-V sends the next pressed character literally. Like CTRL-V and then CTRL-C inputes a 3byte seq. 
      *                                                                            controlled by the IEXTEN flag
      * ICRNL controls the translation of '\r' to '\n' 
      * OPOST controls the translation of '\n' to '\r\n'
      * VMIN sets the minimum number of bytes of input needed before read can return.
      * VTIME sets the maximum time to wait befoe read can return 
      */
    
     
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL |INPCK |ISTRIP | IXON );
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int editorReadKey(){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if(nread == -1 && errno != EAGAIN) die("read");
    }

    if(c == '\x1b'){
        char seq[3];

        if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if(seq[0] == '['){
            if(seq[1] >= '0' && seq[1] <= '9'){
                if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if(seq[2] == '-'){
                    switch(seq[1]){
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            }else{
                switch(seq[1]){
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        }
        return '\x1b';
    }else{
        return c;
    }
}

int getWindowsSize(int *rows, int *cols){
    struct winsize ws;

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        return -1;
    }else{
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** append buffer ***/

struct abuf{
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len){
    char *new = realloc(ab->b, ab->len + len);

    if(new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab){
    free(ab->b);
}

/*** output ***/

void editorDrawRows(struct abuf *ab){
    int y;
    for(y = 0; y < E.screenrows; y++){
        if(y == E.screenrows / 3){
            char welcome[89];
            int welcomelen = snprintf(welcome, sizeof(welcome),
                    "Kilo editor --version %s", KILO_VERSION);
            if(welcomelen > E.screencols) welcomelen = E.screencols;
            int padding = (E.screencols - welcomelen) / 2;
            if (padding){
                abAppend(ab, "~", 1);
                padding--;
            }
            while(padding--){
                abAppend(ab, " ", 1);
            }
            abAppend(ab, welcome, welcomelen);
        }else{
            abAppend(ab, "~", 1);
        }

        abAppend(ab, "\x1b[K", 3);
        if(y < E.screenrows - 1){
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen(){
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
//  abAppend(&ab, "\x1b[2J", 4);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** input ***/

void editorMoveCursor(int key){
    switch(key){
        case ARROW_LEFT:
            if(E.cx != 0){
                E.cx--;
            }
            break;
        case ARROW_RIGHT:
            if(E.cx != E.screencols - 1){
                E.cx++;
            }
            break;
        case ARROW_UP:
            if(E.cy != 0){
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if(E.cy != E.screenrows - 1){
                E.cy++;
            }
            break;
    }
}

void editorProcessKeypress(){
    int c = editorReadKey();

    switch(c){
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            E.cx = E.screencols - 1;
            break;
        
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenrows;
                while(times--)
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP: ARROW_DOWN);
            }
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

/*** initialize ***/

void initEditor(){
    E.cx = 0;
    E.cy = 0;

    if(getWindowsSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(){
    enableRawMode();
    initEditor();

    while(1){
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
