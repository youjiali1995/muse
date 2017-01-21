#ifndef MUSE_STR_H__
#define MUSE_STR_H__

typedef struct {
    char *str;
    size_t len;
} str_t;

void str_init(str_t *str);

#endif
