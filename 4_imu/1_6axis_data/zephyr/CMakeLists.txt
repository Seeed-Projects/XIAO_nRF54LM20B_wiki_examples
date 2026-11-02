cmake_minimum_required(VERSION 3.13.1)

# Application devicetree overlay: add regulator-boot-on to nPM1300 LDO1 (imu_vdd),
# fixing the IMU "not ready" issue caused by the supply not being ready during
# the driver init stage.
set(DTS_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/app.overlay" CACHE FILEPATH "devicetree overlay" FORCE)

include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(m20b)

FILE(GLOB app_sources ../src/*.c*)
target_sources(app PRIVATE ${app_sources})
