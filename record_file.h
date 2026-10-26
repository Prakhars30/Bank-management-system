/* record_file.h - fixed-size records in a file: record N lives at offset (N-1)*size */
#ifndef RECORD_FILE_H
#define RECORD_FILE_H

int         rf_open_all(void);              /* open every data file ONCE per process */
int         rf_fd(int file);
const char *rf_name(int file);
int         rf_record_size(int file);
int         rf_file_by_name(const char *name);   /* -1 if unknown */
int         rf_count(int file);
long        rf_size(int file);
int         rf_read(int file, int id, void *buf);          /* 0 ok, -1 error */
int         rf_write(int file, int id, const void *buf);
int         rf_append(int file, const void *buf);          /* returns the new id */

#endif
