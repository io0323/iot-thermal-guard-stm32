# 警告オプション（詳細設計書 第1章、CLAUDE.md）。
# 自分のコードのターゲットにだけ付ける（third_party には付けない）。

set(TG_WARNING_FLAGS
    -Wall
    -Wextra
    -Werror
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wstrict-prototypes
    -Wcast-align
    -Wswitch-enum
    -fno-common
)

# ホスト版は ARM と char の符号を揃える（ターゲット版は arm-none-eabi の既定で unsigned char）
if(TG_TARGET)
    set(TG_HOST_FLAGS)
else()
    set(TG_HOST_FLAGS -funsigned-char)
endif()

function(tg_warnings target)
    target_compile_options(${target} PRIVATE ${TG_WARNING_FLAGS} ${TG_HOST_FLAGS})
endfunction()
