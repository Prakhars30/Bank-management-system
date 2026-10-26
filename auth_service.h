#ifndef AUTH_SERVICE_H
#define AUTH_SERVICE_H

int  auth_login(const char *username, const char *password, int *out_user);   /* 0 or E_* */
void auth_hash_password(const char *password, const char *salt, char out_hash[65]);
void auth_new_salt(char salt[17]);

#endif
