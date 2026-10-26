/* manager_menu.c - commands for role manager */
#include <string.h>
#include "account_service.h"
#include "config.h"
#include "errors.h"
#include "feedback_service.h"
#include "loan_service.h"
#include "menus.h"
#include "records.h"
#include "user_service.h"
#include "util.h"

static void cmd_set_status(int fd, Session *s, int argc, char **argv, int status, const char *name)
{
    int id;
    if (argc != 1 || parse_int(argv[0], &id) < 0) { send_line(fd, "ERR usage: %s <account_id>", name); return; }
    send_result(fd, set_status(s, id, status), status == ACCT_ACTIVE ? "account activated" : "account deactivated");
}

static void cmd_loans_pending(int fd)
{
    Out out;
    out_init(&out);
    send_rows(fd, view_pending(&out), &out);
}

static void cmd_assign(int fd, Session *s, int argc, char **argv)
{
    int loan, emp;
    if (argc != 2 || parse_int(argv[0], &loan) < 0 || parse_int(argv[1], &emp) < 0) {
        send_line(fd, "ERR usage: ASSIGN <loan_id> <employee_id>");
        return;
    }
    send_result(fd, assign(s, loan, emp), "loan assigned");
}

static void cmd_employees(int fd)
{
    Out out;
    out_init(&out);
    send_rows(fd, list_employees(&out), &out);
}

static void cmd_feedback_list(int fd)
{
    Out out;
    out_init(&out);
    send_rows(fd, review_all(&out), &out);
}

static void cmd_passwd(int fd, Session *s, int argc, char **argv)
{
    if (argc != 2) { send_line(fd, "ERR usage: PASSWD <old> <new>"); return; }
    send_result(fd, change_password(s, argv[0], argv[1]), "password changed");
}

static void cmd_help(int fd)
{
    static const char *const lines[] = {
        "ACTIVATE <account_id>", "DEACTIVATE <account_id>", "LOANS_PENDING",
        "ASSIGN <loan_id> <employee_id>", "EMPLOYEES", "FEEDBACK_LIST",
        "PASSWD <old> <new>", "LOGOUT", "QUIT"
    };
    send_help(fd, lines, sizeof lines / sizeof lines[0]);
}

int manager_dispatch(int fd, Session *s, const char *cmd, char *args)
{
    char *argv[MAX_ARGS];
    int argc = split_args(args, argv, MAX_ARGS);

    if      (!strcmp(cmd, "ACTIVATE"))      cmd_set_status(fd, s, argc, argv, ACCT_ACTIVE, "ACTIVATE");
    else if (!strcmp(cmd, "DEACTIVATE"))    cmd_set_status(fd, s, argc, argv, ACCT_INACTIVE, "DEACTIVATE");
    else if (!strcmp(cmd, "LOANS_PENDING")) cmd_loans_pending(fd);
    else if (!strcmp(cmd, "ASSIGN"))        cmd_assign(fd, s, argc, argv);
    else if (!strcmp(cmd, "EMPLOYEES"))     cmd_employees(fd);
    else if (!strcmp(cmd, "FEEDBACK_LIST")) cmd_feedback_list(fd);
    else if (!strcmp(cmd, "PASSWD"))        cmd_passwd(fd, s, argc, argv);
    else if (!strcmp(cmd, "HELP"))          cmd_help(fd);
    else send_line(fd, "ERR unknown command, try HELP");
    return 0;
}
