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

# ホスト版は ARM と char の符号を揃える
set(TG_HOST_FLAGS -funsigned-char)

function(tg_warnings target)
    target_compile_options(${target} PRIVATE ${TG_WARNING_FLAGS} ${TG_HOST_FLAGS})
endfunction()
