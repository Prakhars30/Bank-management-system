/* util.c - small helpers shared by everything */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include "config.h"
#include "errors.h"
#include "records.h"
#include "util.h"


/* ---- text buffer ---- */

void out_init(Out *o) { o->text = NULL; o->len = 0; o->cap = 0; }

void out_free(Out *o) { free(o->text); out_init(o); }

void out_add(Out *o, const char *fmt, ...)
{
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return; }
    if (o->len + n + 1 > o->cap) {
        size_t cap = o->cap ? o->cap * 2 : 256;
        while (cap < o->len + n + 1) cap *= 2;
        char *p = realloc(o->text, cap);
        if (!p) { va_end(ap2); return; }
        o->text = p;
        o->cap = cap;
    }
    vsnprintf(o->text + o->len, n + 1, fmt, ap2);
    va_end(ap2);
    o->len += n;
}

/* ---- parsing and formatting ---- */

int split_args(char *line, char *argv[], int max)
{
    int argc = 0;
    char *p = line;
    while (*p && argc < max) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

int parse_cents(const char *s, long *cents)
{
    long whole = 0, frac = 0;
    int digits = 0;
    if (!s) return -1;
    for (; isdigit((unsigned char)*s); s++) {
        whole = whole * 10 + (*s - '0');
        if (++digits > 12) return -1;
    }
    if (digits == 0) return -1;
    if (*s == '.') {
        int d = 0;
        for (s++; isdigit((unsigned char)*s); s++, d++) {
            if (d == 2) return -1;
            frac = frac * 10 + (*s - '0');
        }
        if (d == 0) return -1;
        if (d == 1) frac *= 10;
    }
    if (*s) return -1;
    *cents = whole * 100 + frac;
    return 0;
}

int parse_int(const char *s, int *value)
{
    char *end;
    if (!s || !*s) return -1;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (*end || errno || v < 0 || v > INT_MAX) return -1;
    *value = (int)v;
    return 0;
}

void fmt_money(long cents, char *buf)
{
    snprintf(buf, 32, "%ld.%02ld", cents / 100, cents % 100);
}

void fmt_time(long t, char *buf)
{
    time_t tt = t;
    struct tm tm;
    localtime_r(&tt, &tm);
    strftime(buf, 32, "%Y-%m-%d %H:%M:%S", &tm);
}

const char *role_name(int role)
{
    switch (role) {
    case ROLE_CUSTOMER: return "customer";
    case ROLE_EMPLOYEE: return "employee";
    case ROLE_MANAGER:  return "manager";
    case ROLE_ADMIN:    return "admin";
    default:            return "?";
    }
}

int role_from_name(const char *name)
{
    for (int r = ROLE_CUSTOMER; r <= ROLE_ADMIN; r++)
        if (strcmp(name, role_name(r)) == 0) return r;
    return 0;
}

const char *status_name(int status)
{
    switch (status) {
    case ACCT_ACTIVE:   return "active";
    case ACCT_INACTIVE: return "inactive";
    case ACCT_CLOSED:   return "closed";
    default:            return "?";
    }
}

/* ---- replies to the client ---- */

static int write_all(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = send(fd, buf, len, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        buf += n;
        len -= n;
    }
    return 0;
}

int send_line(int fd, const char *fmt, ...)
{
    char buf[MAX_LINE];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if (n > (int)sizeof buf - 2) n = sizeof buf - 2;
    buf[n++] = '\n';
    return write_all(fd, buf, n);
}

void send_result(int fd, int rc, const char *ok_text)
{
    if (rc < 0) send_line(fd, "ERR %s", err_text(rc));
    else        send_line(fd, "OK %s", ok_text);
}

void send_rows(int fd, int rc, Out *out)
{
    if (rc < 0) {
        send_line(fd, "ERR %s", err_text(rc));
    } else {
        if (out->len) write_all(fd, out->text, out->len);
        send_line(fd, "OK %d row%s", rc, rc == 1 ? "" : "s");
    }
    out_free(out);
}

void send_help(int fd, const char *const *lines, int n)
{
    for (int i = 0; i < n; i++) send_line(fd, "| %s", lines[i]);
    send_line(fd, "OK help");
}
