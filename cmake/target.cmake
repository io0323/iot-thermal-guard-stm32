# ターゲット版の実行ファイル build/target-*/mcu_c_01.elf（P5、ADR-0004・ADR-0005）。
# CMakeLists.txt が TG_TARGET のときだけ読む。app・service・lib・main はホスト版と同じライブラリをそのまま使う。

enable_language(ASM)

set(TG_CMSIS_CORE ${CMAKE_SOURCE_DIR}/third_party/cmsis_core)
set(TG_CMSIS_DEVICE ${CMAKE_SOURCE_DIR}/third_party/cmsis_device_f4)
set(TG_LL ${CMAKE_SOURCE_DIR}/third_party/stm32f4xx_hal_driver)
set(TG_LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/bsp/stm32f4/STM32F401RETx.ld)

# ベンダーのヘッダ（CMSIS・LL）。SYSTEM にして、自分のコードの警告オプションをベンダーのヘッダに当てない。
# LL はヘッダの inline 関数だけを使い、HAL・LL のソース（.c）はビルドに入れない。
add_library(tg_vendor_headers INTERFACE)
target_include_directories(tg_vendor_headers SYSTEM INTERFACE
    ${TG_CMSIS_CORE}/Include
    ${TG_CMSIS_DEVICE}/Include
    ${TG_LL}/Inc
)
target_compile_definitions(tg_vendor_headers INTERFACE STM32F401xE)

# ベンダーのソース：スタートアップと SystemInit（テンプレートのまま、警告オプションは付けない）
add_library(tg_vendor OBJECT
    ${TG_CMSIS_DEVICE}/Source/Templates/gcc/startup_stm32f401xe.s
    ${TG_CMSIS_DEVICE}/Source/Templates/system_stm32f4xx.c
)
target_link_libraries(tg_vendor PRIVATE tg_vendor_headers)

# bsp_stm32f4 と main は実行ファイルに直接入れる。スタートアップの弱いハンドラ（.weak SysTick_Handler など）は
# 定義済みとして扱われるので、静的ライブラリに入れた ISR はリンカに拾われない
add_executable(mcu_c_01
    bsp/stm32f4/bsp_adc.c
    bsp/stm32f4/bsp_clock.c
    bsp/stm32f4/bsp_io.c
    bsp/stm32f4/bsp_reset.c
    bsp/stm32f4/reset_decode.c
    bsp/stm32f4/bsp_stack.c
    bsp/stm32f4/bsp_stm32f4.c
    bsp/stm32f4/bsp_uart.c
    bsp/stm32f4/main_stm32f4.c
    $<TARGET_OBJECTS:tg_vendor>
)
set_target_properties(mcu_c_01 PROPERTIES SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_include_directories(mcu_c_01 PRIVATE ${CMAKE_SOURCE_DIR}/bsp/stm32f4)
target_link_libraries(mcu_c_01 PRIVATE tg_firmware tg_app tg_service tg_lib tg_ports tg_vendor_headers)
target_link_options(mcu_c_01 PRIVATE
    -T${TG_LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_BINARY_DIR}/mcu_c_01.map
    -Wl,--cref
    -Wl,--print-memory-usage
)
set_target_properties(mcu_c_01 PROPERTIES LINK_DEPENDS ${TG_LINKER_SCRIPT})
tg_warnings(mcu_c_01)

add_custom_command(TARGET mcu_c_01 POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:mcu_c_01> ${CMAKE_BINARY_DIR}/mcu_c_01.bin
    COMMAND ${CMAKE_SIZE} -A -x $<TARGET_FILE:mcu_c_01>
    COMMENT "mcu_c_01.bin と区画ごとのサイズ"
    VERBATIM
)
