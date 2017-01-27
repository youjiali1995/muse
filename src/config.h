#ifndef MUSE_CONFIG_H__
#define MUSE_CONFIG_H__

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int port;
    bool daemon;
    int worker;
    int timeout;
    int src_root;
    int err_root;
} config_t;

int config_load(config_t *config, const char *config_file);

#endif
