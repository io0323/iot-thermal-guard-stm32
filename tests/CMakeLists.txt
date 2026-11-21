# テスト：単体（Unity、手書きのフェイク）、ゴールデン、仮想時間のシナリオ（sim_harness）。CMock はまだ使わない。

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
tg_unit_test(test_tick unit/test_tick.c)
tg_unit_test(test_ntc_table unit/test_ntc_table.c)
tg_unit_test(test_median unit/test_median.c)
tg_unit_test(test_moving_avg unit/test_moving_avg.c)
# service
tg_unit_test(test_link unit/test_link.c)
tg_unit_test(test_upstream unit/test_upstream.c)
tg_unit_test(test_scheduler unit/test_scheduler.c)
tg_unit_test(test_report unit/test_report.c)
tg_unit_test(test_boot_id unit/test_boot_id.c)
tg_unit_test(test_temp_sensor unit/test_temp_sensor.c)
tg_unit_test(test_button unit/test_button.c)
tg_unit_test(test_health unit/test_health.c)
tg_unit_test(test_diag unit/test_diag.c)
# app
tg_unit_test(test_command_handler unit/test_command_handler.c)
tg_unit_test(test_cooling_policy unit/test_cooling_policy.c)
tg_unit_test(test_safety_fsm unit/test_safety_fsm.c)
tg_unit_test(test_event_map unit/test_event_map.c)
tg_unit_test(test_alarm unit/test_alarm.c)
# main（firmware.c の配線をフェイクのポートで確かめる）
tg_unit_test(test_firmware unit/test_firmware.c)

# ゴールデン：external/iot-common-spec/uart/golden/*.json → build/generated/golden_vectors.h
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

# 仮想時間のシナリオ：app・service・firmware をフェイクのポートでつなぎ、1ms ずつ進める
add_library(tg_sim_harness STATIC scenario/sim_harness.c)
target_include_directories(tg_sim_harness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/scenario)
target_link_libraries(tg_sim_harness PUBLIC tg_fakes tg_firmware tg_app tg_service tg_lib tg_host_sim)
target_compile_definitions(tg_sim_harness PRIVATE _DEFAULT_SOURCE _DARWIN_C_SOURCE)
tg_warnings(tg_sim_harness)

tg_unit_test(test_scenario_csv scenario/test_scenario_csv.c)
target_link_libraries(test_scenario_csv PRIVATE tg_host_sim)
target_compile_definitions(test_scenario_csv PRIVATE SCENARIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scenario/data")

add_executable(test_scenarios scenario/test_scenarios.c)
target_link_libraries(test_scenarios PRIVATE unity tg_sim_harness tg_config)
tg_warnings(test_scenarios)
add_test(NAME test_scenarios COMMAND test_scenarios)
target_compile_definitions(test_scenarios PRIVATE SCENARIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scenario/data")
