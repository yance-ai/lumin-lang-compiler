// lm_time.h —— 日期时间 + 日志
#ifndef LM_TIME_H
#define LM_TIME_H

#include "lm_value.h"

// ===== 日期时间 =====
// now()：当前时间 map {year,month,day,hour,minute,second,weekday,yday,isdst}
Value lumyr_now(void);
// timestamp()：Unix 时间戳（秒，double）
double lumyr_timestamp(void);
// timestamp_ms()：Unix 时间戳（毫秒，long long）
long long lumyr_timestamp_ms(void);
// sleep_ms(ms)：休眠毫秒
void lumyr_sleep_ms(long long ms);
// date_str()："2026-09-07"
char* lumyr_date_str(void);
// time_str()："15:30:45"
char* lumyr_time_str(void);
// datetime_str()："2026-09-07 15:30:45"
char* lumyr_datetime_str(void);
// format_time(fmt, ts?)：按 strftime 格式化；ts 为 -1 用当前时间
char* lumyr_format_time(const char* fmt, double ts);

// ===== 日志 =====
// log_level: 0=debug, 1=info, 2=warn, 3=error, 4=fatal
void lumyr_log(int level, const char* msg);

#endif // LM_TIME_H
