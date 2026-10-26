/* session.h - one Session per client connection (one process per client) */
#ifndef SESSION_H
#define SESSION_H

typedef struct {
    int  logged_in;
    int  user_id;
    int  role;                 /* copied from users.bin, never from the client */
    int  account_id;           /* customers only; picked at login */
    char username[32];
    int  failed_attempts;
    int  holds_session_lock;
    int  timed_out;            /* set when the read timeout fired */
} Session;

#endif
