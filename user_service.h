#ifndef USER_SERVICE_H
#define USER_SERVICE_H
#include "session.h"
#include "records.h"
#include "util.h"

int add_user(Session *s, const char *name, const char *password, int role);   /* new user id */
int add_customer(Session *s, const char *name, const char *password);         /* user + account; new user id */
int delete_user(Session *s, int user_id);
int modify_user(Session *s, int user_id, const char *field, const char *value);
int search_user(const char *name, Out *out);
int search_customer(const char *name, Out *out);
int set_role(Session *s, int user_id, int role);
int change_password(Session *s, const char *old_pw, const char *new_pw);
int list_employees(Out *out);
int find_user_by_name(const char *name, UserRecord *out);                     /* user id or 0 */

#endif
