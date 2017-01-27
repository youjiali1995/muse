#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include "util.h"

void muse_log(const char *fmt, ...)
{
    FILE *log_file = fopen("log/muse.log", "a+");
    if (!log_file)
        return;

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (!tm)
        return;

    fprintf(log_file, "[%4d: %02d: %02d: %02d:%02d:%02d] [pid: %5d] ",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec, getpid());

    va_list ap;
    va_start(ap, fmt);
    vfprintf(log_file, fmt, ap);
    va_end(ap);
    fprintf(log_file, "\n");
    fclose(log_file);
}
