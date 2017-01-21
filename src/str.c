#include <assert.h>
#include <stddef.h>
#include "str.h"

void str_init(str_t *str)
{
    assert(str);
    str->str = NULL;
    str->len = 0;
}
