# ASan・UBSan（host-debug）。指摘が1件でもあれば異常終了させる。
option(TG_SANITIZE "AddressSanitizer と UndefinedBehaviorSanitizer を付ける" OFF)

if(TG_SANITIZE)
    set(TG_SAN_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_compile_options(${TG_SAN_FLAGS})
    add_link_options(-fsanitize=address,undefined)
endif()
