#ifndef MUSE_UTIL_H__
#define MUSE_UTIL_H__

#define MUSE_ERROR (0)
#define MUSE_OK (1)

#define MUSE_ERR_ON(cond, msg, ret) \
    do { \
        if ((cond)) { \
            fprintf(stderr, "[ERROR in %s:%d]: %s\n", __FILE__, __LINE__, (msg)); \
            return (ret); \
        } \
    } while (0)

#endif
