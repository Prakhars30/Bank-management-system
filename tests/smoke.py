#!/usr/bin/env python3
"""Smoke test: starts a fresh server in a temp directory and checks every role's commands."""
import os, shutil, socket, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = 5077
failures = 0


class Client:
    def __init__(self):
        self.sock = socket.create_connection(("127.0.0.1", PORT), timeout=10)
        self.f = self.sock.makefile("rw", newline="\n")
        self.read()                                   # greeting

    def read(self):
        rows = []
        while True:
            line = self.f.readline()
            if not line:
                return rows + ["<EOF>"]
            rows.append(line.rstrip())
            if line.startswith("OK") or line.startswith("ERR"):
                return rows

    def do(self, line):
        self.f.write(line + "\n")
        self.f.flush()
        return self.read()

    def login(self, user, pw="pass123"):
        r = self.do(f"LOGIN {user} {pw}")
        assert r[-1].startswith("OK"), r
        return self


def check(name, got, want):
    global failures
    last = got[-1] if isinstance(got, list) else got
    ok = last.startswith(want)
    if not ok:
        failures += 1
    print(("  ok   " if ok else "  FAIL ") + f"{name}: {last!r}" + ("" if ok else f"   (wanted {want!r})"))


def main():
    tmp = tempfile.mkdtemp()
    server = subprocess.Popen([os.path.join(ROOT, "server"), str(PORT)], cwd=tmp,
                              stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    time.sleep(0.8)
    try:
        run()
    finally:
        server.terminate()
        server.wait()
        err = server.stderr.read().decode()
        if err.strip():
            print("server stderr:", err)
        shutil.rmtree(tmp)
    print("FAILED" if failures else "ALL PASSED")
    sys.exit(1 if failures else 0)


def run():
    print("login")
    c = Client()
    check("no login yet", c.do("BALANCE"), "ERR please LOGIN")
    check("bad password", c.do("LOGIN alice nope"), "ERR invalid")
    check("good login", c.do("LOGIN alice pass123"), "OK welcome")
    check("login twice", c.do("LOGIN alice pass123"), "ERR already")
    c2 = Client()
    check("second session same user", c2.do("LOGIN alice pass123"), "ERR already logged in")
    check("logout frees user", c.do("LOGOUT"), "OK")
    check("now second can log in", c2.do("LOGIN alice pass123"), "OK welcome")
    c2.do("LOGOUT"); c2.sock.close()
    c3 = Client()
    for _ in range(2):
        c3.do("LOGIN alice bad")
    check("3 bad attempts disconnects", c3.do("LOGIN alice bad"), "ERR too many")

    print("customer")
    a = Client().login("alice")
    check("balance", a.do("BALANCE"), "OK balance 875.00")
    check("deposit", a.do("DEPOSIT 25"), "OK")
    check("withdraw too much", a.do("WITHDRAW 99999"), "ERR insufficient")
    check("negative amount", a.do("DEPOSIT -5"), "ERR usage")
    check("zero amount", a.do("DEPOSIT 0"), "ERR amount must be positive")
    check("three decimals", a.do("DEPOSIT 1.234"), "ERR usage")
    check("transfer to self", a.do("TRANSFER 1 1"), "ERR cannot transfer")
    check("transfer to missing", a.do("TRANSFER 99 1"), "ERR not found")
    check("transfer ok", a.do("TRANSFER 2 10"), "OK")
    check("balance after", a.do("BALANCE"), "OK balance 890.00")
    check("history rows", a.do("HISTORY"), "OK 5 rows")
    check("details", a.do("DETAILS"), "OK 1 row")
    check("feedback", a.do("FEEDBACK I like this bank"), "OK")
    check("feedback empty", a.do("FEEDBACK"), "ERR bad argument")
    check("admin cmd refused", a.do("ADDUSER x y admin"), "ERR unknown")
    check("passwd wrong old", a.do("PASSWD wrong new1"), "ERR wrong password")
    check("passwd", a.do("PASSWD pass123 pass456"), "OK")
    a.do("LOGOUT")
    check("login with new password", a.do("LOGIN alice pass456"), "OK welcome")
    a.do("PASSWD pass456 pass123")

    print("joint account")
    carol = Client().login("carol")
    dave = Client().login("dave")
    check("carol balance", carol.do("BALANCE"), "OK balance 250.00")
    check("dave withdraw", dave.do("WITHDRAW 50"), "OK")
    check("carol sees it", carol.do("BALANCE"), "OK balance 200.00")

    print("manager / employee / loans")
    m = Client().login("manager1")
    e1 = Client().login("employee1")
    e2 = Client().login("employee2")
    check("pending", m.do("LOANS_PENDING"), "OK 1 row")
    check("employees", m.do("EMPLOYEES"), "OK 2 rows")
    check("employee sees none yet", e1.do("LOANS_ASSIGNED"), "OK 0 rows")
    check("assign to non employee", m.do("ASSIGN 1 3"), "ERR not an active employee")
    check("assign", m.do("ASSIGN 1 4"), "OK")
    check("assign again refused", m.do("ASSIGN 1 5"), "ERR loan status")
    check("employee1 sees loan", e1.do("LOANS_ASSIGNED"), "OK 1 row")
    check("employee2 can't process it", e2.do("LOAN_PROCESS 1 APPROVE"), "ERR loan changed")
    check("manager deactivates", m.do("DEACTIVATE 1"), "OK")
    check("deposit refused", a.do("DEPOSIT 1"), "ERR account inactive")
    check("transfer in refused", Client().login("bob").do("TRANSFER 1 1"), "ERR account inactive")
    check("approve to inactive", e1.do("LOAN_PROCESS 1 APPROVE"), "ERR account inactive")
    check("loan still assigned", e1.do("LOANS_ASSIGNED"), "OK 1 row")
    check("loan apply refused on inactive", a.do("LOAN_APPLY 10"), "ERR account inactive")
    check("manager activates", m.do("ACTIVATE 1"), "OK")
    check("approve", e1.do("LOAN_PROCESS 1 approve"), "OK")
    check("balance with loan", a.do("BALANCE"), "OK balance 5890.00")
    check("approve twice", e1.do("LOAN_PROCESS 1 APPROVE"), "ERR loan changed")
    check("feedback list", m.do("FEEDBACK_LIST"), "OK 2 rows")
    check("employee viewtxns", e1.do("VIEWTXNS 1"), "OK 6 rows")
    check("employee cannot deposit", e1.do("DEPOSIT 5"), "ERR unknown")
    check("employee findcustomer", e1.do("FINDCUSTOMER alice"), "OK 1 row")
    check("findcustomer on staff", e1.do("FINDCUSTOMER manager1"), "ERR not a customer")
    check("modifycustomer staff", e1.do("MODIFYCUSTOMER 3 password x"), "ERR not a customer")
    check("modifycustomer role field", e1.do("MODIFYCUSTOMER 6 role admin"), "ERR field not allowed")
    check("addcustomer", e1.do("ADDCUSTOMER erin pass123"), "OK customer created")
    check("addcustomer duplicate", e1.do("ADDCUSTOMER erin pass123"), "ERR username already")
    check("new customer logs in", Client().login("erin").do("BALANCE"), "OK balance 0.00")
    check("modifycustomer username", e1.do("MODIFYCUSTOMER 10 username erin2"), "OK")
    check("old name gone, new works", Client().login("erin2", "pass123").do("BALANCE"), "OK balance 0.00")
    check("username collision", e1.do("MODIFYCUSTOMER 10 username alice"), "ERR username already")

    print("admin")
    ad = Client().login("admin", "admin123")
    check("admin cannot move money", ad.do("DEPOSIT 5"), "ERR unknown")
    check("adduser customer refused", ad.do("ADDUSER zed pass123 customer"), "ERR customers are added")
    check("adduser", ad.do("ADDUSER frank pass123 employee"), "OK user created")
    check("adduser duplicate", ad.do("ADDUSER frank pass123 manager"), "ERR username already")
    check("searchuser", ad.do("SEARCHUSER frank"), "OK 1 row")
    check("modifyuser role", ad.do("MODIFYUSER 11 role manager"), "OK")
    check("customer can't be promoted", ad.do("MODIFYUSER 6 role admin"), "ERR bad role")
    check("staff can't become customer", ad.do("MODIFYUSER 11 role customer"), "ERR bad role")
    fr = Client().login("frank")
    check("role change applies on next command", fr.do("LOANS_PENDING"), "OK")
    check("old role commands gone", fr.do("ADDCUSTOMER q q"), "ERR unknown")
    check("delete user", ad.do("DELETEUSER 11"), "OK user deleted")
    check("deleted user's next command", fr.do("LOANS_PENDING"), "ERR account deactivated")
    check("deleted user can't log in", Client().do("LOGIN frank pass123"), "ERR account deactivated")
    check("deleted name stays reserved", ad.do("ADDUSER frank x employee"), "ERR username already")
    check("delete twice", ad.do("DELETEUSER 11"), "ERR not found")
    check("delete admin2 ok (another admin exists)", ad.do("DELETEUSER 2"), "OK")
    check("last admin delete refused", ad.do("DELETEUSER 1"), "ERR cannot remove the last")
    check("last admin demote refused", ad.do("MODIFYUSER 1 role manager"), "ERR cannot remove the last")
    check("searchaccount", ad.do("SEARCHACCOUNT 1"), "OK 1 row")
    check("modifyaccount balance refused", ad.do("MODIFYACCOUNT 1 balance_cents 5"), "ERR field not allowed")
    check("one account rule: add", ad.do("ADDACCOUNT 6"), "ERR user already holds")
    check("addaccount for staff", ad.do("ADDACCOUNT 3"), "ERR not a customer")

    print("closing accounts / deleting customers")
    check("delete account with money", ad.do("DELETEACCOUNT 1"), "ERR balance must be zero")
    check("delete user with money", ad.do("DELETEUSER 6"), "ERR balance must be zero")
    erin = Client().login("erin2")
    check("empty account closes", ad.do("DELETEACCOUNT 4"), "OK account closed")
    check("closed deposit", erin.do("DEPOSIT 1"), "ERR account closed")
    check("closed balance still readable", erin.do("BALANCE"), "OK balance 0.00")
    check("transfer to closed", a.do("TRANSFER 4 1"), "ERR account closed")
    check("manager activate closed", m.do("ACTIVATE 4"), "ERR account closed")
    check("closed is final (modify)", ad.do("MODIFYACCOUNT 4 status active"), "ERR account closed")
    check("holder with closed acct can get new one", ad.do("ADDACCOUNT 10"), "OK account created")
    check("carol leaves joint account", ad.do("DELETEUSER 8"), "OK")
    check("dave is now holder1", ad.do("SEARCHACCOUNT 3"), "OK 1 row")
    check("dave still works", dave.do("BALANCE"), "OK balance 200.00")
    check("carol disconnected", carol.do("BALANCE"), "ERR account deactivated")
    check("move holder2 to bob (has account)", ad.do("MODIFYACCOUNT 3 holder2_id 7"), "ERR user already holds")
    check("remove holder2", ad.do("MODIFYACCOUNT 3 holder2_id 0"), "OK")

    print("employee deletion releases loans")
    check("alice loan", a.do("LOAN_APPLY 100"), "OK loan 2")
    check("assign to e2", m.do("ASSIGN 2 5"), "OK")
    check("delete employee2", ad.do("DELETEUSER 5"), "OK")
    check("loan back in pending", m.do("LOANS_PENDING"), "OK 1 row")
    check("deleted employee cut off", e2.do("LOANS_ASSIGNED"), "ERR account deactivated")

    print("misc")
    check("unknown command", a.do("FLY"), "ERR unknown")
    check("help", a.do("HELP"), "OK help")
    check("quit", a.do("QUIT"), "OK bye")


main()
