cmake_minimum_required(VERSION 3.13.1)

# Application devicetree overlay: adds regulator-boot-on to the nPM1300
# LDO1 (imu_vdd) rail, fixing the IMU being "not ready" because its power
# rail is not up during driver init.
set(DTS_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/app.overlay" CACHE FILEPATH "devicetree overlay" FORCE)

include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(m20a)

FILE(GLOB app_sources ../src/*.c*)
target_sources(app PRIVATE ${app_sources})
