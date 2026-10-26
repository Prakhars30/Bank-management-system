/* config.h - tunable numbers in one place */
#ifndef CONFIG_H
#define CONFIG_H

#define DEFAULT_PORT           5050
#define DATA_DIR               "data"

#define LOGIN_TIMEOUT_SECONDS  60      /* wait for the client before login   */
#define IDLE_TIMEOUT_SECONDS   300     /* wait for the client after login    */
#define MAX_LOGIN_ATTEMPTS     3
#define JOURNAL_COMPACT_BYTES  (1024 * 1024)

#define MAX_LINE               1024    /* longest command line               */
#define MAX_ARGS               8

#endif
