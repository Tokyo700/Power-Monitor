package com.aditya.powermonitor.nativecore

/**
 * Kotlin side of the C core in app/src/main/cpp. Every calculation and every /sys and
 * /proc read the app does goes through here. The `external` functions are implemented in
 * C (jni_bridge.c) and call powermon.c.
 *
 * The C side returns text as "key=value" lines, and the wrappers below turn those into
 * values. Text is used instead of many separate calls so that adding a field later only
 * means adding a line in C.
 */
object NativeCore {

    private const val POWER_SUPPLY_ROOT = "/sys/class/power_supply"
    private const val THERMAL_ROOT = "/sys/class/thermal"

    init {
        // Loads libpowermon.so, built by CMake from app/src/main/cpp.
        System.loadLibrary("powermon")
    }

    // ---- raw native functions (implemented in jni_bridge.c) ----

    private external fun nativeBatteryState(root: String): String
    private external fun nativeThermalState(root: String): String
    private external fun nativeIntervalStart(prev: Long, hasPrev: Boolean, now: Long): Long
    private external fun nativeBatteryDrop(
        prevLevel: Int,
        prevCharging: Boolean,
        curLevel: Int,
        curCharging: Boolean,
    ): Int
    private external fun nativeDrainShares(
        foregroundMs: LongArray,
        serviceMs: LongArray,
        dropPercent: Int,
    ): FloatArray
    private external fun nativeAggregateUsage(
        packages: Array<String?>,
        classes: Array<String?>,
        types: IntArray,
        times: LongArray,
        windowStart: Long,
        windowEnd: Long,
    ): Array<String>
    private external fun nativeSelfCpu(): String

    // ---- battery and temperature ----

    /**
     * What the kernel says about the battery, read from /sys/class/power_supply.
     * Keys: found, capacity, charging, status, healthText, hasTemp, tempDeciC,
     * voltageMv, currentUa, cycleCount, fullCapacity, designCapacity, healthPercent.
     * Returns an empty map if the battery folder can't be read.
     */
    fun batteryState(): Map<String, String> = parse(nativeBatteryState(POWER_SUPPLY_ROOT))

    /** Hottest thermal zone in °C, or null if the phone doesn't let apps read any. */
    fun hottestZoneC(): Float? {
        val values = parse(nativeThermalState(THERMAL_ROOT))
        val milliC = values["maxMilliC"]?.toIntOrNull() ?: return null
        return if (milliC < 0) null else milliC / 1000f
    }

    // ---- estimation rules ----

    /** Start of the next logging interval; see powermon.c pm_interval_start. */
    fun intervalStart(previousTimestamp: Long?, now: Long): Long =
        nativeIntervalStart(previousTimestamp ?: 0L, previousTimestamp != null, now)

    /** Percentage points lost between two readings; 0 if charging at either end. */
    fun batteryDrop(
        previousLevel: Int,
        previousCharging: Boolean,
        currentLevel: Int,
        currentCharging: Boolean,
    ): Int = nativeBatteryDrop(previousLevel, previousCharging, currentLevel, currentCharging)

    /** Each app's share of [dropPercent]; one entry per input app, in the same order. */
    fun drainShares(
        foregroundMs: LongArray,
        serviceMs: LongArray,
        dropPercent: Int,
    ): FloatArray = nativeDrainShares(foregroundMs, serviceMs, dropPercent)

    // ---- usage ----

    /** One raw UsageEvents entry, as parallel arrays for the C side. */
    class UsageEvents {
        val packages = ArrayList<String?>()
        val classes = ArrayList<String?>()
        val types = ArrayList<Int>()
        val times = ArrayList<Long>()

        fun add(packageName: String?, className: String?, type: Int, timestamp: Long) {
            packages += packageName
            classes += className
            types += type
            times += timestamp
        }
    }

    /** One app's totals as the C side reports them. */
    data class UsageLine(
        val packageName: String,
        val foregroundMs: Long,
        val serviceMs: Long,
        val launches: Int,
        val lastUsed: Long,
    )

    /**
     * Turns time-ordered events into per-app totals for [windowStart, windowEnd), sorted
     * by total active time (highest first). Logic is in pm_aggregate_usage.
     */
    fun aggregateUsage(events: UsageEvents, windowStart: Long, windowEnd: Long): List<UsageLine> {
        val lines = nativeAggregateUsage(
            events.packages.toTypedArray(),
            events.classes.toTypedArray(),
            events.types.toIntArray(),
            events.times.toLongArray(),
            windowStart,
            windowEnd,
        )
        return lines.map { line ->
            val f = line.split('\t')
            UsageLine(
                packageName = f[0],
                foregroundMs = f[1].toLong(),
                serviceMs = f[2].toLong(),
                launches = f[3].toInt(),
                lastUsed = f[4].toLong(),
            )
        }
    }

    // ---- this app's own CPU ----

    /** (user, kernel) CPU milliseconds of this process, or (null, null) if unreadable. */
    fun selfCpuMs(): Pair<Long?, Long?> {
        val values = parse(nativeSelfCpu())
        if (values["readable"] != "1") return null to null
        return values["userMs"]?.toLongOrNull() to values["kernelMs"]?.toLongOrNull()
    }

    /** Turns "key=value" lines into a map. */
    private fun parse(text: String): Map<String, String> =
        text.lineSequence()
            .mapNotNull { line ->
                val eq = line.indexOf('=')
                if (eq > 0) line.substring(0, eq) to line.substring(eq + 1) else null
            }
            .toMap()
}
