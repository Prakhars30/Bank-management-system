/* seed_data.c - first run only: when users.bin is empty, create demo users and a little history.
 * It goes through the normal services, so the data is exactly what real commands would make. */
#include <stdio.h>
#include <string.h>
#include "account_service.h"
#include "errors.h"
#include "feedback_service.h"
#include "loan_service.h"
#include "record_file.h"
#include "records.h"
#include "seed_data.h"
#include "user_service.h"

static void check(const char *what, int rc)
{
    if (rc < 0) fprintf(stderr, "seed: %s failed (%s)\n", what, err_text(rc));
}

int seed_if_missing(void)
{
    if (rf_count(FILE_USERS) > 0) return 0;

    Session admin;
    memset(&admin, 0, sizeof admin);
    admin.logged_in = 1;
    admin.role = ROLE_ADMIN;

    check("admin",     add_user(&admin, "admin",     "admin123", ROLE_ADMIN));
    check("admin2",    add_user(&admin, "admin2",    "admin123", ROLE_ADMIN));
    check("manager1",  add_user(&admin, "manager1",  "pass123",  ROLE_MANAGER));
    check("employee1", add_user(&admin, "employee1", "pass123",  ROLE_EMPLOYEE));
    check("employee2", add_user(&admin, "employee2", "pass123",  ROLE_EMPLOYEE));

    int alice = add_customer(&admin, "alice", "pass123");          /* one account each */
    int bob   = add_customer(&admin, "bob",   "pass123");
    int carol = add_user(&admin, "carol", "pass123", ROLE_CUSTOMER);   /* carol and dave share one joint account */
    int dave  = add_user(&admin, "dave",  "pass123", ROLE_CUSTOMER);
    check("alice", alice); check("bob", bob); check("carol", carol); check("dave", dave);
    check("joint account", add_account(&admin, carol, dave));

    Session sa = { .logged_in = 1, .user_id = alice, .role = ROLE_CUSTOMER, .account_id = find_any_account(alice) };
    Session sb = { .logged_in = 1, .user_id = bob,   .role = ROLE_CUSTOMER, .account_id = find_any_account(bob) };
    Session sc = { .logged_in = 1, .user_id = carol, .role = ROLE_CUSTOMER, .account_id = find_any_account(carol) };

    check("alice deposit",  deposit(&sa, 100000));                 /* 1000.00 */
    check("alice withdraw", withdraw(&sa, 2500));                  /*   25.00 */
    check("bob deposit",    deposit(&sb, 50000));                  /*  500.00 */
    check("transfer",       transfer(&sa, sb.account_id, 10000));  /*  100.00 alice -> bob */
    check("carol deposit",  deposit(&sc, 25000));                  /*  250.00 into the joint account */
    check("loan",           apply(&sa, 500000));                   /* 5000.00, stays PENDING */
    check("feedback",       add(&sb, "Great service, thank you!"));
    return 0;
}
