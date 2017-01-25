#ifndef MUSE_STR_H__
#define MUSE_STR_H__

#include <stdbool.h>

typedef struct {
    char *str;
    size_t len;
} str_t;

#define STR(s) (str_t){s, sizeof(s) - 1}

void str_init(str_t *str);
bool str_eq(const str_t *l, const str_t *r);

#endif
