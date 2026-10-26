/* menus.h - one dispatch function per role. Each turns a command line into service calls. */
#ifndef MENUS_H
#define MENUS_H
#include "session.h"

/* return 0 to keep the session going, -1 to end it */
int customer_dispatch(int fd, Session *s, const char *cmd, char *args);
int employee_dispatch(int fd, Session *s, const char *cmd, char *args);
int manager_dispatch(int fd, Session *s, const char *cmd, char *args);
int admin_dispatch(int fd, Session *s, const char *cmd, char *args);

#endif
