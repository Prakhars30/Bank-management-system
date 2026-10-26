#!/usr/bin/env python3
"""A live server must heal an unfinished transaction left by a dead process, whichever lock it touches first."""
import os, shutil, socket, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = 5079
failures = 0


class Client:
    def __init__(self, user, pw="pass123"):
        self.f = socket.create_connection(("127.0.0.1", PORT), timeout=10).makefile("rw", newline="\n")
        self.read()
        self.do(f"LOGIN {user} {pw}")

    def read(self):
        rows = []
        while True:
            line = self.f.readline()
            rows.append(line.rstrip())
            if line.startswith("OK") or line.startswith("ERR") or not line:
                return rows

    def do(self, line):
        self.f.write(line + "\n")
        self.f.flush()
        return self.read()


def check(name, got, want):
    global failures
    ok = got == want
    failures += (not ok)
    print(("  ok   " if ok else "  FAIL ") + f"{name}: {got!r}" + ("" if ok else f" (wanted {want!r})"))


def crash(tmp):
    subprocess.run([os.path.join(ROOT, "tests", "crash_test")], cwd=tmp, check=True, stdout=subprocess.DEVNULL)


def main():
    tmp = tempfile.mkdtemp()
    server = subprocess.Popen([os.path.join(ROOT, "server"), str(PORT)], cwd=tmp, stdout=subprocess.DEVNULL)
    time.sleep(0.8)
    try:
        print("dead transaction, first touched by a READ of the account record")
        crash(tmp)
        check("balance healed", Client("alice").do("BALANCE")[-1], "OK balance 875.00")
        check("ghost txn gone", Client("employee1").do("VIEWTXNS 1")[-1], "OK 3 rows")

        print("dead transaction, first touched by a scan of transactions.bin")
        crash(tmp)
        check("history healed", Client("alice").do("HISTORY")[-1], "OK 3 rows")
        check("balance healed", Client("alice").do("BALANCE")[-1], "OK balance 875.00")

        print("dead transaction, first touched by an APPEND on a different account")
        crash(tmp)
        check("bob deposit heals alice's dead tx first", Client("bob").do("DEPOSIT 1")[-1], "OK deposited")
        check("alice balance healed", Client("alice").do("BALANCE")[-1], "OK balance 875.00")
        check("txn ids stay contiguous", Client("employee1").do("VIEWTXNS 2")[-1], "OK 3 rows")

        print("dead transaction, first touched by a WRITE to the same account")
        crash(tmp)
        check("alice deposit", Client("alice").do("DEPOSIT 5")[-1], "OK deposited")
        check("balance is old + 5", Client("alice").do("BALANCE")[-1], "OK balance 880.00")
    finally:
        server.terminate()
        server.wait()
        shutil.rmtree(tmp)
    print("FAILED" if failures else "ALL PASSED")
    sys.exit(1 if failures else 0)


main()
