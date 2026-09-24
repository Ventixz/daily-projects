#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>

#define DEFAULT_PORT 9090

/* A growable byte buffer used to accumulate bytes received from a socket
 * until a complete '\n'-terminated line is available. TCP is a byte stream,
 * not a message stream: a single line can arrive split across several
 * recv() calls, and several lines can arrive in one. Every server in this
 * project uses the same buffer so that detail is handled in exactly one
 * place. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} linebuf_t;

void linebuf_init(linebuf_t *lb);
void linebuf_free(linebuf_t *lb);
void linebuf_append(linebuf_t *lb, const char *data, size_t n);

/* Removes and returns one complete line (without the trailing '\n' or an
 * optional preceding '\r'), shifting any remaining bytes to the front of
 * the buffer. Returns NULL and leaves lb untouched if no full line is
 * buffered yet. Caller owns the returned string and must free() it. */
char *linebuf_take_line(linebuf_t *lb);

/* Creates, binds and listens on a TCP socket on `port` with SO_REUSEADDR
 * set, using `backlog` as the listen backlog. Exits the process on
 * failure -- there is no sensible way to run any of these servers without
 * a listening socket. */
int make_listener(int port, int backlog);

/* Parses one already-extracted protocol line and decides what to do with
 * it:
 *   - *out is set to a malloc'd response to send verbatim (or NULL).
 *   - *close is set to 1 if the connection should be closed after any
 *     response is sent (the QUIT command).
 *   - *sleep_ms is set to a positive delay for the SLEEP command; the
 *     caller is responsible for waiting that long (however its
 *     concurrency model allows) and then sending "OK\n" itself. This
 *     function never sleeps -- it only decides that sleeping should
 *     happen, which is the whole point of the exercise. */
void protocol_handle_line(const char *line, char **out, int *close, int *sleep_ms);

/* Sends every byte in `data`, looping over send() as needed. Used by the
 * two blocking servers, where a connection's thread of control owns the
 * socket exclusively and can afford to wait. */
void send_all(int fd, const char *data, size_t n);

/* Blocking request/response loop for one already-accepted connection:
 * sends the READY banner, then reads and answers lines until QUIT or
 * disconnect. Shared verbatim by the sequential and threaded servers --
 * the only difference between those two programs is *what calls this
 * function and how many of them run at once*, which is exactly the
 * comparison this project is about. */
void serve_blocking_connection(int fd);

#endif
