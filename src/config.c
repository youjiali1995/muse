#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include "config.h"
#include "json.h"
#include "util.h"

static char *read_file(const char *file_name)
{
    int fd = open(file_name, O_RDONLY);
    MUSE_ERR_ON(fd == -1, strerror(errno), NULL);

    struct stat buf;
    int ret = fstat(fd, &buf);
    MUSE_ERR_ON(ret == -1, strerror(errno), NULL);

    char *p = malloc(buf.st_size + 1);
    MUSE_ERR_ON(!p, "malloc", NULL);

    if (read(fd, p, buf.st_size) == -1) {
        free(p);
        close(fd);
        MUSE_ERR_ON(1, strerror(errno), NULL);
    }
    p[buf.st_size] = '\0';
    close(fd);
    return p;
}

static void config_init(config_t *config)
{
    memset(config, 0, sizeof(config_t));
}

int config_load(config_t *config, const char *config_file)
{
    config_init(config);

    char *json = read_file(config_file);
    MUSE_ERR_ON(!json, "load file \'config.json\' failed", MUSE_ERR);

    json_value v;
    json_init(&v);
    int ret = json_parse(&v, json);
    MUSE_ERR_ON(ret == JSON_PARSE_ERROR, "bad json format", MUSE_ERR);

    json_value *p = json_get_object_value(&v, "port");
    MUSE_ERR_ON(!p || json_get_type(p) != JSON_NUMBER, "port not specified or error type", MUSE_ERR);
    config->port = (int) json_get_number(p);
    MUSE_ERR_ON(config->port > 65535, "port specified greater than 65535", MUSE_ERR);

    p = json_get_object_value(&v, "daemon");
    MUSE_ERR_ON(!p, "daemon not specified", MUSE_ERR);
    int type = json_get_type(p);
    if (type == JSON_TRUE)
        config->daemon = true;
    else if (type == JSON_FALSE)
        config->daemon = false;
    else
        MUSE_ERR_ON(1, "daemon error type", MUSE_ERR);

    p = json_get_object_value(&v, "worker");
    MUSE_ERR_ON(!p || json_get_type(p) != JSON_NUMBER, "worker not specified or error type", MUSE_ERR);
    config->worker = (int) json_get_number(p);
    MUSE_ERR_ON(config->worker > sysconf(_SC_NPROCESSORS_ONLN), "worker specified greater than cpu cores", MUSE_ERR);

    p = json_get_object_value(&v, "timeout");
    MUSE_ERR_ON(!p || json_get_type(p) != JSON_NUMBER, "timeout not specified or error type", MUSE_ERR);
    config->timeout = (int) json_get_number(p);

    p = json_get_object_value(&v, "src_root");
    MUSE_ERR_ON(!p || json_get_type(p) != JSON_STRING, "src_root not specified or error type", MUSE_ERR);
    char *src_root = json_get_string(p);
    config->src_root= open(src_root, O_RDONLY);
    MUSE_ERR_ON(config->src_root < 0, strerror(errno), MUSE_ERR);
    struct stat stat;
    fstat(config->src_root, &stat);
    MUSE_ERR_ON(!S_ISDIR(stat.st_mode), "src_root not directory", MUSE_ERR);

    p = json_get_object_value(&v, "err_root");
    MUSE_ERR_ON(!p || json_get_type(p) != JSON_STRING, "err_root not specified or error type", MUSE_ERR);
    char *err_root = json_get_string(p);
    config->err_root = open(err_root, O_RDONLY);
    MUSE_ERR_ON(config->err_root < 0, strerror(errno), MUSE_ERR);
    fstat(config->err_root, &stat);
    MUSE_ERR_ON(!S_ISDIR(stat.st_mode), "err_root not directory", MUSE_ERR);

    json_free(&v);
    free(json);
    return MUSE_OK;
}
