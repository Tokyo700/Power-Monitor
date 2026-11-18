/*
 * powermon.c - Implementation of the Power Monitor's C core. See powermon.h.
 */
#define _GNU_SOURCE
#include "powermon.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_INTERVAL_MS (15LL * 60 * 1000)
#define MAX_INTERVAL_MS (6LL * 60 * 60 * 1000)
#define SAME_VISIT_GAP_MS 2000LL
#define SERVICE_WEIGHT 0.3f
#define MAX_APPS 512

/* ============================================================ file helpers */

/* Reads a whole small file (sysfs/proc) into buf, without the trailing newline. */
static int read_text(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    size_t len = fread(buf, 1, size - 1, f);
    fclose(f);
    buf[len] = '\0';
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == ' '))
        buf[--len] = '\0';
    return len > 0 ? 0 : -1;
}

static int read_ll(const char *dir, const char *name, long long *out)
{
    char path[512], text[64];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (read_text(path, text, sizeof text) != 0)
        return -1;
    char *end;
    long long v = strtoll(text, &end, 10);
    if (end == text)
        return -1;
    *out = v;
    return 0;
}

/* ================================================================ battery */

int pm_health_percent(long long full, long long design)
{
    if (full <= 0 || design <= 0)
        return -1;
    return (int)((full * 100 + design / 2) / design); /* rounded */
}

int pm_read_battery(const char *root, pm_battery_t *out)
{
    memset(out, 0, sizeof *out);
    out->capacity_percent = -1;
    out->voltage_mv = -1;
    out->cycle_count = -1;
    out->full_capacity = -1;
    out->design_capacity = -1;
    out->health_percent = -1;

    DIR *dir = opendir(root);
    if (!dir)
        return -1;

    struct dirent *entry;
    char supply[512], type[32];
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;
        snprintf(supply, sizeof supply, "%s/%s", root, entry->d_name);
        char type_path[600];
        snprintf(type_path, sizeof type_path, "%s/type", supply);
        if (read_text(type_path, type, sizeof type) == 0 && strcmp(type, "Battery") == 0)
            break;                        /* found it: `supply` is the battery directory */
    }
    int found = entry != NULL;
    closedir(dir);
    if (!found)
        return -1;

    out->found = 1;
    long long v;
    char path[600];

    snprintf(path, sizeof path, "%s/status", supply);
    read_text(path, out->status, sizeof out->status);
    out->charging = strcmp(out->status, "Charging") == 0 || strcmp(out->status, "Full") == 0;

    snprintf(path, sizeof path, "%s/health", supply);
    read_text(path, out->health_text, sizeof out->health_text);

    if (read_ll(supply, "capacity", &v) == 0)
        out->capacity_percent = (int)v;
    if (read_ll(supply, "temp", &v) == 0) {        /* tenths of a degree Celsius */
        out->has_temp = 1;
        out->temp_deci_c = (int)v;
    }
    if (read_ll(supply, "voltage_now", &v) == 0)   /* microvolts -> millivolts */
        out->voltage_mv = (int)(v / 1000);
    if (read_ll(supply, "current_now", &v) == 0)
        out->current_ua = (int)v;
    if (read_ll(supply, "cycle_count", &v) == 0)
        out->cycle_count = (int)v;

    /* Health compares the charge the battery holds when full with its original design
     * value. Some phones report charge (uAh), others energy (uWh); the ratio is the same. */
    long long full = -1, design = -1;
    if (read_ll(supply, "charge_full", &full) == 0 &&
        read_ll(supply, "charge_full_design", &design) == 0) {
        /* charge pair found */
    } else if (read_ll(supply, "energy_full", &full) == 0 &&
               read_ll(supply, "energy_full_design", &design) == 0) {
        /* energy pair found */
    } else {
        full = design = -1;
    }
    out->full_capacity = full;
    out->design_capacity = design;
    out->health_percent = pm_health_percent(full, design);
    return 0;
}

int pm_max_thermal_milli_c(const char *root, int *zones_read)
{
    int max = -1;
    *zones_read = 0;

    DIR *dir = opendir(root);
    if (!dir)
        return -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "thermal_zone", 12) != 0)
            continue;
        char path[600];
        snprintf(path, sizeof path, "%s/%s/temp", root, entry->d_name);
        long long v;
        char text[32];
        if (read_text(path, text, sizeof text) != 0)
            continue;
        v = strtoll(text, NULL, 10);
        (*zones_read)++;
        if (v > max)
            max = (int)v;
    }
    closedir(dir);
    return max;
}

/* ======================================================= estimation rules */

int pm_battery_drop(int prev_level, int prev_charging, int cur_level, int cur_charging)
{
    if (prev_charging || cur_charging)
        return 0;
    int drop = prev_level - cur_level;
    return drop < 0 ? 0 : drop;
}

long long pm_interval_start(long long prev_timestamp, int has_prev, long long now)
{
    if (has_prev) {
        long long age = now - prev_timestamp;
        if (age >= 1 && age <= MAX_INTERVAL_MS)
            return prev_timestamp;
    }
    return now - DEFAULT_INTERVAL_MS;
}

void pm_drain_shares(const long long *fg, const long long *svc, int n,
                     int drop_percent, float *out)
{
    double total = 0;
    double weights[MAX_APPS];
    for (int i = 0; i < n && i < MAX_APPS; i++) {
        weights[i] = (double)fg[i] + (double)svc[i] * SERVICE_WEIGHT;
        total += weights[i];
    }
    for (int i = 0; i < n; i++) {
        if (drop_percent == 0 || total <= 0)
            out[i] = 0.0f;
        else
            out[i] = (float)(drop_percent * weights[i] / total);
    }
}

/* ======================================================== usage aggregation */

/* A set of strings (component names) that are currently open. */
typedef struct {
    char **items;
    int count, cap;
} StrSet;

static int set_index(const StrSet *s, const char *x)
{
    for (int i = 0; i < s->count; i++)
        if (strcmp(s->items[i], x) == 0)
            return i;
    return -1;
}

/* Returns 1 if x was added, 0 if it was already there. */
static int set_add(StrSet *s, const char *x)
{
    if (set_index(s, x) >= 0)
        return 0;
    if (s->count == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 4;
        s->items = realloc(s->items, s->cap * sizeof *s->items);
    }
    s->items[s->count++] = strdup(x);
    return 1;
}

/* Returns 1 if x was removed, 0 if it was not there. */
static int set_remove(StrSet *s, const char *x)
{
    int i = set_index(s, x);
    if (i < 0)
        return 0;
    free(s->items[i]);
    s->items[i] = s->items[--s->count];
    return 1;
}

static void set_clear(StrSet *s)
{
    for (int i = 0; i < s->count; i++)
        free(s->items[i]);
    s->count = 0;
}

/*
 * Adds up the time during which at least one component of an app is open. Two screens
 * of the same app open at once count as one interval, not two.
 */
typedef struct {
    StrSet open;
    long long window_start;
    long long since;          /* when the current interval started */
    long long total;
    long long last_closed_at;
    int saw_event;
} Interval;

static void interval_init(Interval *v, long long window_start)
{
    memset(v, 0, sizeof *v);
    v->window_start = window_start;
    v->last_closed_at = LLONG_MIN / 2;
}

/* Returns 1 if the app went from "nothing open" to "something open". */
static int interval_open(Interval *v, const char *component, long long t)
{
    v->saw_event = 1;
    int was_idle = v->open.count == 0;
    if (set_add(&v->open, component) && was_idle) {
        v->since = t;
        return 1;
    }
    return 0;
}

static void interval_close(Interval *v, const char *component, long long t)
{
    if (!v->saw_event && v->open.count == 0) {
        /* First event is a close: the component was already open when the window began. */
        v->total += t - v->window_start;
        v->last_closed_at = t;
    } else if (set_remove(&v->open, component) && v->open.count == 0) {
        v->total += t - v->since;
        v->last_closed_at = t;
    }
    v->saw_event = 1;
}

/* Anything still open runs until the end of the window. Returns the total. */
static long long interval_finish(Interval *v, long long window_end)
{
    if (v->open.count > 0) {
        v->total += window_end - v->since;
        set_clear(&v->open);
    }
    return v->total;
}

typedef struct {
    char package[256];
    Interval foreground;
    Interval service;
    int launches;
    long long last_used;
} Tracker;

static Tracker *tracker_for(Tracker *list, int *count, const char *pkg, long long window_start)
{
    for (int i = 0; i < *count; i++)
        if (strcmp(list[i].package, pkg) == 0)
            return &list[i];
    if (*count >= MAX_APPS)
        return NULL;
    Tracker *t = &list[(*count)++];
    memset(t, 0, sizeof *t);
    snprintf(t->package, sizeof t->package, "%s", pkg);
    interval_init(&t->foreground, window_start);
    interval_init(&t->service, window_start);
    return t;
}

int pm_aggregate_usage(const pm_event_t *events, int n,
                       long long window_start, long long window_end,
                       pm_app_usage_t *out, int max_out)
{
    Tracker *trackers = calloc(MAX_APPS, sizeof *trackers);
    if (!trackers)
        return -1;
    int tracker_count = 0;

    for (int i = 0; i < n; i++) {
        const pm_event_t *e = &events[i];
        Tracker *t = tracker_for(trackers, &tracker_count, e->package, window_start);
        if (!t)
            continue;
        const char *component = e->component ? e->component : "";

        switch (e->type) {
        case PM_EVENT_ACTIVITY_RESUMED: {
            int came_to_front = interval_open(&t->foreground, component, e->timestamp);
            if (came_to_front &&
                e->timestamp - t->foreground.last_closed_at > SAME_VISIT_GAP_MS)
                t->launches++;
            if (e->timestamp > t->last_used)
                t->last_used = e->timestamp;
            break;
        }
        case PM_EVENT_ACTIVITY_PAUSED:
            interval_close(&t->foreground, component, e->timestamp);
            if (e->timestamp > t->last_used)
                t->last_used = e->timestamp;
            break;
        case PM_EVENT_FGS_START:
            interval_open(&t->service, component, e->timestamp);
            break;
        case PM_EVENT_FGS_STOP:
            interval_close(&t->service, component, e->timestamp);
            break;
        default:
            break;
        }
    }

    /* Collect the apps that had any activity, then sort by total time (stable). */
    int written = 0;
    for (int i = 0; i < tracker_count; i++) {
        Tracker *t = &trackers[i];
        long long fg = interval_finish(&t->foreground, window_end);
        long long svc = interval_finish(&t->service, window_end);
        if (fg == 0 && svc == 0)
            continue;
        if (written >= max_out)
            break;
        pm_app_usage_t *u = &out[written++];
        snprintf(u->package, sizeof u->package, "%s", t->package);
        u->foreground_ms = fg;
        u->service_ms = svc;
        u->launches = t->launches;
        u->last_used = t->last_used;
    }
    for (int i = 1; i < written; i++) {          /* insertion sort: keeps ties in order */
        pm_app_usage_t key = out[i];
        long long key_total = key.foreground_ms + key.service_ms;
        int j = i - 1;
        while (j >= 0 && out[j].foreground_ms + out[j].service_ms < key_total) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }

    for (int i = 0; i < tracker_count; i++) {
        set_clear(&trackers[i].foreground.open);
        set_clear(&trackers[i].service.open);
        free(trackers[i].foreground.open.items);
        free(trackers[i].service.open.items);
    }
    free(trackers);
    return written;
}

/* ================================================================ self CPU */

int pm_self_cpu_ms(long long *user_ms, long long *kernel_ms)
{
    char buf[1024];
    if (read_text("/proc/self/stat", buf, sizeof buf) != 0)
        return -1;

    /* The process name (field 2) is in parentheses and may contain spaces, so skip
     * past the last ')'. The next field is the state (field 3), so field n is index n-3. */
    char *p = strrchr(buf, ')');
    if (!p)
        return -1;
    p += 2;

    long long utime = 0, stime = 0;
    int index = 3;
    char *save = NULL;
    for (char *tok = strtok_r(p, " ", &save); tok; tok = strtok_r(NULL, " ", &save), index++) {
        if (index == 14)
            utime = strtoll(tok, NULL, 10);
        else if (index == 15) {
            stime = strtoll(tok, NULL, 10);
            break;
        }
    }

    long hz = sysconf(_SC_CLK_TCK);
    if (hz <= 0)
        hz = 100;
    *user_ms = utime * 1000 / hz;
    *kernel_ms = stime * 1000 / hz;
    return 0;
}
