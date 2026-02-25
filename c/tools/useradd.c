#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>

static int valid_username(const char *s) {
    if (!s || !*s)
        return 0;
    for (; *s; s++) {
        if (!isalnum((unsigned char)*s) && *s != '.' && *s != '-' && *s != '_')
            return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    const char *datadir = NULL;
    const char *username = NULL;
    const char *password = NULL;
    const char *domain = "localhost";

    static struct option long_opts[] = {
        { "datadir",  required_argument, NULL, 'd' },
        { "user",     required_argument, NULL, 'u' },
        { "password", required_argument, NULL, 'p' },
        { "domain",   required_argument, NULL, 'D' },
        { "help",     no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "d:u:p:D:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'd': datadir = optarg; break;
        case 'u': username = optarg; break;
        case 'p': password = optarg; break;
        case 'D': domain = optarg; break;
        case 'h':
            printf("Usage: useradd -d <datadir> -u <username> -p <password> [-D <domain>]\n"
                   "  -d, --datadir <path>     Data directory\n"
                   "  -u, --user <username>    Username (localpart of JID)\n"
                   "  -p, --password <pass>    Password in plain text\n"
                   "  -D, --domain <domain>    Domain (default: localhost)\n"
                   "  -h, --help               Show usage\n");
            return 0;
        default:
            return 1;
        }
    }

    if (!datadir || !username || !password) {
        fprintf(stderr, "Error: -d, -u, and -p are required.\n");
        return 1;
    }

    if (!valid_username(username)) {
        fprintf(stderr, "Error: Invalid username '%s'. "
                "Only alphanumeric, '.', '-', '_' allowed.\n", username);
        return 1;
    }

    /* Check if user directory already exists */
    char userdir[1280];
    snprintf(userdir, sizeof(userdir), "%s/%s", datadir, username);

    struct stat st;
    if (stat(userdir, &st) == 0) {
        fprintf(stderr, "Error: User '%s@%s' already exists.\n", username, domain);
        return 1;
    }

    /* Create user directory */
    if (mkdir(userdir, 0755) < 0) {
        fprintf(stderr, "Error: mkdir %s: %s\n", userdir, strerror(errno));
        return 1;
    }

    /* Create user.conf */
    char path[1536];
    snprintf(path, sizeof(path), "%s/user.conf", userdir);
    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "Error: fopen %s: %s\n", path, strerror(errno));
        return 1;
    }
    fprintf(fp, "password = %s\n", password);
    fclose(fp);

    /* Create roster.xml */
    snprintf(path, sizeof(path), "%s/roster.xml", userdir);
    fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "Error: fopen %s: %s\n", path, strerror(errno));
        return 1;
    }
    fprintf(fp, "<?xml version=\"1.0\"?>\n<roster/>\n");
    fclose(fp);

    /* Create offline directory */
    snprintf(path, sizeof(path), "%s/offline", userdir);
    if (mkdir(path, 0755) < 0) {
        fprintf(stderr, "Error: mkdir %s: %s\n", path, strerror(errno));
        return 1;
    }

    printf("User '%s@%s' created successfully.\n", username, domain);
    return 0;
}
