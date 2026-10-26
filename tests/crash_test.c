/* crash_test.c - leave an unfinished transaction behind, exactly like a process killed mid-transfer:
 * account 1 changed in place, a transaction record appended, no COMMIT. Run in the server's directory. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../lock_manager.h"
#include "../record_file.h"
#include "../records.h"
#include "../tx_manager.h"

int main(void)
{
    AccountRecord a;
    TxnRecord t;
    Session s;
    memset(&s, 0, sizeof s);
    if (rf_open_all() < 0) return 1;

    tx_begin(&s);
    tx_lock_record(FILE_ACCOUNTS, 1, LOCK_WRITE);
    rf_read(FILE_ACCOUNTS, 1, &a);
    a.balance_cents += 99999900;
    tx_write_record(FILE_ACCOUNTS, 1, &a);
    tx_lock_table(FILE_TXNS);
    memset(&t, 0, sizeof t);
    t.txn_id = rf_count(FILE_TXNS) + 1;
    t.account_id = 1;
    snprintf(t.type, sizeof t.type, "GHOST");
    t.amount_cents = 99999900;
    t.balance_after_cents = a.balance_cents;
    tx_append_record(FILE_TXNS, &t);
    printf("left an unfinished transaction behind\n");
    fflush(stdout);
    _exit(0);                                   /* no commit, no rollback: the OS frees the locks */
}
