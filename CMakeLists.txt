cmake_minimum_required(VERSION 3.25)
project(iot_thermal_guard_stm32 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(warnings)
include(sanitizers)

# 依存の向き：app → service → lib、app・service → ports。逆向きのリンクは作らない。

add_library(tg_ports INTERFACE)
target_include_directories(tg_ports INTERFACE ${CMAKE_SOURCE_DIR}/ports)

add_library(tg_config INTERFACE)
target_include_directories(tg_config INTERFACE ${CMAKE_SOURCE_DIR}/config)

add_library(tg_lib STATIC
    lib/cobs.c
    lib/crc16.c
    lib/crc32.c
    lib/ring_buffer.c
)
target_include_directories(tg_lib PUBLIC ${CMAKE_SOURCE_DIR}/lib)
tg_warnings(tg_lib)

add_library(tg_service STATIC
    service/boot_id.c
    service/link.c
    service/link_frame.c
    service/report.c
    service/scheduler.c
    service/temp_sensor.c
    service/upstream.c
)
target_include_directories(tg_service PUBLIC ${CMAKE_SOURCE_DIR}/service)
target_link_libraries(tg_service PUBLIC tg_lib tg_ports)
tg_warnings(tg_service)

add_library(tg_app STATIC
    app/command_handler.c
    app/cooling_policy.c
    app/safety_fsm.c
)
target_include_directories(tg_app PUBLIC ${CMAKE_SOURCE_DIR}/app)
target_link_libraries(tg_app PUBLIC tg_service tg_ports PRIVATE tg_config)
tg_warnings(tg_app)

# main/firmware.c：タスクの登録と配線（両方の bsp で共通）
add_library(tg_firmware STATIC main/firmware.c)
target_include_directories(tg_firmware PUBLIC ${CMAKE_SOURCE_DIR}/main)
target_link_libraries(tg_firmware PUBLIC tg_app tg_service tg_ports PRIVATE tg_config)
tg_warnings(tg_firmware)

# bsp_host：ホスト版の実行ファイル build/host/mcu_c_01（契約テストが前提にしている名前）
add_library(tg_bsp_host STATIC
    bsp/host/bsp_host.c
    bsp/host/host_log.c
)
target_include_directories(tg_bsp_host PUBLIC ${CMAKE_SOURCE_DIR}/bsp/host)
target_link_libraries(tg_bsp_host PUBLIC tg_firmware tg_lib tg_ports)
target_compile_definitions(tg_bsp_host PRIVATE _DEFAULT_SOURCE _DARWIN_C_SOURCE)
tg_warnings(tg_bsp_host)
find_library(MATH_LIB m)
if(MATH_LIB)
    target_link_libraries(tg_bsp_host PRIVATE ${MATH_LIB})
endif()

add_executable(mcu_c_01 bsp/host/main.c)
target_link_libraries(mcu_c_01 PRIVATE tg_bsp_host)
target_compile_definitions(mcu_c_01 PRIVATE _DEFAULT_SOURCE _DARWIN_C_SOURCE)
set_target_properties(mcu_c_01 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
tg_warnings(mcu_c_01)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
