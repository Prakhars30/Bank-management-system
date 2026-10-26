/* client_handler.c - one process per client: read lines, log in, revalidate, dispatch by role */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include "auth_service.h"
#include "account_service.h"
#include "client_handler.h"
#include "config.h"
#include "errors.h"
#include "lock_manager.h"
#include "menus.h"
#include "record_file.h"
#include "records.h"
#include "session.h"
#include "tx_manager.h"
#include "util.h"

/* SO_RCVTIMEO: a blocked read fails with EAGAIN after N seconds.
 * Only the wait for the client is timed, never a transaction. */
static void set_read_timeout(int fd, int seconds)
{
    struct timeval tv = { seconds, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
}

/* one line, without the newline; -1 on EOF or error (errno is 0 on EOF) */
static int read_line(int fd, char *buf, int size)
{
    int n = 0;
    for (;;) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r < 0 && errno == EINTR) continue;
        if (r == 0) { errno = 0; return -1; }
        if (r < 0) return -1;
        if (c == '\n') break;
        if (c != '\r' && n < size - 1) buf[n++] = c;
    }
    buf[n] = '\0';
    return n;
}

static void end_reason(int fd, Session *s)
{
    if (s->timed_out) send_line(fd, "ERR session timed out");
}

/* Runs before every command after login: one read of this user's record under a
 * read lock. Role changes apply at once; a deactivated user is disconnected.
 * Account status and membership are NOT checked here but inside each transaction. */
static int session_revalidate(int fd, Session *s)
{
    UserRecord u;
    tx_begin(s);
    int ok = tx_lock_record(FILE_USERS, s->user_id, LOCK_READ) == 0 && rf_read(FILE_USERS, s->user_id, &u) == 0;
    tx_commit();
    if (!ok) { send_line(fd, "ERR %s", err_text(E_BUSY)); return -1; }
    if (!u.is_active) { send_line(fd, "ERR %s", err_text(E_DEACTIVATED)); return -1; }
    s->role = u.role;
    return 0;
}

static int cmd_login(int fd, char *args, Session *s)
{
    char *argv[MAX_ARGS];
    int uid;
    if (split_args(args, argv, MAX_ARGS) != 2) { send_line(fd, "ERR usage: LOGIN <username> <password>"); return 0; }

    int rc = auth_login(argv[0], argv[1], &uid);
    if (rc < 0) {
        if (++s->failed_attempts >= MAX_LOGIN_ATTEMPTS) {
            send_line(fd, "ERR too many failed attempts");
            return -1;
        }
        send_line(fd, "ERR %s", err_text(rc));
        return 0;
    }
    if (session_acquire(uid) < 0) { send_line(fd, "ERR already logged in elsewhere"); return 0; }

    s->logged_in = 1;
    s->holds_session_lock = 1;
    s->user_id = uid;
    snprintf(s->username, sizeof s->username, "%s", argv[0]);
    if (session_revalidate(fd, s) < 0) return -1;                  /* also loads the role */
    if (s->role == ROLE_CUSTOMER) s->account_id = find_any_account(uid);

    set_read_timeout(fd, IDLE_TIMEOUT_SECONDS);
    send_line(fd, "OK welcome %s (%s), try HELP", s->username, role_name(s->role));
    return 0;
}

static void cmd_logout(int fd, Session *s)
{
    if (s->holds_session_lock) session_release(s->user_id);
    memset(s, 0, sizeof *s);
    set_read_timeout(fd, LOGIN_TIMEOUT_SECONDS);
    send_line(fd, "OK logged out");
}

static int dispatch_by_role(int fd, Session *s, const char *cmd, char *args)
{
    switch (s->role) {                                 /* the role comes from users.bin, never from the client */
    case ROLE_CUSTOMER: return customer_dispatch(fd, s, cmd, args);
    case ROLE_EMPLOYEE: return employee_dispatch(fd, s, cmd, args);
    case ROLE_MANAGER:  return manager_dispatch(fd, s, cmd, args);
    case ROLE_ADMIN:    return admin_dispatch(fd, s, cmd, args);
    default:            send_line(fd, "ERR unknown role"); return -1;
    }
}

void handle_client(int fd)
{
    Session s;
    char line[MAX_LINE];
    memset(&s, 0, sizeof s);
    set_read_timeout(fd, LOGIN_TIMEOUT_SECONDS);
    send_line(fd, "OK bank server ready, LOGIN <username> <password>");

    for (;;) {
        if (read_line(fd, line, sizeof line) < 0) {
            s.timed_out = (errno == EAGAIN || errno == EWOULDBLOCK);
            break;
        }
        char *cmd = line;                                           /* VERB arg arg ... */
        while (*cmd == ' ') cmd++;
        char *args = cmd;
        while (*args && *args != ' ') args++;
        if (*args) *args++ = '\0';
        while (*args == ' ') args++;
        for (char *p = cmd; *p; p++) *p = (char)toupper((unsigned char)*p);
        for (char *e = args + strlen(args); e > args && e[-1] == ' '; ) *--e = '\0';

        if (*cmd == '\0') { send_line(fd, "ERR empty command"); continue; }
        if (!strcmp(cmd, "QUIT")) { send_line(fd, "OK bye"); break; }

        if (!s.logged_in) {
            if (!strcmp(cmd, "LOGIN")) { if (cmd_login(fd, args, &s) < 0) break; }
            else send_line(fd, "ERR please LOGIN first");
            continue;
        }
        if (!strcmp(cmd, "LOGOUT")) { cmd_logout(fd, &s); continue; }
        if (!strcmp(cmd, "LOGIN"))  { send_line(fd, "ERR already logged in"); continue; }

        if (session_revalidate(fd, &s) < 0) break;
        if (dispatch_by_role(fd, &s, cmd, args) < 0) break;
    }

    end_reason(fd, &s);                                             /* normal exit path */
    if (s.holds_session_lock) session_release(s.user_id);
    close(fd);
}
