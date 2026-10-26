#ifndef FEEDBACK_SERVICE_H
#define FEEDBACK_SERVICE_H
#include "session.h"
#include "util.h"

int add(Session *s, const char *text);       /* 1..255 characters */
int review_all(Out *out);                    /* newest first */

#endif
