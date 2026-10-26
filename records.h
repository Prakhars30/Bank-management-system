/* records.h - the six on-disk record structs and their constants */
#ifndef RECORDS_H
#define RECORDS_H

enum { ROLE_CUSTOMER = 1, ROLE_EMPLOYEE = 2, ROLE_MANAGER = 3, ROLE_ADMIN = 4 };
enum { ACCT_ACTIVE = 1, ACCT_INACTIVE = 2, ACCT_CLOSED = 3 };      /* closed is final */

#define LOAN_PENDING  "PENDING"
#define LOAN_ASSIGNED "ASSIGNED"
#define LOAN_APPROVED "APPROVED"
#define LOAN_REJECTED "REJECTED"

/* The data files. The order is also the global lock order (see server_side note). */
enum { FILE_USERS, FILE_ACCOUNTS, FILE_LOANS, FILE_TXNS, FILE_FEEDBACK, FILE_JOURNAL, NUM_FILES };

/* Journal phases */
enum { PH_BEGIN = 1, PH_IMAGE, PH_APPEND, PH_COMMIT, PH_ROLLBACK };

/* ids are record numbers: 1, 2, 3 ... and are never reused */

typedef struct {                       /* users.bin */
    int  user_id;
    char username[32];
    char salt[17];
    char password_hash[65];
    int  role;                         /* ROLE_*  */
    int  is_active;                    /* 0 means soft-deleted */
} UserRecord;

typedef struct {                       /* accounts.bin */
    int  account_id;
    int  holder1_id;                   /* never 0 on an open account */
    int  holder2_id;                   /* 0 means no second holder   */
    long balance_cents;
    int  status;                       /* ACCT_* */
} AccountRecord;

typedef struct {                       /* loans.bin */
    int  loan_id;
    int  account_id;
    long amount_cents;
    char status[12];                   /* LOAN_* */
    int  assigned_employee_id;         /* 0 when PENDING */
} LoanRecord;

typedef struct {                       /* transactions.bin */
    int  txn_id;
    int  account_id;
    char type[16];                     /* DEPOSIT, WITHDRAW, TRANSFER_OUT, TRANSFER_IN, LOAN_CREDIT */
    long amount_cents;
    long balance_after_cents;
    long timestamp;
} TxnRecord;

typedef struct {                       /* feedback.bin */
    int  feedback_id;
    int  user_id;
    char message[256];
    long timestamp;
} FeedbackRecord;

typedef struct {                       /* journal.bin */
    long tx_id;
    int  phase;                        /* PH_* */
    int  pid;
    char file[32];                     /* "users.bin", ... */
    int  record_id;
    long old_size;                     /* APPEND: file size before the append */
    char before_image[128];            /* IMAGE: copy of one record */
} JournalEntry;

/* before_image holds a whole record, so the in-place records must fit in it */
_Static_assert(sizeof(UserRecord)    <= 128, "UserRecord must fit in before_image[128]");
_Static_assert(sizeof(AccountRecord) <= 128, "AccountRecord must fit in before_image[128]");
_Static_assert(sizeof(LoanRecord)    <= 128, "LoanRecord must fit in before_image[128]");

#endif
