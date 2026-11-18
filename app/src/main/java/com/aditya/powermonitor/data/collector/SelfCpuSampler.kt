package com.aditya.powermonitor.data.collector

import android.os.Process
import android.os.SystemClock
import com.aditya.powermonitor.nativecore.NativeCore
import java.io.File

/** CPU usage of our own process, plus a check of what the OS still lets us read. */
data class CpuSample(
    /** CPU time this process has used since it started (user + kernel). */
    val processCpuMs: Long,
    /** Wall-clock time since this process started. */
    val processUptimeMs: Long,
    /** processCpuMs / (uptime × cores): average share of the whole CPU we've used. */
    val averageCpuPercent: Float,
    val cores: Int,
    /** utime and stime from /proc/self/stat, converted from clock ticks to ms. */
    val userMs: Long?,
    val kernelMs: Long?,
    /** Whether system-wide /proc/stat is readable. Android 8+ blocks it for normal apps. */
    val canReadSystemProcStat: Boolean,
    /** Whether another process's stat (init, pid 1) is readable. Also blocked. */
    val canReadOtherProcess: Boolean,
)

class SelfCpuSampler {

    fun sample(): CpuSample {
        val cpuMs = Process.getElapsedCpuTime()
        val uptimeMs = SystemClock.elapsedRealtime() - Process.getStartElapsedRealtime()
        val cores = Runtime.getRuntime().availableProcessors()
        val (userMs, kernelMs) = readSelfStat()
        return CpuSample(
            processCpuMs = cpuMs,
            processUptimeMs = uptimeMs,
            averageCpuPercent =
                if (uptimeMs > 0) cpuMs * 100f / (uptimeMs * cores) else 0f,
            cores = cores,
            userMs = userMs,
            kernelMs = kernelMs,
            canReadSystemProcStat = canRead("/proc/stat"),
            canReadOtherProcess = canRead("/proc/1/stat"),
        )
    }

    /**
     * Reads user and kernel CPU time from /proc/self/stat. The parsing is in C
     * (pm_self_cpu_ms in powermon.c), which explains the tick and ')' handling there.
     */
    private fun readSelfStat(): Pair<Long?, Long?> = NativeCore.selfCpuMs()

    private fun canRead(path: String): Boolean = try {
        File(path).readText().isNotEmpty()
    } catch (e: Exception) {
        false
    }
}
