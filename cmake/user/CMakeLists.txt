cmake_minimum_required(VERSION 3.22)

# User include paths
set(User_Include_Dirs
    ${CMAKE_CURRENT_SOURCE_DIR}/../../LCD
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../CJSON
    ${CMAKE_CURRENT_SOURCE_DIR}/../../MQTT
    ${CMAKE_CURRENT_SOURCE_DIR}/../../onenet/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LVGL
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LVGL/src
)

set(LVGL_ROOT
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LVGL
)

file(GLOB LVGL_CORE_SOURCES
    "${LVGL_ROOT}/src/core/*.c"
    "${LVGL_ROOT}/src/draw/*.c"
    "${LVGL_ROOT}/src/draw/sw/*.c"
    "${LVGL_ROOT}/src/hal/*.c"
    "${LVGL_ROOT}/src/misc/*.c"
    "${LVGL_ROOT}/src/widgets/*.c"
)

set(LVGL_EXTRA_SOURCES
    "${LVGL_ROOT}/src/extra/lv_extra.c"
)

set(LVGL_FONT_SOURCES
    "${LVGL_ROOT}/src/font/lv_font.c"
    "${LVGL_ROOT}/src/font/lv_font_fmt_txt.c"
    "${LVGL_ROOT}/src/font/lv_font_montserrat_14.c"
    "${LVGL_ROOT}/src/font/lv_font_montserrat_16.c"
    "${LVGL_ROOT}/src/font/lv_font_montserrat_20.c"
)

# User sources
set(User_Sources
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/firmware_version.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/ina226_pv.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/ina226_3.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/esp8266_at.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/esp8266_udp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/esp8266_onenet_at.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/esp8266_onenet.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/ota_update.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/lv_port_energy.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/energy_lvgl_ui.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../MQTT/MqttKit.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../onenet/src/base64.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../onenet/src/hmac_sha1.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../CJSON/cJSON.c
    ${LVGL_CORE_SOURCES}
    ${LVGL_EXTRA_SOURCES}
    ${LVGL_FONT_SOURCES}
)

# Interface library for user code
add_library(user_code INTERFACE)
target_include_directories(user_code INTERFACE ${User_Include_Dirs})
target_compile_definitions(user_code INTERFACE LV_CONF_INCLUDE_SIMPLE)

# Add user sources to the project executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${User_Sources})

# Link user_code interface to project
target_link_libraries(${CMAKE_PROJECT_NAME} user_code)
