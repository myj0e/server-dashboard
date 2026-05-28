#include "config.h"
#include "toml.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#define DEFAULT_HOST           "127.0.0.1"
#define DEFAULT_PORT           18080
#define DEFAULT_INTERVAL_MS    1000
#define MIN_INTERVAL_MS        250

static void config_set_defaults(config_t *cfg) {
    strncpy(cfg->listen_host, DEFAULT_HOST, sizeof(cfg->listen_host) - 1);
    cfg->listen_port = DEFAULT_PORT;
    cfg->sample_interval_ms = DEFAULT_INTERVAL_MS;
    cfg->enable_gpu = true;
    cfg->enable_process_ops = false;
    cfg->enable_auth = false;
    cfg->config_path[0] = '\0';
}

static bool validate_config(config_t *cfg) {
    bool ok = true;

    if (cfg->listen_host[0] == '\0') {
        fprintf(stderr, "Error: listen_host must not be empty\n");
        ok = false;
    }
    if (cfg->listen_port < 1 || cfg->listen_port > 65535) {
        fprintf(stderr, "Error: listen_port must be in 1..65535, got %d\n", cfg->listen_port);
        ok = false;
    }
    if (cfg->sample_interval_ms < MIN_INTERVAL_MS) {
        fprintf(stderr, "Error: sample_interval_ms must be >= %d, got %d\n",
                MIN_INTERVAL_MS, cfg->sample_interval_ms);
        ok = false;
    }

    return ok;
}

static int parse_toml(config_t *cfg, const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "Error: cannot open config file '%s': %s\n", path, strerror(errno));
        return -1;
    }

    char errbuf[200];
    toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);

    if (!conf) {
        fprintf(stderr, "Error: failed to parse config: %s\n", errbuf);
        return -1;
    }

    /* listen_host */
    toml_datum_t host_d = toml_string_in(conf, "listen_host");
    if (host_d.ok) {
        strncpy(cfg->listen_host, host_d.u.s, sizeof(cfg->listen_host) - 1);
        free(host_d.u.s);
    }

    /* listen_port */
    toml_datum_t port_d = toml_int_in(conf, "listen_port");
    if (port_d.ok) cfg->listen_port = (int)port_d.u.i;

    /* sample_interval_ms */
    toml_datum_t intv_d = toml_int_in(conf, "sample_interval_ms");
    if (intv_d.ok) cfg->sample_interval_ms = (int)intv_d.u.i;

    /* enable_gpu */
    toml_datum_t gpu_d = toml_bool_in(conf, "enable_gpu");
    if (gpu_d.ok) cfg->enable_gpu = gpu_d.u.b;

    /* enable_process_ops */
    toml_datum_t ops_d = toml_bool_in(conf, "enable_process_ops");
    if (ops_d.ok) cfg->enable_process_ops = ops_d.u.b;

    /* enable_auth */
    toml_datum_t auth_d = toml_bool_in(conf, "enable_auth");
    if (auth_d.ok) cfg->enable_auth = auth_d.u.b;

    toml_free(conf);
    return validate_config(cfg) ? 0 : -1;
}

int config_load(config_t *cfg, int argc, char **argv) {
    config_set_defaults(cfg);

    const char *config_path = NULL;

    static struct option long_opts[] = {
        {"config", required_argument, 0, 'c'},
        {"help",   no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "c:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'c':
            config_path = optarg;
            break;
        case 'h':
            printf("Usage: %s [--config|-c <path>]\n", argv[0]);
            printf("  -c, --config <path>  Path to TOML configuration file\n");
            printf("                        Default: ./config/monitor.conf\n");
            printf("                        Fallback: /etc/monitor-dashboard/config.toml\n");
            exit(0);
        default:
            fprintf(stderr, "Usage: %s [--config|-c <path>]\n", argv[0]);
            return -1;
        }
    }

    if (config_path) {
        strncpy(cfg->config_path, config_path, sizeof(cfg->config_path) - 1);
    } else {
        /* try default locations */
        if (access("./config/monitor.conf", R_OK) == 0) {
            strncpy(cfg->config_path, "./config/monitor.conf", sizeof(cfg->config_path) - 1);
        } else if (access("/etc/monitor-dashboard/config.toml", R_OK) == 0) {
            strncpy(cfg->config_path, "/etc/monitor-dashboard/config.toml",
                    sizeof(cfg->config_path) - 1);
        } else {
            fprintf(stderr, "Error: no config file found. Use --config <path>.\n");
            return -1;
        }
    }

    return parse_toml(cfg, cfg->config_path);
}
