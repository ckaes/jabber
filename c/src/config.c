#include "config.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>

config_t g_config;

void config_defaults(config_t *cfg) {
    snprintf(cfg->domain, sizeof(cfg->domain), "localhost");
    cfg->port = 5222;
    snprintf(cfg->bind_address, sizeof(cfg->bind_address), "0.0.0.0");
    snprintf(cfg->datadir, sizeof(cfg->datadir), "./data");
    snprintf(cfg->logfile, sizeof(cfg->logfile), "./xmppd.log");
    cfg->loglevel = LOG_INFO;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

static int parse_loglevel(const char *s) {
    if (strcasecmp(s, "DEBUG") == 0) return LOG_DEBUG;
    if (strcasecmp(s, "INFO") == 0)  return LOG_INFO;
    if (strcasecmp(s, "WARN") == 0)  return LOG_WARN;
    if (strcasecmp(s, "ERROR") == 0) return LOG_ERROR;
    return LOG_INFO;
}

int config_load(const char *path, config_t *cfg) {
    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1;

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        /* Strip newline */
        line[strcspn(line, "\n")] = '\0';

        /* Skip comments and blank lines */
        char *s = trim(line);
        if (*s == '#' || *s == '\0')
            continue;

        char *eq = strchr(s, '=');
        if (!eq)
            continue;

        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);

        if (strcmp(key, "domain") == 0)
            snprintf(cfg->domain, sizeof(cfg->domain), "%s", val);
        else if (strcmp(key, "port") == 0)
            cfg->port = atoi(val);
        else if (strcmp(key, "bind_address") == 0)
            snprintf(cfg->bind_address, sizeof(cfg->bind_address), "%s", val);
        else if (strcmp(key, "datadir") == 0)
            snprintf(cfg->datadir, sizeof(cfg->datadir), "%s", val);
        else if (strcmp(key, "logfile") == 0)
            snprintf(cfg->logfile, sizeof(cfg->logfile), "%s", val);
        else if (strcmp(key, "loglevel") == 0)
            cfg->loglevel = parse_loglevel(val);
    }

    fclose(fp);
    return 0;
}

int config_parse_args(int argc, char **argv, config_t *cfg) {
    static struct option long_opts[] = {
        { "config",   required_argument, NULL, 'c' },
        { "domain",   required_argument, NULL, 'd' },
        { "port",     required_argument, NULL, 'p' },
        { "datadir",  required_argument, NULL, 'D' },
        { "logfile",  required_argument, NULL, 'l' },
        { "loglevel", required_argument, NULL, 'L' },
        { "help",     no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    /* First pass: look for --config/-c to load config file */
    const char *config_path = NULL;
    optind = 1;
    int opt;
    while ((opt = getopt_long(argc, argv, "c:d:p:D:l:L:h", long_opts, NULL)) != -1) {
        if (opt == 'c')
            config_path = optarg;
    }

    /* Load config file (if specified or default exists) */
    if (config_path) {
        config_load(config_path, cfg);
    } else {
        config_load("./xmppd.conf", cfg); /* ok if missing */
    }

    /* Second pass: CLI overrides */
    optind = 1;
    while ((opt = getopt_long(argc, argv, "c:d:p:D:l:L:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'c':
            break; /* already handled */
        case 'd':
            snprintf(cfg->domain, sizeof(cfg->domain), "%s", optarg);
            break;
        case 'p':
            cfg->port = atoi(optarg);
            break;
        case 'D':
            snprintf(cfg->datadir, sizeof(cfg->datadir), "%s", optarg);
            break;
        case 'l':
            snprintf(cfg->logfile, sizeof(cfg->logfile), "%s", optarg);
            break;
        case 'L':
            cfg->loglevel = parse_loglevel(optarg);
            break;
        case 'h':
            printf("Usage: xmppd [options]\n"
                   "  -c, --config <path>     Config file (default: ./xmppd.conf)\n"
                   "  -d, --domain <domain>   Server domain\n"
                   "  -p, --port <port>       Listen port\n"
                   "  -D, --datadir <path>    Data directory\n"
                   "  -l, --logfile <path>    Log file path\n"
                   "  -L, --loglevel <level>  Log level (DEBUG/INFO/WARN/ERROR)\n"
                   "  -h, --help              Show usage\n");
            return 1;
        default:
            return -1;
        }
    }
    return 0;
}
