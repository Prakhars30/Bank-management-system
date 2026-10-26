#!/usr/bin/env python3
"""Stress test: concurrent transfers, then kill -9 the whole server in the middle of them,
restart it, and check that crash recovery left the books consistent."""
import os, random, re, shutil, signal, socket, subprocess, sys, tempfile, threading, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = 5078


class Client:
    def __init__(self, user, pw="pass123"):
        self.sock = socket.create_connection(("127.0.0.1", PORT), timeout=30)
        self.f = self.sock.makefile("rw", newline="\n")
        self.read()
        assert self.do(f"LOGIN {user} {pw}")[-1].startswith("OK")

    def read(self):
        rows = []
        while True:
            line = self.f.readline()
            if not line:
                raise ConnectionError
            rows.append(line.rstrip())
            if line.startswith("OK") or line.startswith("ERR"):
                return rows

    def do(self, line):
        self.f.write(line + "\n")
        self.f.flush()
        return self.read()


def start(tmp):
    p = subprocess.Popen([os.path.join(ROOT, "server"), str(PORT)], cwd=tmp, start_new_session=True,
                         stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    time.sleep(0.8)
    return p


def balances():
    ad = Client("admin", "admin123")
    em = Client("employee1")
    total, bad = 0, []
    for acct in (1, 2, 3):
        row = ad.do(f"SEARCHACCOUNT {acct}")[0]
        bal = round(float(re.search(r"balance=([\d.]+)", row).group(1)) * 100)
        total += bal
        last = [r for r in em.do(f"VIEWTXNS {acct}") if r.startswith("|")][-1]
        after = round(float(re.search(r"balance_after=([\d.]+)", last).group(1)) * 100)
        if after != bal:
            bad.append((acct, bal, after))
    return total, bad


def worker(users, stop, counts):
    try:
        c = Client(users[0])
        mine = users[1]
        while not stop.is_set():
            to = random.choice([a for a in (1, 2, 3) if a != mine])
            r = c.do(f"TRANSFER {to} {random.randint(1, 500) / 100:.2f}")[-1]
            counts[r.split()[0]] = counts.get(r.split()[0], 0) + 1
    except Exception:
        pass


def main():
    tmp = tempfile.mkdtemp()
    server = start(tmp)
    ok = True
    try:
        before, bad = balances()
        print("total before:", before / 100, "consistent:", not bad)

        print("-- round 1: 6 clients hammer transfers for 4 s (no crash)")
        stop, counts = threading.Event(), {}
        ts = [threading.Thread(target=worker, args=(u, stop, counts))
              for u in (("alice", 1), ("bob", 2), ("carol", 3), ("dave", 3), ("alice", 1), ("bob", 2)) if True]
        # a second login of the same user is refused, so only the first of each is used
        [t.start() for t in ts]
        time.sleep(4)
        stop.set()
        [t.join() for t in ts]
        after, bad = balances()
        print("   results:", counts, "total after:", after / 100, "inconsistent:", bad)
        ok &= (after == before and not bad)

        print("-- round 2: kill -9 the whole server in the middle of transfers, x3")
        for i in range(3):
            stop, counts = threading.Event(), {}
            ts = [threading.Thread(target=worker, args=(u, stop, counts))
                  for u in (("alice", 1), ("bob", 2), ("carol", 3), ("dave", 3))]
            [t.start() for t in ts]
            time.sleep(random.uniform(0.5, 1.5))
            os.killpg(server.pid, signal.SIGKILL)
            server.wait()
            stop.set()
            [t.join() for t in ts]
            server = start(tmp)                                  # recover_all runs before listening
            after, bad = balances()
            print(f"   crash {i + 1}: transfers done before kill {counts}, total {after / 100}, inconsistent: {bad}")
            ok &= (after == before and not bad)
        journal = os.path.getsize(os.path.join(tmp, "data", "journal.bin"))
        print("journal size:", journal, "bytes")
    finally:
        try:
            os.killpg(server.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        shutil.rmtree(tmp)
    print("PASSED" if ok else "FAILED")
    sys.exit(0 if ok else 1)


main()
