#ifndef XMPPD_CONFIG_H
#define XMPPD_CONFIG_H

typedef struct config {
    char domain[256];
    int  port;
    char bind_address[256];
    char datadir[1024];
    char logfile[1024];
    int  loglevel;
} config_t;

void config_defaults(config_t *cfg);
int  config_load(const char *path, config_t *cfg);
int  config_parse_args(int argc, char **argv, config_t *cfg);

/* Global config accessible to all modules */
extern config_t g_config;

#endif
