#ifndef MUSE_UTIL_H__
#define MUSE_UTIL_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MUSE_ERROR (-1)
#define MUSE_OK (0)

#define MUSE_ERR_ON(cond, msg, ret) \
    do { \
        if ((cond)) { \
            fprintf(stderr, "[ERROR in %s:%d]: %s\n", __FILE__, __LINE__, (msg)); \
            return (ret); \
        } \
    } while (0)


#define MUSE_EXIT_ON(cond, msg) \
    do { \
        if ((cond)) { \
            fprintf(stderr, "[EXIT in %s:%d]: %s\n", __FILE__, __LINE__, (msg)); \
            exit(MUSE_ERROR); \
        } \
    } while (0)

void muse_log(const char *fmt, ...);

#endif
