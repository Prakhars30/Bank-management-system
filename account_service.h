#ifndef ACCOUNT_SERVICE_H
#define ACCOUNT_SERVICE_H
#include "session.h"
#include "util.h"

/* customer side */
int  deposit(Session *s, long cents);
int  withdraw(Session *s, long cents);
int  transfer(Session *s, int to_account, long cents);
long balance(Session *s);                       /* cents, or an E_* code */
int  details(Session *s, Out *out);
int  history(Session *s, Out *out);

/* admin / manager / employee side */
int  add_account(Session *s, int holder1, int holder2);
int  delete_account(Session *s, int account_id);
int  modify_account(Session *s, int account_id, const char *field, const char *value);
int  search_account(int account_id, Out *out);
int  set_status(Session *s, int account_id, int status);
int  open_account(Session *s, int holder1, int holder2);   /* inside a transaction */
int  history_for(int account_id, Out *out);
int  credit(int account_id, long cents);                   /* inside a transaction */

/* lookups */
int  holder_has_open_account(int user_id);      /* account id, or 0 */
int  find_any_account(int user_id);             /* open one, else the latest closed, else 0 */

#endif
