cmake_minimum_required(VERSION 3.13.1)

# Application devicetree overlay: enable the BLE controller node under &radio
# (see app.overlay) and disable &ieee802154 to free the RADIO.
set(DTS_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/app.overlay" CACHE FILEPATH "devicetree overlay" FORCE)

include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(m20b)

FILE(GLOB app_sources ../src/*.c*)
target_sources(app PRIVATE ${app_sources})

# LED status header lives in ../include.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include)
