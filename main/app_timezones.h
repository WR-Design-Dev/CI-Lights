#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

size_t app_timezone_count(void);
const char *app_timezone_name(size_t index);
bool app_timezone_is_valid(const char *name);
bool app_timezone_time_supported(int64_t timestamp);
bool app_timezone_offset(const char *name, int64_t timestamp, int32_t *offset);
bool app_timezone_localtime(const char *name, int64_t timestamp, struct tm *local);
/* Resolve a local wall-clock timestamp. Repeated times use the first occurrence;
   nonexistent spring-forward times move forward by the gap. */
bool app_timezone_resolve_local(const char *name, int64_t local_timestamp, int64_t *timestamp);
