# テスト：単体（Unity、手書きのフェイク）とゴールデン。CMock は P1 では使わない。

set(UNITY_DIR ${CMAKE_SOURCE_DIR}/third_party/unity)
add_library(unity STATIC ${UNITY_DIR}/src/unity.c)
target_include_directories(unity PUBLIC ${UNITY_DIR}/src)
target_compile_definitions(unity PUBLIC UNITY_EXCLUDE_FLOAT UNITY_EXCLUDE_DOUBLE)

add_library(tg_fakes STATIC fakes/fake_ports.c)
target_include_directories(tg_fakes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/fakes)
target_link_libraries(tg_fakes PUBLIC tg_ports)
tg_warnings(tg_fakes)

function(tg_unit_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE unity tg_fakes tg_firmware tg_app tg_service tg_lib tg_config)
    tg_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# lib
tg_unit_test(test_cobs unit/test_cobs.c)
tg_unit_test(test_crc unit/test_crc.c)
tg_unit_test(test_ring_buffer unit/test_ring_buffer.c)
tg_unit_test(test_byteorder unit/test_byteorder.c)
# service
tg_unit_test(test_link unit/test_link.c)
tg_unit_test(test_upstream unit/test_upstream.c)
tg_unit_test(test_scheduler unit/test_scheduler.c)
tg_unit_test(test_report unit/test_report.c)
tg_unit_test(test_boot_id unit/test_boot_id.c)
tg_unit_test(test_temp_sensor unit/test_temp_sensor.c)
# app
tg_unit_test(test_command_handler unit/test_command_handler.c)
tg_unit_test(test_cooling_policy unit/test_cooling_policy.c)
tg_unit_test(test_safety_fsm unit/test_safety_fsm.c)
# main（firmware.c の配線をフェイクのポートで確かめる）
tg_unit_test(test_firmware unit/test_firmware.c)

# ゴールデン：external/iot-common-spec/uart/golden/*.json → build/generated/golden_vectors.h
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GOLDEN_DIR ${CMAKE_SOURCE_DIR}/external/iot-common-spec/uart/golden)
set(GOLDEN_HEADER ${CMAKE_BINARY_DIR}/generated/golden_vectors.h)
file(GLOB GOLDEN_JSON CONFIGURE_DEPENDS ${GOLDEN_DIR}/*.json)
add_custom_command(
    OUTPUT ${GOLDEN_HEADER}
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/gen_golden_vectors.py ${GOLDEN_DIR} ${GOLDEN_HEADER}
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/gen_golden_vectors.py ${GOLDEN_JSON}
    COMMENT "ゴールデンのテストベクタを生成"
    VERBATIM
)
add_custom_target(golden_vectors DEPENDS ${GOLDEN_HEADER})
tg_unit_test(test_golden golden/test_golden.c ${GOLDEN_HEADER})
add_dependencies(test_golden golden_vectors)
target_include_directories(test_golden PRIVATE ${CMAKE_BINARY_DIR}/generated)
