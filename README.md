# Bank server

A multi-process banking server in C. One process per client, plain text commands over TCP,
records in fixed-size binary files, `fcntl` locks, and a before-image journal so a crash
(even `kill -9`) never leaves half a transaction behind.

## Build and run

    make              # builds ./server and ./client
    ./server [port]   # default port 5050; creates ./data/ and seeds demo data on first run
    ./client [host] [port]

    make reseed       # delete ./data so the next run starts fresh
    make test         # every command of every role + crash-healing tests
    make stress       # concurrent transfers and kill -9 of the whole server

Try it:

    $ ./client
    > LOGIN alice pass123
    > DEPOSIT 12.50
    > BALANCE
    > HISTORY

## Demo users (seeded when users.bin is empty)

| role     | users                          | password   |
|----------|--------------------------------|------------|
| admin    | `admin`, `admin2`              | `admin123` |
| manager  | `manager1`                     | `pass123`  |
| employee | `employee1`, `employee2`       | `pass123`  |
| customer | `alice`, `bob` (one account each), `carol` + `dave` (one joint account) | `pass123` |

There are also a few transactions, one PENDING loan (alice's) and one feedback entry.

## Protocol

The client sends one line `VERB arg arg`. The server answers with zero or more `| row` lines,
then exactly one line starting `OK` or `ERR`. A menu client, web page or GUI only has to turn a
choice into a line like `DEPOSIT 12.50` and show the reply. Amounts are decimal (`12`, `12.5`, `12.50`).

| role     | commands |
|----------|----------|
| any      | `LOGIN user pw`, `LOGOUT`, `QUIT`, `PASSWD old new`, `HELP` |
| customer | `DEPOSIT amt`, `WITHDRAW amt`, `BALANCE`, `TRANSFER to_account amt`, `DETAILS`, `HISTORY`, `LOAN_APPLY amt`, `FEEDBACK text` |
| employee | `ADDCUSTOMER name pw`, `MODIFYCUSTOMER id password\|username value`, `FINDCUSTOMER name`, `VIEWTXNS account_id`, `LOANS_ASSIGNED`, `LOAN_PROCESS loan_id APPROVE\|REJECT` |
| manager  | `ACTIVATE id`, `DEACTIVATE id`, `LOANS_PENDING`, `ASSIGN loan_id employee_id`, `EMPLOYEES`, `FEEDBACK_LIST` |
| admin    | `ADDUSER name pw employee\|manager\|admin`, `DELETEUSER id`, `MODIFYUSER id password\|username\|role value`, `SEARCHUSER name`, `ADDACCOUNT h1 [h2]`, `DELETEACCOUNT id`, `MODIFYACCOUNT id holder1_id\|holder2_id\|status value`, `SEARCHACCOUNT id` |

## Source map (one file per design box)

| design box | file |
|---|---|
| Server, SeedData, ClientHandler | `server.c`, `seed_data.c`, `client_handler.c` |
| AuthService, Session, Config | `auth_service.c`, `session.h`, `config.h` |
| TxManager, RecordFile, LockManager | `tx_manager.c`, `record_file.c`, `lock_manager.c` |
| the 6 record structs | `records.h` |
| Customer / Employee / Manager / Admin menus | `customer_menu.c`, `employee_menu.c`, `manager_menu.c`, `admin_menu.c` |
| the 4 services | `user_service.c`, `account_service.c`, `loan_service.c`, `feedback_service.c` |

Small extras: `util.c` (text buffer, parsing, replies), `errors.h` (error codes and their text),
`client.c` (a line client), `tests/`.

## How the important parts work

* **Lock order** (never take an earlier lock while holding a later one): sessions lock, users table lock,
  user records, accounts table lock, account records, loan records, then the append locks of
  transactions and feedback. Inside a file, lowest id first. Record N is locked by locking byte N;
  a table lock is a byte far past the end of the file.
* **Transactions**: every command calls `tx_begin` ... `tx_finish(rc)` (rolls back if `rc < 0`, commits otherwise).
  `tx_write_record` writes a before-image to `journal.bin` first; `tx_append_record` journals the old
  file size first. Locks are held until commit.
* **Healing**: a live transaction keeps its locks until commit. So if I have just taken a lock and the
  journal still shows an unfinished transaction on that record, its owner died. `tx_lock_record` and
  `tx_lock_table` heal it (the WHOLE transaction, locking everything it touched in global order),
  every scan calls `heal_before_scan(file)`, and `recover_all` runs before the server listens.
* **Gate**: every transaction holds a shared lock on one byte of `journal.bin`. After a commit, if the
  journal is over 1 MB and a non-blocking exclusive lock on that byte succeeds, nobody has a transaction
  open, so dead ones are rolled back and the journal is truncated.
* **Sessions**: one lock byte per user in `sessions_lock` (released by the OS if the process dies).
  Before every command `session_revalidate` re-reads the user record, so a deleted user is cut off and
  a role change applies at once. Account status and membership are checked inside each transaction,
  under the account lock.

## Things to know

* Passwords are salted SHA-256 (fine for a project; use a slow hash such as bcrypt/argon2 for real use).
* A TRANSFER writes two `TxnRecord`s (TRANSFER_OUT, TRANSFER_IN) so both accounts show it in HISTORY.
* `lock_two_ordered` from the LockManager diagram is not a separate function: transfers simply call
  `tx_lock_record` lowest id first, which also does the healing check.
* On start-up `recover_all` runs before the seed, so a crash during seeding is undone and seeded again.
* Changing an employee's role away from employee does not return their ASSIGNED loans to PENDING
  (the design says that only deleting the employee does).
* The journal is only compacted when no transaction is open anywhere, so under constant heavy load it
  keeps growing until there is a quiet moment.
* The journal is not `fsync`ed: it survives a killed process, not a power cut.
