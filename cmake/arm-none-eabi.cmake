# ターゲット版（STM32F401RE、Cortex-M4F）のツールチェーン（詳細設計書 第1章、ADR-0004）。
# ubuntu-24.04 の apt の gcc-arm-none-eabi を使う。手元では tools/target-docker.sh が同じ版のコンテナで動かす。
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy CACHE FILEPATH "objcopy")
set(CMAKE_SIZE arm-none-eabi-size CACHE FILEPATH "size")

# 試しのリンクをしない（リンカスクリプトがないと実行ファイルを作れないため）
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TG_MCU_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16")
set(CMAKE_C_FLAGS_INIT "${TG_MCU_FLAGS} -ffunction-sections -fdata-sections -fstack-usage -fcallgraph-info=su")
set(CMAKE_ASM_FLAGS_INIT "${TG_MCU_FLAGS} -x assembler-with-cpp")
# -nostartfiles：newlib の crt0（exit → stdio の後始末 → malloc を持ち込む）を使わず、cmsis_device_f4 の
# startup_stm32f401xe.s の Reset_Handler から始める
set(CMAKE_EXE_LINKER_FLAGS_INIT "${TG_MCU_FLAGS} -Wl,--gc-sections -nostartfiles --specs=nano.specs --specs=nosys.specs")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# 最適化（詳細設計書 第1章・P5）：target-debug は -Og -g3、target-release は -Os -g
set(CMAKE_C_FLAGS_DEBUG_INIT "-Og -g3")
set(CMAKE_C_FLAGS_RELEASE_INIT "-Os -g")
set(CMAKE_ASM_FLAGS_DEBUG_INIT "-g3")
set(CMAKE_ASM_FLAGS_RELEASE_INIT "-g")
