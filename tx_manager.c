/* tx_manager.c - transactions with a before-image journal, plus crash healing.
 *
 * How a transaction works:
 *   tx_begin            enter the gate (shared lock on journal.bin)
 *   tx_lock_*           take record / table locks (lock order is the caller's job)
 *   tx_write_record     journal a before-image, then change the record in place
 *   tx_append_record    journal the old file size, then append
 *   tx_commit           journal COMMIT, release every lock, leave the gate
 * The BEGIN entry is written lazily, so read-only commands never touch the journal.
 *
 * A live transaction keeps its locks until commit. So if I have just got a lock and
 * the journal still shows an unfinished transaction on that record, its process is
 * dead (the OS freed its locks). Healing rolls back that WHOLE transaction. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <unistd.h>
#include "config.h"
#include "lock_manager.h"
#include "record_file.h"
#include "records.h"
#include "tx_manager.h"

#define MAX_HELD 1024

typedef struct { int file; int id; int mode; } Held;      /* id 0 = the table lock */

static struct {
    long tx_id;
    int  began;                  /* BEGIN is in the journal */
    int  nheld;
    Held held[MAX_HELD];
} tx;

/* ------------------------------------------------------------------ journal */

static int journal_write(long tx_id, int phase, int file, int id, long old_size, const void *image)
{
    JournalEntry e;
    memset(&e, 0, sizeof e);
    e.tx_id = tx_id;
    e.phase = phase;
    e.pid = getpid();
    if (file >= 0) strncpy(e.file, rf_name(file), sizeof e.file - 1);
    e.record_id = id;
    e.old_size = old_size;
    if (image) memcpy(e.before_image, image, rf_record_size(file));
    return write(rf_fd(FILE_JOURNAL), &e, sizeof e) == (ssize_t)sizeof e ? 0 : -1;
}

/* read the whole journal into memory; caller frees. Returns the entry count. */
static int journal_load(JournalEntry **out)
{
    int n = rf_count(FILE_JOURNAL);
    JournalEntry *j = malloc((n ? n : 1) * sizeof *j);
    if (!j) { *out = NULL; return 0; }
    n = (int)(pread(rf_fd(FILE_JOURNAL), j, (size_t)n * sizeof *j, 0) / sizeof *j);
    if (n < 0) n = 0;
    *out = j;
    return n;
}

static int has_entries(const JournalEntry *j, int n, long tx_id)
{
    for (int i = 0; i < n; i++)
        if (j[i].tx_id == tx_id) return 1;
    return 0;
}

static int is_finished(const JournalEntry *j, int n, long tx_id)
{
    for (int i = 0; i < n; i++)
        if (j[i].tx_id == tx_id && (j[i].phase == PH_COMMIT || j[i].phase == PH_ROLLBACK)) return 1;
    return 0;
}

/* Put every record and file back the way the journal says it was, newest entry first. */
static void undo_tx(long tx_id)
{
    JournalEntry *j;
    int n = journal_load(&j);
    for (int i = n - 1; i >= 0; i--) {
        if (j[i].tx_id != tx_id) continue;
        int file = rf_file_by_name(j[i].file);
        if (file < 0) continue;
        if (j[i].phase == PH_IMAGE)
            rf_write(file, j[i].record_id, j[i].before_image);
        else if (j[i].phase == PH_APPEND) {
            int r = ftruncate(rf_fd(file), j[i].old_size);      /* cut the appended record(s) off again */
            (void)r;
        }
    }
    free(j);
}

/* ------------------------------------------------------------------ held locks */

static Held *find_held(int file, int id)
{
    for (int i = 0; i < tx.nheld; i++)
        if (tx.held[i].file == file && tx.held[i].id == id) return &tx.held[i];
    return NULL;
}

static void release_all_locks(void)
{
    for (int i = tx.nheld - 1; i >= 0; i--) {
        int fd = rf_fd(tx.held[i].file);
        if (tx.held[i].id == 0) unlock_table(fd);
        else                    unlock_record(fd, tx.held[i].id);
    }
    tx.nheld = 0;
}

/* ------------------------------------------------------------------ healing */

/* Is there an unfinished transaction of another process that touched this record
 * (id > 0) or appended to this file (id == 0)? Returns its tx_id or 0. */
static long find_open_tx(int file, int id)
{
    JournalEntry *j;
    int n = journal_load(&j);
    long found = 0;
    for (int i = 0; i < n && !found; i++) {
        if (j[i].tx_id == tx.tx_id || strcmp(j[i].file, rf_name(file)) != 0) continue;
        int match = (id > 0 && j[i].phase == PH_IMAGE && j[i].record_id == id) ||
                    (id == 0 && j[i].phase == PH_APPEND);
        if (match && !is_finished(j, n, j[i].tx_id)) found = j[i].tx_id;
    }
    free(j);
    return found;
}

typedef struct { int file; int id; } Item;

static int by_lock_order(const void *a, const void *b)
{
    const Item *x = a, *y = b;
    return x->file != y->file ? x->file - y->file : x->id - y->id;
}

/* Roll back one whole unfinished transaction. Lock everything it touched in the
 * global order first; if it is still unfinished once we hold those locks, its
 * owner is dead (or it never will touch them again), so undo it. */
static int heal_transaction(long target)
{
    JournalEntry *j;
    int n = journal_load(&j);
    Item *items = malloc((n ? n : 1) * sizeof *items);
    char *taken = calloc(n ? n : 1, 1);
    int ni = 0, rc = 0;
    if (!items || !taken) { free(j); free(items); free(taken); return -1; }

    for (int i = 0; i < n; i++) {
        if (j[i].tx_id != target) continue;
        int file = rf_file_by_name(j[i].file);
        if (file < 0 || (j[i].phase != PH_IMAGE && j[i].phase != PH_APPEND)) continue;
        Item it = { file, j[i].phase == PH_IMAGE ? j[i].record_id : 0 };
        int dup = 0;
        for (int k = 0; k < ni; k++)
            if (items[k].file == it.file && items[k].id == it.id) dup = 1;
        if (!dup) items[ni++] = it;
    }
    free(j);
    qsort(items, ni, sizeof *items, by_lock_order);

    for (int k = 0; k < ni && rc == 0; k++) {
        if (find_held(items[k].file, items[k].id)) continue;      /* my own transaction already has it */
        int fd = rf_fd(items[k].file);
        if ((items[k].id == 0 ? lock_table(fd) : lock_record(fd, items[k].id, LOCK_WRITE)) < 0) rc = -1;
        else taken[k] = 1;
    }
    if (rc == 0) {
        n = journal_load(&j);
        if (has_entries(j, n, target) && !is_finished(j, n, target)) {
            undo_tx(target);
            rc = journal_write(target, PH_ROLLBACK, -1, 0, 0, NULL);
        }
        free(j);
    }
    for (int k = ni - 1; k >= 0; k--) {
        if (!taken[k]) continue;
        int fd = rf_fd(items[k].file);
        if (items[k].id == 0) unlock_table(fd);
        else                  unlock_record(fd, items[k].id);
    }
    free(items);
    free(taken);
    return rc;
}

/* Right after taking a lock: 0 = nothing to heal, 1 = a dead transaction was healed
 * (our lock was dropped, caller must lock again), -1 = error (lock dropped). */
static int heal_orphans(int file, int id)
{
    long dead = find_open_tx(file, id);
    if (!dead) return 0;
    if (id == 0) unlock_table(rf_fd(file));
    else         unlock_record(rf_fd(file), id);
    return heal_transaction(dead) < 0 ? -1 : 1;
}

/* the unfinished transactions (other than mine) that touched this file; file may be NULL = all */
static int collect_open(const JournalEntry *j, int n, const char *file, long *out)
{
    int cnt = 0;
    for (int i = 0; i < n; i++) {
        if (j[i].tx_id == tx.tx_id) continue;
        if (file && strcmp(j[i].file, file) != 0) continue;
        if (is_finished(j, n, j[i].tx_id)) continue;
        int dup = 0;
        for (int k = 0; k < cnt; k++)
            if (out[k] == j[i].tx_id) dup = 1;
        if (!dup) out[cnt++] = j[i].tx_id;
    }
    return cnt;
}

static int heal_matching(const char *file)
{
    JournalEntry *j;
    int n = journal_load(&j);
    long *open_ids = malloc((n ? n : 1) * sizeof *open_ids);
    int rc = 0;
    if (!open_ids) { free(j); return -1; }
    int cnt = collect_open(j, n, file, open_ids);
    free(j);
    for (int k = 0; k < cnt; k++)
        if (heal_transaction(open_ids[k]) < 0) rc = -1;
    free(open_ids);
    return rc;
}

/* A live transaction holds its locks, so healing it just waits until it commits and
 * then finds nothing to do. A dead one gets rolled back. */
int heal_before_scan(int file) { return heal_matching(rf_name(file)); }

int recover_all(void) { return heal_matching(NULL); }

/* ------------------------------------------------------------------ transaction calls */

static long new_tx_id(void)
{
    long id = 0;
    while (id == 0) {
        if (getrandom(&id, sizeof id, 0) != (ssize_t)sizeof id) id = (long)getpid() * 1000003L;
        id &= 0x7fffffffffffffffL;
    }
    return id;
}

int tx_begin(Session *s)
{
    (void)s;
    gate_enter();                    /* shared; held until commit/rollback */
    tx.tx_id = new_tx_id();
    tx.began = 0;
    tx.nheld = 0;
    return 0;
}

int tx_lock_record(int file, int id, int mode)
{
    Held *h = find_held(file, id);
    if (h) {                                          /* already mine; maybe upgrade read -> write */
        if (mode == LOCK_WRITE && h->mode == LOCK_READ) {
            if (lock_record(rf_fd(file), id, LOCK_WRITE) < 0) return -1;
            h->mode = LOCK_WRITE;
        }
        return 0;
    }
    if (tx.nheld >= MAX_HELD) return -1;
    for (;;) {
        if (lock_record(rf_fd(file), id, mode) < 0) return -1;
        int r = heal_orphans(file, id);
        if (r == 0) break;
        if (r < 0) return -1;
    }
    tx.held[tx.nheld++] = (Held){ file, id, mode };
    return 0;
}

int tx_lock_table(int file)
{
    if (find_held(file, 0)) return 0;
    if (tx.nheld >= MAX_HELD) return -1;
    for (;;) {
        if (lock_table(rf_fd(file)) < 0) return -1;
        int r = heal_orphans(file, 0);                /* heal the file BEFORE anyone appends to it */
        if (r == 0) break;
        if (r < 0) return -1;
    }
    tx.held[tx.nheld++] = (Held){ file, 0, LOCK_WRITE };
    return 0;
}

static int ensure_begun(void)
{
    if (tx.began) return 0;
    if (journal_write(tx.tx_id, PH_BEGIN, -1, 0, 0, NULL) < 0) return -1;
    tx.began = 1;
    return 0;
}

int tx_log_before_image(int file, int id)
{
    char image[128];
    if (ensure_begun() < 0 || rf_read(file, id, image) < 0) return -1;
    return journal_write(tx.tx_id, PH_IMAGE, file, id, 0, image);
}

int tx_log_append(int file)
{
    if (ensure_begun() < 0) return -1;
    return journal_write(tx.tx_id, PH_APPEND, file, 0, rf_size(file), NULL);
}

int tx_write_record(int file, int id, const void *rec)
{
    Held *h = find_held(file, id);
    if (!h || h->mode != LOCK_WRITE) return -1;       /* must hold the write lock */
    if (tx_log_before_image(file, id) < 0) return -1;
    return rf_write(file, id, rec);
}

int tx_append_record(int file, const void *rec)
{
    if (!find_held(file, 0)) return -1;               /* must hold the table lock */
    if (tx_log_append(file) < 0) return -1;
    return rf_append(file, rec);
}

/* After a commit the journal may have grown. If nobody has a transaction open,
 * finish off any dead ones and start the journal again from empty. */
static void maybe_compact_journal(void)
{
    if (rf_size(FILE_JOURNAL) <= JOURNAL_COMPACT_BYTES) return;
    if (gate_try_exclusive() < 0) return;             /* somebody is mid-transaction: try later */
    recover_all();
    if (ftruncate(rf_fd(FILE_JOURNAL), 0) != 0) { /* leave it; we will try again */ }
    gate_release_exclusive();
}

static void tx_end(void)
{
    release_all_locks();
    gate_leave();
    if (tx.began) { tx.began = 0; maybe_compact_journal(); }
}

int tx_commit(void)
{
    if (tx.began) journal_write(tx.tx_id, PH_COMMIT, -1, 0, 0, NULL);
    tx_end();
    return 0;
}

int tx_rollback(void)
{
    if (tx.began) {
        undo_tx(tx.tx_id);
        journal_write(tx.tx_id, PH_ROLLBACK, -1, 0, 0, NULL);
    }
    tx_end();
    return 0;
}

int tx_finish(int rc)
{
    if (rc < 0) tx_rollback();
    else        tx_commit();
    return rc;
}
