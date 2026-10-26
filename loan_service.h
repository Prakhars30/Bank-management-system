#ifndef LOAN_SERVICE_H
#define LOAN_SERVICE_H
#include "session.h"
#include "util.h"

int apply(Session *s, long cents);                                  /* customer */
int view_assigned(Session *s, Out *out);                            /* employee */
int process(Session *s, int loan_id, const char *decision);         /* employee: APPROVE or REJECT */
int view_pending(Out *out);                                         /* manager */
int assign(Session *s, int loan_id, int employee_id);               /* manager */

#endif
