/* tx_manager.h - transactions: locks, before-images, journal, healing */
#ifndef TX_MANAGER_H
#define TX_MANAGER_H
#include "session.h"

int tx_begin(Session *s);
int tx_lock_record(int file, int id, int mode);   /* LOCK_READ or LOCK_WRITE */
int tx_lock_table(int file);                      /* the append lock of a file */
int tx_log_before_image(int file, int id);
int tx_log_append(int file);
int tx_commit(void);
int tx_rollback(void);

/* conveniences built from the calls above */
int tx_write_record(int file, int id, const void *rec);   /* image, then write   */
int tx_append_record(int file, const void *rec);          /* APPEND, then append; returns new id */
int tx_finish(int rc);                                    /* rc < 0 rolls back, else commits; returns rc */

int heal_before_scan(int file);                   /* call before reading a whole file */
int recover_all(void);                            /* roll back every unfinished transaction */

#endif
