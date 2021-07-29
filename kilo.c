/*** includes ***/

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)
// 0x1f <--> 00011111 => turing off the first 3 bits to mimick the CTRL key

void disableRawMode();
void enableRawMode();

/*** data ***/

struct termios orig_termios;

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

void enableRawMode(){

     /*
      * tcgetattr reads the current attribures of a terminal into a struct 
      * modify the struct by hand 
      * pass the modifed struct to tcsetattr 
      * TCSAFLUSH discards any input that has not been read and also waits for pending output to be written to the terminal
      */
     
    if(tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = orig_termios;

    /*
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
    
    raw.c_iflag &= ~(BRKINT | IXON | ICRNL | ICRNL | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag &= ~(CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

void disableRawMode(){
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) die("tcsetattr");
}

char editorReadKey(){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}

/*** input ***/

void editorProcessKeypress(){
    char c = editorReadKey();

    switch(c){
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2j", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

/*** output ***/

void editorDrawRows(){
    int y;
    for(y = 0; y < 24; y++){
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}

void editorRefreshScreen(){
    write(STDOUT_FILENO, "\x1b[2j", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    editorDrawRows();

    write(STDOUT_FILENO, "\x1b[H", 3);
}

/*** initialize ***/

int main(){
    enableRawMode();

    while(1){
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
