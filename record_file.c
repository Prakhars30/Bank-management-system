/* record_file.c - open the data files once and read/write fixed-size records */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "config.h"
#include "record_file.h"
#include "records.h"

static const char *NAMES[NUM_FILES] = {
    "users.bin", "accounts.bin", "loans.bin", "transactions.bin", "feedback.bin", "journal.bin"
};
static const int SIZES[NUM_FILES] = {
    sizeof(UserRecord), sizeof(AccountRecord), sizeof(LoanRecord),
    sizeof(TxnRecord), sizeof(FeedbackRecord), sizeof(JournalEntry)
};
static int fds[NUM_FILES] = { -1, -1, -1, -1, -1, -1 };

/* Open each file exactly once per process: closing ANY descriptor of a file
 * would silently drop all of this process's fcntl locks on it. */
int rf_open_all(void)
{
    mkdir(DATA_DIR, 0755);
    for (int f = 0; f < NUM_FILES; f++) {
        char path[128];
        snprintf(path, sizeof path, "%s/%s", DATA_DIR, NAMES[f]);
        int flags = O_RDWR | O_CREAT;
        if (f == FILE_JOURNAL) flags |= O_APPEND;     /* journal entries are only ever appended */
        fds[f] = open(path, flags, 0644);
        if (fds[f] < 0) { perror(path); return -1; }
    }
    return 0;
}

int         rf_fd(int file)          { return fds[file]; }
const char *rf_name(int file)        { return NAMES[file]; }
int         rf_record_size(int file) { return SIZES[file]; }

int rf_file_by_name(const char *name)
{
    for (int f = 0; f < NUM_FILES; f++)
        if (strcmp(NAMES[f], name) == 0) return f;
    return -1;
}

long rf_size(int file)
{
    struct stat st;
    if (fstat(fds[file], &st) < 0) return 0;
    return st.st_size;
}

int rf_count(int file) { return (int)(rf_size(file) / SIZES[file]); }

int rf_read(int file, int id, void *buf)
{
    if (id < 1) return -1;
    off_t off = (off_t)(id - 1) * SIZES[file];
    return pread(fds[file], buf, SIZES[file], off) == SIZES[file] ? 0 : -1;
}

int rf_write(int file, int id, const void *buf)
{
    if (id < 1) return -1;
    off_t off = (off_t)(id - 1) * SIZES[file];
    return pwrite(fds[file], buf, SIZES[file], off) == SIZES[file] ? 0 : -1;
}

int rf_append(int file, const void *buf)
{
    int id = rf_count(file) + 1;
    return rf_write(file, id, buf) == 0 ? id : -1;
}
