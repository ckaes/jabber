#include "config.h"
#include "log.h"
#include "server.h"
#include "xml.h"
#include <stdio.h>
#include <stdlib.h>
#include <libxml/parser.h>

int main(int argc, char **argv) {
    config_defaults(&g_config);

    int r = config_parse_args(argc, argv, &g_config);
    if (r != 0)
        return (r > 0) ? 0 : 1;

    if (log_init(g_config.logfile, g_config.loglevel) < 0)
        return 1;

    log_write(LOG_INFO, "xmppd starting on %s:%d domain=%s datadir=%s",
              g_config.bind_address, g_config.port,
              g_config.domain, g_config.datadir);

    xmlInitParser();
    xml_init_sax_handler();

    if (server_init(&g_config) < 0) {
        log_write(LOG_ERROR, "Failed to initialize server");
        log_close();
        xmlCleanupParser();
        return 1;
    }

    server_run();
    server_shutdown();

    xmlCleanupParser();
    log_write(LOG_INFO, "xmppd shutting down");
    log_close();
    return 0;
}
