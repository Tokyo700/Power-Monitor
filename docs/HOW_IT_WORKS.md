# How the Power Monitor works

This document explains the whole app: what each part does, why it is split between Kotlin
and C, and how one logging run flows from the phone's sensors to the screen.

## 1. The big picture

```
 ┌──────────────────────── Android app (Kotlin) ────────────────────────┐
 │  UI (Compose)   WorkManager (every 15 min)   Room database (SQLite)   │
 │       │                 │                          ▲                  │
 │       ▼                 ▼                          │                  │
 │   ViewModels ──► MonitorRepository ────────────────┘                  │
 │                    │          │                                       │
 │   Android APIs     │          │  (things only Java/Kotlin can call)   │
 │   BatteryManager   │          │  UsageStatsManager, WorkManager, Room │
 └────────────────────┼──────────┼──────────────────────────────────────┘
                      │ JNI      │ JNI
 ┌────────────────────▼──────────▼──────────── C core (app/src/main/cpp) ┐
 │  NativeCore.kt ──► jni_bridge.c ──► powermon.c                        │
 │                                       • read /sys/class/power_supply  │
 │                                       • read /sys/class/thermal       │
 │                                       • read /proc/self/stat          │
 │                                       • battery drop and drain shares │
 │                                       • usage aggregation             │
 └───────────────────────────────────────────────────────────────────────┘
```

**Rule of thumb:** anything that needs Android's Java APIs stays in Kotlin. Everything
that reads files or does arithmetic lives in C.

| Job | Where | Why there |
|---|---|---|
| Show screens, navigation | Kotlin (Compose) | UI toolkit is Java/Kotlin only |
| Run every 15 minutes | Kotlin (WorkManager) | Android scheduling API |
| Save history | Kotlin (Room) | Database library is Java/Kotlin only |
| Get the usage event stream | Kotlin (`UsageStatsManager`) | Android API, Java only |
| Read battery health, cycles, temperature from `/sys` | **C** | plain file reads |
| Read hottest thermal zone | **C** | plain file reads |
| Read this app's own CPU time from `/proc/self/stat` | **C** | plain file read and parsing |
| Battery drop and interval rules | **C** | pure arithmetic |
| Split drain between apps | **C** | pure arithmetic |
| Turn usage events into per-app totals | **C** | the main algorithm |

## 2. What is C, and what JNI does

- **C core** (`app/src/main/cpp/powermon.c`, `powermon.h`): plain C functions. They know
  nothing about Android, so they compile on any Linux computer.
- **JNI bridge** (`jni_bridge.c`): JNI is the Java Native Interface, the standard way for
  Java/Kotlin code to call C. Each C function is named
  `Java_<package>_<class>_<method>`. The bridge converts Java strings and arrays to C
  values, calls the C core, and converts the result back.
- **Kotlin wrapper** (`nativecore/NativeCore.kt`): declares the functions as `external`,
  loads `libpowermon.so` with `System.loadLibrary`, and offers friendly functions such as
  `batteryDrop(...)` and `aggregateUsage(...)` to the rest of the app.
- **Build**: `app/build.gradle.kts` points Gradle at `src/main/cpp/CMakeLists.txt`. The
  Android NDK compiles the C code into `libpowermon.so` for the phone's processor.

**Why text (`key=value` lines) instead of many JNI functions?** Passing a whole block of
text back keeps the bridge small. Adding a new field only means adding one line in C and
one line of parsing in Kotlin.

## 3. The battery: what is read and how

Android's `BatteryManager` gives the level, status, and plug type. The kernel also exposes
a richer set of values in `/sys/class/power_supply/<battery>/`. The C core scans that folder
and finds the entry whose `type` file says `Battery`. This is the same way it works on a
Linux laptop, which is how the code was tested.

| File | Meaning | Used for |
|---|---|---|
| `capacity` | state of charge, % | level (fallback) |
| `status` | Charging, Discharging, Full | charging flag |
| `temp` | temperature in tenths of °C | temperature (preferred) |
| `voltage_now` | µV | voltage |
| `current_now` | µA | current |
| `cycle_count` | charge cycles | cycles |
| `charge_full` and `charge_full_design` | charge when full, and when new (µAh) | **health %** |
| `energy_full` and `energy_full_design` | same, in energy (µWh), used by some phones | health % fallback |

**Battery health** = full ÷ design × 100, rounded (`pm_health_percent`). On this laptop the
values give 3 917 000 ÷ 7 394 000 = **53%**, which matches the cycle count of 323.

**Temperature** comes from two places:
1. The battery's `temp` file (preferred, when readable).
2. The hottest thermal zone under `/sys/class/thermal/thermal_zone*/temp` (millidegrees).

Many phones block apps from reading thermal zones (SELinux). When that happens the C core
returns "not readable", and the app shows "not readable" rather than a wrong number.

## 4. One logging run, step by step

WorkManager runs `LoggingWorker` every 15 minutes, which calls `MonitorRepository.logSnapshot()`.

1. **Choose the window.** `NativeCore.intervalStart(previous, now)` → `pm_interval_start`.
   The window starts at the previous snapshot, so intervals never overlap. If there is no
   recent snapshot (or the clock jumped), it uses the last 15 minutes.
2. **Read the battery.** `BatteryCollector.read()` combines the Android values with the C
   values (health, cycles, temperature).
3. **Read usage events.** `UsageCollector` asks `UsageStatsManager` for every event in the
   window and copies them into parallel arrays (package, activity, type, time).
4. **Aggregate in C.** `pm_aggregate_usage` walks the events in order:
   - "Resumed" (`ACTIVITY_RESUMED`) opens a foreground interval; "Paused" closes it.
   - Foreground-service start and stop do the same for background work.
   - Several screens of one app open at once count as **one** interval (no double counting).
   - Reopening within 2 seconds of closing is the same visit, not a new launch.
   - Anything still open at the end runs until the window ends.
   Apps are returned sorted by total active time.
5. **Estimate drain.** `NativeCore.batteryDrop` gives the percentage lost (0 while
   charging). `NativeCore.drainShares` splits that loss between apps in proportion to
   `foreground + 0.3 × service` time (`pm_drain_shares`).
6. **Save** the battery reading and one row per app in one database transaction, so both
   succeed or neither does.

## 5. The C functions, one by one

| Function (powermon.c) | What it does |
|---|---|
| `pm_read_battery` | Finds the battery folder and reads the files above |
| `pm_health_percent` | full ÷ design × 100, rounded |
| `pm_max_thermal_milli_c` | Hottest thermal zone, in millidegrees |
| `pm_battery_drop` | Level lost between two readings, 0 if charging |
| `pm_interval_start` | Start of the next window |
| `pm_drain_shares` | Splits a drop between apps |
| `pm_aggregate_usage` | Turns the event stream into per-app totals |
| `pm_self_cpu_ms` | This process's CPU time from `/proc/self/stat` |

**Operating-system ideas used here**
- `/sys` and `/proc` are the kernel's interface: reading a file gives you kernel data.
- `opendir` / `readdir` walk a directory, like `ls`.
- Events in time order are processed with an "open set", the same bookkeeping an OS uses
  when several things can be running at once.
- `/proc/self/stat` splits CPU time into user and kernel time, which is how the OS
  accounts for CPU use per process.

## 6. Testing

- **C tests (run on your computer, no phone needed):**
  ```bash
  cd app/src/main/cpp/tests && ./run_tests.sh
  ```
  27 checks cover the drain rules, health, usage aggregation (including the edge cases that
  the old Kotlin tests covered), and the real sensors of the computer running the tests.
- **Kotlin unit tests:** `./gradlew test` (CSV and history-filter tests).
- **On the phone:** the debug screen shows the C values under "Health (C core)", "Cycle
  count", and "Hottest thermal zone". "not readable" means the phone blocked that file.

## 7. Limits to know when presenting

- The usage stream needs the **Usage access** permission, which the app already asks for.
- Drain per app is an **estimate**. Android does not report per-app battery use.
- Thermal and some battery files may be blocked on a particular phone; the app reports
  "not readable" in that case.
- The C core is built by the NDK, so the phone build needs the NDK installed (Android
  Studio: SDK Manager → SDK Tools → NDK (Side by side) and CMake).
