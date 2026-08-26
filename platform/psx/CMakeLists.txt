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
file(GLOB PLAT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/*.c ${CMAKE_CURRENT_SOURCE_DIR}/*.S)

# M3 stops the engine at a single static scene instead of the main menu, so the
# scene loader can be walked with nothing else running. See psx_m3.c.
#   cmake -DPSX_M3=OFF ...    builds the game as the engine intends it
option(PSX_M3 "build the M3 static-scene harness instead of the game menu" ON)

# Deliberately fault at boot, to prove the exception handler is armed. Turn it
# on after any change to psx_exc.S; the expected report is an AdEL with an odd
# badvaddr, and anything else means the vector is not really ours.
option(PSX_EXC_SELFTEST "fault on purpose at boot to test psx_exc.c" OFF)

# M4 draws the scene's actors on the GPU at the end of the M3 harness.
option(PSX_M4 "draw the actors through the GPU after the M3 scene" ON)

# M5 hands control to the engine's own MainLoop on a fixed cube, with neither
# the menu nor the introduction in front of it. It replaces the M3 harness
# rather than following it, so turning it on turns M3 off.
#   cmake -DPSX_M5=ON ...
option(PSX_M5 "run the engine's game loop instead of the M3 static scene" OFF)


# Which scene the harness opens. 0 is Twinsen's house; 59 is the heaviest in
# the game by brick data (tools/scene_census.py) and is the one to check a
# memory claim against.
set(PSX_M3_CUBE 0 CACHE STRING "the cube the M3/M4 harness loads")

set(PORT_DEFS -DPORT_PSX)
if(PSX_M5)
    list(APPEND PORT_DEFS -DPORT_PSX_M5)
elseif(PSX_M3)
    list(APPEND PORT_DEFS -DPORT_PSX_M3)
endif()
if(PSX_M4)
    list(APPEND PORT_DEFS -DPORT_PSX_M4)
endif()
list(APPEND PORT_DEFS -DPSX_M3_CUBE=${PSX_M3_CUBE})

# The bumper screens, their fades and the FLA intro cost about ninety seconds
# of emulated CD before anything else happens, and none of it is under test.
# SKIP_INTRO is the engine's own knob (PERSO.C); this only exposes it.
option(PSX_SKIP_INTRO "skip the bumpers and the FLA intro" OFF)
if(PSX_SKIP_INTRO)
    list(APPEND PORT_DEFS -DSKIP_INTRO)
endif()
if(PSX_EXC_SELFTEST)
    list(APPEND PORT_DEFS -DPSX_EXC_SELFTEST)
endif()
# Diagnose the handler rather than the fault: psx_exc.S puts one character on
# the TTY and stops, before any C runs. See docs/M3-NOTES.md.
option(PSX_EXC_TRACE "trace the exception handler's own entry path" OFF)
if(PSX_EXC_TRACE)
    list(APPEND PORT_DEFS -DPSX_EXC_TRACE)
endif()
# What the hardware said the moment the vector was entered, and a name for each
# step of taking the vector over. For the retail-BIOS install path, where the
# report's own numbers came back inconsistent with the triage that produced
# them. See docs/M5-NOTES.md.
option(PSX_EXC_ENTRYLOG "record vector entry state and narrate the install" OFF)
if(PSX_EXC_ENTRYLOG)
    list(APPEND PORT_DEFS -DPSX_EXC_ENTRYLOG)
endif()
# COMPILE_OPTIONS below is a semicolon-separated list, which is what
# PORT_DEFS already is.
set(PORT_DEFS_SEMI "${PORT_DEFS}")

# The engine files are named .C, which CMake reads as C++. They are C.
set_source_files_properties(${ENGINE_SRC} PROPERTIES
    LANGUAGE C
    COMPILE_OPTIONS "-std=gnu89;${PORT_DEFS_SEMI};-include;${CMAKE_CURRENT_SOURCE_DIR}/watcom_compat.h;-Wno-implicit-function-declaration;-Wno-implicit-int;-Wno-int-conversion;-Wno-incompatible-pointer-types;-Wno-pointer-sign;-Wno-discarded-qualifiers;-Wno-format;-Wno-builtin-declaration-mismatch"
)

psn00bsdk_add_executable(lba1psx GPREL
    ${PLAT_SRC}
    ${TRANSLATE_SRC}
    ${ENGINE_SRC}
)

target_compile_options(lba1psx PRIVATE ${PORT_DEFS} -fcommon -Wall -Wno-unused -Wno-parentheses)

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
