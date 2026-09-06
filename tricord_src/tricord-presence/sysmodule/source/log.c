#include "log.h"
#include <3ds.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/stat.h>

#define LOG_PATH   "sdmc:/3ds/tricord-presence/log.txt"
#define LOG_MAX    (256 * 1024)

static LightLock s_lock;

void logInit(void) {
    LightLock_Init(&s_lock);
    struct stat st;
    if (stat(LOG_PATH, &st) == 0 && st.st_size > LOG_MAX) remove(LOG_PATH);
}

void logPrintf(const char *fmt, ...) {
    LightLock_Lock(&s_lock);
    FILE *f = fopen(LOG_PATH, "a");
    if (f) {
        fprintf(f, "[%llu] ", (unsigned long long)(svcGetSystemTick() / SYSCLOCK_ARM11 / 1000));
        va_list ap;
        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);
        fputc('\n', f);
        fclose(f);
    }
    LightLock_Unlock(&s_lock);
}
