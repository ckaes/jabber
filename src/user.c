#include "user.h"
#include "config.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>

void user_get_datapath(const char *username, char *path, size_t pathsize) {
    snprintf(path, pathsize, "%s/%s", g_config.datadir, username);
}

int user_exists(const char *username) {
    char path[1280];
    snprintf(path, sizeof(path), "%s/%s/user.conf", g_config.datadir, username);

    struct stat st;
    return stat(path, &st) == 0;
}

int user_check_password(const char *username, const char *password) {
    char path[1280];
    snprintf(path, sizeof(path), "%s/%s/user.conf", g_config.datadir, username);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        log_write(LOG_DEBUG, "User file not found: %s", path);
        return 0;
    }

    char line[1024];
    int matched = 0;
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n\r")] = '\0';

        /* Skip comments and blanks */
        char *s = line;
        while (isspace((unsigned char)*s)) s++;
        if (*s == '#' || *s == '\0')
            continue;

        char *eq = strchr(s, '=');
        if (!eq)
            continue;

        *eq = '\0';
        /* Trim key */
        char *key = s;
        char *kend = eq - 1;
        while (kend > key && isspace((unsigned char)*kend)) *kend-- = '\0';

        /* Trim value */
        char *val = eq + 1;
        while (isspace((unsigned char)*val)) val++;
        char *vend = val + strlen(val) - 1;
        while (vend > val && isspace((unsigned char)*vend)) *vend-- = '\0';

        if (strcmp(key, "password") == 0) {
            if (strcmp(val, password) == 0)
                matched = 1;
            break;
        }
    }

    fclose(fp);
    return matched;
}
