/* kilo-style terminal editor UI: raw mode, key decoding, screen drawing.
 * Build: make   Run: ./kilo [file]   Keys: Ctrl-S save, Ctrl-F find, Ctrl-Q quit (twice if dirty) */
#define _GNU_SOURCE
#include "editor.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

enum Key { BACKSPACE = 127, ARROW_LEFT = 1000, ARROW_RIGHT, ARROW_UP, ARROW_DOWN,
           DEL_KEY, HOME_KEY, END_KEY, PAGE_UP, PAGE_DOWN };

static struct termios orig_termios;
static Editor E;
static int rows_visible, cols_visible, rowoff, coloff;
static char *filename;
static char status[128];
static time_t status_time;

static void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
    perror(s);
    exit(1);
}

static void disable_raw(void) { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); }

static void enable_raw(void) {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
    atexit(disable_raw);
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;   /* read() times out after 100ms so Esc alone is distinguishable */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

static int read_key(void) {
    int n;
    char c;
    while ((n = read(STDIN_FILENO, &c, 1)) != 1)
        if (n == -1 && errno != EAGAIN) die("read");
    if (c != '\x1b') return c;
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {
        if (isdigit(seq[1])) {
            if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
            if (seq[2] == '~') {
                switch (seq[1]) {
                case '1': case '7': return HOME_KEY;
                case '3': return DEL_KEY;
                case '4': case '8': return END_KEY;
                case '5': return PAGE_UP;
                case '6': return PAGE_DOWN;
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
        if (seq[1] == 'H') return HOME_KEY;
        if (seq[1] == 'F') return END_KEY;
    }
    return '\x1b';
}

static void set_status(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(status, sizeof status, fmt, ap);
    va_end(ap);
    status_time = time(NULL);
}

/* Output is accumulated and written once per frame to avoid flicker. */
typedef struct { char *b; int len; } Abuf;
static void ab_append(Abuf *ab, const char *s, int len) {
    ab->b = realloc(ab->b, ab->len + len);
    memcpy(ab->b + ab->len, s, len);
    ab->len += len;
}

static void scroll(void) {
    if (E.cy < rowoff) rowoff = E.cy;
    if (E.cy >= rowoff + rows_visible) rowoff = E.cy - rows_visible + 1;
    if (E.cx < coloff) coloff = E.cx;
    if (E.cx >= coloff + cols_visible) coloff = E.cx - cols_visible + 1;
}

static void refresh(void) {
    scroll();
    Abuf ab = {NULL, 0};
    ab_append(&ab, "\x1b[?25l\x1b[H", 9);
    for (int y = 0; y < rows_visible; y++) {
        int r = y + rowoff;
        if (r >= E.numrows) {
            if (E.numrows == 0 && y == rows_visible / 3) {
                char w[80];
                int wl = snprintf(w, sizeof w, "kilo-c -- a tiny editor");
                int pad = (cols_visible - wl) / 2;
                if (pad > 0) { ab_append(&ab, "~", 1); pad--; }
                while (pad-- > 0) ab_append(&ab, " ", 1);
                ab_append(&ab, w, wl);
            } else ab_append(&ab, "~", 1);
        } else {
            int len = E.rows[r].size - coloff;
            if (len < 0) len = 0;
            if (len > cols_visible) len = cols_visible;
            ab_append(&ab, E.rows[r].chars + coloff, len);
        }
        ab_append(&ab, "\x1b[K\r\n", 5);
    }
    char bar[160];
    int l = snprintf(bar, sizeof bar, "%.20s - %d lines%s", filename ? filename : "[No Name]",
                     E.numrows, E.dirty ? " (modified)" : "");
    char rs[32];
    int rl = snprintf(rs, sizeof rs, "%d/%d", E.cy + 1, E.numrows);
    if (l > cols_visible) l = cols_visible;
    ab_append(&ab, "\x1b[7m", 4);
    ab_append(&ab, bar, l);
    while (l < cols_visible) {
        if (cols_visible - l == rl) { ab_append(&ab, rs, rl); break; }
        ab_append(&ab, " ", 1); l++;
    }
    ab_append(&ab, "\x1b[m\r\n\x1b[K", 9);
    if (status[0] && time(NULL) - status_time < 5) {
        int sl = (int)strlen(status);
        ab_append(&ab, status, sl > cols_visible ? cols_visible : sl);
    }
    char buf[32];
    int bl = snprintf(buf, sizeof buf, "\x1b[%d;%dH\x1b[?25h", E.cy - rowoff + 1, E.cx - coloff + 1);
    ab_append(&ab, buf, bl);
    write(STDOUT_FILENO, ab.b, ab.len);
    free(ab.b);
}

/* Prompt on the status line; returns malloc'd text, or NULL if cancelled with Esc. */
static char *prompt(const char *label) {
    size_t cap = 64, len = 0;
    char *buf = malloc(cap);
    buf[0] = '\0';
    for (;;) {
        set_status("%s%s", label, buf);
        refresh();
        int c = read_key();
        if (c == BACKSPACE || c == DEL_KEY || c == CTRL_KEY('h')) {
            if (len) buf[--len] = '\0';
        } else if (c == '\x1b') {
            set_status("");
            free(buf);
            return NULL;
        } else if (c == '\r') {
            if (len) { set_status(""); return buf; }
        } else if (!iscntrl(c) && c < 128) {
            if (len + 1 >= cap) buf = realloc(buf, cap *= 2);
            buf[len++] = (char)c;
            buf[len] = '\0';
        }
    }
}

static void save(void) {
    if (!filename) {
        filename = prompt("Save as: ");
        if (!filename) { set_status("Save aborted"); return; }
    }
    int n = ed_save(&E, filename);
    if (n < 0) set_status("Can't save! I/O error: %s", strerror(errno));
    else set_status("%d bytes written to disk", n);
}

static void find(void) {
    char *q = prompt("Search: ");
    if (!q) return;
    if (!ed_find(&E, q)) set_status("Not found: %s", q);
    else rowoff = E.numrows;   /* force scroll() to bring the hit into view */
    free(q);
}

static void process_key(void) {
    static int quit_times = 2;
    int c = read_key();
    switch (c) {
    case '\r': ed_insert_newline(&E); break;
    case CTRL_KEY('q'):
        if (E.dirty && quit_times > 1) {
            set_status("Unsaved changes! Press Ctrl-Q again to quit.");
            quit_times--;
            return;
        }
        write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
        exit(0);
    case CTRL_KEY('s'): save(); break;
    case CTRL_KEY('f'): find(); break;
    case HOME_KEY: E.cx = 0; break;
    case END_KEY: if (E.cy < E.numrows) E.cx = E.rows[E.cy].size; break;
    case BACKSPACE: case CTRL_KEY('h'): ed_backspace(&E); break;
    case DEL_KEY: ed_move_right(&E); ed_backspace(&E); break;
    case PAGE_UP: case PAGE_DOWN:
        for (int i = rows_visible; i > 0; i--) c == PAGE_UP ? ed_move_up(&E) : ed_move_down(&E);
        break;
    case ARROW_UP: ed_move_up(&E); break;
    case ARROW_DOWN: ed_move_down(&E); break;
    case ARROW_LEFT: ed_move_left(&E); break;
    case ARROW_RIGHT: ed_move_right(&E); break;
    case CTRL_KEY('l'): case '\x1b': break;
    default:
        if (c >= 32 && c < 127) ed_insert_char(&E, c);
        else if (c == '\t') ed_insert_char(&E, '\t');
    }
    quit_times = 2;
}

int main(int argc, char **argv) {
    ed_init(&E);
    if (argc >= 2) {
        filename = strdup(argv[1]);
        if (ed_open(&E, filename) == -1 && errno != ENOENT) die("fopen");
    }
    enable_raw();
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) { ws.ws_row = 24; ws.ws_col = 80; }
    rows_visible = ws.ws_row - 2;
    cols_visible = ws.ws_col;
    set_status("HELP: Ctrl-S save | Ctrl-F find | Ctrl-Q quit");
    for (;;) { refresh(); process_key(); }
}
