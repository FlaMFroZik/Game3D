#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static SrvLogLevel g_log_level = SRV_LOG_INFO;

void srv_log_init(SrvLogLevel level) {
    g_log_level = level;
}

void srv_log(SrvLogLevel level, const char *format, ...) {
    time_t wall;
    const struct tm *local;
    char stamp[16];
    va_list args;

    if (level > g_log_level) return;

    wall = time(NULL);
    local = localtime(&wall);
    if (local != NULL && strftime(stamp, sizeof stamp, "%H:%M:%S", local) > 0) {
        printf("[%s] ", stamp);
    } else {
        printf("[--:--:--] ");
    }

    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    putchar('\n');
    fflush(stdout);
}
