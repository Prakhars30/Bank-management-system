/* employee_menu.c - commands for role employee (no customer money commands) */
#include <stdio.h>
#include <string.h>
#include "account_service.h"
#include "config.h"
#include "errors.h"
#include "loan_service.h"
#include "menus.h"
#include "user_service.h"
#include "util.h"

static void cmd_addcustomer(int fd, Session *s, int argc, char **argv)
{
    if (argc != 2) { send_line(fd, "ERR usage: ADDCUSTOMER <name> <password>"); return; }
    int rc = add_customer(s, argv[0], argv[1]);
    char msg[64];
    snprintf(msg, sizeof msg, "customer created, user_id %d", rc);
    send_result(fd, rc, msg);
}

static void cmd_modifycustomer(int fd, Session *s, int argc, char **argv)
{
    int id;
    if (argc != 3 || parse_int(argv[0], &id) < 0) { send_line(fd, "ERR usage: MODIFYCUSTOMER <user_id> <password|username> <value>"); return; }
    send_result(fd, modify_user(s, id, argv[1], argv[2]), "customer updated");
}

static void cmd_findcustomer(int fd, int argc, char **argv)
{
    Out out;
    out_init(&out);
    if (argc != 1) { send_line(fd, "ERR usage: FINDCUSTOMER <name>"); return; }
    send_rows(fd, search_customer(argv[0], &out), &out);
}

static void cmd_viewtxns(int fd, int argc, char **argv)
{
    int id;
    Out out;
    out_init(&out);
    if (argc != 1 || parse_int(argv[0], &id) < 0) { send_line(fd, "ERR usage: VIEWTXNS <account_id>"); return; }
    send_rows(fd, history_for(id, &out), &out);
}

static void cmd_loans_assigned(int fd, Session *s)
{
    Out out;
    out_init(&out);
    send_rows(fd, view_assigned(s, &out), &out);
}

static void cmd_loan_process(int fd, Session *s, int argc, char **argv)
{
    int id;
    if (argc != 2 || parse_int(argv[0], &id) < 0) { send_line(fd, "ERR usage: LOAN_PROCESS <loan_id> APPROVE|REJECT"); return; }
    for (char *p = argv[1]; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    send_result(fd, process(s, id, argv[1]), "loan processed");
}

static void cmd_passwd(int fd, Session *s, int argc, char **argv)
{
    if (argc != 2) { send_line(fd, "ERR usage: PASSWD <old> <new>"); return; }
    send_result(fd, change_password(s, argv[0], argv[1]), "password changed");
}

static void cmd_help(int fd)
{
    static const char *const lines[] = {
        "ADDCUSTOMER <name> <password>", "MODIFYCUSTOMER <user_id> <password|username> <value>",
        "FINDCUSTOMER <name>", "VIEWTXNS <account_id>", "LOANS_ASSIGNED",
        "LOAN_PROCESS <loan_id> APPROVE|REJECT", "PASSWD <old> <new>", "LOGOUT", "QUIT"
    };
    send_help(fd, lines, sizeof lines / sizeof lines[0]);
}

int employee_dispatch(int fd, Session *s, const char *cmd, char *args)
{
    char *argv[MAX_ARGS];
    int argc = split_args(args, argv, MAX_ARGS);

    if      (!strcmp(cmd, "ADDCUSTOMER"))    cmd_addcustomer(fd, s, argc, argv);
    else if (!strcmp(cmd, "MODIFYCUSTOMER")) cmd_modifycustomer(fd, s, argc, argv);
    else if (!strcmp(cmd, "FINDCUSTOMER"))   cmd_findcustomer(fd, argc, argv);
    else if (!strcmp(cmd, "VIEWTXNS"))       cmd_viewtxns(fd, argc, argv);
    else if (!strcmp(cmd, "LOANS_ASSIGNED")) cmd_loans_assigned(fd, s);
    else if (!strcmp(cmd, "LOAN_PROCESS"))   cmd_loan_process(fd, s, argc, argv);
    else if (!strcmp(cmd, "PASSWD"))         cmd_passwd(fd, s, argc, argv);
    else if (!strcmp(cmd, "HELP"))           cmd_help(fd);
    else send_line(fd, "ERR unknown command, try HELP");
    return 0;
}
