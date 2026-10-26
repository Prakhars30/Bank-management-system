/* loan_service.c - loans.bin. Status flow:
 *   PENDING -> ASSIGNED -> APPROVED or REJECTED
 * (ASSIGNED -> PENDING happens only when the employee is deleted, in user_service.c) */
#include <stdio.h>
#include <string.h>
#include "account_service.h"
#include "errors.h"
#include "lock_manager.h"
#include "loan_service.h"
#include "record_file.h"
#include "records.h"
#include "tx_manager.h"

static int validate_transition(const char *old_status, const char *new_status)
{
    if (!strcmp(old_status, LOAN_PENDING)  && !strcmp(new_status, LOAN_ASSIGNED)) return 0;
    if (!strcmp(old_status, LOAN_ASSIGNED) && !strcmp(new_status, LOAN_APPROVED)) return 0;
    if (!strcmp(old_status, LOAN_ASSIGNED) && !strcmp(new_status, LOAN_REJECTED)) return 0;
    return E_BAD_TRANSITION;                          /* any other jump is refused */
}

static int loan_exists(int id) { return id >= 1 && id <= rf_count(FILE_LOANS); }

static void add_loan_row(Out *out, const LoanRecord *l)
{
    char amount[32];
    fmt_money(l->amount_cents, amount);
    out_add(out, "| loan=%d account=%d amount=%s status=%s employee=%d\n",
            l->loan_id, l->account_id, amount, l->status, l->assigned_employee_id);
}

/* ---------------------------------------------------------------- customer */

static int apply_body(Session *s, long cents)
{
    AccountRecord a;
    LoanRecord l;
    if (cents <= 0) return E_BAD_AMOUNT;
    if (s->account_id < 1 || s->account_id > rf_count(FILE_ACCOUNTS)) return E_NO_ACCOUNT;
    if (tx_lock_record(FILE_ACCOUNTS, s->account_id, LOCK_READ) < 0) return E_BUSY;
    if (rf_read(FILE_ACCOUNTS, s->account_id, &a) < 0) return E_FAIL;
    if (a.holder1_id != s->user_id && a.holder2_id != s->user_id) return E_NOT_HOLDER;
    if (a.status == ACCT_CLOSED) return E_CLOSED;
    if (a.status == ACCT_INACTIVE) return E_INACTIVE;          /* refused if the account is not active */

    if (tx_lock_table(FILE_LOANS) < 0) return E_BUSY;
    memset(&l, 0, sizeof l);
    l.loan_id = rf_count(FILE_LOANS) + 1;
    l.account_id = s->account_id;
    l.amount_cents = cents;
    snprintf(l.status, sizeof l.status, "%s", LOAN_PENDING);
    return tx_append_record(FILE_LOANS, &l) < 0 ? E_FAIL : l.loan_id;
}

int apply(Session *s, long cents)
{
    tx_begin(s);
    heal_before_scan(FILE_LOANS);
    return tx_finish(apply_body(s, cents));
}

/* ---------------------------------------------------------------- employee */

int view_assigned(Session *s, Out *out)                 /* an employee only sees loans assigned to them */
{
    int rows = 0;
    tx_begin(s);
    heal_before_scan(FILE_LOANS);
    int n = rf_count(FILE_LOANS);
    for (int id = 1; id <= n; id++) {
        LoanRecord l;
        if (rf_read(FILE_LOANS, id, &l) == 0 && !strcmp(l.status, LOAN_ASSIGNED) &&
            l.assigned_employee_id == s->user_id) {
            add_loan_row(out, &l);
            rows++;
        }
    }
    return tx_finish(rows);
}

/* learn the loan's account without locking, so the account can be locked first (global order) */
static int peek_account_of_loan(int loan_id)
{
    LoanRecord l;
    if (!loan_exists(loan_id) || rf_read(FILE_LOANS, loan_id, &l) < 0) return E_NOT_FOUND;
    return l.account_id;
}

static int approve_body(Session *s, int loan_id, int account_id)
{
    LoanRecord l;
    if (tx_lock_record(FILE_ACCOUNTS, account_id, LOCK_WRITE) < 0) return E_BUSY;     /* account first ... */
    if (tx_lock_record(FILE_LOANS, loan_id, LOCK_WRITE) < 0) return E_BUSY;           /* ... then the loan */
    if (rf_read(FILE_LOANS, loan_id, &l) < 0) return E_FAIL;                          /* re-read under the lock */
    if (strcmp(l.status, LOAN_ASSIGNED) != 0 || l.assigned_employee_id != s->user_id ||
        l.account_id != account_id) return E_LOAN_CHANGED;
    if (validate_transition(l.status, LOAN_APPROVED) < 0) return E_BAD_TRANSITION;

    int rc = credit(account_id, l.amount_cents);        /* closed / inactive account: loan stays ASSIGNED */
    if (rc < 0) return rc;
    snprintf(l.status, sizeof l.status, "%s", LOAN_APPROVED);
    return tx_write_record(FILE_LOANS, loan_id, &l) < 0 ? E_FAIL : 0;
}

static int reject_body(Session *s, int loan_id)
{
    LoanRecord l;
    if (tx_lock_record(FILE_LOANS, loan_id, LOCK_WRITE) < 0) return E_BUSY;
    if (rf_read(FILE_LOANS, loan_id, &l) < 0) return E_FAIL;
    if (strcmp(l.status, LOAN_ASSIGNED) != 0 || l.assigned_employee_id != s->user_id) return E_LOAN_CHANGED;
    if (validate_transition(l.status, LOAN_REJECTED) < 0) return E_BAD_TRANSITION;
    snprintf(l.status, sizeof l.status, "%s", LOAN_REJECTED);
    return tx_write_record(FILE_LOANS, loan_id, &l) < 0 ? E_FAIL : 0;
}

int process(Session *s, int loan_id, const char *decision)
{
    int approve = strcmp(decision, "APPROVE") == 0;
    if (!approve && strcmp(decision, "REJECT") != 0) return E_BAD_ARG;

    if (!approve) {
        tx_begin(s);
        return tx_finish(loan_exists(loan_id) ? reject_body(s, loan_id) : E_NOT_FOUND);
    }
    int account_id = peek_account_of_loan(loan_id);
    if (account_id < 0) return account_id;
    tx_begin(s);
    return tx_finish(approve_body(s, loan_id, account_id));
}

/* ---------------------------------------------------------------- manager */

int view_pending(Out *out)
{
    int rows = 0;
    tx_begin(NULL);
    heal_before_scan(FILE_LOANS);
    int n = rf_count(FILE_LOANS);
    for (int id = 1; id <= n; id++) {
        LoanRecord l;
        if (rf_read(FILE_LOANS, id, &l) == 0 && !strcmp(l.status, LOAN_PENDING)) {
            add_loan_row(out, &l);
            rows++;
        }
    }
    return tx_finish(rows);
}

/* read-lock the employee's user record and check role and active (users come before loans) */
static int is_employee(int user_id)
{
    UserRecord u;
    if (user_id < 1 || user_id > rf_count(FILE_USERS)) return 0;
    if (tx_lock_record(FILE_USERS, user_id, LOCK_READ) < 0) return 0;
    return rf_read(FILE_USERS, user_id, &u) == 0 && u.role == ROLE_EMPLOYEE && u.is_active;
}

static int assign_body(int loan_id, int employee_id)
{
    LoanRecord l;
    if (!loan_exists(loan_id)) return E_NOT_FOUND;
    if (!is_employee(employee_id)) return E_NOT_EMPLOYEE;
    if (tx_lock_record(FILE_LOANS, loan_id, LOCK_WRITE) < 0) return E_BUSY;
    if (rf_read(FILE_LOANS, loan_id, &l) < 0) return E_FAIL;
    if (validate_transition(l.status, LOAN_ASSIGNED) < 0) return E_BAD_TRANSITION;   /* only PENDING loans */
    snprintf(l.status, sizeof l.status, "%s", LOAN_ASSIGNED);
    l.assigned_employee_id = employee_id;
    return tx_write_record(FILE_LOANS, loan_id, &l) < 0 ? E_FAIL : 0;
}

int assign(Session *s, int loan_id, int employee_id)    /* managers assign loans but never approve them */
{
    tx_begin(s);
    return tx_finish(assign_body(loan_id, employee_id));
}
