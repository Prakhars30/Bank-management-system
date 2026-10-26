/* lock_manager.h - fcntl byte-range locks.
 * Record N of a file is locked by locking byte N. The table lock is one byte
 * far past the end of the file, so it never moves as the file grows. */
#ifndef LOCK_MANAGER_H
#define LOCK_MANAGER_H

#define LOCK_READ  1
#define LOCK_WRITE 2

int  session_acquire(int user_id);      /* 0 ok, -1 if that user is already logged in */
void session_release(int user_id);

int  lock_record(int fd, int id, int mode);     /* blocks; -1 on error (deadlock too) */
int  unlock_record(int fd, int id);
int  lock_table(int fd);
int  unlock_table(int fd);

int  gate_enter(void);                  /* every transaction holds this shared  */
int  gate_leave(void);
int  gate_try_exclusive(void);          /* 0 only if nobody has a transaction open */
int  gate_release_exclusive(void);

#endif
