# gcov（host-coverage）。gcovr で app・service の行カバレッジを集計する（詳細設計書 第1章・第9章、NFR-08）。
option(TG_COVERAGE "gcov の計測を付ける（サニタイザーとは併用しない）" OFF)

if(TG_COVERAGE)
    if(TG_SANITIZE)
        message(FATAL_ERROR "TG_COVERAGE と TG_SANITIZE は併用しない")
    endif()
    add_compile_options(--coverage -O0)
    add_link_options(--coverage)
endif()
