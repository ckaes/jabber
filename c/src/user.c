#include "user.h"
#include "config.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>

void user_get_datapath(const char *username, char *path, size_t pathsize) {
    snprintf(path, pathsize, "%s/%s", g_config.datadir, username);
}

int user_exists(const char *username) {
    char path[1280];
    snprintf(path, sizeof(path), "%s/%s/user.conf", g_config.datadir, username);

    struct stat st;
    return stat(path, &st) == 0;
}

static int valid_username(const char *s) {
    if (!s || !*s)
        return 0;
    for (; *s; s++) {
        if (!isalnum((unsigned char)*s) && *s != '.' && *s != '-' && *s != '_')
            return 0;
    }
    return 1;
}

int user_create(const char *username, const char *password) {
    if (!valid_username(username))
        return -2;

    if (user_exists(username))
        return -1;

    char userdir[1280];
    snprintf(userdir, sizeof(userdir), "%s/%s", g_config.datadir, username);

    if (mkdir(userdir, 0755) < 0) {
        log_write(LOG_WARN, "user_create: mkdir %s failed", userdir);
        return -3;
    }

    char path[1536];

    /* Write user.conf */
    snprintf(path, sizeof(path), "%s/user.conf", userdir);
    FILE *fp = fopen(path, "w");
    if (!fp) {
        log_write(LOG_WARN, "user_create: fopen %s failed", path);
        return -3;
    }
    fprintf(fp, "password = %s\n", password);
    fclose(fp);

    /* Write empty roster.xml */
    snprintf(path, sizeof(path), "%s/roster.xml", userdir);
    fp = fopen(path, "w");
    if (!fp) {
        log_write(LOG_WARN, "user_create: fopen %s failed", path);
        return -3;
    }
    fprintf(fp, "<?xml version=\"1.0\"?>\n<roster/>\n");
    fclose(fp);

    /* Create offline directory */
    snprintf(path, sizeof(path), "%s/offline", userdir);
    if (mkdir(path, 0755) < 0) {
        log_write(LOG_WARN, "user_create: mkdir %s failed", path);
        return -3;
    }

    return 0;
}

int user_change_password(const char *username, const char *password) {
    char path[1280];
    snprintf(path, sizeof(path), "%s/%s/user.conf", g_config.datadir, username);

    FILE *fp = fopen(path, "w");
    if (!fp) {
        log_write(LOG_WARN, "user_change_password: fopen %s failed", path);
        return -1;
    }
    fprintf(fp, "password = %s\n", password);
    fclose(fp);
    return 0;
}

int user_delete(const char *username) {
    char userdir[1280];
    snprintf(userdir, sizeof(userdir), "%s/%s", g_config.datadir, username);

    /* Remove offline messages */
    char offlinedir[1536];
    snprintf(offlinedir, sizeof(offlinedir), "%s/offline", userdir);
    DIR *d = opendir(offlinedir);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] == '.')
                continue;
            char fpath[1792];
            snprintf(fpath, sizeof(fpath), "%s/%s", offlinedir, ent->d_name);
            unlink(fpath);
        }
        closedir(d);
    }
    rmdir(offlinedir);

    /* Remove per-user files */
    char path[1536];
    snprintf(path, sizeof(path), "%s/user.conf", userdir);
    unlink(path);
    snprintf(path, sizeof(path), "%s/roster.xml", userdir);
    unlink(path);

    rmdir(userdir);
    return 0;
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
