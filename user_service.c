/* user_service.c - users.bin: add, delete (soft), modify, search, passwords.
 * Changes that affect username uniqueness or the last-admin rule take the
 * users table lock first. */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "account_service.h"
#include "auth_service.h"
#include "errors.h"
#include "lock_manager.h"
#include "record_file.h"
#include "records.h"
#include "tx_manager.h"
#include "user_service.h"

/* ---------------------------------------------------------------- helpers */

static int validate_username(const char *name)
{
    size_t len = strlen(name);
    if (len < 1 || len > 31) return E_BAD_ARG;
    for (size_t i = 0; i < len; i++)
        if (!isalnum((unsigned char)name[i]) && !strchr("_.-", name[i])) return E_BAD_ARG;
    return 0;
}

static int validate_password(const char *pw)
{
    size_t len = strlen(pw);
    return (len >= 1 && len <= 64) ? 0 : E_BAD_ARG;
}

int find_user_by_name(const char *name, UserRecord *out)       /* user id or 0 */
{
    int n = rf_count(FILE_USERS);
    for (int id = 1; id <= n; id++) {
        UserRecord u;
        if (rf_read(FILE_USERS, id, &u) == 0 && strcmp(u.username, name) == 0) {
            if (out) *out = u;
            return id;
        }
    }
    return 0;
}

static int username_exists(const char *name)                   /* soft-deleted users keep their name */
{
    return find_user_by_name(name, NULL) != 0;
}

/* Is this user the only active admin left? Caller holds the users table lock. */
static int is_last_admin(int user_id)
{
    int admins = 0, target_is_admin = 0;
    int n = rf_count(FILE_USERS);
    for (int id = 1; id <= n; id++) {
        UserRecord u;
        if (rf_read(FILE_USERS, id, &u) < 0 || u.role != ROLE_ADMIN || !u.is_active) continue;
        admins++;
        if (id == user_id) target_is_admin = 1;
    }
    return target_is_admin && admins == 1;
}

/* Append a user. Caller holds the users table lock. Returns the new user id. */
static int append_user(const char *name, const char *password, int role)
{
    UserRecord u;
    if (validate_username(name) < 0 || validate_password(password) < 0) return E_BAD_ARG;
    if (role < ROLE_CUSTOMER || role > ROLE_ADMIN) return E_BAD_ROLE;
    if (username_exists(name)) return E_EXISTS;
    memset(&u, 0, sizeof u);
    u.user_id = rf_count(FILE_USERS) + 1;
    snprintf(u.username, sizeof u.username, "%s", name);
    auth_new_salt(u.salt);
    auth_hash_password(password, u.salt, u.password_hash);
    u.role = role;
    u.is_active = 1;
    int id = tx_append_record(FILE_USERS, &u);
    return id < 0 ? E_FAIL : id;
}

/* ---------------------------------------------------------------- add */

static int add_user_body(const char *name, const char *password, int role)
{
    if (tx_lock_table(FILE_USERS) < 0) return E_BUSY;
    return append_user(name, password, role);
}

int add_user(Session *s, const char *name, const char *password, int role)
{
    tx_begin(s);
    heal_before_scan(FILE_USERS);
    return tx_finish(add_user_body(name, password, role));
}

/* ADDCUSTOMER: one transaction, under the users table lock: user, then accounts table lock, then account */
static int add_customer_body(Session *s, const char *name, const char *password)
{
    if (tx_lock_table(FILE_USERS) < 0) return E_BUSY;
    int uid = append_user(name, password, ROLE_CUSTOMER);
    if (uid < 0) return uid;
    int aid = open_account(s, uid, 0);
    return aid < 0 ? aid : uid;
}

int add_customer(Session *s, const char *name, const char *password)
{
    tx_begin(s);
    heal_before_scan(FILE_USERS);
    return tx_finish(add_customer_body(s, name, password));
}

/* ---------------------------------------------------------------- delete (soft) */

/* a customer leaves their account: sole holder -> close it (balance must be zero);
 * joint -> just remove this holder */
static int detach_from_account(int user_id)
{
    AccountRecord a;
    if (tx_lock_table(FILE_ACCOUNTS) < 0) return E_BUSY;
    heal_before_scan(FILE_ACCOUNTS);
    int aid = holder_has_open_account(user_id);
    if (aid == 0) return 0;
    if (tx_lock_record(FILE_ACCOUNTS, aid, LOCK_WRITE) < 0) return E_BUSY;
    if (rf_read(FILE_ACCOUNTS, aid, &a) < 0) return E_FAIL;
    if (a.holder2_id == 0) {
        if (a.balance_cents != 0) return E_BALANCE;
        a.status = ACCT_CLOSED;
    } else if (a.holder1_id == user_id) {
        a.holder1_id = a.holder2_id;                  /* the remaining holder becomes holder1 */
        a.holder2_id = 0;
    } else {
        a.holder2_id = 0;
    }
    return tx_write_record(FILE_ACCOUNTS, aid, &a) < 0 ? E_FAIL : 0;
}

/* a deleted employee's ASSIGNED loans go back to PENDING (the only allowed way back) */
static int release_assigned_loans(int employee_id)
{
    heal_before_scan(FILE_LOANS);
    int n = rf_count(FILE_LOANS);
    for (int id = 1; id <= n; id++) {
        LoanRecord l;
        if (rf_read(FILE_LOANS, id, &l) < 0 || strcmp(l.status, LOAN_ASSIGNED) != 0 ||
            l.assigned_employee_id != employee_id) continue;
        if (tx_lock_record(FILE_LOANS, id, LOCK_WRITE) < 0) return E_BUSY;
        if (rf_read(FILE_LOANS, id, &l) < 0) return E_FAIL;                /* re-read under the lock */
        if (strcmp(l.status, LOAN_ASSIGNED) != 0 || l.assigned_employee_id != employee_id) continue;
        snprintf(l.status, sizeof l.status, "%s", LOAN_PENDING);
        l.assigned_employee_id = 0;
        if (tx_write_record(FILE_LOANS, id, &l) < 0) return E_FAIL;
    }
    return 0;
}

static int delete_user_body(int user_id)
{
    UserRecord u;
    if (tx_lock_table(FILE_USERS) < 0) return E_BUSY;
    if (user_id < 1 || user_id > rf_count(FILE_USERS)) return E_NOT_FOUND;
    if (tx_lock_record(FILE_USERS, user_id, LOCK_WRITE) < 0) return E_BUSY;
    if (rf_read(FILE_USERS, user_id, &u) < 0) return E_FAIL;
    if (!u.is_active) return E_NOT_FOUND;
    if (u.role == ROLE_ADMIN && is_last_admin(user_id)) return E_LAST_ADMIN;

    int rc = 0;
    if (u.role == ROLE_CUSTOMER) rc = detach_from_account(user_id);
    else if (u.role == ROLE_EMPLOYEE) rc = release_assigned_loans(user_id);
    if (rc < 0) return rc;

    u.is_active = 0;                                  /* soft delete: the record stays, ids never move */
    return tx_write_record(FILE_USERS, user_id, &u) < 0 ? E_FAIL : 0;
}

int delete_user(Session *s, int user_id)
{
    tx_begin(s);
    heal_before_scan(FILE_USERS);
    return tx_finish(delete_user_body(user_id));
}

/* ---------------------------------------------------------------- modify */

static int modify_user_body(Session *s, int user_id, int by_name, const char *value)
{
    UserRecord u;
    if (by_name && tx_lock_table(FILE_USERS) < 0) return E_BUSY;
    if (tx_lock_record(FILE_USERS, user_id, LOCK_WRITE) < 0) return E_BUSY;
    if (rf_read(FILE_USERS, user_id, &u) < 0) return E_FAIL;
    if (s->role == ROLE_EMPLOYEE && (u.role != ROLE_CUSTOMER || !u.is_active)) return E_NOT_CUSTOMER;
    if (!u.is_active) return E_NOT_FOUND;
    if (by_name) {
        if (username_exists(value)) return E_EXISTS;
        snprintf(u.username, sizeof u.username, "%s", value);
    } else {
        auth_new_salt(u.salt);
        auth_hash_password(value, u.salt, u.password_hash);
    }
    return tx_write_record(FILE_USERS, user_id, &u) < 0 ? E_FAIL : 0;
}

/* MODIFYUSER (admin: password, username, role) and MODIFYCUSTOMER (employee: password, username of a customer) */
int modify_user(Session *s, int user_id, const char *field, const char *value)
{
    if (strcmp(field, "role") == 0) {
        if (s->role != ROLE_ADMIN) return E_BAD_FIELD;
        return set_role(s, user_id, role_from_name(value));
    }
    int by_name = strcmp(field, "username") == 0;
    if (!by_name && strcmp(field, "password") != 0) return E_BAD_FIELD;
    if ((by_name ? validate_username(value) : validate_password(value)) < 0) return E_BAD_ARG;
    if (user_id < 1 || user_id > rf_count(FILE_USERS)) return E_NOT_FOUND;

    tx_begin(s);
    heal_before_scan(FILE_USERS);
    return tx_finish(modify_user_body(s, user_id, by_name, value));
}

static int set_role_body(int user_id, int role)
{
    UserRecord u;
    if (tx_lock_table(FILE_USERS) < 0) return E_BUSY;
    if (tx_lock_record(FILE_USERS, user_id, LOCK_WRITE) < 0) return E_BUSY;
    if (rf_read(FILE_USERS, user_id, &u) < 0) return E_FAIL;
    if (!u.is_active) return E_NOT_FOUND;
    if (u.role == ROLE_CUSTOMER) return E_BAD_ROLE;             /* customers can't be promoted */
    if (u.role == ROLE_ADMIN && role != ROLE_ADMIN && is_last_admin(user_id)) return E_LAST_ADMIN;
    u.role = role;
    return tx_write_record(FILE_USERS, user_id, &u) < 0 ? E_FAIL : 0;
}

int set_role(Session *s, int user_id, int role)
{
    if (role < ROLE_EMPLOYEE || role > ROLE_ADMIN) return E_BAD_ROLE;   /* staff can't become customers */
    if (user_id < 1 || user_id > rf_count(FILE_USERS)) return E_NOT_FOUND;
    tx_begin(s);
    heal_before_scan(FILE_USERS);
    return tx_finish(set_role_body(user_id, role));
}

/* PASSWD: check the old password, store a new salted hash */
static int change_password_body(Session *s, const char *old_pw, const char *new_pw)
{
    UserRecord u;
    char hash[65];
    if (tx_lock_record(FILE_USERS, s->user_id, LOCK_WRITE) < 0) return E_BUSY;
    if (rf_read(FILE_USERS, s->user_id, &u) < 0) return E_FAIL;
    auth_hash_password(old_pw, u.salt, hash);
    if (strcmp(hash, u.password_hash) != 0) return E_BAD_PASSWORD;
    auth_new_salt(u.salt);
    auth_hash_password(new_pw, u.salt, u.password_hash);
    return tx_write_record(FILE_USERS, s->user_id, &u) < 0 ? E_FAIL : 0;
}

int change_password(Session *s, const char *old_pw, const char *new_pw)
{
    if (validate_password(new_pw) < 0) return E_BAD_ARG;
    tx_begin(s);
    return tx_finish(change_password_body(s, old_pw, new_pw));
}

/* ---------------------------------------------------------------- searches (read only) */

static int search_body(const char *name, int customers_only, Out *out)
{
    UserRecord u;
    int id = find_user_by_name(name, NULL);
    if (id == 0) return E_NOT_FOUND;
    if (tx_lock_record(FILE_USERS, id, LOCK_READ) < 0) return E_BUSY;
    if (rf_read(FILE_USERS, id, &u) < 0) return E_FAIL;
    if (customers_only) {
        if (u.role != ROLE_CUSTOMER) return E_NOT_CUSTOMER;
        int aid = find_any_account(id);
        AccountRecord a;
        out_add(out, "| user_id=%d account_id=%d status=%s\n", id, aid,
                aid && rf_read(FILE_ACCOUNTS, aid, &a) == 0 ? status_name(a.status) : "none");
    } else {
        out_add(out, "| user_id=%d username=%s role=%s active=%d\n", id, u.username, role_name(u.role), u.is_active);
    }
    return 1;
}

int search_user(const char *name, Out *out)
{
    tx_begin(NULL);
    heal_before_scan(FILE_USERS);
    return tx_finish(search_body(name, 0, out));
}

int search_customer(const char *name, Out *out)
{
    tx_begin(NULL);
    heal_before_scan(FILE_USERS);
    return tx_finish(search_body(name, 1, out));
}

int list_employees(Out *out)
{
    int rows = 0;
    tx_begin(NULL);
    heal_before_scan(FILE_USERS);
    int n = rf_count(FILE_USERS);
    for (int id = 1; id <= n; id++) {
        UserRecord u;
        if (rf_read(FILE_USERS, id, &u) == 0 && u.role == ROLE_EMPLOYEE && u.is_active) {
            out_add(out, "| id=%d username=%s\n", id, u.username);
            rows++;
        }
    }
    return tx_finish(rows);
}
