/*
 * test_powermon.c - Tests for the C core. Run on your computer, not on the phone:
 *
 *     cd app/src/main/cpp/tests && ./run_tests.sh
 *
 * Each CHECK prints PASS or FAIL. The program exits with 1 if any check fails.
 */
#include <stdio.h>
#include <string.h>

#include "../powermon.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                           \
    do {                                                                      \
        checks++;                                                             \
        if (cond) {                                                           \
            printf("PASS  %s\n", #cond);                                      \
        } else {                                                              \
            failures++;                                                       \
            printf("FAIL  %s  (line %d)\n", #cond, __LINE__);                 \
        }                                                                     \
    } while (0)

/* ---- drain estimation ---- */

static void test_drain_rules(void)
{
    CHECK(pm_battery_drop(80, 0, 77, 0) == 3);
    CHECK(pm_battery_drop(80, 1, 77, 0) == 0);     /* charging at start */
    CHECK(pm_battery_drop(80, 0, 77, 1) == 0);     /* charging at end */
    CHECK(pm_battery_drop(50, 0, 52, 0) == 0);     /* level went up: no drop */

    long long now = 1000000000LL;
    CHECK(pm_interval_start(0, 0, now) == now - 15LL * 60 * 1000);
    CHECK(pm_interval_start(now - 60000, 1, now) == now - 60000);
    CHECK(pm_interval_start(now - 7LL * 60 * 60 * 1000, 1, now) == now - 15LL * 60 * 1000);
    CHECK(pm_interval_start(now + 1, 1, now) == now - 15LL * 60 * 1000);

    /* Shares split the drop in proportion to fg + 0.3 * service. */
    long long fg[] = {30, 10};
    long long svc[] = {0, 0};
    float out[2];
    pm_drain_shares(fg, svc, 2, 4, out);
    CHECK(out[0] == 3.0f && out[1] == 1.0f);

    long long fg2[] = {10, 0};
    long long svc2[] = {0, 10};
    pm_drain_shares(fg2, svc2, 2, 13, out);
    CHECK(out[0] == 10.0f && out[1] == 3.0f);

    pm_drain_shares(fg, svc, 2, 0, out);
    CHECK(out[0] == 0.0f && out[1] == 0.0f);

    long long idle[] = {0};
    pm_drain_shares(idle, idle, 1, 4, out);
    CHECK(out[0] == 0.0f);
}

/* ---- battery health ---- */

static void test_health(void)
{
    CHECK(pm_health_percent(3917000, 7394000) == 53);
    CHECK(pm_health_percent(4000, 4000) == 100);
    CHECK(pm_health_percent(0, 4000) == -1);
    CHECK(pm_health_percent(4000, -1) == -1);
}

/* ---- usage aggregation (ported from the old Kotlin UsageAggregator tests) ---- */

static pm_event_t ev(const char *pkg, const char *cls, int type, long long t)
{
    pm_event_t e = {pkg, cls, type, t};
    return e;
}

static void test_usage(void)
{
    pm_app_usage_t out[16];

    /* One app open from 10s to 40s in a 0..60s window. */
    pm_event_t a[] = {
        ev("app.a", "Main", PM_EVENT_ACTIVITY_RESUMED, 10000),
        ev("app.a", "Main", PM_EVENT_ACTIVITY_PAUSED, 40000),
    };
    int n = pm_aggregate_usage(a, 2, 0, 60000, out, 16);
    CHECK(n == 1 && out[0].foreground_ms == 30000 && out[0].launches == 1);

    /* Two screens open at once count as one interval (no double counting). */
    pm_event_t b[] = {
        ev("app.a", "ScreenA", PM_EVENT_ACTIVITY_RESUMED, 0),
        ev("app.a", "ScreenB", PM_EVENT_ACTIVITY_RESUMED, 5000),
        ev("app.a", "ScreenA", PM_EVENT_ACTIVITY_PAUSED, 20000),
        ev("app.a", "ScreenB", PM_EVENT_ACTIVITY_PAUSED, 30000),
    };
    n = pm_aggregate_usage(b, 4, 0, 60000, out, 16);
    CHECK(n == 1 && out[0].foreground_ms == 30000);

    /* Switching between screens of one app quickly is not a new launch. */
    pm_event_t c[] = {
        ev("app.a", "Main", PM_EVENT_ACTIVITY_RESUMED, 0),
        ev("app.a", "Main", PM_EVENT_ACTIVITY_PAUSED, 10000),
        ev("app.a", "Main", PM_EVENT_ACTIVITY_RESUMED, 10500),
    };
    n = pm_aggregate_usage(c, 3, 0, 60000, out, 16);
    CHECK(n == 1 && out[0].launches == 1);

    /* Still open at the end of the window: counts until the window end. */
    pm_event_t d[] = {ev("app.a", "Main", PM_EVENT_ACTIVITY_RESUMED, 50000)};
    n = pm_aggregate_usage(d, 1, 0, 60000, out, 16);
    CHECK(n == 1 && out[0].foreground_ms == 10000);

    /* Already open when the window started: counts from the window start. */
    pm_event_t e[] = {ev("app.a", "Main", PM_EVENT_ACTIVITY_PAUSED, 15000)};
    n = pm_aggregate_usage(e, 1, 0, 60000, out, 16);
    CHECK(n == 1 && out[0].foreground_ms == 15000);

    /* Foreground-service time is tracked separately; apps are sorted by total time. */
    pm_event_t f[] = {
        ev("small", "M", PM_EVENT_ACTIVITY_RESUMED, 0),
        ev("small", "M", PM_EVENT_ACTIVITY_PAUSED, 1000),
        ev("big", "S", PM_EVENT_FGS_START, 0),
        ev("big", "S", PM_EVENT_FGS_STOP, 50000),
    };
    n = pm_aggregate_usage(f, 4, 0, 60000, out, 16);
    CHECK(n == 2);
    CHECK(strcmp(out[0].package, "big") == 0 && out[0].service_ms == 50000);
    CHECK(strcmp(out[1].package, "small") == 0 && out[1].foreground_ms == 1000);

    /* An app with no activity is left out. */
    pm_event_t g[] = {ev("ghost", "G", 99, 0)};
    n = pm_aggregate_usage(g, 1, 0, 60000, out, 16);
    CHECK(n == 0);
}

/* ---- reading this laptop's real sensors (skipped if the folder is missing) ---- */

static void test_real_sensors(void)
{
    pm_battery_t b;
    int rc = pm_read_battery("/sys/class/power_supply", &b);
    if (rc == 0) {
        printf("INFO  battery: %s, %d%%, health %d%%, cycles %d, temp %s\n", b.status,
               b.capacity_percent, b.health_percent, b.cycle_count,
               b.has_temp ? "known" : "unknown");
        CHECK(b.capacity_percent >= 0 && b.capacity_percent <= 100);
    } else {
        printf("INFO  no battery under /sys/class/power_supply (skipped)\n");
    }

    int zones = 0;
    int max = pm_max_thermal_milli_c("/sys/class/thermal", &zones);
    printf("INFO  thermal zones readable: %d, hottest: %d m°C\n", zones, max);

    long long user = -1, kernel = -1;
    CHECK(pm_self_cpu_ms(&user, &kernel) == 0 && user >= 0 && kernel >= 0);
}

int main(void)
{
    test_drain_rules();
    test_health();
    test_usage();
    test_real_sensors();

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
