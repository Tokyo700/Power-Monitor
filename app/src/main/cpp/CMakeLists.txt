# Builds the C core into libpowermon.so, which the Android app loads at startup.
# Android Gradle runs this file automatically through the NDK (see app/build.gradle.kts).
cmake_minimum_required(VERSION 3.22)
project(powermon C)

add_library(powermon SHARED
    powermon.c
    jni_bridge.c
)

target_compile_options(powermon PRIVATE -Wall -Wextra)
