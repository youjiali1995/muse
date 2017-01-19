#ifndef MUSE_SERVER_H__
#define MUSE_SERVER_H__

#include "config.h"

#define MAX_EVENTS (10000)
#define MAX_CONNECTIONS (10000)

extern config_t server_cfg;
extern int epfd;

#endif
