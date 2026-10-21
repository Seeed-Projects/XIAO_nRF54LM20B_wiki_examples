cmake_minimum_required(VERSION 3.13.1)

# Application-layer devicetree overlay: add regulator-boot-on to the nPM1300
# LDO1 (imu_vdd), fixing an IMU "not ready" issue at driver init caused by
# power not being ready.
set(DTS_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/app.overlay" CACHE FILEPATH "devicetree overlay" FORCE)

include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(m20b)

FILE(GLOB app_sources ../src/*.c*)
target_sources(app PRIVATE ${app_sources})
