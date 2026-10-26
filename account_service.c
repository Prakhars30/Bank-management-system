/* account_service.c - money commands, account administration, account lookups.
 * Every command checks the account (status, holders) UNDER the account lock. */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "account_service.h"
#include "errors.h"
#include "lock_manager.h"
#include "record_file.h"
#include "records.h"
#include "tx_manager.h"

/* ---------------------------------------------------------------- helpers */

static int validate_amount(long cents) { return cents > 0 ? 0 : E_BAD_AMOUNT; }

static int check_account_active(const AccountRecord *a)
{
    if (a->status == ACCT_CLOSED)   return E_CLOSED;
    if (a->status == ACCT_INACTIVE) return E_INACTIVE;
    return 0;
}

static int account_exists(int id) { return id >= 1 && id <= rf_count(FILE_ACCOUNTS); }

/* Lock the session's account and make sure the session user still holds it.
 * (The account was picked at login; it may have changed since.) */
static int lock_my_account(Session *s, int mode, AccountRecord *a)
{
    if (!account_exists(s->account_id)) return E_NO_ACCOUNT;
    if (tx_lock_record(FILE_ACCOUNTS, s->account_id, mode) < 0) return E_BUSY;
    if (rf_read(FILE_ACCOUNTS, s->account_id, a) < 0) return E_FAIL;
    if (a->holder1_id != s->user_id && a->holder2_id != s->user_id) return E_NOT_HOLDER;
    return 0;
}

/* one TxnRecord per balance change; the caller holds the account lock */
static int append_txn(int account_id, const char *type, long amount, long balance_after)
{
    TxnRecord t;
    if (tx_lock_table(FILE_TXNS) < 0) return E_BUSY;
    memset(&t, 0, sizeof t);
    t.txn_id = rf_count(FILE_TXNS) + 1;
    t.account_id = account_id;
    snprintf(t.type, sizeof t.type, "%s", type);
    t.amount_cents = amount;
    t.balance_after_cents = balance_after;
    t.timestamp = time(NULL);
    return tx_append_record(FILE_TXNS, &t) < 0 ? E_FAIL : 0;
}

/* ---------------------------------------------------------------- lookups */

int holder_has_open_account(int user_id)
{
    int n = rf_count(FILE_ACCOUNTS);
    for (int id = 1; id <= n; id++) {
        AccountRecord a;
        if (rf_read(FILE_ACCOUNTS, id, &a) == 0 && a.status != ACCT_CLOSED &&
            (a.holder1_id == user_id || a.holder2_id == user_id))
            return id;
    }
    return 0;
}

int find_any_account(int user_id)
{
    int open = 0, latest = 0;
    heal_before_scan(FILE_ACCOUNTS);
    int n = rf_count(FILE_ACCOUNTS);
    for (int id = 1; id <= n; id++) {
        AccountRecord a;
        if (rf_read(FILE_ACCOUNTS, id, &a) < 0) continue;
        if (a.holder1_id != user_id && a.holder2_id != user_id) continue;
        if (a.status != ACCT_CLOSED) open = id;
        latest = id;
    }
    return open ? open : latest;
}

/* ---------------------------------------------------------------- customer: money */

static int money_command(Session *s, long cents, int is_deposit)
{
    AccountRecord a;
    int rc = validate_amount(cents);
    if (rc < 0) return rc;
    tx_begin(s);
    rc = lock_my_account(s, LOCK_WRITE, &a);
    if (rc == 0) rc = check_account_active(&a);
    if (rc == 0 && is_deposit) {
        if (a.balance_cents > LONG_MAX - cents) rc = E_OVERFLOW;
        else a.balance_cents += cents;
    } else if (rc == 0) {
        if (a.balance_cents < cents) rc = E_FUNDS;
        else a.balance_cents -= cents;
    }
    if (rc == 0 && tx_write_record(FILE_ACCOUNTS, a.account_id, &a) < 0) rc = E_FAIL;
    if (rc == 0) rc = append_txn(a.account_id, is_deposit ? "DEPOSIT" : "WITHDRAW", cents, a.balance_cents);
    return tx_finish(rc);
}

int deposit(Session *s, long cents)  { return money_command(s, cents, 1); }
int withdraw(Session *s, long cents) { return money_command(s, cents, 0); }

static int transfer_body(Session *s, int to_account, long cents)
{
    AccountRecord from, to;
    int rc = validate_amount(cents);
    if (rc < 0) return rc;
    if (!account_exists(s->account_id)) return E_NO_ACCOUNT;
    if (!account_exists(to_account)) return E_NOT_FOUND;
    if (to_account == s->account_id) return E_SAME_ACCOUNT;

    int lo = s->account_id < to_account ? s->account_id : to_account;     /* lowest id first */
    int hi = s->account_id < to_account ? to_account : s->account_id;
    if (tx_lock_record(FILE_ACCOUNTS, lo, LOCK_WRITE) < 0 || tx_lock_record(FILE_ACCOUNTS, hi, LOCK_WRITE) < 0)
        return E_BUSY;

    rc = lock_my_account(s, LOCK_WRITE, &from);        /* already locked: just re-reads and checks holder */
    if (rc < 0) return rc;
    if ((rc = check_account_active(&from)) < 0) return rc;
    if (rf_read(FILE_ACCOUNTS, to_account, &to) < 0) return E_FAIL;
    if ((rc = check_account_active(&to)) < 0) return rc;          /* closed or inactive target is refused */
    if (from.balance_cents < cents) return E_FUNDS;
    if (to.balance_cents > LONG_MAX - cents) return E_OVERFLOW;

    from.balance_cents -= cents;
    to.balance_cents += cents;
    if (tx_write_record(FILE_ACCOUNTS, from.account_id, &from) < 0 ||
        tx_write_record(FILE_ACCOUNTS, to.account_id, &to) < 0) return E_FAIL;
    if ((rc = append_txn(from.account_id, "TRANSFER_OUT", cents, from.balance_cents)) < 0) return rc;
    return append_txn(to.account_id, "TRANSFER_IN", cents, to.balance_cents);
}

int transfer(Session *s, int to_account, long cents)
{
    tx_begin(s);
    return tx_finish(transfer_body(s, to_account, cents));
}

/* ---------------------------------------------------------------- customer: reads */

long balance(Session *s)
{
    AccountRecord a;
    tx_begin(s);
    int rc = lock_my_account(s, LOCK_READ, &a);
    tx_finish(rc);
    return rc < 0 ? rc : a.balance_cents;
}

int details(Session *s, Out *out)                      /* ids only: reading users while holding an account lock would break the lock order */
{
    AccountRecord a;
    tx_begin(s);
    int rc = lock_my_account(s, LOCK_READ, &a);
    if (rc == 0) {
        out_add(out, "| account_id=%d holder1_id=%d holder2_id=%d status=%s\n",
                a.account_id, a.holder1_id, a.holder2_id, status_name(a.status));
        rc = 1;
    }
    return tx_finish(rc);
}

static int list_txns(int account_id, Out *out)
{
    int n = rf_count(FILE_TXNS), rows = 0;
    for (int id = 1; id <= n; id++) {
        TxnRecord t;
        if (rf_read(FILE_TXNS, id, &t) < 0 || t.account_id != account_id) continue;
        char amount[32], after[32], when[32];
        fmt_money(t.amount_cents, amount);
        fmt_money(t.balance_after_cents, after);
        fmt_time(t.timestamp, when);
        out_add(out, "| txn=%d %s %s amount=%s balance_after=%s\n", t.txn_id, when, t.type, amount, after);
        rows++;
    }
    return rows;
}

int history(Session *s, Out *out)
{
    AccountRecord a;
    tx_begin(s);
    heal_before_scan(FILE_TXNS);
    int rc = lock_my_account(s, LOCK_READ, &a);
    if (rc == 0) rc = list_txns(a.account_id, out);
    return tx_finish(rc);
}

int history_for(int account_id, Out *out)               /* employee VIEWTXNS */
{
    if (!account_exists(account_id)) return E_NOT_FOUND;
    tx_begin(NULL);
    heal_before_scan(FILE_TXNS);
    return tx_finish(list_txns(account_id, out));
}

/* ---------------------------------------------------------------- loan credit */

int credit(int account_id, long cents)                  /* caller holds the account write lock */
{
    AccountRecord a;
    if (rf_read(FILE_ACCOUNTS, account_id, &a) < 0) return E_NOT_FOUND;
    int rc = check_account_active(&a);
    if (rc < 0) return rc;
    if (a.balance_cents > LONG_MAX - cents) return E_OVERFLOW;
    a.balance_cents += cents;
    if (tx_write_record(FILE_ACCOUNTS, account_id, &a) < 0) return E_FAIL;
    return append_txn(account_id, "LOAN_CREDIT", cents, a.balance_cents);
}

/* ---------------------------------------------------------------- administration */

int open_account(Session *s, int holder1, int holder2)  /* caller is inside a transaction */
{
    (void)s;
    AccountRecord a;
    if (tx_lock_table(FILE_ACCOUNTS) < 0) return E_BUSY;
    memset(&a, 0, sizeof a);
    a.account_id = rf_count(FILE_ACCOUNTS) + 1;
    a.holder1_id = holder1;
    a.holder2_id = holder2;
    a.status = ACCT_ACTIVE;
    int id = tx_append_record(FILE_ACCOUNTS, &a);
    return id < 0 ? E_FAIL : id;
}

/* holder must be an active customer; read-lock the record (users come before accounts in the lock order) */
static int check_holder_user(int user_id)
{
    UserRecord u;
    if (user_id < 1 || user_id > rf_count(FILE_USERS)) return E_NOT_FOUND;
    if (tx_lock_record(FILE_USERS, user_id, LOCK_READ) < 0) return E_BUSY;
    if (rf_read(FILE_USERS, user_id, &u) < 0) return E_FAIL;
    return (u.role == ROLE_CUSTOMER && u.is_active) ? 0 : E_NOT_CUSTOMER;
}

static int add_account_body(Session *s, int holder1, int holder2)
{
    if (holder1 == holder2) return E_BAD_ARG;
    int lo = holder2 == 0 || holder1 < holder2 ? holder1 : holder2;      /* read-lock lowest id first */
    int hi = lo == holder1 ? holder2 : holder1;
    int rc = check_holder_user(lo);
    if (rc == 0 && hi != 0) rc = check_holder_user(hi);
    if (rc < 0) return rc;
    if (tx_lock_table(FILE_ACCOUNTS) < 0) return E_BUSY;
    if (holder_has_open_account(holder1) || (holder2 && holder_has_open_account(holder2))) return E_ONE_ACCOUNT;
    return open_account(s, holder1, holder2);
}

int add_account(Session *s, int holder1, int holder2)
{
    if (holder1 < 1 || holder2 < 0) return E_BAD_ARG;
    tx_begin(s);
    heal_before_scan(FILE_ACCOUNTS);
    return tx_finish(add_account_body(s, holder1, holder2));
}

static int delete_account_body(int account_id)
{
    AccountRecord a;
    if (!account_exists(account_id)) return E_NOT_FOUND;
    if (tx_lock_record(FILE_ACCOUNTS, account_id, LOCK_WRITE) < 0) return E_BUSY;
    if (rf_read(FILE_ACCOUNTS, account_id, &a) < 0) return E_FAIL;
    if (a.status == ACCT_CLOSED) return E_CLOSED;
    if (a.balance_cents != 0) return E_BALANCE;
    a.status = ACCT_CLOSED;                              /* holders stay, for history */
    return tx_write_record(FILE_ACCOUNTS, account_id, &a) < 0 ? E_FAIL : 0;
}

int delete_account(Session *s, int account_id)
{
    tx_begin(s);
    return tx_finish(delete_account_body(account_id));
}

static int modify_account_body(int account_id, const char *field, const char *value)
{
    AccountRecord a;
    int v = 0, rc;
    int is_holder1 = strcmp(field, "holder1_id") == 0;
    int is_holder2 = strcmp(field, "holder2_id") == 0;
    int is_status  = strcmp(field, "status") == 0;     /* balance can never be edited */
    if (!is_holder1 && !is_holder2 && !is_status) return E_BAD_FIELD;
    if (!account_exists(account_id)) return E_NOT_FOUND;
    if (!is_status) {
        if (parse_int(value, &v) < 0 || (is_holder1 && v == 0)) return E_BAD_ARG;
        if (v != 0 && (rc = check_holder_user(v)) < 0) return rc;
    }
    if (tx_lock_table(FILE_ACCOUNTS) < 0) return E_BUSY;
    if (tx_lock_record(FILE_ACCOUNTS, account_id, LOCK_WRITE) < 0) return E_BUSY;
    if (rf_read(FILE_ACCOUNTS, account_id, &a) < 0) return E_FAIL;
    if (a.status == ACCT_CLOSED) return E_CLOSED;

    if (is_status) {
        if (strcmp(value, "active") == 0)        a.status = ACCT_ACTIVE;
        else if (strcmp(value, "inactive") == 0) a.status = ACCT_INACTIVE;
        else return E_BAD_ARG;
    } else {
        int other = is_holder1 ? a.holder2_id : a.holder1_id;
        if (v != 0 && v == other) return E_BAD_ARG;
        /* a user keeps at most one open account (this account's own holders are fine) */
        if (v != 0 && v != a.holder1_id && v != a.holder2_id && holder_has_open_account(v)) return E_ONE_ACCOUNT;
        if (is_holder1) a.holder1_id = v;
        else            a.holder2_id = v;
    }
    return tx_write_record(FILE_ACCOUNTS, account_id, &a) < 0 ? E_FAIL : 0;
}

int modify_account(Session *s, int account_id, const char *field, const char *value)
{
    tx_begin(s);
    heal_before_scan(FILE_ACCOUNTS);
    return tx_finish(modify_account_body(account_id, field, value));
}

int search_account(int account_id, Out *out)
{
    AccountRecord a;
    if (!account_exists(account_id)) return E_NOT_FOUND;
    tx_begin(NULL);
    heal_before_scan(FILE_ACCOUNTS);
    int rc = tx_lock_record(FILE_ACCOUNTS, account_id, LOCK_READ) < 0 ? E_BUSY : 0;
    if (rc == 0 && rf_read(FILE_ACCOUNTS, account_id, &a) < 0) rc = E_FAIL;
    if (rc == 0) {
        char bal[32];
        fmt_money(a.balance_cents, bal);
        out_add(out, "| account_id=%d holder1_id=%d holder2_id=%d balance=%s status=%s\n",
                a.account_id, a.holder1_id, a.holder2_id, bal, status_name(a.status));
        rc = 1;
    }
    return tx_finish(rc);
}

int set_status(Session *s, int account_id, int status)   /* manager: ACTIVATE / DEACTIVATE */
{
    AccountRecord a;
    tx_begin(s);
    int rc = account_exists(account_id) ? 0 : E_NOT_FOUND;
    if (rc == 0 && tx_lock_record(FILE_ACCOUNTS, account_id, LOCK_WRITE) < 0) rc = E_BUSY;
    if (rc == 0 && rf_read(FILE_ACCOUNTS, account_id, &a) < 0) rc = E_FAIL;
    if (rc == 0 && a.status == ACCT_CLOSED) rc = E_CLOSED;      /* only DELETEACCOUNT closes */
    if (rc == 0) {
        a.status = status;
        if (tx_write_record(FILE_ACCOUNTS, account_id, &a) < 0) rc = E_FAIL;
    }
    return tx_finish(rc);
}
