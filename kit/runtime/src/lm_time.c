// lm_time.c —— 日期时间 + 日志
#include "lm_time.h"
#include "gc_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

Value lumyr_now(void) {
    time_t t = time(NULL);
    struct tm* lt = localtime(&t);
    Value m = val_map();
    lumyr_map_set(&m, lumyr_make_string("year"), lumyr_make_int(lt->tm_year + 1900));
    lumyr_map_set(&m, lumyr_make_string("month"), lumyr_make_int(lt->tm_mon + 1));
    lumyr_map_set(&m, lumyr_make_string("day"), lumyr_make_int(lt->tm_mday));
    lumyr_map_set(&m, lumyr_make_string("hour"), lumyr_make_int(lt->tm_hour));
    lumyr_map_set(&m, lumyr_make_string("minute"), lumyr_make_int(lt->tm_min));
    lumyr_map_set(&m, lumyr_make_string("second"), lumyr_make_int(lt->tm_sec));
    lumyr_map_set(&m, lumyr_make_string("weekday"), lumyr_make_int(lt->tm_wday)); // 0=周日
    lumyr_map_set(&m, lumyr_make_string("yday"), lumyr_make_int(lt->tm_yday + 1));
    lumyr_map_set(&m, lumyr_make_string("isdst"), lumyr_make_bool(lt->tm_isdst > 0));
    return m;
}

double lumyr_timestamp(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

long long lumyr_timestamp_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void lumyr_sleep_ms(long long ms) {
    if(ms <= 0) return;
    /* usleep 阻塞期间不执行 VM 代码、不修改 GC 根，栈稳定，标记安全点。
     * 否则长时间 sleep 会导致 GC 等待所有线程 at_safepoint 超时。 */
    gc_enter_native_block();
    usleep((useconds_t)(ms * 1000));
    gc_leave_native_block();
}

static void fmt_buf(char* buf, size_t sz, const char* fmt, time_t t) {
    struct tm* lt = localtime(&t);
    strftime(buf, sz, fmt, lt);
}

char* lumyr_date_str(void) {
    char buf[32];
    fmt_buf(buf, sizeof(buf), "%Y-%m-%d", time(NULL));
    return strdup(buf);
}

char* lumyr_time_str(void) {
    char buf[32];
    fmt_buf(buf, sizeof(buf), "%H:%M:%S", time(NULL));
    return strdup(buf);
}

char* lumyr_datetime_str(void) {
    char buf[32];
    fmt_buf(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", time(NULL));
    return strdup(buf);
}

char* lumyr_format_time(const char* fmt, double ts) {
    if(!fmt) return strdup("");
    time_t t = (ts < 0) ? time(NULL) : (time_t)ts;
    char buf[256];
    fmt_buf(buf, sizeof(buf), fmt, t);
    return strdup(buf);
}

static const char* level_names[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

void lumyr_log(int level, const char* msg) {
    if(level < 0 || level > 4) level = 1;
    char buf[32];
    fmt_buf(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", time(NULL));
    fprintf(stderr, "[%s] [%s] %s\n", buf, level_names[level], msg ? msg : "");
    fflush(stderr);
}
