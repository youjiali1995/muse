#ifndef MUSE_CONFIG_H__
#define MUSE_CONFIG_H__

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t port;
    bool daemon;
    int worker;
    int timeout;
    int root_fd;
} config_t;

int config_load(config_t *config, const char *config_file);

#endif
