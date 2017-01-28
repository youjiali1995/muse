#ifndef MUSE_DICT_H__
#define MUSE_DICT_H__

#include "str.h"

typedef struct {
    size_t hash;
    str_t *key;
    void *val;
} dict_entry_t;

typedef struct {
    size_t used;
    size_t size;
    dict_entry_t *dict;
} dict_t;

#define DICT_INIT_SIZE 16

dict_t *dict_new(void);
void *dict_get(dict_t *dict, const str_t *key);
void dict_put(dict_t *dict, str_t *key, void *val);
void dict_free(dict_t *dict, void (*key_free)(str_t *), void (*val_free)(void *));

#endif
