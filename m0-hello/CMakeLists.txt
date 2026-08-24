cmake_minimum_required(VERSION 3.21)

project(
    m0hello
    LANGUAGES    C ASM
    VERSION      0.1.0
    DESCRIPTION  "psx-lba M0: 640x480i framebuffer and the CLUT blit path"
)

psn00bsdk_add_executable(m0hello GPREL main.c)
