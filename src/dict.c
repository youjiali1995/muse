#include <stdlib.h>
#include <assert.h>
#include "dict.h"

/* BKDR hash */
static size_t str_hash(const str_t *str)
{
    assert(str);

    size_t seed = 131;
    size_t hash = 0;
    for (int i = 0; i < str->len; i++)
        hash = hash * seed + str->str[i];
    return hash;
}

dict_t *dict_new(void)
{
    dict_t *dict = malloc(sizeof(*dict));
    if (!dict)
        return NULL;
    dict->used = 0;
    dict->size = DICT_INIT_SIZE;
    dict->dict = calloc(dict->size, sizeof(dict_entry_t));
    if (!dict->dict)
        return NULL;
    return dict;
}

#define PROBE(i) ((i) + 1)

void *dict_get(dict_t *dict, const str_t *key)
{
    assert(dict && key);

    size_t hash = str_hash(key);
    for (size_t i = hash & (dict->size - 1); dict->dict[i].key; i = PROBE(i) & (dict->size - 1)) {
        if (dict->dict[i].hash == hash && str_eq(dict->dict[i].key, key))
            return dict->dict[i].val;
    }
    return NULL;
}

static void dict_resize(dict_t *dict, size_t size)
{
    dict_entry_t *old = dict->dict;
    dict->size = size;
    dict->dict = calloc(dict->size, sizeof(dict_entry_t));
    size_t used = dict->used;
    dict->used = 0;

    for (dict_entry_t *e = old; used > 0; e++) {
        if (e->key) {
            dict_put(dict, e->key, e->val);
            used--;
        }
    }
    free(old);
}

void dict_put(dict_t *dict, str_t *key, void *val)
{
    assert(dict && key);

    size_t hash = str_hash(key);
    size_t i;
    for (i = hash & (dict->size - 1); dict->dict[i].key; i = PROBE(i) & (dict->size - 1)) {
        if (dict->dict[i].hash == hash && str_eq(dict->dict[i].key, key)) {
            dict->dict[i].val = val;
            break;
        }
    }
    if (!dict->dict[i].key) {
        dict->dict[i].hash = hash;
        dict->dict[i].key = key;
        dict->dict[i].val = val;
    }
    dict->used++;
    if (dict->used * 3 >= dict->size * 2)
        dict_resize(dict, dict->size * 2);
    return;
}

void dict_free(dict_t *dict, void (*key_free)(str_t *), void (*val_free)(void *))
{
    assert(dict);

    for (dict_entry_t *e = dict->dict; dict->used > 0; e++) {
        if (e->key) {
            if (key_free)
                (*key_free)(e->key);
            if (val_free)
                (*val_free)(e->val);
            dict->used--;
        }
    }
    free(dict->dict);
    free(dict);
}
