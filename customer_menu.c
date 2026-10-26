/* customer_menu.c - commands for role customer */
#include <stdio.h>
#include <string.h>
#include "account_service.h"
#include "config.h"
#include "errors.h"
#include "feedback_service.h"
#include "loan_service.h"
#include "menus.h"
#include "user_service.h"
#include "util.h"

static void cmd_deposit(int fd, Session *s, int argc, char **argv)
{
    long cents;
    if (argc != 1 || parse_cents(argv[0], &cents) < 0) { send_line(fd, "ERR usage: DEPOSIT <amount>"); return; }
    send_result(fd, deposit(s, cents), "deposited");
}

static void cmd_withdraw(int fd, Session *s, int argc, char **argv)
{
    long cents;
    if (argc != 1 || parse_cents(argv[0], &cents) < 0) { send_line(fd, "ERR usage: WITHDRAW <amount>"); return; }
    send_result(fd, withdraw(s, cents), "withdrawn");
}

static void cmd_balance(int fd, Session *s)
{
    char buf[32], msg[64];
    long b = balance(s);
    if (b < 0) { send_result(fd, (int)b, ""); return; }
    fmt_money(b, buf);
    snprintf(msg, sizeof msg, "balance %s", buf);
    send_result(fd, 0, msg);
}

static void cmd_transfer(int fd, Session *s, int argc, char **argv)
{
    int to;
    long cents;
    if (argc != 2 || parse_int(argv[0], &to) < 0 || parse_cents(argv[1], &cents) < 0) {
        send_line(fd, "ERR usage: TRANSFER <to_account> <amount>");
        return;
    }
    send_result(fd, transfer(s, to, cents), "transferred");
}

static void cmd_passwd(int fd, Session *s, int argc, char **argv)
{
    if (argc != 2) { send_line(fd, "ERR usage: PASSWD <old> <new>"); return; }
    send_result(fd, change_password(s, argv[0], argv[1]), "password changed");
}

static void cmd_details(int fd, Session *s)
{
    Out out;
    out_init(&out);
    send_rows(fd, details(s, &out), &out);
}

static void cmd_history(int fd, Session *s)
{
    Out out;
    out_init(&out);
    send_rows(fd, history(s, &out), &out);
}

static void cmd_loan_apply(int fd, Session *s, int argc, char **argv)
{
    long cents;
    if (argc != 1 || parse_cents(argv[0], &cents) < 0) { send_line(fd, "ERR usage: LOAN_APPLY <amount>"); return; }
    int rc = apply(s, cents);
    char msg[64];
    snprintf(msg, sizeof msg, "loan %d is pending", rc);
    send_result(fd, rc, msg);
}

static void cmd_feedback(int fd, Session *s, const char *text)
{
    send_result(fd, add(s, text), "thank you for your feedback");
}

static void cmd_help(int fd)
{
    static const char *const lines[] = {
        "DEPOSIT <amount>", "WITHDRAW <amount>", "BALANCE", "TRANSFER <to_account> <amount>",
        "DETAILS", "HISTORY", "LOAN_APPLY <amount>", "FEEDBACK <text>",
        "PASSWD <old> <new>", "LOGOUT", "QUIT"
    };
    send_help(fd, lines, sizeof lines / sizeof lines[0]);
}

int customer_dispatch(int fd, Session *s, const char *cmd, char *args)
{
    char *argv[MAX_ARGS];
    if (!strcmp(cmd, "FEEDBACK")) { cmd_feedback(fd, s, args); return 0; }     /* takes the rest of the line */
    int argc = split_args(args, argv, MAX_ARGS);

    if      (!strcmp(cmd, "DEPOSIT"))    cmd_deposit(fd, s, argc, argv);
    else if (!strcmp(cmd, "WITHDRAW"))   cmd_withdraw(fd, s, argc, argv);
    else if (!strcmp(cmd, "BALANCE"))    cmd_balance(fd, s);
    else if (!strcmp(cmd, "TRANSFER"))   cmd_transfer(fd, s, argc, argv);
    else if (!strcmp(cmd, "PASSWD"))     cmd_passwd(fd, s, argc, argv);
    else if (!strcmp(cmd, "DETAILS"))    cmd_details(fd, s);
    else if (!strcmp(cmd, "HISTORY"))    cmd_history(fd, s);
    else if (!strcmp(cmd, "LOAN_APPLY")) cmd_loan_apply(fd, s, argc, argv);
    else if (!strcmp(cmd, "HELP"))       cmd_help(fd);
    else send_line(fd, "ERR unknown command, try HELP");
    return 0;
}
