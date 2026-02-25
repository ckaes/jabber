#ifndef XMPPD_USER_H
#define XMPPD_USER_H

#include <stddef.h>

int  user_exists(const char *username);
int  user_check_password(const char *username, const char *password);
void user_get_datapath(const char *username, char *path, size_t pathsize);

/* Returns 0 on success, -1 conflict, -2 invalid username, -3 I/O error */
int  user_create(const char *username, const char *password);
/* Returns 0 on success, -1 on I/O error */
int  user_change_password(const char *username, const char *password);
/* Returns 0 on success */
int  user_delete(const char *username);

#endif
