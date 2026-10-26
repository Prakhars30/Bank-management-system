#!/usr/bin/env python3
"""bank_ui.py - a menu-driven terminal front end for the bank server.

Standalone: it speaks the server's plain text protocol (the same lines ./client sends),
so nothing in the C code, the Makefile or ./client has to change.

    python3 bank_ui.py [host] [port]        # defaults: 127.0.0.1 5050

Every prompt can be cancelled by pressing Enter on an empty line.
"""
import getpass
import os
import re
import socket
import sys

# ----------------------------------------------------------------------------- colours

USE_COLOR = sys.stdout.isatty() and "NO_COLOR" not in os.environ


def _c(code, text):
    return f"\033[{code}m{text}\033[0m" if USE_COLOR else text


def bold(t):   return _c("1", t)
def dim(t):    return _c("2", t)
def red(t):    return _c("31", t)
def green(t):  return _c("32", t)
def yellow(t): return _c("33", t)
def cyan(t):   return _c("36", t)


class ServerClosed(Exception):
    pass


class Cancel(Exception):
    pass


# ----------------------------------------------------------------------------- connection

class Conn:
    """One TCP connection. send() returns (ok, message, rows)."""

    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=10)
        self.sock.settimeout(None)
        self.f = self.sock.makefile("rw", encoding="utf-8", newline="\n")
        self._read_reply()                                    # greeting

    def _read_reply(self):
        rows = []
        while True:
            try:
                line = self.f.readline()
            except OSError:
                raise ServerClosed()
            if not line:
                raise ServerClosed()
            line = line.rstrip("\r\n")
            if line.startswith("OK") or line.startswith("ERR"):
                ok = line.startswith("OK")
                return ok, line.split(" ", 1)[1] if " " in line else "", rows
            rows.append(line[2:] if line.startswith("| ") else line)

    def send(self, cmd):
        try:
            self.f.write(cmd + "\n")
            self.f.flush()
        except OSError:
            raise ServerClosed()
        return self._read_reply()

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


# ----------------------------------------------------------------------------- tables

PRETTY = {"id": "ID"}
RIGHT = {"amount", "balance", "balance_after"}


def pretty_header(key):
    return " ".join(PRETTY.get(w, w.capitalize()) for w in key.replace("_", " ").split()) \
        .replace(" Id", " ID")


def style_cell(key, text):
    low = text.lower()
    if key in ("status",):
        if low in ("active", "approved"):            return green(text)
        if low in ("pending", "assigned", "inactive"): return yellow(text)
        if low in ("closed", "rejected"):            return red(text)
    if key == "active":
        return green(text) if text == "yes" else red(text)
    if key == "type":
        if low in ("deposit", "transfer_in"):        return green(text)
        if low in ("withdraw", "transfer_out"):      return red(text)
    return text


def draw_table(headers, rows, keys):
    widths = [max([len(h)] + [len(r[i]) for r in rows]) for i, h in enumerate(headers)]

    def rule(l, m, r):
        return "  " + dim(l + m.join("─" * (w + 2) for w in widths) + r)

    def fmt(cells, styled):
        out = []
        for i, t in enumerate(cells):
            padded = t.rjust(widths[i]) if keys[i] in RIGHT else t.ljust(widths[i])
            out.append(" " + (style_cell(keys[i], padded) if styled else bold(padded)) + " ")
        sep = dim("│")
        return "  " + sep + sep.join(out) + sep

    print(rule("┌", "┬", "┐"))
    print(fmt(headers, False))
    print(rule("├", "┼", "┤"))
    for r in rows:
        print(fmt(r, True))
    print(rule("└", "┴", "┘"))


KV_ROW = re.compile(r"\w+=\S*(\s+\w+=\S*)*")
HISTORY_ROW = re.compile(r"txn=(\d+) (\S+ \S+) (\S+) amount=(\S+) balance_after=(\S+)")
FEEDBACK_ROW = re.compile(r"#(\d+) user=(\d+) (\S+ \S+): (.*)")


def show_rows(rows):
    if not rows:
        return
    if all(HISTORY_ROW.fullmatch(r) for r in rows):
        data = [list(HISTORY_ROW.fullmatch(r).groups()) for r in rows]
        draw_table(["Txn", "Date", "Type", "Amount", "Balance"], data,
                   ["txn", "date", "type", "amount", "balance_after"])
    elif all(FEEDBACK_ROW.fullmatch(r) for r in rows):
        for r in rows:
            n, user, when, text = FEEDBACK_ROW.fullmatch(r).groups()
            print(f"  {cyan('#' + n)} {dim('user ' + user + ' · ' + when)}")
            print(f"     {text}")
    elif all(KV_ROW.fullmatch(r) for r in rows):
        parsed = [re.findall(r"(\w+)=(\S*)", r) for r in rows]
        keys = [k for k, _ in parsed[0]]
        data = []
        for p in parsed:
            d = dict(p)
            row = []
            for k in keys:
                v = d.get(k, "")
                if k == "active":
                    v = "yes" if v == "1" else "no"
                row.append(v)
            data.append(row)
        draw_table([pretty_header(k) for k in keys], data, keys)
    else:
        for r in rows:
            print("  " + r)


# ----------------------------------------------------------------------------- prompts

def ask(prompt, check=None, secret=False):
    while True:
        try:
            v = (getpass.getpass if secret else input)(f"  {prompt}: ")
        except (EOFError, KeyboardInterrupt):
            print()
            raise Cancel()
        v = v.strip()
        if not v:
            raise Cancel()
        err = check(v) if check else None
        if err:
            print(red("  " + err))
            continue
        return v


def check_amount(v):
    if not re.fullmatch(r"\d+(\.\d{1,2})?", v) or float(v) <= 0:
        return "Enter a positive amount like 50 or 12.50"


def check_int(v):
    if not re.fullmatch(r"\d+", v):
        return "Enter a number"


def check_word(v):
    if " " in v:
        return "No spaces allowed"


def ask_amount(p="Amount"):  return ask(p, check_amount)
def ask_int(p):              return ask(p, check_int)
def ask_word(p):             return ask(p, check_word)


def pick(prompt, options):
    for i, o in enumerate(options, 1):
        print(f"    {cyan(str(i))}  {o}")
    v = ask(prompt, lambda x: None if x.isdigit() and 1 <= int(x) <= len(options) else "Pick a number from the list")
    return options[int(v) - 1]


def confirm(question):
    try:
        v = input(f"  {yellow(question)} [y/N]: ").strip().lower()
    except (EOFError, KeyboardInterrupt):
        print()
        return False
    return v in ("y", "yes")


def ask_new_password(label="New password"):
    new = ask(label, check_word, secret=True)
    again = ask("Repeat it", None, secret=True)
    if new != again:
        print(red("  ✘ Passwords do not match"))
        raise Cancel()
    return new


# ----------------------------------------------------------------------------- output helpers

FATAL = ("account deactivated", "session timed out", "too many failed attempts")


def nice(msg):
    return msg[:1].upper() + msg[1:] if msg else msg


def run(conn, cmd, rows_first=True):
    """Send one command, print rows then the verdict, return (ok, msg, rows)."""
    ok, msg, rows = conn.send(cmd)
    if rows:
        print()
        show_rows(rows)
        print()
    if ok:
        if msg.startswith("OK") or not rows:
            print(f"  {green('✔')} {nice(msg)}")
        else:
            print(f"  {dim(msg)}")
    else:
        print(f"  {red('✘')} {nice(msg)}")
        if msg in FATAL:
            raise ServerClosed()
    return ok, msg, rows


def box(lines):
    width = max(len(re.sub(r"\033\[[0-9;]*m", "", l)) for l in lines) + 4
    print(dim("  ╭" + "─" * width + "╮"))
    for l in lines:
        visible = len(re.sub(r"\033\[[0-9;]*m", "", l))
        print(dim("  │") + "  " + l + " " * (width - visible - 2) + dim("│"))
    print(dim("  ╰" + "─" * width + "╯"))


# ----------------------------------------------------------------------------- actions
# Every action: (conn) -> None. Raise Cancel to back out.

def a_balance(c):
    ok, msg, _ = c.send("BALANCE")
    if ok:
        print(f"\n  Current balance:  {bold(green(msg.replace('balance ', '')))}\n")
    else:
        print(f"  {red('✘')} {nice(msg)}")


def a_deposit(c):   run(c, f"DEPOSIT {ask_amount()}")
def a_withdraw(c):  run(c, f"WITHDRAW {ask_amount()}")


def a_transfer(c):
    to = ask_int("To account number")
    amt = ask_amount()
    if confirm(f"Send {amt} to account {to}?"):
        run(c, f"TRANSFER {to} {amt}")
    else:
        print(dim("  Cancelled"))


def a_details(c):   run(c, "DETAILS")
def a_history(c):   run(c, "HISTORY")


def a_loan_apply(c):
    amt = ask_amount("Loan amount")
    if confirm(f"Apply for a loan of {amt}?"):
        run(c, f"LOAN_APPLY {amt}")
    else:
        print(dim("  Cancelled"))


def a_feedback(c):
    text = ask("Your feedback")
    run(c, f"FEEDBACK {text}")


def a_passwd(c):
    old = ask("Current password", check_word, secret=True)
    new = ask_new_password()
    run(c, f"PASSWD {old} {new}")


# employee
def a_addcustomer(c):
    name = ask_word("New customer username")
    pw = ask("Initial password", check_word, secret=True)
    run(c, f"ADDCUSTOMER {name} {pw}")


def a_modifycustomer(c):
    uid = ask_int("Customer user ID")
    field = pick("Change which field", ["password", "username"])
    val = ask("New password" if field == "password" else "New username", check_word,
              secret=(field == "password"))
    run(c, f"MODIFYCUSTOMER {uid} {field} {val}")


def a_findcustomer(c):  run(c, f"FINDCUSTOMER {ask_word('Customer username')}")
def a_viewtxns(c):      run(c, f"VIEWTXNS {ask_int('Account ID')}")
def a_loans_assigned(c): run(c, "LOANS_ASSIGNED")


def a_loan_process(c):
    ok, _, rows = run(c, "LOANS_ASSIGNED")
    if not ok or not rows:
        return
    lid = ask_int("Loan ID to process")
    decision = pick("Decision", ["APPROVE", "REJECT"])
    if confirm(f"{decision.capitalize()} loan {lid}?"):
        run(c, f"LOAN_PROCESS {lid} {decision}")
    else:
        print(dim("  Cancelled"))


# manager
def a_activate(c):    run(c, f"ACTIVATE {ask_int('Account ID')}")


def a_deactivate(c):
    aid = ask_int("Account ID")
    if confirm(f"Deactivate account {aid}?"):
        run(c, f"DEACTIVATE {aid}")
    else:
        print(dim("  Cancelled"))


def a_loans_pending(c): run(c, "LOANS_PENDING")
def a_employees(c):     run(c, "EMPLOYEES")
def a_feedback_list(c): run(c, "FEEDBACK_LIST")


def a_assign(c):
    ok, _, rows = run(c, "LOANS_PENDING")
    if not ok or not rows:
        return
    lid = ask_int("Loan ID")
    ok, _, rows = run(c, "EMPLOYEES")
    if not ok or not rows:
        return
    eid = ask_int("Employee ID to assign it to")
    run(c, f"ASSIGN {lid} {eid}")


# admin
def a_adduser(c):
    name = ask_word("New username")
    pw = ask("Password", check_word, secret=True)
    role = pick("Role", ["employee", "manager", "admin"])
    run(c, f"ADDUSER {name} {pw} {role}")


def a_modifyuser(c):
    uid = ask_int("User ID")
    field = pick("Change which field", ["password", "username", "role"])
    if field == "role":
        val = pick("New role", ["employee", "manager", "admin"])
    else:
        val = ask("New " + field, check_word, secret=(field == "password"))
    run(c, f"MODIFYUSER {uid} {field} {val}")


def a_deleteuser(c):
    uid = ask_int("User ID to delete")
    if confirm(f"Really delete user {uid}? This cannot be undone."):
        run(c, f"DELETEUSER {uid}")
    else:
        print(dim("  Cancelled"))


def a_searchuser(c):  run(c, f"SEARCHUSER {ask_word('Username')}")


def a_addaccount(c):
    h1 = ask_int("Holder 1 user ID")
    h2 = ""
    if confirm("Add a second (joint) holder?"):
        h2 = " " + ask_int("Holder 2 user ID")
    run(c, f"ADDACCOUNT {h1}{h2}")


def a_modifyaccount(c):
    aid = ask_int("Account ID")
    field = pick("Change which field", ["holder1_id", "holder2_id", "status"])
    if field == "status":
        val = pick("New status", ["active", "inactive"])
    else:
        val = ask_int("New user ID (0 to remove holder 2)")
    run(c, f"MODIFYACCOUNT {aid} {field} {val}")


def a_deleteaccount(c):
    aid = ask_int("Account ID to close")
    if confirm(f"Really close account {aid}?"):
        run(c, f"DELETEACCOUNT {aid}")
    else:
        print(dim("  Cancelled"))


def a_searchaccount(c): run(c, f"SEARCHACCOUNT {ask_int('Account ID')}")


def a_raw(c):
    cmd = ask("Command (e.g. HELP)")
    run(c, cmd)


MENUS = {
    "customer": [
        ("Check balance", a_balance), ("Deposit", a_deposit), ("Withdraw", a_withdraw),
        ("Transfer money", a_transfer), ("Account details", a_details),
        ("Transaction history", a_history), ("Apply for a loan", a_loan_apply),
        ("Send feedback", a_feedback), ("Change password", a_passwd),
    ],
    "employee": [
        ("Add customer", a_addcustomer), ("Modify customer", a_modifycustomer),
        ("Find customer", a_findcustomer), ("View account transactions", a_viewtxns),
        ("Loans assigned to me", a_loans_assigned), ("Approve / reject a loan", a_loan_process),
        ("Change password", a_passwd),
    ],
    "manager": [
        ("Activate account", a_activate), ("Deactivate account", a_deactivate),
        ("Pending loans", a_loans_pending), ("Assign loan to employee", a_assign),
        ("List employees", a_employees), ("Customer feedback", a_feedback_list),
        ("Change password", a_passwd),
    ],
    "admin": [
        ("Add user", a_adduser), ("Modify user", a_modifyuser), ("Delete user", a_deleteuser),
        ("Search user", a_searchuser), ("Add account", a_addaccount),
        ("Modify account", a_modifyaccount), ("Delete account", a_deleteaccount),
        ("Search account", a_searchaccount), ("Change password", a_passwd),
    ],
}


# ----------------------------------------------------------------------------- screens

def banner():
    print()
    box([bold("B A N K"), dim("management system")])


def login_screen(conn):
    """Returns (username, role), or None if the person chose to quit."""
    banner()
    print(dim("\n  Sign in  (leave the username empty to quit)\n"))
    while True:
        try:
            user = ask("Username", check_word)
        except Cancel:
            return None
        try:
            pw = ask("Password", check_word, secret=True)
        except Cancel:
            continue
        ok, msg, _ = conn.send(f"LOGIN {user} {pw}")
        if ok:
            m = re.match(r"welcome (\S+) \((\w+)\)", msg)
            return (m.group(1), m.group(2)) if m else (user, "customer")
        print(f"  {red('✘')} {nice(msg)}\n")
        if msg in FATAL:
            raise ServerClosed()


def session(conn, user, role):
    """The menu loop. Returns 'logout' or 'quit'."""
    menu = MENUS.get(role, [])
    while True:
        print()
        title = [bold(user) + dim(f"  ·  {role}")]
        if role == "customer":
            ok, msg, _ = conn.send("BALANCE")
            if ok:
                title.append("Balance  " + bold(green(msg.replace("balance ", ""))))
        box(title)
        print()
        for i, (label, _) in enumerate(menu, 1):
            print(f"   {cyan(str(i).rjust(2))}  {label}")
        print(f"   {cyan(' r')}  {dim('Run a raw command')}")
        print(f"   {cyan(' 0')}  Log out")
        print(f"   {cyan(' q')}  Quit")
        print()
        try:
            choice = input(f"  {bold('Choose')} › ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            print()
            return "quit"
        if choice == "0":
            conn.send("LOGOUT")
            return "logout"
        if choice == "q":
            try:
                conn.send("QUIT")
            except ServerClosed:
                pass
            return "quit"
        if choice == "r":
            action = a_raw
        elif choice.isdigit() and 1 <= int(choice) <= len(menu):
            action = menu[int(choice) - 1][1]
        else:
            print(red("  Pick one of the numbers above"))
            continue
        print()
        try:
            action(conn)
        except Cancel:
            print(dim("  Cancelled"))


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 5050
    while True:
        try:
            conn = Conn(host, port)
        except OSError as e:
            print(red(f"\n  Cannot reach the server at {host}:{port} ({e.strerror or e})"))
            print(dim("  Is ./server running?\n"))
            return 1
        try:
            who = login_screen(conn)
            if who is None:
                conn.send("QUIT")
                print(dim("\n  Goodbye.\n"))
                return 0
            if session(conn, *who) == "quit":
                print(dim("\n  Goodbye.\n"))
                return 0
            print(dim("\n  You have been logged out.\n"))
        except ServerClosed:
            print(yellow("\n  The server closed the connection. Starting a new one...\n"))
        finally:
            conn.close()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print(dim("\n\n  Goodbye.\n"))