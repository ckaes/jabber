#ifndef XMPPD_USER_H
#define XMPPD_USER_H

#include <stddef.h>

int  user_exists(const char *username);
int  user_check_password(const char *username, const char *password);
void user_get_datapath(const char *username, char *path, size_t pathsize);

#endif
