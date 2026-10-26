/* errors.h - every service returns >= 0 on success or one of these (negative) codes */
#ifndef ERRORS_H
#define ERRORS_H

enum {
    E_FAIL = -1,       E_BUSY = -2,        E_NOT_FOUND = -3,   E_EXISTS = -4,
    E_BAD_ARG = -5,    E_BAD_AMOUNT = -6,  E_OVERFLOW = -7,    E_FUNDS = -8,
    E_BALANCE = -9,    E_CLOSED = -10,     E_INACTIVE = -11,   E_NOT_HOLDER = -12,
    E_SAME_ACCOUNT = -13, E_LAST_ADMIN = -14, E_NOT_CUSTOMER = -15, E_BAD_ROLE = -16,
    E_ONE_ACCOUNT = -17,  E_LOAN_CHANGED = -18, E_BAD_TRANSITION = -19, E_NOT_EMPLOYEE = -20,
    E_BAD_PASSWORD = -21, E_BAD_LOGIN = -22,    E_DEACTIVATED = -23,    E_NO_ACCOUNT = -24,
    E_BAD_FIELD = -25
};

static inline const char *err_text(int code)
{
    switch (code) {
    case E_BUSY:           return "busy, try again";
    case E_NOT_FOUND:      return "not found";
    case E_EXISTS:         return "username already exists";
    case E_BAD_ARG:        return "bad argument";
    case E_BAD_AMOUNT:     return "amount must be positive";
    case E_OVERFLOW:       return "balance would overflow";
    case E_FUNDS:          return "insufficient funds";
    case E_BALANCE:        return "balance must be zero";
    case E_CLOSED:         return "account closed";
    case E_INACTIVE:       return "account inactive";
    case E_NOT_HOLDER:     return "not an account holder";
    case E_SAME_ACCOUNT:   return "cannot transfer to your own account";
    case E_LAST_ADMIN:     return "cannot remove the last active admin";
    case E_NOT_CUSTOMER:   return "not a customer";
    case E_BAD_ROLE:       return "bad role";
    case E_ONE_ACCOUNT:    return "user already holds an open account";
    case E_LOAN_CHANGED:   return "loan changed";
    case E_BAD_TRANSITION: return "loan status change not allowed";
    case E_NOT_EMPLOYEE:   return "not an active employee";
    case E_BAD_PASSWORD:   return "wrong password";
    case E_BAD_LOGIN:      return "invalid username or password";
    case E_DEACTIVATED:    return "account deactivated";
    case E_NO_ACCOUNT:     return "no account";
    case E_BAD_FIELD:      return "field not allowed";
    default:               return "failed";
    }
}

#endif
