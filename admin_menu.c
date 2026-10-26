/* admin_menu.c - commands for role admin (no customer commands, so an admin can't move money) */
#include <stdio.h>
#include <string.h>
#include "account_service.h"
#include "config.h"
#include "errors.h"
#include "menus.h"
#include "records.h"
#include "user_service.h"
#include "util.h"

static void cmd_adduser(int fd, Session *s, int argc, char **argv)
{
    int role = argc == 3 ? role_from_name(argv[2]) : 0;
    if (argc != 3) { send_line(fd, "ERR usage: ADDUSER <name> <password> <employee|manager|admin>"); return; }
    if (role == ROLE_CUSTOMER) { send_line(fd, "ERR customers are added by employees (ADDCUSTOMER)"); return; }
    int rc = add_user(s, argv[0], argv[1], role);
    char msg[64];
    snprintf(msg, sizeof msg, "user created, user_id %d", rc);
    send_result(fd, role ? rc : E_BAD_ROLE, msg);
}

static void cmd_deleteuser(int fd, Session *s, int argc, char **argv)
{
    int id;
    if (argc != 1 || parse_int(argv[0], &id) < 0) { send_line(fd, "ERR usage: DELETEUSER <user_id>"); return; }
    send_result(fd, delete_user(s, id), "user deleted");
}

static void cmd_modifyuser(int fd, Session *s, int argc, char **argv)
{
    int id;
    if (argc != 3 || parse_int(argv[0], &id) < 0) { send_line(fd, "ERR usage: MODIFYUSER <user_id> <password|username|role> <value>"); return; }
    send_result(fd, modify_user(s, id, argv[1], argv[2]), "user updated");
}

static void cmd_searchuser(int fd, int argc, char **argv)
{
    Out out;
    out_init(&out);
    if (argc != 1) { send_line(fd, "ERR usage: SEARCHUSER <name>"); return; }
    send_rows(fd, search_user(argv[0], &out), &out);
}

static void cmd_addaccount(int fd, Session *s, int argc, char **argv)
{
    int h1, h2 = 0;
    if ((argc != 1 && argc != 2) || parse_int(argv[0], &h1) < 0 || (argc == 2 && parse_int(argv[1], &h2) < 0)) {
        send_line(fd, "ERR usage: ADDACCOUNT <holder1_id> [holder2_id]");
        return;
    }
    int rc = add_account(s, h1, h2);
    char msg[64];
    snprintf(msg, sizeof msg, "account created, account_id %d", rc);
    send_result(fd, rc, msg);
}

static void cmd_deleteaccount(int fd, Session *s, int argc, char **argv)
{
    int id;
    if (argc != 1 || parse_int(argv[0], &id) < 0) { send_line(fd, "ERR usage: DELETEACCOUNT <account_id>"); return; }
    send_result(fd, delete_account(s, id), "account closed");
}

static void cmd_modifyaccount(int fd, Session *s, int argc, char **argv)
{
    int id;
    if (argc != 3 || parse_int(argv[0], &id) < 0) {
        send_line(fd, "ERR usage: MODIFYACCOUNT <account_id> <holder1_id|holder2_id|status> <value>");
        return;
    }
    send_result(fd, modify_account(s, id, argv[1], argv[2]), "account updated");
}

static void cmd_searchaccount(int fd, int argc, char **argv)
{
    int id;
    Out out;
    out_init(&out);
    if (argc != 1 || parse_int(argv[0], &id) < 0) { send_line(fd, "ERR usage: SEARCHACCOUNT <account_id>"); return; }
    send_rows(fd, search_account(id, &out), &out);
}

static void cmd_passwd(int fd, Session *s, int argc, char **argv)
{
    if (argc != 2) { send_line(fd, "ERR usage: PASSWD <old> <new>"); return; }
    send_result(fd, change_password(s, argv[0], argv[1]), "password changed");
}

static void cmd_help(int fd)
{
    static const char *const lines[] = {
        "ADDUSER <name> <password> <employee|manager|admin>", "DELETEUSER <user_id>",
        "MODIFYUSER <user_id> <password|username|role> <value>", "SEARCHUSER <name>",
        "ADDACCOUNT <holder1_id> [holder2_id]", "DELETEACCOUNT <account_id>",
        "MODIFYACCOUNT <account_id> <holder1_id|holder2_id|status> <value>", "SEARCHACCOUNT <account_id>",
        "PASSWD <old> <new>", "LOGOUT", "QUIT"
    };
    send_help(fd, lines, sizeof lines / sizeof lines[0]);
}

int admin_dispatch(int fd, Session *s, const char *cmd, char *args)
{
    char *argv[MAX_ARGS];
    int argc = split_args(args, argv, MAX_ARGS);

    if      (!strcmp(cmd, "ADDUSER"))       cmd_adduser(fd, s, argc, argv);
    else if (!strcmp(cmd, "DELETEUSER"))    cmd_deleteuser(fd, s, argc, argv);
    else if (!strcmp(cmd, "MODIFYUSER"))    cmd_modifyuser(fd, s, argc, argv);
    else if (!strcmp(cmd, "SEARCHUSER"))    cmd_searchuser(fd, argc, argv);
    else if (!strcmp(cmd, "ADDACCOUNT"))    cmd_addaccount(fd, s, argc, argv);
    else if (!strcmp(cmd, "DELETEACCOUNT")) cmd_deleteaccount(fd, s, argc, argv);
    else if (!strcmp(cmd, "MODIFYACCOUNT")) cmd_modifyaccount(fd, s, argc, argv);
    else if (!strcmp(cmd, "SEARCHACCOUNT")) cmd_searchaccount(fd, argc, argv);
    else if (!strcmp(cmd, "PASSWD"))        cmd_passwd(fd, s, argc, argv);
    else if (!strcmp(cmd, "HELP"))          cmd_help(fd);
    else send_line(fd, "ERR unknown command, try HELP");
    return 0;
}
