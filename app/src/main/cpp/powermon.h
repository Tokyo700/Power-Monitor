/*
 * powermon.h - The C core of the Power Monitor.
 *
 * Everything here is plain C with no Android calls, so it can be compiled and tested
 * on a Linux computer (see tests/test_powermon.c). The Android app reaches these
 * functions through the JNI bridge in jni_bridge.c.
 *
 * OS concepts used:
 *   - Reading kernel data through /sys and /proc (files are the kernel's interface).
 *   - Directory traversal (opendir/readdir) to find the battery and thermal zones.
 *   - Processing a time-ordered event stream into per-app intervals.
 */
#ifndef POWERMON_H
#define POWERMON_H

#include <stddef.h>

/* ------------------------------------------------------------------ battery */

typedef struct {
    int found;                 /* 1 if a battery was found, 0 otherwise */
    int capacity_percent;      /* state of charge, 0-100, -1 if unknown */
    int charging;              /* 1 if charging or full, 0 otherwise */
    char status[32];           /* "Charging", "Discharging", "Full", ... */
    char health_text[32];      /* "Good", "Overheat", ... as the kernel reports it */
    int has_temp;              /* 1 if temperature is known */
    int temp_deci_c;           /* temperature in tenths of a degree Celsius */
    int voltage_mv;            /* -1 if unknown */
    int current_ua;            /* instantaneous current, microamps; 0 if unknown */
    int cycle_count;           /* -1 if unknown */
    long long full_capacity;   /* charge (or energy) when full, -1 if unknown */
    long long design_capacity; /* charge (or energy) when new, -1 if unknown */
    int health_percent;        /* full / design * 100, -1 if unknown */
} pm_battery_t;

/*
 * Finds the battery under a power-supply root (normally /sys/class/power_supply) by
 * looking for an entry whose "type" file says "Battery", then reads its values.
 * Returns 0 if a battery was found, -1 otherwise.
 */
int pm_read_battery(const char *power_supply_root, pm_battery_t *out);

/* full / design as a percentage, rounded. Returns -1 if either value is not positive. */
int pm_health_percent(long long full, long long design);

/*
 * Highest temperature across the thermal zones under thermal_root (normally
 * /sys/class/thermal), in millidegrees Celsius. *zones_read gets how many zones could
 * be read; the return value is -1 if none could be read (SELinux often blocks apps).
 */
int pm_max_thermal_milli_c(const char *thermal_root, int *zones_read);

/* ------------------------------------------------------- battery estimation */

/* Percentage points lost between two readings; 0 if charging at either end. */
int pm_battery_drop(int prev_level, int prev_charging, int cur_level, int cur_charging);

/*
 * Where a new logging interval starts. Uses the previous snapshot time when it is
 * recent and not in the future; otherwise falls back to the default 15-minute window.
 * Pass has_prev = 0 when there is no previous snapshot.
 */
long long pm_interval_start(long long prev_timestamp, int has_prev, long long now);

/*
 * Splits a battery drop between apps in proportion to their activity. Foreground time
 * counts fully; foreground-service time counts 0.3x. Writes n shares into out, which
 * sum to drop_percent (or are all zero when nothing can be attributed).
 */
void pm_drain_shares(const long long *foreground_ms, const long long *service_ms,
                     int n, int drop_percent, float *out);

/* ------------------------------------------------------------- usage events */

/* Event types, same numbers as android.app.usage.UsageEvents.Event. */
#define PM_EVENT_ACTIVITY_RESUMED 1
#define PM_EVENT_ACTIVITY_PAUSED 2
#define PM_EVENT_FGS_START 19
#define PM_EVENT_FGS_STOP 20

typedef struct {
    const char *package;     /* package name */
    const char *component;   /* activity/service class name, "" if none */
    int type;                /* one of the PM_EVENT_ values, others are ignored */
    long long timestamp;     /* milliseconds since the epoch */
} pm_event_t;

typedef struct {
    char package[256];
    long long foreground_ms;     /* time with at least one screen in front */
    long long service_ms;        /* time with a foreground service running */
    int launches;                /* times the app came to the front */
    long long last_used;         /* last timestamp seen for this app */
} pm_app_usage_t;

/*
 * Turns time-ordered events into per-app totals for [window_start, window_end).
 * Writes at most max_out apps (sorted by total active time, highest first) and returns
 * how many were written. Apps with no activity are left out.
 */
int pm_aggregate_usage(const pm_event_t *events, int n,
                       long long window_start, long long window_end,
                       pm_app_usage_t *out, int max_out);

/* -------------------------------------------------------------- self CPU */

/*
 * Reads this process's CPU time from /proc/self/stat (utime + stime), in milliseconds.
 * Returns 0 on success, -1 if the file cannot be read or parsed.
 */
int pm_self_cpu_ms(long long *user_ms, long long *kernel_ms);

#endif /* POWERMON_H */
