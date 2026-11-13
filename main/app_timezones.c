#include "app_timezones.h"
#include <string.h>

typedef struct { uint32_t at; int32_t offset; } timezone_transition_t;
typedef struct { uint16_t first, count; int32_t initial; } timezone_group_t;
typedef struct { const char *name; uint16_t group; } timezone_name_t;
#include "app_timezones.inc"

size_t app_timezone_count(void) { return sizeof(TIMEZONE_NAMES) / sizeof(TIMEZONE_NAMES[0]); }
const char *app_timezone_name(size_t index) { return index < app_timezone_count() ? TIMEZONE_NAMES[index].name : NULL; }

static const timezone_group_t *find_zone(const char *name)
{
    if (name == NULL) return NULL;
    size_t low = 0, high = app_timezone_count();
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int order = strcmp(name, TIMEZONE_NAMES[middle].name);
        if (order == 0) return &TIMEZONE_GROUPS[TIMEZONE_NAMES[middle].group];
        if (order < 0) high = middle;
        else low = middle + 1;
    }
    return NULL;
}

bool app_timezone_is_valid(const char *name) { return find_zone(name) != NULL; }
bool app_timezone_time_supported(int64_t stamp) { return stamp >= APP_TIMEZONE_START && stamp < APP_TIMEZONE_END; }

bool app_timezone_offset(const char *name, int64_t stamp, int32_t *offset)
{
    const timezone_group_t *group = find_zone(name);
    if (group == NULL || offset == NULL || !app_timezone_time_supported(stamp)) return false;
    uint32_t relative = (uint32_t) (stamp - APP_TIMEZONE_START);
    size_t low = 0, high = group->count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (TIMEZONE_TRANSITIONS[group->first + middle].at <= relative) low = middle + 1;
        else high = middle;
    }
    *offset = low == 0 ? group->initial : TIMEZONE_TRANSITIONS[group->first + low - 1].offset;
    return true;
}

bool app_timezone_localtime(const char *name, int64_t stamp, struct tm *local)
{
    int32_t offset;
    if (local == NULL || !app_timezone_offset(name, stamp, &offset)) return false;
    time_t wall = (time_t) (stamp + offset);
    return gmtime_r(&wall, local) != NULL;
}

bool app_timezone_resolve_local(const char *name, int64_t wall, int64_t *stamp)
{
    if (stamp == NULL || find_zone(name) == NULL) return false;
    int64_t exact = INT64_MAX, gap = INT64_MAX, gap_wall = INT64_MAX;
    for (int day = -2; day <= 2; ++day) {
        int32_t assumed, actual;
        if (!app_timezone_offset(name, wall + day * INT64_C(86400), &assumed)) continue;
        int64_t candidate = wall - assumed;
        if (!app_timezone_offset(name, candidate, &actual)) continue;
        int64_t resolved = candidate + actual;
        if (resolved == wall && candidate < exact) exact = candidate;
        if (resolved > wall && resolved < gap_wall) { gap = candidate; gap_wall = resolved; }
    }
    if (exact != INT64_MAX) { *stamp = exact; return true; }
    if (gap != INT64_MAX) { *stamp = gap; return true; }
    return false;
}
