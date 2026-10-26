/* lock_manager.c - fcntl record locks, the journal gate and the per-user session lock */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include "config.h"
#include "lock_manager.h"
#include "record_file.h"
#include "records.h"

#define TABLE_OFFSET ((off_t)1 << 40)          /* far past any real file end */
#define GATE_OFFSET  (((off_t)1 << 40) + 1)

/* one lock/unlock of a single byte; retries when a signal interrupts it */
static int byte_lock(int fd, int type, off_t offset, int wait)
{
    struct flock fl;
    fl.l_type = type;
    fl.l_whence = SEEK_SET;
    fl.l_start = offset;
    fl.l_len = 1;
    while (fcntl(fd, wait ? F_SETLKW : F_SETLK, &fl) < 0) {
        if (errno == EINTR) continue;
        return -1;                              /* EDEADLK, EAGAIN, ... */
    }
    return 0;
}

int lock_record(int fd, int id, int mode)
{
    return byte_lock(fd, mode == LOCK_READ ? F_RDLCK : F_WRLCK, id, 1);
}
int unlock_record(int fd, int id) { return byte_lock(fd, F_UNLCK, id, 0); }
int lock_table(int fd)            { return byte_lock(fd, F_WRLCK, TABLE_OFFSET, 1); }
int unlock_table(int fd)          { return byte_lock(fd, F_UNLCK, TABLE_OFFSET, 0); }

/* ---- the gate: a shared lock on one byte of journal.bin ---- */

int gate_enter(void)             { return byte_lock(rf_fd(FILE_JOURNAL), F_RDLCK, GATE_OFFSET, 1); }
int gate_leave(void)             { return byte_lock(rf_fd(FILE_JOURNAL), F_UNLCK, GATE_OFFSET, 0); }
int gate_try_exclusive(void)     { return byte_lock(rf_fd(FILE_JOURNAL), F_WRLCK, GATE_OFFSET, 0); }
int gate_release_exclusive(void) { return byte_lock(rf_fd(FILE_JOURNAL), F_UNLCK, GATE_OFFSET, 0); }

/* ---- session lock: byte N of sessions_lock belongs to user N.
 * The OS drops it when the process dies, so a crash can never leave a user locked out. ---- */

static int sessions_fd = -1;

int session_acquire(int user_id)
{
    if (sessions_fd < 0) {
        char path[128];
        snprintf(path, sizeof path, "%s/sessions_lock", DATA_DIR);
        sessions_fd = open(path, O_RDWR | O_CREAT, 0644);
        if (sessions_fd < 0) return -1;
    }
    return byte_lock(sessions_fd, F_WRLCK, user_id, 0);
}

void session_release(int user_id)
{
    if (sessions_fd >= 0) byte_lock(sessions_fd, F_UNLCK, user_id, 0);
}
