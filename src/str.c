#include <assert.h>
#include <stddef.h>
#include "str.h"

void str_init(str_t *str)
{
    assert(str);
    str->str = NULL;
    str->len = 0;
}

bool str_eq(const str_t *l, const str_t *r)
{
    assert(l && r);

    if (l->len != r->len)
        return false;
    for (size_t i = 0; i < l->len; i++) {
        if (l->str[i] != r->str[i])
            return false;
    }
    return true;
}
