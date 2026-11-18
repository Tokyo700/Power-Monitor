/*
 * jni_bridge.c - Connects the Kotlin class com.aditya.powermonitor.nativecore.NativeCore
 * to the C functions in powermon.c.
 *
 * JNI (Java Native Interface) names each function
 *     Java_<package>_<class>_<method>
 * with dots replaced by underscores. The Kotlin side declares these as `external fun`.
 *
 * Rule of thumb used here: every jstring and jobject we create or get is released in the
 * same function, because Android allows only a limited number of local references.
 */
#include <jni.h>
#include <stdlib.h>
#include <string.h>

#include "powermon.h"


/* Builds "key=value\n" lines in a growing buffer. */
typedef struct {
    char *data;
    size_t len, cap;
} Text;

static void text_append(Text *t, const char *key, const char *value)
{
    size_t need = strlen(key) + strlen(value) + 3;
    if (t->len + need + 1 > t->cap) {
        t->cap = (t->len + need + 1) * 2;
        t->data = realloc(t->data, t->cap);
    }
    t->len += sprintf(t->data + t->len, "%s=%s\n", key, value);
}

static void text_append_int(Text *t, const char *key, long long value)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%lld", value);
    text_append(t, key, buf);
}

/* Copies a Java string to malloc'd C memory and releases the JNI reference. */
static char *dup_jstring(JNIEnv *env, jstring js)
{
    if (!js)
        return strdup("");
    const char *chars = (*env)->GetStringUTFChars(env, js, NULL);
    char *copy = strdup(chars ? chars : "");
    if (chars)
        (*env)->ReleaseStringUTFChars(env, js, chars);
    return copy;
}

/* ---------------------------------------------------------------- battery */

JNIEXPORT jstring JNICALL
Java_com_aditya_powermonitor_nativecore_NativeCore_nativeBatteryState(JNIEnv *env, jclass cls, jstring root_js)
{
    (void)cls;
    char *root = dup_jstring(env, root_js);
    pm_battery_t b;
    Text t = {0};
    int ok = pm_read_battery(root, &b);
    free(root);

    text_append_int(&t, "found", ok == 0 && b.found ? 1 : 0);
    if (ok == 0) {
        text_append_int(&t, "capacity", b.capacity_percent);
        text_append_int(&t, "charging", b.charging);
        text_append(&t, "status", b.status);
        text_append(&t, "healthText", b.health_text);
        text_append_int(&t, "hasTemp", b.has_temp);
        text_append_int(&t, "tempDeciC", b.temp_deci_c);
        text_append_int(&t, "voltageMv", b.voltage_mv);
        text_append_int(&t, "currentUa", b.current_ua);
        text_append_int(&t, "cycleCount", b.cycle_count);
        text_append_int(&t, "fullCapacity", b.full_capacity);
        text_append_int(&t, "designCapacity", b.design_capacity);
        text_append_int(&t, "healthPercent", b.health_percent);
    }

    jstring result = (*env)->NewStringUTF(env, t.data ? t.data : "");
    free(t.data);
    return result;
}

JNIEXPORT jstring JNICALL
Java_com_aditya_powermonitor_nativecore_NativeCore_nativeThermalState(JNIEnv *env, jclass cls, jstring root_js)
{
    (void)cls;
    char *root = dup_jstring(env, root_js);
    int zones = 0;
    int max = pm_max_thermal_milli_c(root, &zones);
    free(root);

    Text t = {0};
    text_append_int(&t, "maxMilliC", max);
    text_append_int(&t, "zonesRead", zones);
    jstring result = (*env)->NewStringUTF(env, t.data);
    free(t.data);
    return result;
}

/* ---------------------------------------------------------- estimation */

JNIEXPORT jlong JNICALL
Java_com_aditya_powermonitor_nativecore_NativeCore_nativeIntervalStart(JNIEnv *env, jclass cls, jlong prev, jboolean has_prev,
                                 jlong now)
{
    (void)env;
    (void)cls;
    return pm_interval_start(prev, has_prev == JNI_TRUE, now);
}

JNIEXPORT jint JNICALL
Java_com_aditya_powermonitor_nativecore_NativeCore_nativeBatteryDrop(JNIEnv *env, jclass cls, jint prev_level,
                               jboolean prev_charging, jint cur_level,
                               jboolean cur_charging)
{
    (void)env;
    (void)cls;
    return pm_battery_drop(prev_level, prev_charging == JNI_TRUE, cur_level,
                           cur_charging == JNI_TRUE);
}

JNIEXPORT jfloatArray JNICALL
Java_com_aditya_powermonitor_nativecore_NativeCore_nativeDrainShares(JNIEnv *env, jclass cls, jlongArray fg_js,
                               jlongArray svc_js, jint drop_percent)
{
    (void)cls;
    jsize n = (*env)->GetArrayLength(env, fg_js);
    jlong *fg = (*env)->GetLongArrayElements(env, fg_js, NULL);
    jlong *svc = (*env)->GetLongArrayElements(env, svc_js, NULL);

    float *shares = calloc(n > 0 ? n : 1, sizeof *shares);
    pm_drain_shares((const long long *)fg, (const long long *)svc, n, drop_percent, shares);

    (*env)->ReleaseLongArrayElements(env, fg_js, fg, JNI_ABORT);
    (*env)->ReleaseLongArrayElements(env, svc_js, svc, JNI_ABORT);

    jfloatArray result = (*env)->NewFloatArray(env, n);
    (*env)->SetFloatArrayRegion(env, result, 0, n, shares);
    free(shares);
    return result;
}

/* --------------------------------------------------------------- usage */

/*
 * Input: parallel arrays, one entry per UsageEvents event, in time order.
 * Output: one string per app, "package<TAB>foreground_ms<TAB>service_ms<TAB>launches<TAB>last_used".
 */
JNIEXPORT jobjectArray JNICALL
Java_com_aditya_powermonitor_nativecore_NativeCore_nativeAggregateUsage(JNIEnv *env, jclass cls, jobjectArray packages,
                                  jobjectArray classes, jintArray types, jlongArray times,
                                  jlong window_start, jlong window_end)
{
    (void)cls;
    jsize n = (*env)->GetArrayLength(env, packages);
    jint *type_values = (*env)->GetIntArrayElements(env, types, NULL);
    jlong *time_values = (*env)->GetLongArrayElements(env, times, NULL);

    pm_event_t *events = calloc(n > 0 ? n : 1, sizeof *events);
    for (jsize i = 0; i < n; i++) {
        jstring pkg = (jstring)(*env)->GetObjectArrayElement(env, packages, i);
        jstring cmp = (jstring)(*env)->GetObjectArrayElement(env, classes, i);
        events[i].package = dup_jstring(env, pkg);
        events[i].component = cmp ? dup_jstring(env, cmp) : strdup("");
        events[i].type = type_values[i];
        events[i].timestamp = time_values[i];
        if (pkg)
            (*env)->DeleteLocalRef(env, pkg);
        if (cmp)
            (*env)->DeleteLocalRef(env, cmp);
    }
    (*env)->ReleaseIntArrayElements(env, types, type_values, JNI_ABORT);
    (*env)->ReleaseLongArrayElements(env, times, time_values, JNI_ABORT);

    enum { MAX_OUT = 512 };
    pm_app_usage_t *apps = calloc(MAX_OUT, sizeof *apps);
    int count = pm_aggregate_usage(events, n, window_start, window_end, apps, MAX_OUT);

    jclass string_class = (*env)->FindClass(env, "java/lang/String");
    jobjectArray result = (*env)->NewObjectArray(env, count > 0 ? count : 0, string_class, NULL);
    for (int i = 0; i < count; i++) {
        char line[512];
        snprintf(line, sizeof line, "%s\t%lld\t%lld\t%d\t%lld", apps[i].package,
                 apps[i].foreground_ms, apps[i].service_ms, apps[i].launches,
                 apps[i].last_used);
        jstring js = (*env)->NewStringUTF(env, line);
        (*env)->SetObjectArrayElement(env, result, i, js);
        (*env)->DeleteLocalRef(env, js);
    }

    for (jsize i = 0; i < n; i++) {
        free((void *)events[i].package);
        free((void *)events[i].component);
    }
    free(events);
    free(apps);
    return result;
}

/* ---------------------------------------------------------- self CPU */

JNIEXPORT jstring JNICALL
Java_com_aditya_powermonitor_nativecore_NativeCore_nativeSelfCpu(JNIEnv *env, jclass cls)
{
    (void)cls;
    long long user = 0, kernel = 0;
    int ok = pm_self_cpu_ms(&user, &kernel);

    Text t = {0};
    text_append_int(&t, "readable", ok == 0 ? 1 : 0);
    text_append_int(&t, "userMs", user);
    text_append_int(&t, "kernelMs", kernel);
    jstring result = (*env)->NewStringUTF(env, t.data);
    free(t.data);
    return result;
}
