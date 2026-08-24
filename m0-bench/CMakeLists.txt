cmake_minimum_required(VERSION 3.21)

project(
    m0bench
    LANGUAGES    C ASM
    VERSION      0.1.0
    DESCRIPTION  "psx-lba M0: software rasteriser calibration against the DS port"
)

psn00bsdk_add_executable(m0bench GPREL
    main_psx.c
    bench_core.c
    bench_globals.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../translate/s_poly.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../translate/s_fillv.c
)

target_include_directories(m0bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../translate
)
