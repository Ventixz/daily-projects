/* Terminal front end: raw mode, key loop, redraw. Ctrl-S save, Ctrl-F find, Ctrl-Q quit. */
#include "editor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

static struct termios orig;
static void raw_off(void) { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig); }
static void raw_on(void) {
    tcgetattr(STDIN_FILENO, &orig); atexit(raw_off);
    struct termios r = orig;
    r.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    r.c_oflag &= ~(OPOST);
    r.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    r.c_cc[VMIN] = 0; r.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &r);
}
static int read_key(void) {
    char c; int n;
    while ((n = read(STDIN_FILENO, &c, 1)) != 1) if (n == -1) exit(1);
    if (c == '\x1b') {
        char s[2];
        if (read(STDIN_FILENO, &s[0], 1) != 1 || read(STDIN_FILENO, &s[1], 1) != 1) return '\x1b';
        if (s[0] == '[') switch (s[1]) { case 'A': return 1001; case 'B': return 1002; case 'C': return 1003; case 'D': return 1004; }
        return '\x1b';
    }
    return c;
}
static void refresh(Editor *e, int top, const char *msg) {
    struct winsize ws; if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || !ws.ws_col) { ws.ws_row = 24; ws.ws_col = 80; }
    int h = ws.ws_row - 2;
    char *body = ed_render(e, top, h, ws.ws_col), *p;
    printf("\x1b[?25l\x1b[H");
    for (p = strtok(body, "\n"); p; p = strtok(NULL, "\n")) printf("%s\x1b[K\r\n", p);
    printf("\x1b[7m%d lines %s\x1b[K\x1b[m\r\n%s\x1b[K", e->numrows, e->dirty ? "(modified)" : "", msg);
    printf("\x1b[%d;%dH\x1b[?25h", e->cy - top + 1, e->cx + 1);
    fflush(stdout); free(body);
}
int main(int argc, char **argv) {
    Editor e; ed_init(&e);
    const char *path = argc > 1 ? argv[1] : NULL;
    if (path) ed_open(&e, path);
    raw_on(); int top = 0; const char *msg = "Ctrl-S save | Ctrl-F find | Ctrl-Q quit";
    for (;;) {
        struct winsize ws; ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws); int h = ws.ws_row > 2 ? ws.ws_row - 2 : 20;
        if (e.cy < top) top = e.cy;
        if (e.cy >= top + h) top = e.cy - h + 1;
        refresh(&e, top, msg); msg = "";
        int c = read_key();
        switch (c) {
        case CTRL('q'): printf("\x1b[2J\x1b[H"); ed_free(&e); return 0;
        case CTRL('s'): msg = (path && ed_save(&e, path) == 0) ? "saved" : "save failed (no filename?)"; break;
        case CTRL('f'): msg = ed_find(&e, "TODO", e.cy + 1) ? "found TODO" : "no TODO"; break;
        case 1001: ed_move(&e, 0, -1); break; case 1002: ed_move(&e, 0, 1); break;
        case 1003: ed_move(&e, 1, 0); break;  case 1004: ed_move(&e, -1, 0); break;
        case '\r': ed_insert_newline(&e); break;
        case 127: case CTRL('h'): ed_delete_char(&e); break;
        default: if (c >= 32 && c < 127) ed_insert_char(&e, c);
        }
    }
}
