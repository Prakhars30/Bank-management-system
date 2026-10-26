/* util.h - tiny helpers: text buffer, parsing, replies to the client */
#ifndef UTIL_H
#define UTIL_H
#include <stddef.h>

/* Growing text buffer. Services write result rows ("| ...\n") into it. */
typedef struct { char *text; size_t len, cap; } Out;
void out_init(Out *o);
void out_add(Out *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void out_free(Out *o);

int  split_args(char *line, char *argv[], int max);   /* splits on spaces, in place */
int  parse_cents(const char *s, long *cents);         /* "12", "12.5", "12.50" */
int  parse_int(const char *s, int *value);
void fmt_money(long cents, char *buf);                /* buf >= 32 bytes */
void fmt_time(long t, char *buf);                     /* buf >= 32 bytes */

const char *role_name(int role);
int         role_from_name(const char *name);         /* 0 if unknown */
const char *status_name(int status);

/* Replies. A reply is zero or more "| row" lines, then one line starting OK or ERR. */
int  send_line(int fd, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void send_result(int fd, int rc, const char *ok_text);
void send_rows(int fd, int rc, Out *out);             /* rc = number of rows, or an error */
void send_help(int fd, const char *const *lines, int n);

#endif
