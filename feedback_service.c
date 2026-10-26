/* feedback_service.c - feedback.bin is append-only */
#include <string.h>
#include <time.h>
#include "errors.h"
#include "feedback_service.h"
#include "record_file.h"
#include "records.h"
#include "tx_manager.h"

static int add_body(Session *s, const char *text)
{
    FeedbackRecord f;
    if (tx_lock_table(FILE_FEEDBACK) < 0) return E_BUSY;          /* the append lock */
    memset(&f, 0, sizeof f);
    f.feedback_id = rf_count(FILE_FEEDBACK) + 1;
    f.user_id = s->user_id;
    strncpy(f.message, text, sizeof f.message - 1);
    f.timestamp = time(NULL);
    return tx_append_record(FILE_FEEDBACK, &f) < 0 ? E_FAIL : f.feedback_id;
}

int add(Session *s, const char *text)
{
    size_t len = strlen(text);
    if (len < 1 || len > 255) return E_BAD_ARG;
    tx_begin(s);
    return tx_finish(add_body(s, text));
}

int review_all(Out *out)                                 /* newest first */
{
    int rows = 0;
    tx_begin(NULL);
    heal_before_scan(FILE_FEEDBACK);
    for (int id = rf_count(FILE_FEEDBACK); id >= 1; id--) {
        FeedbackRecord f;
        char when[32];
        if (rf_read(FILE_FEEDBACK, id, &f) < 0) continue;
        fmt_time(f.timestamp, when);
        out_add(out, "| #%d user=%d %s: %s\n", f.feedback_id, f.user_id, when, f.message);
        rows++;
    }
    return tx_finish(rows);
}
