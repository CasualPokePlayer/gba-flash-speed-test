cmake_minimum_required(VERSION 3.13)

list(APPEND CMAKE_MODULE_PATH $ENV{DEVKITPRO}/cmake)
list(APPEND CMAKE_MODULE_PATH $ENV{DEVKITPRO}/cmake/Platform)
set(DKP_GBA_PLATFORM_LIBRARY libgba)

include(GBA)
include(NintendoGBA)

project(FlashSpeedTest LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)

add_executable(FlashSpeedTestMB main.c)
target_compile_definitions(FlashSpeedTestMB PRIVATE MULTIBOOT)
add_executable(FlashSpeedTestROM main.c)
gba_create_rom(FlashSpeedTestMB MULTIBOOT PAD)
gba_create_rom(FlashSpeedTestROM PAD)
