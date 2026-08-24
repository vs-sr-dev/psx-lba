cmake_minimum_required(VERSION 3.21)

project(
    lba1psx
    LANGUAGES    C ASM
    VERSION      0.1.0
    DESCRIPTION  "Little Big Adventure - PlayStation"
)

set(TOP ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(ENGINE ${TOP}/engine)
set(TRANSLATE ${TOP}/translate)

file(GLOB ENGINE_SRC
    ${ENGINE}/game/*.C
    ${ENGINE}/LIB_3D/*.C
    ${ENGINE}/LIB_CD/*.C
    ${ENGINE}/LIB_MENU/*.C
    ${ENGINE}/LIB_MIDI/*.C
    ${ENGINE}/LIB_MIX/*.C
    ${ENGINE}/LIB_SVGA/*.C
    ${ENGINE}/LIB_SYS/*.C
)
file(GLOB TRANSLATE_SRC ${TRANSLATE}/*.c)
file(GLOB PLAT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

# The engine files are named .C, which CMake reads as C++. They are C.
set_source_files_properties(${ENGINE_SRC} PROPERTIES
    LANGUAGE C
    COMPILE_OPTIONS "-std=gnu89;-DPORT_PSX;-include;${CMAKE_CURRENT_SOURCE_DIR}/watcom_compat.h;-Wno-implicit-function-declaration;-Wno-implicit-int;-Wno-int-conversion;-Wno-incompatible-pointer-types;-Wno-pointer-sign;-Wno-discarded-qualifiers;-Wno-format;-Wno-builtin-declaration-mismatch"
)

psn00bsdk_add_executable(lba1psx GPREL
    ${PLAT_SRC}
    ${TRANSLATE_SRC}
    ${ENGINE_SRC}
)

target_compile_options(lba1psx PRIVATE -DPORT_PSX -fcommon -Wall -Wno-unused -Wno-parentheses)

target_include_directories(lba1psx PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/compat/inc
    ${ENGINE}
    ${ENGINE}/game
    ${ENGINE}/LIB_SYS
    ${ENGINE}/LIB_CD
    ${ENGINE}/LIB_MENU
    ${ENGINE}/LIB_SVGA
    ${TRANSLATE}
)
